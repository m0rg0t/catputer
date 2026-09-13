#include "lofi/controller.h"
#include "lofi/sample_bank.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace lofi {
namespace {

std::uint16_t audibleBpm(const Snapshot& snapshot) {
    const int value = static_cast<int>(snapshot.bpm);
    if (value < static_cast<int>(kMusicMinBpm) || value > static_cast<int>(kMusicMaxBpm)) {
        return kMusicMinBpm;
    }
    return snapshot.bpm;
}

std::uint16_t adjustedBpm(const Snapshot& snapshot, std::uint16_t configured, int direction) {
    const int base = configured == 0 ? static_cast<int>(audibleBpm(snapshot)) : static_cast<int>(configured);
    return static_cast<std::uint16_t>(std::clamp(base + direction,
                                                 static_cast<int>(kMusicMinBpm),
                                                 static_cast<int>(kMusicMaxBpm)));
}

} // namespace

Controller::Controller(std::uint64_t seed):initialSeed_(seed) { snapshot_.config=initialConfig(); populateView(); }
Config Controller::initialConfig() const {
    Config c;
    c.seed = initialSeed_;
    c.mood = static_cast<Mood>(saved.settings.mood);
    c.soundEngine = static_cast<SoundEngine>(saved.settings.engine);
    c.bpm = saved.settings.bpm;
    c.texture = saved.settings.texture;
    // Config::volume remains the engine's normalized session parameter. The
    // 0..300 master gain is carried by Settings and applied by the platform.
    c.volume = 78;
    return c;
}
void Controller::setSnapshot(const Snapshot& s) {
    const auto& a=s.config;const auto& b=requestedConfig_;
    const bool applied=a.seed==b.seed && a.mood==b.mood && a.soundEngine==b.soundEngine &&
                       a.bpm==b.bpm && a.texture==b.texture;
    if(!s.changePending && (snapshot_.changePending || applied)) configRequested_=false;
    if(s.paused==desiredPaused_) pauseRequested_=false;
    snapshot_=s;
}
void Controller::notice(const char* message,std::uint64_t duration) {
    std::snprintf(view.notice,sizeof(view.notice),"%s",message); noticeUntil_=view.timeMs+duration;
}
void Controller::storageResult(bool success) {
    view.sdReady=success;
    if(success) notice(dirty?"SD UPDATED - CHANGES PENDING":"SAVED TO SD");
    else { dirty=true; notice("SAVE FAILED - KEPT IN MEMORY",5000); }
}
Favorite Controller::currentFavorite() const {
    const auto& c=snapshot_.config;
    Favorite favorite;
    favorite.seed = c.seed;
    favorite.bankFingerprint = c.soundEngine==SoundEngine::Hybrid?fingerprint(builtinSampleBankId()):0u;
    favorite.mood = static_cast<std::uint8_t>(c.mood);
    favorite.engine = static_cast<std::uint8_t>(c.soundEngine);
    favorite.texture = c.texture;
    favorite.schema = kSessionSchema;
    favorite.bpm = c.bpm;
    return favorite;
}
void Controller::tick(std::uint64_t now) {
    view.timeMs=now;
    if(noticeUntil_ && now>=noticeUntil_) { view.notice[0]=0; noticeUntil_=0; }
    view.clean=manualClean_ || (view.screen==Screen::Radio && now-lastInput_>8000 && !view.notice[0]);
    populateView();
}
void Controller::populateView() {
    const auto& s=snapshot_;
    view.seed=s.config.seed; view.motion=saved.settings.motion; view.mood=static_cast<int>(s.config.mood); view.bpm=s.bpm;
    view.volume=saved.settings.volume; view.playing=!s.paused; view.pending=s.changePending || configRequested_;
    view.beatPhase=static_cast<float>((std::uint32_t(s.barPhaseQ16)*4u)%65536u)/65536.0f;
    view.level=static_cast<float>(s.recentPeak)/32768.0f; view.favorite=findFavorite(saved,currentFavorite())>=0;
    std::memset(view.items,0,sizeof(view.items)); view.itemCount=0;
    if(view.screen==Screen::Moods) {
        view.itemCount=3;
        for(int i=0;i<3;++i) std::snprintf(view.items[i],32,"%s",moodName(static_cast<Mood>(i)));
    } else if(view.screen==Screen::Favorites) {
        view.itemCount=saved.count;
        for(unsigned i=0;i<saved.count;++i) {
            const auto& favorite=saved.favorites[i];
            const bool old=favorite.schema!=kSessionSchema;
            if(favorite.bpm==0) {
                std::snprintf(view.items[i],32,"%016llX %c %s",
                              static_cast<unsigned long long>(favorite.seed),favorite.engine?'H':'S',
                              old?"OLD":"AUTO");
            } else {
                std::snprintf(view.items[i],32,"%016llX %c %3u%s",
                              static_cast<unsigned long long>(favorite.seed),favorite.engine?'H':'S',
                              static_cast<unsigned>(favorite.bpm),old?" OLD":"");
            }
        }
    } else if(view.screen==Screen::Settings) {
        const auto& v=saved.settings; view.itemCount=8;
        std::snprintf(view.items[0],32,"VOLUME          %3u%%",static_cast<unsigned>(v.volume));
        if(v.bpm==0) {
            std::snprintf(view.items[1],32,"BPM             AUTO %3u",static_cast<unsigned>(s.bpm));
        } else {
            std::snprintf(view.items[1],32,"BPM             %3u",static_cast<unsigned>(v.bpm));
        }
        std::snprintf(view.items[2],32,"BRIGHTNESS      %3u",static_cast<unsigned>(v.brightness));
        std::snprintf(view.items[3],32,"TEXTURE         %3u",static_cast<unsigned>(v.texture));
        std::snprintf(view.items[4],32,"MOTION          %s",v.motion==0?"OFF":v.motion==1?"LOW":"FULL");
        std::snprintf(view.items[5],32,"ENGINE          %s",v.engine?"HYBRID":"SYNTH");
        std::snprintf(view.items[6],32,"DIAGNOSTICS");
        std::snprintf(view.items[7],32,"RESET SETTINGS");
    } else if(view.screen==Screen::Diagnostics) {
        view.itemCount=7;
        std::snprintf(view.items[0],32,"SEED %016llX",static_cast<unsigned long long>(s.config.seed));
        std::snprintf(view.items[1],32,"ENGINE %s",soundEngineName(s.config.soundEngine));
        std::snprintf(view.items[2],32,"BAR %u   BPM %u",s.bar+1,s.bpm);
        std::snprintf(view.items[3],32,"VOICES %u/%u",s.activeVoices,s.voiceCapacity);
        std::snprintf(view.items[4],32,"SCORE EVENTS %u",s.scoreEventCount);
        std::snprintf(view.items[5],32,"SD %s",view.sdReady?"AVAILABLE":"MEMORY ONLY");
        std::snprintf(view.items[6],32,"V%s  MONO 32KHZ",LOFI_VERSION);
    }
    view.selection=std::max(0,std::min(view.selection,std::max(0,view.itemCount-1)));
}
Action Controller::changedConfig() {
    Config c=configRequested_?requestedConfig_:snapshot_.config;
    c.mood=static_cast<Mood>(saved.settings.mood);
    c.soundEngine=static_cast<SoundEngine>(saved.settings.engine);
    c.bpm=saved.settings.bpm;
    c.texture=saved.settings.texture;
    requestedConfig_=c;configRequested_=true;
    dirty=true; notice("CHANGE AT NEXT BAR"); return {ActionKind::Config,c,false};
}
Action Controller::key(int ch) {
    lastInput_=view.timeMs;
    if(ch>=0 && ch<128) ch=std::tolower(static_cast<unsigned char>(ch));
    if(ch==27 || ch=='`') { view.screen=Screen::Radio; view.selection=0; return {}; }
    if(ch==' ') { desiredPaused_=!(pauseRequested_?desiredPaused_:snapshot_.paused);pauseRequested_=true;notice(desiredPaused_?"PAUSED":"PLAYING");return {ActionKind::Pause,{},desiredPaused_}; }
    if(ch=='-' || ch=='=' || ch=='+') {
        auto& v=saved.settings.volume;
        v=static_cast<std::uint16_t>(std::clamp(int(v)+(ch=='-'?-5:5),0,300)); dirty=true;
        char text[30]; std::snprintf(text,sizeof(text),"VOLUME %u%%",static_cast<unsigned>(v)); notice(text); return {};
    }
    if(ch=='h') { view.screen=Screen::Help; view.selection=0; return {}; }
    if(ch=='s') { view.screen=Screen::Settings; view.selection=0; populateView(); return {}; }
    if(ch=='m') { view.screen=Screen::Moods; view.selection=saved.settings.mood; populateView(); return {}; }
    if(ch=='l') { view.screen=Screen::Favorites; view.selection=0; populateView(); return {}; }
    if(ch=='v') { manualClean_=!manualClean_; view.screen=Screen::Radio; return {}; }
    if(ch=='n') { notice("NEXT SESSION AT NEXT BAR"); return {ActionKind::Next,{},false}; }
    if(ch=='e') { saved.settings.engine^=1; return changedConfig(); }
    if(ch=='f') {
        auto f=currentFavorite(); int i=findFavorite(saved,f);
        if(i>=0) { removeFavorite(saved,static_cast<std::size_t>(i)); notice("FAVORITE REMOVED"); }
        else if(!addFavorite(saved,f)) { notice("FAVORITES FULL (8)"); return {}; }
        else notice(view.sdReady?"FAVORITE ADDED":"FAVORITE IN MEMORY - ADD SD",4000);
        dirty=true; return {};
    }
    if(view.screen==Screen::Radio) return {};
    if(view.screen==Screen::Favorites && ch==127 && saved.count) {
        removeFavorite(saved,view.selection); dirty=true; notice("FAVORITE REMOVED"); populateView(); return {};
    }
    if(ch==';' || ch=='.' || ((ch==',' || ch=='/') && view.screen!=Screen::Settings)) {
        const int direction=(ch==';' || ch==',')?-1:1;
        view.selection=std::clamp(view.selection+direction,0,std::max(0,view.itemCount-1)); return {};
    }
    if(view.screen==Screen::Settings && (ch==',' || ch=='/' || ch=='\n')) {
        const int direction=ch==','?-1:1; auto& v=saved.settings;
        if(view.selection==1) {
            const std::uint16_t previous=v.bpm;
            if(ch=='\n') {
                v.bpm = v.bpm==0 ? audibleBpm(snapshot_) : 0;
            } else {
                v.bpm = adjustedBpm(snapshot_,v.bpm,direction);
            }
            if(v.bpm==previous) {
                return {};
            }
            return changedConfig();
        }
        switch(view.selection) {
            case 0: v.volume=static_cast<std::uint16_t>(std::clamp(int(v.volume)+direction*5,0,300)); break;
            case 2: v.brightness=std::clamp(int(v.brightness)+direction*10,10,100); break;
            case 3: v.texture=std::clamp(int(v.texture)+direction*5,0,100); return changedConfig();
            case 4: v.motion=std::clamp(int(v.motion)+direction,0,2); break;
            case 5: v.engine^=1; return changedConfig();
            case 6: view.screen=Screen::Diagnostics; view.selection=0; populateView(); return {};
            case 7: v=Settings{}; return changedConfig();
        }
        dirty=true; populateView(); return {};
    }
    if(ch=='\n' && view.screen==Screen::Moods) {
        saved.settings.mood=static_cast<std::uint8_t>(view.selection); view.screen=Screen::Radio; return changedConfig();
    }
    if(ch=='\n' && view.screen==Screen::Favorites && saved.count) {
        const auto& f=saved.favorites[view.selection];
        if(f.schema!=kSessionSchema) { notice("OLD FAVORITE - USE EARLIER VERSION",4000); return {}; }
        if(f.engine && f.bankFingerprint!=fingerprint(builtinSampleBankId())) { notice("FAVORITE BANK DOES NOT MATCH",4000); return {}; }
        Config c; c.seed=f.seed; c.mood=static_cast<Mood>(f.mood); c.soundEngine=static_cast<SoundEngine>(f.engine); c.bpm=f.bpm; c.texture=f.texture; c.volume=78;
        saved.settings.mood=f.mood; saved.settings.engine=f.engine; saved.settings.bpm=f.bpm; saved.settings.texture=f.texture;
        requestedConfig_=c;configRequested_=true;
        dirty=true; view.screen=Screen::Radio; notice("FAVORITE AT NEXT BAR"); return {ActionKind::Config,c,false,true};
    }
    return {};
}
}
