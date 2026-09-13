#pragma once
#include "lofi/music.h"
#include "lofi/state.h"
#include "lofi/ui.h"

namespace lofi {
enum class ActionKind : std::uint8_t { None, Config, Pause, Next };
struct Action {
    ActionKind kind=ActionKind::None;
    Config config{};
    bool paused=false;
    bool restartSession=false; // Explicit favorite replay, even at the same seed.
};
class Controller {
public:
    SavedState saved{};
    View view{};
    bool dirty=false;
    explicit Controller(std::uint64_t seed=0xCA7CAFEu);
    Config initialConfig() const;
    void setSnapshot(const Snapshot& snapshot);
    void tick(std::uint64_t nowMs);
    Action key(int character);
    void notice(const char* message, std::uint64_t durationMs=2500);
    void storageResult(bool success);
    void populateView();
    const Snapshot& snapshot() const { return snapshot_; }
    std::uint16_t sleepGainQ15() const { return sleepGainQ15_; }
    bool sleepPausePending() const { return sleepPausePending_; }
    std::uint8_t effectiveBrightness() const;
private:
    std::uint64_t initialSeed_;
    Snapshot snapshot_{};
    Config requestedConfig_{};
    bool configRequested_=false, pauseRequested_=false, desiredPaused_=false;
    std::uint64_t noticeUntil_=0, lastInput_=0;
    std::uint64_t sleepDeadlineMs_=0;
    std::uint16_t sleepGainQ15_=32768;
    std::uint8_t sleepMinutes_=0;
    bool sleepExpired_=false, sleepPausePending_=false, dimmed_=false;
    bool manualClean_=false;
    Favorite currentFavorite() const;
    Action changedConfig();
    void setSleepMinutes(std::uint8_t minutes);
    void cancelSleepTimer();
};
}
