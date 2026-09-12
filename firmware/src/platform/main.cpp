#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <atomic>
#include <array>
#include "lofi/controller.h"
#include "lofi/music.h"
#include "lofi/state.h"
#include "lofi/ui.h"

namespace {
using namespace lofi;
Engine engine;
Controller controller;
Frame frame;
QueueHandle_t commands=nullptr, saves=nullptr;
portMUX_TYPE snapshotLock=portMUX_INITIALIZER_UNLOCKED;
Snapshot published;
std::uint32_t worstRenderUs=0, queueEmptyObservations=0, submittedBlocks=0;
std::atomic<int> saveResult{0};
std::atomic<unsigned> outputVolume{35};
bool ready=false, sdMounted=false;
std::uint32_t lastDraw=0,lastBattery=0,lastSave=0,lastDiagnostic=0,lastKey=0,lastVolumeRepeat=0;
std::uint64_t previousKeys=0, elapsedMs=0;
std::uint32_t previousMs=0;
constexpr char kBuildIdentity[]="cardputer-adv-lofi/application/" LOFI_VERSION;

void quietAdvCodec() {
    if(M5.getBoard()!=m5::board_t::board_M5CardputerADV) return;
    // M5Unified 0.2.17's Cardputer ADV disable callback is empty. Mirror the
    // verified Hermes shutdown state after I2S stops so failures stay quiet.
    M5Cardputer.In_I2C.writeRegister8(0x18,0x31,0x60,100000);
    M5Cardputer.In_I2C.writeRegister8(0x18,0x32,0x00,100000);
    M5Cardputer.In_I2C.writeRegister8(0x18,0x13,0x00,100000);
    M5Cardputer.In_I2C.writeRegister8(0x18,0x12,0x02,100000);
}

bool readStateFile(const char* path,SavedState& value) {
    auto file=SD.open(path,FILE_READ);if(!file || file.isDirectory() || file.size()!=kStateBytes) return false;
    std::array<std::uint8_t,kStateBytes> bytes{};
    const bool good=file.read(bytes.data(),bytes.size())==bytes.size();file.close();
    return good && decodeState(bytes.data(),bytes.size(),value);
}
bool writeStateFile(const SavedState& value) {
    std::array<std::uint8_t,kStateBytes> bytes{};
    if(!encodeState(value,bytes) || (!SD.exists("/LOFI") && !SD.mkdir("/LOFI"))) return false;
    if(SD.exists("/LOFI/state.tmp") && !SD.remove("/LOFI/state.tmp")) return false;
    auto file=SD.open("/LOFI/state.tmp",FILE_WRITE);if(!file) return false;
    const bool written=file.write(bytes.data(),bytes.size())==bytes.size();file.flush();file.close();
    SavedState verified;if(!written || !readStateFile("/LOFI/state.tmp",verified)) return false;
    // Keep one known-good backup. A failed rename leaves the backup readable.
    if(SD.exists("/LOFI/state.bin")) {
        SavedState previous;
        if(readStateFile("/LOFI/state.bin",previous)) {
            if(SD.exists("/LOFI/state.bak") && !SD.remove("/LOFI/state.bak")) return false;
            if(!SD.rename("/LOFI/state.bin","/LOFI/state.bak")) return false;
        } else if(!SD.remove("/LOFI/state.bin")) return false;
    }
    return SD.rename("/LOFI/state.tmp","/LOFI/state.bin");
}
void storageTask(void*) {
    SavedState next;
    for(;;) {
        if(xQueueReceive(saves,&next,portMAX_DELAY)==pdTRUE) saveResult.store(writeStateFile(next)?1:-1,std::memory_order_release);
    }
}
void audioTask(void*) {
    // M5Unified 0.2.17 explicitly requires three runtime buffers in rotation:
    // at most two are queued, so the third remains application-owned while filled.
    static std::int16_t pcm[3][512];
    unsigned index=0;bool pending=false,started=false;unsigned volume=101;
    for(;;) {
        Action command;
        while(xQueueReceive(commands,&command,0)==pdTRUE) {
            switch(command.kind) {
                case ActionKind::Config:engine.requestConfig(command.config);break;
                case ActionKind::Pause:engine.pause(command.paused);break;
                case ActionKind::Next:engine.requestNext();break;
                case ActionKind::None:break;
            }
        }
        const unsigned requested=outputVolume.load(std::memory_order_relaxed);
        if(volume!=requested) {volume=requested;M5Cardputer.Speaker.setVolume(volume*255/100);}
        const auto queued=M5Cardputer.Speaker.isPlaying(0);
        if(queued>=2) {vTaskDelay(1);continue;}
        if(!pending) {
            const auto before=micros();engine.render(pcm[index],512);const auto duration=std::uint32_t(micros()-before);
            auto snap=engine.snapshot();
            // Approximate audible position: queued source blocks plus configured
            // four x 256-frame DMA buffering. Exact hardware latency is unmeasured.
            const std::uint32_t latencyFrames=static_cast<std::uint32_t>(queued)*512+1024;
            const std::uint32_t phaseOffset=static_cast<std::uint32_t>((std::uint64_t(latencyFrames)*snap.bpm*65536)/(kMusicSampleRate*240ull));
            snap.barPhaseQ16=static_cast<std::uint16_t>(snap.barPhaseQ16-phaseOffset);
            portENTER_CRITICAL(&snapshotLock);
            published=snap;if(duration>worstRenderUs) worstRenderUs=duration;
            if(started && queued==0) ++queueEmptyObservations;
            portEXIT_CRITICAL(&snapshotLock);
            pending=true;
        }
        if(M5Cardputer.Speaker.playRaw(pcm[index],512,kMusicSampleRate,false,1,0,false)) {
            index=(index+1)%3;pending=false;started=true;
            portENTER_CRITICAL(&snapshotLock);++submittedBlocks;portEXIT_CRITICAL(&snapshotLock);
        } else vTaskDelay(1);
    }
}
void sendKey(int key) {
    lastKey=millis();const auto previous=controller;auto result=controller.key(key);
    if(result.kind!=ActionKind::None && xQueueSend(commands,&result,pdMS_TO_TICKS(20))!=pdTRUE) {
        controller=previous;controller.notice("AUDIO BUSY - TRY AGAIN");return;
    }
    outputVolume.store(controller.saved.settings.volume,std::memory_order_relaxed);
    M5Cardputer.Display.setBrightness(controller.saved.settings.brightness*255/100);
}
void drawFrame() {
    auto v=controller.view;
    if(controller.saved.settings.motion==0) {v.timeMs=0;v.beatPhase=0;}
    render(frame,v);
    std::uint16_t row[kScreenWidth];
    M5Cardputer.Display.startWrite();
    for(int y=0;y<kScreenHeight;++y) {frame.rowRgb565(y,row);M5Cardputer.Display.pushImage(0,y,kScreenWidth,1,row);}
    M5Cardputer.Display.endWrite();
}
void fail(const char* text) {
    M5Cardputer.Speaker.stop();M5Cardputer.Speaker.end();quietAdvCodec();
    M5Cardputer.Display.fillScreen(0);M5Cardputer.Display.setTextSize(1);M5Cardputer.Display.setTextColor(TFT_WHITE);M5Cardputer.Display.setCursor(8,20);M5Cardputer.Display.println(text);
    Serial.println(text);
}
}

