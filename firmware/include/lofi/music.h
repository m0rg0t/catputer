#pragma once

#include <cstddef>
#include <cstdint>

namespace lofi {

constexpr std::uint32_t kMusicSampleRate = 32000;
constexpr std::uint8_t kMusicVoiceCapacity = 8;
constexpr std::uint32_t kMusicSchemaVersion = 1;

enum class Mood : std::uint8_t {
    Cozy = 0,
    Rainy = 1,
    Night = 2,
};

enum class SoundEngine : std::uint8_t {
    Synth = 0,
    Hybrid = 1,
};

enum class ArrangementSection : std::uint8_t {
    Intro = 0,
    Groove = 1,
    Melody = 2,
    Breakdown = 3,
    Return = 4,
    Outro = 5,
};

// A complete restartable favorite. Values outside their documented ranges are
// clamped by Engine; mood and soundEngine must be valid enum values.
struct Config {
    std::uint64_t seed = UINT64_C(0x4c4f464943415421);
    Mood mood = Mood::Cozy;
    SoundEngine soundEngine = SoundEngine::Synth;
    std::uint8_t volume = 78;       // 0..100
    std::uint8_t texture = 18;      // 0..100
};

struct Snapshot {
    Config config{};
    std::uint64_t transportSample = 0; // Monotonic musical clock; frozen while paused.
    std::uint64_t sessionSample = 0;
    std::uint32_t bar = 0;
    std::uint16_t bpm = 0;
    std::uint16_t barPhaseQ16 = 0;
    std::uint8_t beat = 0;
    std::uint8_t sixteenth = 0;
    ArrangementSection section = ArrangementSection::Intro;
    bool paused = false;
    bool changePending = false;
    std::uint8_t activeVoices = 0;
    std::uint8_t voiceCapacity = kMusicVoiceCapacity;
    std::uint16_t recentPeak = 0;
    std::uint64_t scoreEventHash = 0;
    std::uint32_t scoreEventCount = 0;
};

struct Diagnostics {
    std::uint64_t renderedFrames = 0; // Includes silent frames rendered while paused.
    std::uint64_t transportSample = 0;
    std::uint64_t scoreEventHash = 0;
    std::uint32_t scoreEventCount = 0;
    std::uint32_t stolenVoices = 0;
    std::uint32_t sessionTransitions = 0;
    std::uint16_t absolutePeak = 0;
    std::uint8_t maxActiveVoices = 0;
};

const char* moodName(Mood mood) noexcept;
const char* soundEngineName(SoundEngine engine) noexcept;
bool validConfig(const Config& config) noexcept;

class Engine {
public:
    // Keep long-lived Engine instances in static/application storage on the
    // device; the object contains its delay line and all render state.
    explicit Engine(const Config& config = Config{}) noexcept;
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    // Immediate restart, intended for initialization or a stopped output path.
    void reset(const Config& config) noexcept;

    // Requests use a bounded, coalescing mailbox and take effect at the next
    // bar edge. The audio task should remain the Engine's sole owner.
    bool requestConfig(const Config& config) noexcept;
    void requestNext() noexcept;
    void requestNext(std::uint64_t seed) noexcept;

    // Pause fades gently, then freezes musical transport and voice state.
    // Resume continues the same session through a fade-in.
    void pause(bool paused) noexcept;

    // Produces exactly frames of 32 kHz mono signed PCM16 without allocation.
    void render(std::int16_t* output, std::size_t frames) noexcept;

    Config config() const noexcept;
    Snapshot snapshot() const noexcept;
    Diagnostics diagnostics() const noexcept;

    // Stable, allocation-free favorite representation:
    // lofi1-<16 hex seed>-<mood>-<engine>-<volume>-<texture>
    static constexpr std::size_t kFavoriteCodeCapacity = 48;
    std::size_t writeFavoriteCode(char* output, std::size_t capacity) const noexcept;
    static bool parseFavoriteCode(const char* text, Config& output) noexcept;

private:
    struct Impl;
    static constexpr std::size_t kStorageBytes = 8192;
    alignas(std::max_align_t) std::uint8_t storage_[kStorageBytes];

    Impl& impl() noexcept;
    const Impl& impl() const noexcept;
};

} // namespace lofi
