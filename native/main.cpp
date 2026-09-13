#include "lofi/controller.h"
#include "lofi/music.h"
#include "lofi/output_gain.h"
#include "lofi/sample_bank.h"
#include "lofi/state.h"
#include "lofi/ui.h"
#include "storage.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef LOFI_HAS_SDL
#include <SDL.h>
#endif

namespace fs=std::filesystem;
namespace {
using namespace lofi;
struct Options {
    Config config{};
    double seconds=90;
    fs::path wav,meta,score,shots,animation,state;
    unsigned frames=48, smokeMs=0, volume=100;
    bool noAudio=false, moodExplicit=false, engineExplicit=false, bpmExplicit=false, volumeExplicit=false;
    bool meterExplicit=false, keysExplicit=false, leadExplicit=false, bassExplicit=false;
};
std::uint64_t number(const std::string& s) {
    std::size_t consumed=0; auto value=std::stoull(s,&consumed,0);
    if(consumed!=s.size() || (!s.empty() && s[0]=='-')) throw std::runtime_error("Invalid integer: "+s);
    return value;
}
Tone parseTone(const std::string& value) {
    const char* names[]={"epiano","felt","nylon","vibes","pad","flute"};
    for(unsigned i=0;i<6;++i) if(value==names[i]) return static_cast<Tone>(i);
    throw std::runtime_error("Tone: epiano, felt, nylon, vibes, pad or flute");
}
MusicMeter parseMeter(const std::string& value) {
    if(value=="auto") return MusicMeter::Auto;
    if(value=="4/4") return MusicMeter::FourFour;
    if(value=="3/4") return MusicMeter::ThreeFour;
    if(value=="6/8") return MusicMeter::SixEight;
    throw std::runtime_error("Meter: auto, 4/4, 3/4 or 6/8");
}
BassTone parseBass(const std::string& value) {
    if(value=="round") return BassTone::Round;
    if(value=="upright") return BassTone::Upright;
    if(value=="sub") return BassTone::Sub;
    throw std::runtime_error("Bass: round, upright or sub");
}
Options options(int argc,char** argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        const std::string a=argv[i];
        auto value=[&](){if(i+1>=argc) throw std::runtime_error("Missing value for "+a); return std::string(argv[++i]);};
        if(a=="--wav") o.wav=value();
        else if(a=="--meta") o.meta=value();
        else if(a=="--score") o.score=value();
        else if(a=="--shots") o.shots=value();
        else if(a=="--animation") o.animation=value();
        else if(a=="--state") o.state=value();
        else if(a=="--seed") o.config.seed=number(value());
        else if(a=="--bpm") { auto v=value(); auto n=v=="auto"?0:number(v); if(v!="auto" && (n<kMusicMinBpm || n>kMusicMaxBpm)) throw std::runtime_error("BPM: auto or 40..180"); o.config.bpm=static_cast<std::uint16_t>(n);o.bpmExplicit=true; }
        else if(a=="--meter") {o.config.meter=parseMeter(value());o.meterExplicit=true;}
        else if(a=="--keys") {o.config.keysTone=parseTone(value());o.keysExplicit=true;}
        else if(a=="--lead") {o.config.leadTone=parseTone(value());o.leadExplicit=true;}
        else if(a=="--bass") {o.config.bassTone=parseBass(value());o.bassExplicit=true;}
        else if(a=="--volume") { auto n=number(value()); if(n>OutputGain::kMaxPercent) throw std::runtime_error("Volume: 0..300 percent"); o.volume=static_cast<unsigned>(n);o.volumeExplicit=true; }
        else if(a=="--seconds") { auto s=value(); std::size_t pos=0; o.seconds=std::stod(s,&pos); if(pos!=s.size() || !std::isfinite(o.seconds) || o.seconds<=0 || o.seconds>7200) throw std::runtime_error("Seconds must be in (0,7200]"); }
        else if(a=="--frames") { auto n=number(value()); if(n<1 || n>720) throw std::runtime_error("Frames must be 1..720"); o.frames=static_cast<unsigned>(n); }
        else if(a=="--engine") { o.engineExplicit=true; auto v=value(); if(v=="synth") o.config.soundEngine=SoundEngine::Synth; else if(v=="hybrid") o.config.soundEngine=SoundEngine::Hybrid; else throw std::runtime_error("Engine: synth or hybrid"); }
        else if(a=="--mood") { o.moodExplicit=true; auto v=value(); if(v=="cozy") o.config.mood=Mood::Cozy; else if(v=="rainy") o.config.mood=Mood::Rainy; else if(v=="night") o.config.mood=Mood::Night; else if(v=="sunny") o.config.mood=Mood::Sunny; else throw std::runtime_error("Mood: cozy, rainy, night or sunny"); }
        else if(a=="--no-audio") o.noAudio=true;
        else if(a=="--smoke-ms") {auto n=number(value());if(n<100 || n>60000) throw std::runtime_error("Smoke duration must be 100..60000 ms");o.smokeMs=static_cast<unsigned>(n);}
        else if(a=="--help") {
            std::cout<<"Pocket Lofi native preview\n"
                <<"  --wav FILE [--seconds 90] [--meta FILE]\n"
                <<"  --score FILE   Export scheduled notes/harmony as CSV for --seconds\n"
                <<"  --engine synth|hybrid --mood cozy|rainy|night|sunny --seed INTEGER\n"
                <<"  --bpm auto|40..180 --volume 0..300  Playback/WAV output settings\n"
                <<"  --meter auto|4/4|3/4|6/8  6/8 BPM counts dotted-quarter pulses\n"
                <<"  --keys/--lead epiano|felt|nylon|vibes|pad|flute\n"
                <<"  --bass round|upright|sub\n"
                <<"  --shots DIRECTORY   Export actual renderer scenarios as PPM\n"
                <<"  --animation DIRECTORY [--frames 48]   Export at 12 FPS\n"
                <<"  --state FILE   Optional local settings/favorites\n"
                <<"  --no-audio     Interactive visual preview only\n"
                <<"  --smoke-ms N   Exit the SDL preview after a bounded smoke run\n"
                <<"Without export options, launch the SDL preview. Q quits.\n";
            std::exit(0);
        } else throw std::runtime_error("Unknown option: "+a);
    }
    if(!o.meta.empty() && o.wav.empty()) throw std::runtime_error("--meta requires --wav");
    return o;
}
void parent(const fs::path& p) { if(!p.parent_path().empty()) fs::create_directories(p.parent_path()); }
void write16(std::ostream& f,std::uint16_t n) { char p[]={char(n&255),char(n>>8)}; f.write(p,2); }
void write32(std::ostream& f,std::uint32_t n) { for(int i=0;i<4;++i) f.put(char(n>>(8*i))); }
void waveHeader(std::ostream& f,std::uint32_t bytes) {
    f.write("RIFF",4); write32(f,bytes+36); f.write("WAVEfmt ",8); write32(f,16); write16(f,1); write16(f,1);
    write32(f,kMusicSampleRate); write32(f,kMusicSampleRate*2); write16(f,2); write16(f,16); f.write("data",4); write32(f,bytes);
}
void exportAudio(const Options& o) {
    Engine engine(o.config); parent(o.wav); std::ofstream out(o.wav,std::ios::binary);
    if(!out) throw std::runtime_error("Cannot open WAV output");
    const auto frames=static_cast<std::uint64_t>(std::llround(o.seconds*kMusicSampleRate));
    waveHeader(out,static_cast<std::uint32_t>(frames*2)); std::array<std::int16_t,512> pcm{};
    double worstUs=0; long double squares=0; std::uint64_t clips=0; int outputPeak=0; auto start=std::chrono::steady_clock::now();
    OutputGain gain(o.volume);
    Diagnostics opening=engine.diagnostics();
    const auto initialSnapshot=engine.snapshot();
    const auto initialBpm=initialSnapshot.bpm;
    for(std::uint64_t done=0;done<frames;) {
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(512,frames-done)); auto t=std::chrono::steady_clock::now();
        engine.render(pcm.data(),n);
        if(o.volumeExplicit) gain.process(pcm.data(),n);
        worstUs=std::max(worstUs,std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t).count());
        for(std::size_t i=0;i<n;++i) { write16(out,static_cast<std::uint16_t>(pcm[i])); squares+=static_cast<long double>(pcm[i])*pcm[i];outputPeak=std::max(outputPeak,std::abs(int(pcm[i]))); if(pcm[i]==32767 || pcm[i]==-32768) ++clips; }
        done+=n;
        if(done<=10*kMusicSampleRate) opening=engine.diagnostics();
    }
    out.close(); if(!out) throw std::runtime_error("WAV write failed");
    auto d=engine.diagnostics(); char favorite[Engine::kFavoriteCodeCapacity]{};
    // Persist the initial configuration, not an automatically advanced session.
    Engine initial(o.config); initial.writeFavoriteCode(favorite,sizeof(favorite));
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    if(!o.meta.empty()) {
        parent(o.meta); std::ofstream m(o.meta);
        m<<"{\n  \"version\": \""<<LOFI_VERSION<<"\",\n  \"source\": \"native_shared_engine\",\n  \"hardware_verified\": false,\n"
         <<"  \"generation_schema\": "<<kMusicSchemaVersion<<",\n  \"bpm\": "<<initialBpm<<",\n"
         <<"  \"meter\": \""<<unsigned(initialSnapshot.meterNumerator)<<'/'<<unsigned(initialSnapshot.meterDenominator)<<"\",\n"
         <<"  \"meter_mode\": \""<<meterName(o.config.meter)<<"\",\n"
         <<"  \"keys_tone\": \""<<toneName(o.config.keysTone)<<"\",\n  \"lead_tone\": \""<<toneName(o.config.leadTone)<<"\",\n"
         <<"  \"bass_tone\": \""<<bassToneName(o.config.bassTone)<<"\",\n"
         <<"  \"bpm_override\": "<<o.config.bpm<<",\n  \"output_gain_percent\": "<<o.volume<<",\n"
         <<"  \"output_gain_applied\": "<<(o.volumeExplicit?"true":"false")<<",\n  \"limited_samples\": "<<gain.limitedSamples()<<",\n"
         <<"  \"engine\": \""<<soundEngineName(o.config.soundEngine)<<"\",\n  \"mood\": \""<<moodName(o.config.mood)<<"\",\n"
         <<"  \"favorite_code\": \""<<favorite<<"\",\n  \"sample_bank\": \""<<builtinSampleBankId()<<"\",\n"
         <<"  \"sample_rate\": "<<kMusicSampleRate<<",\n  \"frames\": "<<frames<<",\n  \"rms\": "<<std::sqrt(static_cast<double>(squares/frames))<<",\n"
         <<"  \"peak\": "<<outputPeak<<",\n  \"core_peak\": "<<d.absolutePeak<<",\n  \"clipped_samples\": "<<clips<<",\n  \"max_voices\": "<<int(d.maxActiveVoices)<<",\n"
         <<"  \"voice_capacity\": "<<int(kMusicVoiceCapacity)<<",\n  \"voice_steals\": "<<d.stolenVoices<<",\n  \"dropped_note_events\": "<<d.droppedNoteEvents<<",\n"
         <<"  \"minimum_keys_gain_q15\": "<<d.minimumKeysGainQ15<<",\n"
         <<"  \"score_hash\": \""<<std::hex<<d.scoreEventHash<<std::dec<<"\",\n  \"score_events\": "<<d.scoreEventCount<<",\n"
         <<"  \"opening_seconds\": "<<double(std::min<std::uint64_t>(frames,10*kMusicSampleRate))/kMusicSampleRate<<",\n"
         <<"  \"opening_score_events\": "<<opening.scoreEventCount<<",\n  \"opening_voice_steals\": "<<opening.stolenVoices<<",\n  \"opening_dropped_notes\": "<<opening.droppedNoteEvents<<",\n";
        const char* instruments[]={"keys","bass","lead","kick","snare","hat","rim"};
        m<<"  \"notes_started\": {";
        for(unsigned i=0;i<7;++i) m<<(i?", ":"")<<"\""<<instruments[i]<<"\": "<<d.noteEventsByInstrument[i];
        m<<"},\n  \"first_note_seconds\": {";
        for(unsigned i=0;i<7;++i) {
            m<<(i?", ":"")<<"\""<<instruments[i]<<"\": ";
            if(d.firstNoteSamples[i]==std::numeric_limits<std::uint64_t>::max()) m<<"null";
            else m<<double(d.firstNoteSamples[i])/kMusicSampleRate;
        }
        m<<"},\n"
         <<"  \"host_worst_block_us\": "<<worstUs<<",\n  \"host_render_seconds\": "<<elapsed<<"\n}\n";
        if(!m) throw std::runtime_error("Metadata write failed");
    }
    std::cout<<o.wav<<": "<<frames<<" frames, peak "<<outputPeak<<", clips "<<clips<<", host "<<elapsed<<"s\n";
}
void exportScore(const Options& o) {
    Engine engine(o.config); parent(o.score); std::ofstream out(o.score);
    if(!out) throw std::runtime_error("Cannot open score output");
    out<<"schema,session,seed,bar,bpm,key_pc,minor,chord_root,chord_0,chord_1,chord_2,chord_3,instrument,midi,velocity,start_sample,transport_start_sample,duration_samples,export_frames,meter_numerator,meter_denominator,beats_per_bar,steps_per_bar,steps_per_beat,bar_start_sample,bar_end_sample\n";
    const auto frames=static_cast<std::uint64_t>(std::llround(o.seconds*kMusicSampleRate));
    std::array<std::int16_t,512> pcm{};
    std::uint32_t lastBar=UINT32_MAX, lastSession=UINT32_MAX;
    std::uint64_t notes=0;
    const char* instruments[]={"keys","bass","lead","kick","snare","hat","rim"};
    for(std::uint64_t done=0;;) {
        const auto snap=engine.snapshot();
        const auto session=engine.diagnostics().sessionTransitions;
        if(snap.bar!=lastBar || session!=lastSession) {
            const auto score=engine.scoreBar();
            const auto offset=snap.transportSample-snap.sessionSample;
            for(unsigned i=0;i<score.noteCount;++i) {
                const auto& note=score.notes[i];
                if(offset+note.startSample>=frames) continue;
                out<<kMusicSchemaVersion<<','<<session<<','<<score.seed<<','<<score.bar<<','
                   <<score.bpm<<','<<unsigned(score.keyPitchClass)<<','<<score.minor<<','<<unsigned(score.chordRoot);
                for(auto pitch:score.chordNotes) out<<','<<unsigned(pitch);
                out<<','<<instruments[static_cast<unsigned>(note.instrument)]<<','<<unsigned(note.note)<<','
                   <<unsigned(note.velocity)<<','<<note.startSample<<','<<offset+note.startSample<<','<<note.durationSamples<<','<<frames<<','
                   <<unsigned(score.meterNumerator)<<','<<unsigned(score.meterDenominator)<<','<<unsigned(score.beatsPerBar)<<','
                   <<unsigned(score.stepsPerBar)<<','<<unsigned(score.stepsPerBeat)<<','<<score.barStartSample<<','<<score.barEndSample<<'\n';
                ++notes;
            }
            lastBar=snap.bar; lastSession=session;
        }
        if(done>=frames) break;
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),frames-done));
        engine.render(pcm.data(),n); done+=n;
    }
    if(!out) throw std::runtime_error("Score write failed");
    std::cout<<o.score<<": "<<notes<<" scheduled notes; offline inspection of shared composer\n";
}
void ppm(const fs::path& path,const Frame& frame,unsigned brightness=255) {
    parent(path); std::ofstream f(path,std::ios::binary); if(!f) throw std::runtime_error("Cannot write screenshot");
    f<<"P6\n240 135\n255\n"; std::array<std::uint16_t,240> row{};
    for(int y=0;y<135;++y) { frame.rowRgb565(y,row.data()); for(auto c:row) {
        f.put(char((((c>>11)&31)*255/31)*brightness/255));
        f.put(char((((c>>5)&63)*255/63)*brightness/255));
        f.put(char(((c&31)*255/31)*brightness/255));
    }}
    if(!f) throw std::runtime_error("Screenshot write failed");
}
void exportScreens(const Options& o) {
    Controller controller(o.config.seed); controller.saved.settings.mood=static_cast<std::uint8_t>(o.config.mood);controller.saved.settings.engine=static_cast<std::uint8_t>(o.config.soundEngine);controller.saved.settings.texture=o.config.texture;controller.saved.settings.bpm=o.config.bpm;
    controller.saved.settings.meter=o.config.meter;controller.saved.settings.keysTone=o.config.keysTone;
    controller.saved.settings.leadTone=o.config.leadTone;controller.saved.settings.bassTone=o.config.bassTone;
    if(o.volumeExplicit) controller.saved.settings.volume=o.volume;
    Engine engine(o.config); std::array<std::int16_t,512> buffer{};
    for(int i=0;i<500;++i) engine.render(buffer.data(),buffer.size());
    controller.setSnapshot(engine.snapshot()); controller.tick(3200); controller.view.batteryPercent=76;
    Frame frame;
    auto shot=[&](const char* name){
        controller.populateView();render(frame,controller.view);
        ppm(o.shots/(std::string(name)+".ppm"),frame,
            255u*controller.effectiveBrightness()/std::max(1u,unsigned(controller.saved.settings.brightness)));
    };
    if(!o.shots.empty()) {
        fs::create_directories(o.shots);
        shot("01-playing"); controller.view.clean=true; shot("02-clean-scene"); controller.view.clean=false;
        auto s=engine.snapshot(); s.paused=true; controller.setSnapshot(s); shot("03-paused"); s.paused=false; controller.setSnapshot(s);
        controller.saved.settings.volume=0; shot("04-muted");controller.saved.settings.volume=100;shot("05-volume-full");controller.saved.settings.volume=35;
        controller.key('m');shot("06-moods");controller.key(27);
        s.changePending=true; controller.setSnapshot(s);controller.notice("CHANGE AT NEXT BAR");shot("07-pending");s.changePending=false;controller.setSnapshot(s);controller.view.notice[0]=0;
        controller.key('l');shot("08-favorites-empty");controller.key(27);controller.key('f');controller.key('l');shot("09-favorite-memory");controller.key(27);controller.view.notice[0]=0;
        controller.key('s');shot("10-settings");controller.key('h');shot("11-help");controller.view.screen=Screen::Diagnostics;shot("12-diagnostics");
        controller.view.screen=Screen::Radio;controller.view.batteryPercent=9;shot("13-low-battery");
        controller.notice("SAVE FAILED - KEPT IN MEMORY",5000);shot("15-save-error");controller.view.notice[0]=0;controller.view.sdReady=true;shot("16-sd-ready");
        controller.saved.settings.volume=300;shot("17-volume-boost");
        controller.saved.settings.volume=35;controller.saved.settings.bpm=120;
        auto manual=o.config;manual.bpm=120;engine.reset(manual);controller.setSnapshot(engine.snapshot());
        controller.key('s');controller.view.selection=1;shot("18-bpm-manual");
        controller.saved.settings.bpm=0;auto automatic=o.config;automatic.bpm=0;engine.reset(automatic);controller.setSnapshot(engine.snapshot());
        shot("19-bpm-auto");
        controller.key('i');shot("20-instruments");
        controller.saved.settings.keysTone=Tone::NylonGuitar;controller.saved.settings.leadTone=Tone::SoftFlute;
        controller.saved.settings.bassTone=BassTone::Upright;controller.view.selection=1;shot("21-instrument-selection");
        for(const auto meter:{MusicMeter::FourFour,MusicMeter::ThreeFour,MusicMeter::SixEight}) {
            auto config=o.config;config.meter=meter;engine.reset(config);
            for(int i=0;i<30;++i) engine.render(buffer.data(),buffer.size());
            controller.setSnapshot(engine.snapshot());controller.key(27);controller.view.clean=false;
            shot(meter==MusicMeter::FourFour?"22-music-4-4":meter==MusicMeter::ThreeFour?"23-music-3-4":"24-music-6-8");
        }
        controller.saved.settings.motion=0;shot("25-still-visualizer");controller.saved.settings.motion=2;
        // Exercise the actual controller using a fake wall clock, without
        // waiting half an hour or changing the musical transport for a photo.
        controller=Controller(o.config.seed);engine.reset(o.config);
        controller.setSnapshot(engine.snapshot());controller.view.batteryPercent=76;
        controller.saved.settings.autoDimSeconds=0;
        controller.tick(0);controller.key('s');controller.view.selection=4;controller.key('/');
        shot("26-sleep-settings");controller.key(27);
        controller.tick(1771000);shot("27-sleep-fading");
        controller.tick(1785000);shot("28-sleep-half-fade");
        controller.tick(1800000);
        if(controller.sleepPausePending()) engine.pause(true);
        for(int i=0;i<4;++i) engine.render(buffer.data(),buffer.size());
        controller.setSnapshot(engine.snapshot());shot("29-sleep-ended");
        controller.key(' '); // Deliberate resume clears the expired timer.
        engine.pause(false);
        for(int i=0;i<4;++i) engine.render(buffer.data(),buffer.size());
        controller.setSnapshot(engine.snapshot());
        controller.key('s');controller.view.selection=9;shot("30-settings-scroll");
        controller.key(27);controller.saved.settings.autoDimSeconds=60;
        controller.tick(1860000);shot("31-auto-dim-simulation");
        controller.key(' ');shot("32-wake-without-pause");
        for(const auto mood:{Mood::Sunny,Mood::Rainy,Mood::Night}) {
            controller=Controller(o.config.seed);auto config=o.config;config.mood=mood;
            engine.reset(config);
            for(int i=0;i<60;++i) engine.render(buffer.data(),buffer.size());
            controller.setSnapshot(engine.snapshot());controller.tick(1500);
            controller.view.batteryPercent=76;
            shot(mood==Mood::Sunny?"33-sunny-day":mood==Mood::Rainy?"35-rainy-day":"36-night-room");
            if(mood==Mood::Sunny) {controller.view.clean=true;shot("34-sunny-clean");}
        }
    }
    if(!o.animation.empty()) {
        fs::create_directories(o.animation);
        engine.reset(o.config);controller=Controller(o.config.seed);std::uint64_t produced=0;
        controller.view.screen=Screen::Radio;controller.view.notice[0]=0;
        for(unsigned i=0;i<o.frames;++i) {
            // Capture actual rendered music activity, including the rhythmic
            // pulse, rather than drawing a fabricated beat or spectrum.
            const auto target=(std::uint64_t(i)+1)*kMusicSampleRate/12;
            while(produced<target) {
                auto n=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),target-produced));
                engine.render(buffer.data(),n);produced+=n;
            }
            controller.setSnapshot(engine.snapshot());controller.tick(std::uint64_t(i)*1000/12);
            View v=controller.view;v.clean=true;v.batteryPercent=76;v.notice[0]=0;
            render(frame,v);char name[32];std::snprintf(name,sizeof(name),"%04u.ppm",i);ppm(o.animation/name,frame);
        }
    }
}
void action(Engine& engine,const Action& a) {
    switch(a.kind) {case ActionKind::Config:engine.requestConfig(a.config);if(a.restartSession)engine.requestNext(a.config.seed);break;case ActionKind::Pause:engine.pause(a.paused);break;case ActionKind::Next:engine.requestNext();break;case ActionKind::None:break;}
}
int interactive(const Options& o) {
#ifdef LOFI_HAS_SDL
    if(SDL_Init(SDL_INIT_VIDEO|(o.noAudio?0:SDL_INIT_AUDIO))!=0) throw std::runtime_error(SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
    auto* window=SDL_CreateWindow("Catputer | SPACE play | M moods | S settings | I instruments | E engine | H help | Q quit",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,540,SDL_WINDOW_RESIZABLE);
    if(!window) throw std::runtime_error(SDL_GetError());
    auto* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer) renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer) throw std::runtime_error(SDL_GetError());
    SDL_RenderSetLogicalSize(renderer,240,135);
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGB565,SDL_TEXTUREACCESS_STREAMING,240,135);
    if(!texture) throw std::runtime_error(SDL_GetError());
    SDL_AudioSpec spec{};spec.freq=kMusicSampleRate;spec.format=AUDIO_S16SYS;spec.channels=1;spec.samples=512;
    SDL_AudioDeviceID audio=o.noAudio?0:SDL_OpenAudioDevice(nullptr,0,&spec,nullptr,0);
    if(!o.noAudio && !audio) throw std::runtime_error(std::string("Audio: ")+SDL_GetError());
    Controller controller(o.config.seed);
    if(!o.state.empty()) { loadState(o.state,controller.saved);controller.view.sdReady=true; }
    Config cfg=o.config;
    if(!o.state.empty()) {
        if(!o.moodExplicit) cfg.mood=static_cast<Mood>(controller.saved.settings.mood);
        if(!o.engineExplicit) cfg.soundEngine=static_cast<SoundEngine>(controller.saved.settings.engine);
        if(!o.bpmExplicit) cfg.bpm=controller.saved.settings.bpm;
        if(!o.meterExplicit) cfg.meter=controller.saved.settings.meter;
        if(!o.keysExplicit) cfg.keysTone=controller.saved.settings.keysTone;
        if(!o.leadExplicit) cfg.leadTone=controller.saved.settings.leadTone;
        if(!o.bassExplicit) cfg.bassTone=controller.saved.settings.bassTone;
        cfg.texture=controller.saved.settings.texture;
    }
    controller.saved.settings.mood=static_cast<std::uint8_t>(cfg.mood);
    controller.saved.settings.engine=static_cast<std::uint8_t>(cfg.soundEngine);
    controller.saved.settings.texture=cfg.texture;
    controller.saved.settings.bpm=cfg.bpm;
    controller.saved.settings.meter=cfg.meter;controller.saved.settings.keysTone=cfg.keysTone;
    controller.saved.settings.leadTone=cfg.leadTone;controller.saved.settings.bassTone=cfg.bassTone;
    if(o.volumeExplicit) controller.saved.settings.volume=o.volume;
    OutputGain gain(controller.saved.settings.volume);
    Engine engine(cfg); Frame frame; std::array<std::int16_t,512> pcm{}; std::array<std::uint16_t,240*135> pixels{};
    if(audio) SDL_PauseAudioDevice(audio,0);
    bool running=true;std::uint64_t lastDraw=0,lastSave=0,silentProduced=0;
    SDL_Keycode wakeKey=SDLK_UNKNOWN;
    const auto start=SDL_GetTicks64();
    while(running) {
        const auto now=SDL_GetTicks64()-start;
        controller.setSnapshot(engine.snapshot());controller.tick(now);
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_QUIT) running=false;
            if(event.type==SDL_KEYUP && event.key.keysym.sym==wakeKey) wakeKey=SDLK_UNKNOWN;
            if(event.type==SDL_KEYDOWN) {
                int key=event.key.keysym.sym;
                if(key==wakeKey) continue;
                if(event.key.repeat && key!=SDLK_MINUS && key!=SDLK_EQUALS) continue;
                if(key==SDLK_q && !controller.view.dimmed) {running=false;continue;}
                const bool wasDimmed=controller.view.dimmed;
                if(key==SDLK_RETURN) key='\n';else if(key==SDLK_ESCAPE) key=27;else if(key==SDLK_BACKSPACE) key=127;
                else if(key==SDLK_UP) key=';';else if(key==SDLK_DOWN) key='.';else if(key==SDLK_LEFT) key=',';else if(key==SDLK_RIGHT) key='/';
                action(engine,controller.key(key));
                if(wasDimmed) wakeKey=event.key.keysym.sym;
            }
        }
        if(controller.sleepPausePending()) engine.pause(true);
        gain.setVolume(controller.saved.settings.volume);
        gain.setSleepGain(controller.sleepGainQ15());
        unsigned produced=0;
        while(produced<6 && ((audio && SDL_GetQueuedAudioSize(audio)<6144) || (!audio && silentProduced<now*kMusicSampleRate/1000))) {
            engine.render(pcm.data(),pcm.size());
            gain.process(pcm.data(),pcm.size());
            if(audio && SDL_QueueAudio(audio,pcm.data(),pcm.size()*2)!=0) throw std::runtime_error(SDL_GetError());
            silentProduced+=pcm.size();++produced;
        }
        const unsigned interval=controller.saved.settings.motion==2?83:controller.saved.settings.motion==1?166:250;
        if(now-lastDraw>=interval) {
            auto snap=engine.snapshot();
            // SDL reports queued source PCM; display pulse follows an estimate
            // of playback rather than the end of our pre-rendered queue.
            if(audio) {
                const auto queuedFrames=SDL_GetQueuedAudioSize(audio)/sizeof(std::int16_t);
                const auto phaseOffset=(std::uint64_t(queuedFrames)*snap.bpm*65536u)/
                    (std::uint64_t(kMusicSampleRate)*60u*snap.beatsPerBar);
                snap.barPhaseQ16=static_cast<std::uint16_t>(snap.barPhaseQ16-phaseOffset);
                snap.sixteenth=static_cast<std::uint8_t>((std::uint32_t(snap.barPhaseQ16)*snap.stepsPerBar)/65536u);
                snap.beat=static_cast<std::uint8_t>(snap.sixteenth/snap.stepsPerBeat);
            }
            controller.setSnapshot(snap);controller.tick(now);View v=controller.view;
            if(!controller.saved.settings.motion) v.timeMs=0;
            render(frame,v);for(int y=0;y<135;++y) frame.rowRgb565(y,pixels.data()+y*240);
            // Simulate the LCD backlight relative to the normal preview palette.
            const auto brightness=static_cast<std::uint8_t>(255u*controller.effectiveBrightness()/std::max(1u,unsigned(controller.saved.settings.brightness)));
            SDL_SetTextureColorMod(texture,brightness,brightness,brightness);
            SDL_UpdateTexture(texture,nullptr,pixels.data(),240*2);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);SDL_RenderPresent(renderer);lastDraw=now;
        }
        if(controller.dirty && !o.state.empty() && now-lastSave>2000) {controller.dirty=false;controller.storageResult(saveState(o.state,controller.saved));lastSave=now;}
        if(o.smokeMs && now>=o.smokeMs) running=false;
        SDL_Delay(2);
    }
    if(controller.dirty && !o.state.empty()) saveState(o.state,controller.saved);
    if(audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
#else
    (void)o; throw std::runtime_error("Built without SDL2. Use --wav/--shots, or install SDL2 and rebuild.");
#endif
}
}
int main(int argc,char** argv) {
    try {auto o=options(argc,argv);if(!o.wav.empty()) exportAudio(o);if(!o.score.empty()) exportScore(o);if(!o.shots.empty() || !o.animation.empty()) exportScreens(o);
        if(o.wav.empty() && o.score.empty() && o.shots.empty() && o.animation.empty()) return interactive(o);
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Error: "<<e.what()<<'\n';return 1;}
}
