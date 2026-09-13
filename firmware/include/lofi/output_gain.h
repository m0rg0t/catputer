#pragma once

#include <cstddef>
#include <cstdint>

namespace lofi {

// Shared by the ADV output and native playback, after the musical renderer.
// 0..100% retains the previous ADV level. The upper range supplies stronger
// makeup gain (12x core / 6x nominal at 300%) with a held peak envelope.
// M5Unified's ADV mono path previously amplified by ~2x at full volume.
// Move that 2x pre-gain BEFORE our limiter and halve speaker magnification,
// keeping driver gain below unity so its later PCM conversion cannot clip.
// Below 100%, retain the previous 0..255 squared-volume response.
class OutputGain {
public:
    static constexpr unsigned kMaxPercent = 300;
    static constexpr unsigned kAdvSpeakerMagnification = 8; // Pinned M5Unified 0.2.17, mono.
    static constexpr unsigned kRampFrames = 320; // 10 ms at 32 kHz.
    static constexpr int kLimiterKnee = 24576;
    static constexpr int kLimiterCeiling = 32700;
    static constexpr int kPeakCeiling = 32000;
    static constexpr unsigned kPeakHoldFrames = 256; // 8 ms at 32 kHz.
    static constexpr float kPeakRelease = 0.000624804728f; // 1-exp(-1/1600): 50 ms.

    explicit OutputGain(unsigned percent = 100) noexcept { reset(percent); }
    void reset(unsigned percent) noexcept;
    void setVolume(unsigned percent) noexcept;
    // Independent post-limiter sleep fade; 32768 is unity. User volume stays intact.
    void setSleepGain(std::uint16_t q15) noexcept;
    void process(std::int16_t* samples, std::size_t frames) noexcept;
    unsigned volume() const noexcept { return percent_; }
    std::uint64_t limitedSamples() const noexcept { return limited_; }

private:
    static float gainFor(unsigned percent) noexcept;
    unsigned percent_ = 100, remaining_ = 0;
    float gain_ = 1.0f, target_ = 1.0f, step_ = 0.0f;
    // Crossfade limiter paths with volume changes; no delay or audio buffers.
    float limiterMix_ = 0.0f, limiterTarget_ = 0.0f, limiterStep_ = 0.0f;
    float peakGain_ = 1.0f;
    unsigned peakHold_ = 0;
    unsigned sleepRemaining_ = 0;
    std::uint16_t sleepQ15_ = 32768;
    float sleepGain_ = 1.0f, sleepTarget_ = 1.0f, sleepStep_ = 0.0f;
    std::uint64_t limited_ = 0;
};

} // namespace lofi
