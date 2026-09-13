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
    assert(c.view.itemCount == 8 && c.view.selection == 7);
    c.saved.settings.volume = 300;
    auto reset = c.key('\n');
    assert(reset.kind == ActionKind::Config && c.saved.settings.volume == 35 &&
           c.saved.settings.bpm == 0);
    c.tick(200);
    assert(c.view.itemCount == 8);
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
    Snapshot favoriteSnapshot = snapshotFor(favorite, 96, 96);
    favorite.key('f');
    assert(favorite.saved.count == 1 && favorite.saved.favorites[0].bpm == 96);
    favorite.key('l');
    assert(std::strlen(favorite.view.items[0]) <= 26);
    assert(std::strstr(favorite.view.items[0], "96") != nullptr);
    auto favoriteReplay = favorite.key('\n');
    assert(favoriteReplay.kind == ActionKind::Config && favoriteReplay.restartSession &&
           favoriteReplay.config.bpm == 96);
    assert(favorite.saved.settings.bpm == 96);
    (void)favoriteSnapshot;

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