void setup() {
    using namespace lofi;
    Serial.begin(115200);
    auto config=M5.config();config.internal_mic=false;config.internal_spk=true;
    M5Cardputer.begin(config,true);M5Cardputer.Display.setRotation(1);M5Cardputer.Display.setBrightness(150);
    if(M5.getBoard()!=m5::board_t::board_M5CardputerADV) {fail("Cardputer ADV required");return;}
    // No Wi-Fi/BLE or generic NVS initialization: built-in playback is offline.
    SPI.begin(40,39,14,12);sdMounted=SD.begin(12,SPI,10000000) && SD.cardType()!=CARD_NONE;
    if(sdMounted) {
        if(!readStateFile("/LOFI/state.bin",controller.saved)) {
            if(!readStateFile("/LOFI/state.bak",controller.saved)) readStateFile("/LOFI/state.tmp",controller.saved);
        }
    }
    controller.view.sdReady=sdMounted;
    auto initial=controller.initialConfig();initial.seed=(std::uint64_t(esp_random())<<32)|esp_random();engine.reset(initial);
    controller.setSnapshot(engine.snapshot());controller.notice("SPACE PLAY  M MOOD  H HELP",6000);
    controller.view.batteryPercent=M5Cardputer.Power.getBatteryLevel();
    M5Cardputer.Display.setBrightness(controller.saved.settings.brightness*255/100);
    M5Cardputer.Speaker.end();
    auto speaker=M5Cardputer.Speaker.config();speaker.sample_rate=kMusicSampleRate;speaker.stereo=false;
    speaker.dma_buf_len=256;speaker.dma_buf_count=4;speaker.task_priority=4;speaker.task_pinned_core=1;
    M5Cardputer.Speaker.config(speaker);
    if(!M5Cardputer.Speaker.begin()) {fail("Audio initialization failed");return;}
    M5Cardputer.Speaker.setVolume(controller.saved.settings.volume*255/100);outputVolume.store(controller.saved.settings.volume);
    commands=xQueueCreate(8,sizeof(Action));saves=xQueueCreate(1,sizeof(SavedState));
    if(!commands || !saves) {fail("Audio queue allocation failed");return;}
    if(sdMounted && xTaskCreatePinnedToCore(storageTask,"lofi-storage",4096,nullptr,1,nullptr,0)!=pdPASS) {
        controller.view.sdReady=false;sdMounted=false;controller.notice("SD WORKER FAILED - MEMORY ONLY");
    }
    published=engine.snapshot();
    if(xTaskCreatePinnedToCore(audioTask,"lofi-audio",6144,nullptr,3,nullptr,1)!=pdPASS) {fail("Audio worker allocation failed");return;}
    ready=true;drawFrame();
    Serial.printf("%s ready. SD=%d heap=%u largest=%u scene=%u engine=%u\n",kBuildIdentity,sdMounted,ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),unsigned(Frame::packedBytes),unsigned(sizeof(Engine)));
}

