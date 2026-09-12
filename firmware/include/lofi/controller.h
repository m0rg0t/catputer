#pragma once
#include "lofi/music.h"
#include "lofi/state.h"
#include "lofi/ui.h"

namespace lofi {
enum class ActionKind : std::uint8_t { None, Config, Pause, Next };
struct Action { ActionKind kind=ActionKind::None; Config config{}; bool paused=false; };
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
private:
    std::uint64_t initialSeed_;
    Snapshot snapshot_{};
    Config requestedConfig_{};
    bool configRequested_=false, pauseRequested_=false, desiredPaused_=false;
    std::uint64_t noticeUntil_=0, lastInput_=0;
    bool manualClean_=false;
    Favorite currentFavorite() const;
    Action changedConfig();
};
}
