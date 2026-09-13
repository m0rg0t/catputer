#include "lofi/controller.h"
#include "lofi/sample_bank.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace lofi {
namespace {

constexpr std::uint64_t kSleepFadeMs = 30000;
constexpr std::uint16_t kUnityGainQ15 = 32768;

std::uint64_t elapsedSince(std::uint64_t now, std::uint64_t then) {
    return now >= then ? now - then : 0;
}

std::uint8_t cycleSleepMinutes(std::uint8_t current, int direction) {
    constexpr std::uint8_t values[] = {0, 30, 60, 90};
    int index = 0;
    while (index < 4 && values[index] != current) {
        ++index;
    }
    if (index == 4) {
        index = 0;
    }
    index = (index + direction + 4) % 4;
    return values[index];
}

std::uint8_t cycleAutoDimSeconds(std::uint8_t current, int direction) {
    constexpr std::uint8_t values[] = {0, 30, 60, 120};
    int index = 0;
    while (index < 4 && values[index] != current) {
        ++index;
    }
    if (index == 4) {
        index = 0;
    }
    index = (index + direction + 4) % 4;
    return values[index];
}

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
    c.meter = saved.settings.meter;
    c.keysTone = saved.settings.keysTone;
    c.leadTone = saved.settings.leadTone;
    c.bassTone = saved.settings.bassTone;
    // Config::volume remains the engine's normalized session parameter. The
    // 0..300 master gain is carried by Settings and applied by the platform.
    c.volume = 78;
    return c;
}
void Controller::setSnapshot(const Snapshot& s) {
    const auto& a=s.config;const auto& b=requestedConfig_;
    const bool applied=a.seed==b.seed && a.mood==b.mood && a.soundEngine==b.soundEngine &&
                       a.bpm==b.bpm && a.texture==b.texture && a.meter==b.meter &&
                       a.keysTone==b.keysTone && a.leadTone==b.leadTone &&
                       a.bassTone==b.bassTone;
    if(!s.changePending && (snapshot_.changePending || applied)) configRequested_=false;
    if(s.paused==desiredPaused_) pauseRequested_=false;
    if(sleepExpired_ && s.paused) sleepPausePending_=false;
    snapshot_=s;
}
std::uint8_t Controller::effectiveBrightness() const {
    return dimmed_ ? static_cast<std::uint8_t>(std::min<int>(saved.settings.brightness,10))
                   : saved.settings.brightness;
}
void Controller::cancelSleepTimer() {
    sleepMinutes_=0;
    sleepDeadlineMs_=0;
    sleepGainQ15_=kUnityGainQ15;
    sleepExpired_=false;
    sleepPausePending_=false;
}
void Controller::setSleepMinutes(std::uint8_t minutes) {
    if(minutes==0) {
        cancelSleepTimer();
        notice("SLEEP TIMER OFF");
        return;
    }
    sleepMinutes_=minutes;
    sleepDeadlineMs_=view.timeMs+static_cast<std::uint64_t>(minutes)*60U*1000U;
    sleepGainQ15_=kUnityGainQ15;
    sleepExpired_=false;
    sleepPausePending_=false;
    char text[30];
    std::snprintf(text,sizeof(text),"SLEEP IN %u MIN",static_cast<unsigned>(minutes));
    notice(text);
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
    favorite.meter = c.meter;
    favorite.keysTone = c.keysTone;
    favorite.leadTone = c.leadTone;
    favorite.bassTone = c.bassTone;
    return favorite;
}
void Controller::tick(std::uint64_t now) {
    view.timeMs=now;
    if(noticeUntil_ && now>=noticeUntil_) { view.notice[0]=0; noticeUntil_=0; }
    if(sleepMinutes_!=0 && !sleepExpired_) {
        if(now>=sleepDeadlineMs_) {
            sleepGainQ15_=0;
            sleepExpired_=true;
            desiredPaused_=true;
            pauseRequested_=!snapshot_.paused;
            sleepPausePending_=!snapshot_.paused;
            notice("SLEEP TIMER ENDED");
        } else {
            const std::uint64_t remaining=sleepDeadlineMs_-now;
            sleepGainQ15_=remaining>=kSleepFadeMs?kUnityGainQ15:
                static_cast<std::uint16_t>((remaining*kUnityGainQ15)/kSleepFadeMs);
        }
    }
    const std::uint8_t dimSeconds=saved.settings.autoDimSeconds;
    dimmed_=dimSeconds!=0 && elapsedSince(now,lastInput_)>=
        static_cast<std::uint64_t>(dimSeconds)*1000U;
    view.clean=manualClean_ || (view.screen==Screen::Radio && elapsedSince(now,lastInput_)>8000 && !view.notice[0]);
    populateView();
}
void Controller::populateView() {
    const auto& s=snapshot_;
    view.seed=s.config.seed; view.motion=saved.settings.motion; view.mood=static_cast<int>(s.config.mood); view.bpm=s.bpm;
    view.volume=saved.settings.volume; view.playing=!s.paused; view.pending=s.changePending || configRequested_;
    view.dimmed=dimmed_; view.sleepTimerActive=sleepMinutes_!=0; view.sleepExpired=sleepExpired_;
    view.sleepGainQ15=sleepGainQ15_;
    view.sleepSecondsRemaining=sleepMinutes_!=0 && !sleepExpired_ && sleepDeadlineMs_>view.timeMs
        ? static_cast<std::uint32_t>((sleepDeadlineMs_-view.timeMs+999U)/1000U) : 0;
    view.meter=s.config.meter; view.keysTone=s.config.keysTone; view.leadTone=s.config.leadTone;
    view.bassTone=s.config.bassTone;
    view.meterNumerator=s.meterNumerator == 0 ? 4 : s.meterNumerator;
    view.meterDenominator=s.meterDenominator == 0 ? 4 : s.meterDenominator;
    view.beatsPerBar=s.beatsPerBar == 0 ? view.meterNumerator : s.beatsPerBar;
    view.stepsPerBar=s.stepsPerBar == 0 ? 16 : s.stepsPerBar;
    view.stepsPerBeat=s.stepsPerBeat == 0 ? 4 : s.stepsPerBeat;
    view.beatPhase=static_cast<float>(s.barPhaseQ16)/65535.0f;
    view.level=(s.paused || saved.settings.volume == 0) ? 0.0f :
               (static_cast<float>(s.recentPeak)/32768.0f)*
               (static_cast<float>(sleepGainQ15_)/static_cast<float>(kUnityGainQ15));
    std::memset(view.instrumentLevels,0,sizeof(view.instrumentLevels));
    if(!s.paused && saved.settings.volume != 0) {
        for(std::size_t i=0;i<kMusicInstrumentCount;++i) {
            view.instrumentLevels[i]=static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(s.instrumentLevels[i])*sleepGainQ15_)/kUnityGainQ15);
        }
    }
    view.favorite=findFavorite(saved,currentFavorite())>=0;
    std::memset(view.items,0,sizeof(view.items)); view.itemCount=0;
    if(view.screen==Screen::Moods) {
        view.itemCount=kMoodCount;
        for(int i=0;i<kMoodCount;++i) std::snprintf(view.items[i],32,"%s",moodName(static_cast<Mood>(i)));
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
        const auto& v=saved.settings; view.itemCount=10;
        std::snprintf(view.items[0],32,"VOLUME          %3u%%",static_cast<unsigned>(v.volume));
        if(v.bpm==0) {
            std::snprintf(view.items[1],32,"BPM             AUTO %3u",static_cast<unsigned>(s.bpm));
        } else {
            std::snprintf(view.items[1],32,"BPM             %3u",static_cast<unsigned>(v.bpm));
        }
        std::snprintf(view.items[2],32,"BRIGHTNESS      %3u",static_cast<unsigned>(v.brightness));
        if(v.autoDimSeconds==0) std::snprintf(view.items[3],32,"AUTO DIM        OFF");
        else std::snprintf(view.items[3],32,"AUTO DIM        %3u SEC",static_cast<unsigned>(v.autoDimSeconds));
        if(sleepExpired_) std::snprintf(view.items[4],32,"SLEEP           EXPIRED");
        else if(sleepMinutes_==0) std::snprintf(view.items[4],32,"SLEEP           OFF");
        else std::snprintf(view.items[4],32,"SLEEP           %u:%02u",
                           static_cast<unsigned>(view.sleepSecondsRemaining/60U),
                           static_cast<unsigned>(view.sleepSecondsRemaining%60U));
        std::snprintf(view.items[5],32,"TEXTURE         %3u",static_cast<unsigned>(v.texture));
        std::snprintf(view.items[6],32,"MOTION          %s",v.motion==0?"OFF":v.motion==1?"LOW":"FULL");
        std::snprintf(view.items[7],32,"ENGINE          %s",v.engine?"HYBRID":"SYNTH");
        std::snprintf(view.items[8],32,"DIAGNOSTICS");
        std::snprintf(view.items[9],32,"RESET SETTINGS");
    } else if(view.screen==Screen::Instruments) {
        const auto& settings = saved.settings;
        view.itemCount=4;
        std::snprintf(view.items[0],32,"CHORDS %s",toneName(settings.keysTone));
        std::snprintf(view.items[1],32,"MELODY %s",toneName(settings.leadTone));
        std::snprintf(view.items[2],32,"BASS %s",bassToneName(settings.bassTone));
        if(settings.meter == MusicMeter::Auto) {
            std::snprintf(view.items[3],32,"METER AUTO %u/%u",
                          static_cast<unsigned>(view.meterNumerator),
                          static_cast<unsigned>(view.meterDenominator));
        } else {
            std::snprintf(view.items[3],32,"METER %s",meterName(settings.meter));
        }
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
    c.meter=saved.settings.meter;
    c.keysTone=saved.settings.keysTone;
    c.leadTone=saved.settings.leadTone;
    c.bassTone=saved.settings.bassTone;
    requestedConfig_=c;configRequested_=true;
    dirty=true; notice("CHANGE AT NEXT BAR"); return {ActionKind::Config,c,false};
}
Action Controller::key(int ch) {
    lastInput_=view.timeMs;
    if(dimmed_) {
        dimmed_=false;
        populateView();
        return {};
    }
    if(ch>=0 && ch<128) ch=std::tolower(static_cast<unsigned char>(ch));
    if(ch==27 || ch=='`') { view.screen=Screen::Radio; view.selection=0; return {}; }
    if(ch==' ') {
        desiredPaused_=!(pauseRequested_?desiredPaused_:snapshot_.paused);
        pauseRequested_=true;
        const bool clearSleep=!desiredPaused_ &&
            (sleepExpired_ || sleepGainQ15_!=kUnityGainQ15);
        if(clearSleep) cancelSleepTimer();
        notice(desiredPaused_?"PAUSED":"PLAYING");
        if(clearSleep) populateView();
        return {ActionKind::Pause,{},desiredPaused_};
    }
    if(ch=='-' || ch=='=' || ch=='+') {
        auto& v=saved.settings.volume;
        v=static_cast<std::uint16_t>(std::clamp(int(v)+(ch=='-'?-5:5),0,300)); dirty=true;
        char text[30]; std::snprintf(text,sizeof(text),"VOLUME %u%%",static_cast<unsigned>(v)); notice(text); return {};
    }
    if(ch=='h') { view.screen=Screen::Help; view.selection=0; return {}; }
    if(ch=='s') { view.screen=Screen::Settings; view.selection=0; populateView(); return {}; }
    if(ch=='m') { view.screen=Screen::Moods; view.selection=saved.settings.mood; populateView(); return {}; }
    if(ch=='l') { view.screen=Screen::Favorites; view.selection=0; populateView(); return {}; }
    if(ch=='i') { view.screen=Screen::Instruments; view.selection=0; populateView(); return {}; }
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
    if(ch==';' || ch=='.' || ((ch==',' || ch=='/') && view.screen!=Screen::Settings &&
                              view.screen!=Screen::Instruments)) {
        const int direction=(ch==';' || ch==',')?-1:1;
        view.selection=std::clamp(view.selection+direction,0,std::max(0,view.itemCount-1)); return {};
    }
    if(view.screen==Screen::Instruments && (ch==',' || ch=='/' || ch=='\n')) {
        const int direction=ch==','?-1:1;
        auto cycle=[](int value,int count,int delta) {
            value=(value+delta)%count;
            return value<0?value+count:value;
        };
        switch(view.selection) {
            case 0: saved.settings.keysTone=static_cast<Tone>(cycle(
                static_cast<int>(saved.settings.keysTone),6,direction)); break;
            case 1: saved.settings.leadTone=static_cast<Tone>(cycle(
                static_cast<int>(saved.settings.leadTone),6,direction)); break;
            case 2: saved.settings.bassTone=static_cast<BassTone>(cycle(
                static_cast<int>(saved.settings.bassTone),3,direction)); break;
            case 3: saved.settings.meter=static_cast<MusicMeter>(cycle(
                static_cast<int>(saved.settings.meter),4,direction)); break;
            default: return {};
        }
        return changedConfig();
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
            case 3: v.autoDimSeconds=cycleAutoDimSeconds(v.autoDimSeconds,direction); break;
            case 4: {
                const bool cancelPendingPause=sleepPausePending_;
                setSleepMinutes(cycleSleepMinutes(sleepMinutes_,direction));
                populateView();
                if(cancelPendingPause && !snapshot_.paused) {
                    // A timer pause may already be queued off-thread. Send an
                    // idempotent resume after it so cancelling at the deadline
                    // leaves the transport in its current playing state.
                    desiredPaused_=false;
                    pauseRequested_=true;
                    return {ActionKind::Pause,{},false};
                }
                return {};
            }
            case 5: v.texture=std::clamp(int(v.texture)+direction*5,0,100); return changedConfig();
            case 6: v.motion=std::clamp(int(v.motion)+direction,0,2); break;
            case 7: v.engine^=1; return changedConfig();
            case 8: view.screen=Screen::Diagnostics; view.selection=0; populateView(); return {};
            case 9: v=Settings{}; dimmed_=false; return changedConfig();
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
        c.meter=f.meter; c.keysTone=f.keysTone; c.leadTone=f.leadTone; c.bassTone=f.bassTone;
        saved.settings.mood=f.mood; saved.settings.engine=f.engine; saved.settings.bpm=f.bpm; saved.settings.texture=f.texture;
        saved.settings.meter=f.meter; saved.settings.keysTone=f.keysTone; saved.settings.leadTone=f.leadTone; saved.settings.bassTone=f.bassTone;
        requestedConfig_=c;configRequested_=true;
        dirty=true; view.screen=Screen::Radio; notice("FAVORITE AT NEXT BAR"); return {ActionKind::Config,c,false,true};
    }
    return {};
}
}