void loop() {
    using namespace lofi;
    if(!ready) {delay(50);return;}
    const auto now=millis();elapsedMs+=std::uint32_t(now-previousMs);previousMs=now;
    Snapshot snap;portENTER_CRITICAL(&snapshotLock);snap=published;portEXIT_CRITICAL(&snapshotLock);
    controller.setSnapshot(snap);controller.tick(elapsedMs);
    M5Cardputer.update();std::uint64_t keys=0;
    for(const auto& pos:M5Cardputer.Keyboard.keyList()) {
        if(pos.x<0 || pos.x>=14 || pos.y<0 || pos.y>=4) continue;
        const auto bit=std::uint64_t(1)<<(pos.y*14+pos.x);keys|=bit;
        const auto value=M5Cardputer.Keyboard.getKey(pos);
        const bool volumeKey=value=='-' || value=='=';
        if(!(previousKeys&bit) || (volumeKey && std::uint32_t(now-lastVolumeRepeat)>130)) {
            if(value==KEY_ENTER) sendKey('\n');else if(value==KEY_BACKSPACE) sendKey(127);
            else if(value>=32 && value<127) sendKey(value);
            if(volumeKey) lastVolumeRepeat=now;
        }
    }
    previousKeys=keys;
    // Go opens help. The physical reset/Home button remains hardware-managed.
    if(M5Cardputer.BtnA.wasPressed()) sendKey('h');
    const int result=saveResult.exchange(0,std::memory_order_acq_rel);
    if(result) {
        controller.storageResult(result>0);
        if(result<0) {sdMounted=false;controller.notice("SD SAVE FAILED - RESTART TO RETRY",6000);}
    }
    if(controller.dirty && sdMounted && std::uint32_t(now-lastKey)>2000 && std::uint32_t(now-lastSave)>3000) {
        // Queue receives a value copy; storage never observes mutable UI state.
        if(xQueueOverwrite(saves,&controller.saved)==pdTRUE) {controller.dirty=false;lastSave=now;}
    }
    if(std::uint32_t(now-lastBattery)>5000) {controller.view.batteryPercent=M5Cardputer.Power.getBatteryLevel();lastBattery=now;}
    const unsigned period=controller.saved.settings.motion==2?83:controller.saved.settings.motion==1?166:250;
    if(std::uint32_t(now-lastDraw)>=period || lastKey==now) {controller.populateView();drawFrame();lastDraw=now;}
    if(std::uint32_t(now-lastDiagnostic)>10000) {
        std::uint32_t worst,empty,blocks;portENTER_CRITICAL(&snapshotLock);worst=worstRenderUs;empty=queueEmptyObservations;blocks=submittedBlocks;portEXIT_CRITICAL(&snapshotLock);
        Serial.printf("LOFI heap=%u min=%u largest=%u render_max_us=%u queue_empty=%u blocks=%u\n",ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),worst,empty,blocks);lastDiagnostic=now;
    }
    delay(2);
}
