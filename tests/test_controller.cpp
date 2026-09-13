#include "lofi/controller.h"
#include "lofi/sample_bank.h"

#include <cassert>
#include <cstring>
#include <iostream>

namespace {

lofi::Snapshot snapshotFor(lofi::Controller& controller, std::uint16_t audibleBpm,
                           std::uint16_t configuredBpm = 0) {
    lofi::Snapshot snapshot;
    snapshot.config = controller.initialConfig();
    snapshot.config.bpm = configuredBpm;
    snapshot.bpm = audibleBpm;
    controller.setSnapshot(snapshot);
    controller.tick(100);
    return snapshot;
}

} // namespace

int main() {
    using namespace lofi;

    Controller sunnyMenu(42);
    auto sunnySnapshot = snapshotFor(sunnyMenu, 78);
    sunnyMenu.key('m');
    assert(sunnyMenu.view.itemCount == 4 && std::strcmp(sunnyMenu.view.items[3], "Sunny") == 0);
    for(int i=0;i<3;++i) sunnyMenu.key('.');
    const auto sunnyAction = sunnyMenu.key('\n');
    assert(sunnyAction.kind == ActionKind::Config && sunnyAction.config.mood == Mood::Sunny);
    assert(sunnyMenu.saved.settings.mood == 3);
    // Scene uses the audible snapshot, not a pending menu choice.
    sunnyMenu.populateView();
    assert(sunnyMenu.view.mood == 0);
    sunnySnapshot.config = sunnyAction.config;
    sunnyMenu.setSnapshot(sunnySnapshot);sunnyMenu.populateView();
    assert(sunnyMenu.view.mood == 3);
    sunnyMenu.key('f');sunnyMenu.key('l');
    const auto sunnyRecall = sunnyMenu.key('\n');
    assert(sunnyRecall.kind == ActionKind::Config && sunnyRecall.restartSession &&
           sunnyRecall.config.mood == Mood::Sunny && sunnyMenu.saved.favorites[0].schema == 5);

    Controller c(UINT64_C(0x123456789abcdef0));
    const Snapshot snap = snapshotFor(c, 72);
    c.key('f');
    assert(c.saved.count == 1 && c.saved.favorites[0].seed == snap.config.seed);
    assert(c.saved.favorites[0].bpm == 0);
    c.key('l');
    auto recall = c.key('\n');
    assert(recall.kind == ActionKind::Config && recall.config.seed == snap.config.seed &&
           recall.restartSession);
    assert(recall.config.bpm == 0);

    c.saved.favorites[0].seed = 42;
    c.key('l');
    const auto seedRecall = c.key('\n');
    assert(seedRecall.config.seed == 42 && seedRecall.restartSession);
    auto changed = c.key('e');
    assert(changed.config.seed == 42 && changed.config.soundEngine == SoundEngine::Hybrid);
    auto pause = c.key(' ');
    auto resume = c.key(' ');
    assert(pause.paused && !resume.paused);

    c.saved.favorites[0].engine = 1;
    c.saved.favorites[0].bankFingerprint = 0;
    c.key('l');
    assert(c.key('\n').kind == ActionKind::None);
    c.saved.favorites[0].engine = 0;
    for (std::uint8_t schema = 1; schema < kSessionSchema; ++schema) {
        c.saved.favorites[0].schema = schema;
        c.key('l');
        assert(std::strstr(c.view.items[0], "OLD") != nullptr);
        assert(c.key('\n').kind == ActionKind::None);
        assert(std::strstr(c.view.notice, "EARLIER VERSION") != nullptr);
    }
    c.key(127);
    assert(c.saved.count == 0); // Obsolete favorites remain removable.

    c.key('s');
    for (int i = 0; i < 20; ++i) {
        c.key('.');
    }
    assert(c.view.itemCount == 10 && c.view.selection == 9);
    c.saved.settings.volume = 300;
    auto reset = c.key('\n');
    assert(reset.kind == ActionKind::Config && c.saved.settings.volume == 35 &&
           c.saved.settings.bpm == 0);
    c.tick(200);
    assert(c.view.itemCount == 10);
    for (int i = 0; i < 20; ++i) {
        c.key(';');
    }
    assert(c.view.selection == 0);
    for (int i = 0; i < 80; ++i) {
        c.key(',');
    }
    assert(c.saved.settings.volume == 0);
    for (int i = 0; i < 80; ++i) {
        c.key('/');
    }
    assert(c.saved.settings.volume == 300);

    // Manual BPM starts from the audible AUTO tempo, changes by one, and
    // toggles back to AUTO with Enter. Each tempo edit is a bar-boundary
    // config action and coalesces with any already pending config request.
    Controller tempo(UINT64_C(0x1122334455667788));
    snapshotFor(tempo, 72);
    tempo.key('s');
    tempo.key('.');
    assert(tempo.view.selection == 1);
    assert(std::strstr(tempo.view.items[1], "AUTO") != nullptr);
    auto manual = tempo.key('\n');
    assert(manual.kind == ActionKind::Config && !manual.restartSession && manual.config.bpm == 72 &&
           tempo.saved.settings.bpm == 72);
    auto lower = tempo.key(',');
    assert(lower.kind == ActionKind::Config && lower.config.bpm == 71 &&
           tempo.saved.settings.bpm == 71);
    auto upper = tempo.key('/');
    assert(upper.kind == ActionKind::Config && upper.config.bpm == 72);
    auto automatic = tempo.key('\n');
    assert(automatic.kind == ActionKind::Config && automatic.config.bpm == 0 &&
           tempo.saved.settings.bpm == 0);

    tempo.saved.settings.bpm = kMusicMinBpm;
    auto atMinimum = tempo.key(',');
    assert(atMinimum.kind == ActionKind::None && tempo.saved.settings.bpm == kMusicMinBpm);
    tempo.saved.settings.bpm = kMusicMaxBpm;
    auto atMaximum = tempo.key('/');
    assert(atMaximum.kind == ActionKind::None && tempo.saved.settings.bpm == kMusicMaxBpm);

    // A mood change after a pending BPM change must retain the latest tempo.
    tempo.saved.settings.bpm = 110;
    auto bpmPending = tempo.key(',');
    assert(bpmPending.config.bpm == 109);
    tempo.key('m');
    tempo.key('.');
    auto mixed = tempo.key('\n');
    assert(mixed.kind == ActionKind::Config && !mixed.restartSession && mixed.config.mood == Mood::Rainy &&
           mixed.config.bpm == 109);

    // Simulate the engine applying the coalesced request; the next request
    // must compare the new BPM as part of its applied identity.
    Snapshot applied = tempo.snapshot();
    applied.config = mixed.config;
    applied.bpm = 109;
    applied.changePending = false;
    tempo.setSnapshot(applied);
    tempo.key('s');
    tempo.key('.');
    auto toggleAfterApply = tempo.key('\n');
    assert(toggleAfterApply.kind == ActionKind::Config && toggleAfterApply.config.bpm == 0);

    // Master volume is independent from Config::volume and cannot wrap at 255.
    tempo.saved.settings.volume = 295;
    tempo.key('=');
    assert(tempo.saved.settings.volume == 300);
    tempo.key('=');
    assert(tempo.saved.settings.volume == 300);
    tempo.key('-');
    assert(tempo.saved.settings.volume == 295);

    // Favorites retain and replay manual tempo, and the compact list stays
    // within the renderer's 26-character item budget.
    Controller favorite(UINT64_C(0x0badcafe));
    favorite.saved.settings.meter = MusicMeter::SixEight;
    favorite.saved.settings.keysTone = Tone::NylonGuitar;
    favorite.saved.settings.leadTone = Tone::WarmPad;
    favorite.saved.settings.bassTone = BassTone::Upright;
    Snapshot favoriteSnapshot = snapshotFor(favorite, 96, 96);
    favorite.key('f');
    assert(favorite.saved.count == 1 && favorite.saved.favorites[0].bpm == 96 &&
           favorite.saved.favorites[0].meter == MusicMeter::SixEight &&
           favorite.saved.favorites[0].keysTone == Tone::NylonGuitar &&
           favorite.saved.favorites[0].leadTone == Tone::WarmPad &&
           favorite.saved.favorites[0].bassTone == BassTone::Upright);
    favorite.key('l');
    assert(std::strlen(favorite.view.items[0]) <= 26);
    assert(std::strstr(favorite.view.items[0], "96") != nullptr);
    auto favoriteReplay = favorite.key('\n');
    assert(favoriteReplay.kind == ActionKind::Config && favoriteReplay.restartSession &&
           favoriteReplay.config.bpm == 96 && favoriteReplay.config.meter == MusicMeter::SixEight &&
           favoriteReplay.config.keysTone == Tone::NylonGuitar &&
           favoriteReplay.config.leadTone == Tone::WarmPad &&
           favoriteReplay.config.bassTone == BassTone::Upright);
    assert(favorite.saved.settings.bpm == 96);
    (void)favoriteSnapshot;

    // Instrument selection is a separate four-row screen. Comma/slash and
    // Enter cycle each bounded selector, and every edit coalesces into the
    // same next-bar Config action.
    Controller instruments(UINT64_C(0x33445566));
    snapshotFor(instruments, 88);
    instruments.key('i');
    assert(instruments.view.screen == Screen::Instruments && instruments.view.itemCount == 4);
    auto chordChange = instruments.key('\n');
    assert(chordChange.kind == ActionKind::Config &&
           chordChange.config.keysTone == Tone::FeltPiano &&
           instruments.saved.settings.keysTone == Tone::FeltPiano);
    instruments.key('.');
    auto melodyChange = instruments.key('/');
    assert(melodyChange.kind == ActionKind::Config &&
           melodyChange.config.leadTone == Tone::WarmPad);
    instruments.key('.');
    auto bassChange = instruments.key(',');
    assert(bassChange.kind == ActionKind::Config &&
           bassChange.config.bassTone == BassTone::Sub);
    instruments.key('.');
    auto meterChange = instruments.key('\n');
    assert(meterChange.kind == ActionKind::Config &&
           meterChange.config.meter == MusicMeter::FourFour);
    // Wrap-around is deliberate: comma from the first meter returns to 6/8.
    auto previousMeter = instruments.key(',');
    assert(previousMeter.config.meter == MusicMeter::Auto);
    assert(instruments.view.items[3][0] == 'M');

    // Role levels are the engine's actual snapshot values, but paused and
    // muted views deliberately clear them before the renderer sees them.
    Snapshot levelSnapshot = instruments.snapshot();
    levelSnapshot.paused = false;
    levelSnapshot.instrumentLevels[0] = 42;
    levelSnapshot.instrumentLevels[1] = 18;
    levelSnapshot.recentPeak = 96;
    instruments.saved.settings.volume = 100;
    instruments.setSnapshot(levelSnapshot);
    instruments.populateView();
    assert(instruments.view.instrumentLevels[0] == 42 && instruments.view.instrumentLevels[1] == 18);
    levelSnapshot.paused = true;
    instruments.setSnapshot(levelSnapshot);
    instruments.populateView();
    assert(instruments.view.instrumentLevels[0] == 0 && instruments.view.level == 0.0f);

    // Sleep is a runtime-only wall-clock timer. Each selection starts a fresh
    // duration, including while playback is already paused.
    Controller sleep(UINT64_C(0x88776655));
    Snapshot sleepSnapshot = snapshotFor(sleep, 72);
    sleep.saved.settings.autoDimSeconds = 0;
    sleep.key('s');
    for (int i = 0; i < 4; ++i) sleep.key('.');
    assert(sleep.view.selection == 4);
    sleep.key('\n');
    assert(sleep.view.sleepTimerActive && sleep.view.sleepSecondsRemaining == 1800 &&
           sleep.sleepGainQ15() == 32768 && !sleep.sleepPausePending());
    sleep.key('/');
    assert(sleep.view.sleepSecondsRemaining == 3600);
    sleep.key('/');
    assert(sleep.view.sleepSecondsRemaining == 5400);
    sleep.key('/');
    assert(!sleep.view.sleepTimerActive && sleep.sleepGainQ15() == 32768);

    // The last 30 seconds fade linearly without changing the saved volume.
    sleep.key('\n'); // 30 minutes from the current fake time (100 ms).
    const std::uint64_t deadline = 100U + 30U * 60U * 1000U;
    sleep.saved.settings.volume = 135;
    sleep.tick(deadline - 30000U);
    assert(sleep.sleepGainQ15() == 32768 && sleep.view.sleepSecondsRemaining == 30);
    sleep.tick(deadline - 15000U);
    assert(sleep.sleepGainQ15() == 16384 && sleep.view.sleepSecondsRemaining == 15 &&
           sleep.saved.settings.volume == 135);
    sleepSnapshot.instrumentLevels[0] = 100;
    sleepSnapshot.recentPeak = 16384;
    sleep.setSnapshot(sleepSnapshot);
    sleep.populateView();
    assert(sleep.view.instrumentLevels[0] == 50 && sleep.view.level > 0.249f &&
           sleep.view.level < 0.251f);
    sleep.tick(deadline);
    assert(sleep.sleepGainQ15() == 0 && sleep.sleepPausePending() &&
           sleep.view.sleepExpired && sleep.saved.settings.volume == 135);

    // A fresh still-playing snapshot at the deadline cannot clear the desired
    // pause. Space deliberately reverses that pending pause and cancels the
    // expired timer, so a later stale paused snapshot cannot re-arm it.
    sleep.setSnapshot(sleepSnapshot);
    assert(sleep.sleepPausePending());
    const auto raceResume = sleep.key(' ');
    assert(raceResume.kind == ActionKind::Pause && !raceResume.paused &&
           !sleep.sleepPausePending() && sleep.sleepGainQ15() == 32768);
    sleepSnapshot.paused = true;
    sleep.setSnapshot(sleepSnapshot);
    sleep.tick(deadline + 1);
    assert(!sleep.sleepPausePending() && !sleep.view.sleepTimerActive &&
           !sleep.view.sleepExpired);

    Controller cancelRace(UINT64_C(0x55667788));
    Snapshot cancelSnapshot = snapshotFor(cancelRace, 72);
    cancelRace.saved.settings.autoDimSeconds = 0;
    cancelRace.key('s');
    for (int i = 0; i < 4; ++i) cancelRace.key('.');
    cancelRace.key('\n');
    cancelRace.tick(deadline);
    assert(cancelRace.sleepPausePending());
    cancelRace.setSnapshot(cancelSnapshot); // Still-playing deadline snapshot.
    const auto cancelAtDeadline = cancelRace.key(',');
    assert(cancelAtDeadline.kind == ActionKind::Pause && !cancelAtDeadline.paused &&
           !cancelRace.sleepPausePending() && !cancelRace.view.sleepTimerActive &&
           cancelRace.sleepGainQ15() == 32768);

    Controller pausedSleep(UINT64_C(0x12344321));
    Snapshot pausedSnapshot = snapshotFor(pausedSleep, 72);
    pausedSleep.saved.settings.autoDimSeconds = 0;
    pausedSnapshot.paused = true;
    pausedSleep.setSnapshot(pausedSnapshot);
    pausedSleep.key('s');
    for (int i = 0; i < 4; ++i) pausedSleep.key('.');
    pausedSleep.key('\n');
    pausedSleep.tick(100U + 15U * 60U * 1000U);
    assert(pausedSleep.view.sleepSecondsRemaining == 900);
    pausedSleep.tick(100U + 30U * 60U * 1000U);
    assert(pausedSleep.view.sleepExpired && !pausedSleep.sleepPausePending());
    const auto pausedResume = pausedSleep.key(' ');
    assert(pausedResume.kind == ActionKind::Pause && !pausedResume.paused &&
           !pausedSleep.view.sleepTimerActive);

    // Default auto-dim occurs after 60 seconds. The first key only restores
    // brightness; a repeated key performs the intended action.
    Controller dim(UINT64_C(0x10203040));
    snapshotFor(dim, 72);
    assert(dim.saved.settings.autoDimSeconds == 60 && dim.effectiveBrightness() == 70);
    dim.tick(60100);
    assert(dim.view.dimmed && dim.effectiveBrightness() == 10);
    const auto wakeOnly = dim.key(' ');
    assert(wakeOnly.kind == ActionKind::None && !dim.view.dimmed &&
           dim.effectiveBrightness() == 70);
    const auto afterWake = dim.key(' ');
    assert(afterWake.kind == ActionKind::Pause && afterWake.paused);

    // Auto-dim cycles OFF/30/60/120 and does not mutate on the wake-only key.
    Controller dimSettings(UINT64_C(0x99887766));
    snapshotFor(dimSettings, 72);
    dimSettings.key('s');
    for (int i = 0; i < 3; ++i) dimSettings.key('.');
    assert(dimSettings.view.selection == 3);
    dimSettings.tick(60100);
    assert(dimSettings.view.dimmed);
    dimSettings.key('/');
    assert(dimSettings.saved.settings.autoDimSeconds == 60);
    dimSettings.key('/');
    assert(dimSettings.saved.settings.autoDimSeconds == 120);
    dimSettings.key('/');
    assert(dimSettings.saved.settings.autoDimSeconds == 0);
    dimSettings.tick(999999);
    assert(!dimSettings.view.dimmed);
    levelSnapshot.paused = false;
    instruments.saved.settings.volume = 0;
    instruments.setSnapshot(levelSnapshot);
    instruments.populateView();
    assert(instruments.view.instrumentLevels[0] == 0 && instruments.view.level == 0.0f);

    c.dirty = false;
    c.storageResult(true);
    assert(!c.dirty);
    c.key('-');
    c.storageResult(true);
    assert(c.dirty); // A previous save must not erase a newer edit.
    c.dirty = false;
    c.storageResult(false);
    assert(c.dirty);
    c.key(27);
    c.tick(10000);
    assert(c.view.clean);
    c.key('h');
    c.tick(10001);
    assert(!c.view.clean);

    std::cout << "controller: wide volume, AUTO/manual BPM, coalescing, favorites and bounds passed\n";
}
