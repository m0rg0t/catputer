#pragma once

#include <cstddef>
#include <cstdint>

namespace lofi {

// Delay lengths at the paper's 29,761 Hz, scaled to the 16 kHz network rate.
constexpr std::size_t plateScaled(std::size_t paperSamples) noexcept {
    return (paperSamples * 16000u + 14880u) / 29761u;
}

// A small plate reverb after the figure-eight tank described by Jon Dattorro,
// "Effect Design, Part 1: Reverberator and Other Filters", J. Audio Eng. Soc.
// 45(9), 1997. Written for this project from the paper's block diagram and
// published constants; no third-party implementation is copied.
//
// Budget choices for the ADV (no PSRAM):
// - It takes 32 kHz mono input and runs the network at half rate (16 kHz),
//   decimating by averaging sample pairs and interpolating the output.
// - Every delay line stores int16 with 4x headroom, about 25 KB in total.
// - Fixed inline storage; process() never allocates, locks or touches files.
//   The sole audio owner calls it, like Engine::render().
class PlateReverb {
public:
    struct Settings {
        float decay = 0.5f;        // tank feedback, 0..0.9
        float damping = 0.35f;     // one-pole damping inside the tank, 0..0.95
        float inputLowpass = 0.6f; // input bandwidth coefficient, 0.05..1
    };

    void configure(const Settings& settings) noexcept;
    void clear() noexcept;

    // One 32 kHz mono input sample in, one wet output sample out.
    float process(float input) noexcept;

    static constexpr std::size_t scaled(std::size_t paperSamples) noexcept {
        return plateScaled(paperSamples);
    }
    static constexpr std::size_t kPredelay = 320; // 20 ms at 16 kHz.
    static constexpr std::size_t kExcursion = plateScaled(16);
    static constexpr std::size_t kIn1 = plateScaled(142);
    static constexpr std::size_t kIn2 = plateScaled(107);
    static constexpr std::size_t kIn3 = plateScaled(379);
    static constexpr std::size_t kIn4 = plateScaled(277);
    static constexpr std::size_t kModLeft = plateScaled(672);
    static constexpr std::size_t kDelay1Left = plateScaled(4453);
    static constexpr std::size_t kAllpass2Left = plateScaled(1800);
    static constexpr std::size_t kDelay2Left = plateScaled(3720);
    static constexpr std::size_t kModRight = plateScaled(908);
    static constexpr std::size_t kDelay1Right = plateScaled(4217);
    static constexpr std::size_t kAllpass2Right = plateScaled(2656);
    static constexpr std::size_t kDelay2Right = plateScaled(3163);

    template <std::size_t N>
    struct Line {
        std::int16_t data[N]{};
        std::size_t position = 0; // Next write slot.

        // The value written `ago` samples earlier, 1 <= ago <= N.
        float read(std::size_t ago) const noexcept;
        // Linear interpolation for a fractional delay, 1 <= ago < N.
        float readFractional(float ago) const noexcept;
        void write(float value) noexcept;
        void clear() noexcept;
    };

private:
    Line<kPredelay> predelay_{};
    Line<kIn1> in1_{};
    Line<kIn2> in2_{};
    Line<kIn3> in3_{};
    Line<kIn4> in4_{};
    Line<kModLeft + kExcursion + 2> modLeft_{};
    Line<kDelay1Left> delay1Left_{};
    Line<kAllpass2Left> allpass2Left_{};
    Line<kDelay2Left> delay2Left_{};
    Line<kModRight + kExcursion + 2> modRight_{};
    Line<kDelay1Right> delay1Right_{};
    Line<kAllpass2Right> allpass2Right_{};
    Line<kDelay2Right> delay2Right_{};

    Settings settings_{};
    float inputState_ = 0.0f;
    float dampLeft_ = 0.0f;
    float dampRight_ = 0.0f;
    float lfoSin_ = 0.0f;
    float lfoCos_ = 1.0f;
    float pairSum_ = 0.0f;
    float previousOutput_ = 0.0f;
    float currentOutput_ = 0.0f;
    bool oddFrame_ = false;

    float tick(float input) noexcept;
};

} // namespace lofi
