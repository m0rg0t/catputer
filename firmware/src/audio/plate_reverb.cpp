#include "lofi/plate_reverb.h"

#include <algorithm>
#include <cmath>

namespace lofi {
namespace {

// Stored samples keep 4x headroom above nominal full scale.
constexpr float kStoreScale = 8192.0f;
constexpr float kLoadScale = 1.0f / kStoreScale;
constexpr float kInputDiffusion1 = 0.75f;
constexpr float kInputDiffusion2 = 0.625f;
constexpr float kDecayDiffusion1 = 0.7f;
constexpr float kDecayDiffusion2 = 0.5f;
// About 0.8 Hz at the 16 kHz network rate.
constexpr float kLfoStep = 0.000314159f;

float flushed(float value) noexcept {
    return std::fabs(value) < 1.0e-9f ? 0.0f : value;
}

// Schroeder allpass around one delay line.
template <std::size_t N>
float allpass(PlateReverb::Line<N>& line, std::size_t length, float input,
              float gain) noexcept {
    const float delayed = line.read(length);
    const float feed = input - gain * delayed;
    line.write(feed);
    return delayed + gain * feed;
}

// The tank's first allpass has the opposite sign and a slowly moving length.
template <std::size_t N>
float modulatedAllpass(PlateReverb::Line<N>& line, float length, float input,
                       float gain) noexcept {
    const float delayed = line.readFractional(length);
    const float feed = input + gain * delayed;
    line.write(feed);
    return delayed - gain * feed;
}

} // namespace

template <std::size_t N>
float PlateReverb::Line<N>::read(std::size_t ago) const noexcept {
    std::size_t index = position + N - ago;
    if (index >= N) {
        index -= N;
    }
    return static_cast<float>(data[index]) * kLoadScale;
}

template <std::size_t N>
float PlateReverb::Line<N>::readFractional(float ago) const noexcept {
    const std::size_t whole = static_cast<std::size_t>(ago);
    const float fraction = ago - static_cast<float>(whole);
    const float near = read(whole);
    const float far = read(whole + 1u);
    return near + (far - near) * fraction;
}

template <std::size_t N>
void PlateReverb::Line<N>::write(float value) noexcept {
    const float scaled = std::max(-32767.0f, std::min(32767.0f, value * kStoreScale));
    data[position] = static_cast<std::int16_t>(scaled);
    ++position;
    if (position == N) {
        position = 0;
    }
}

template <std::size_t N>
void PlateReverb::Line<N>::clear() noexcept {
    std::fill(data, data + N, static_cast<std::int16_t>(0));
    position = 0;
}

void PlateReverb::configure(const Settings& settings) noexcept {
    settings_.decay = std::max(0.0f, std::min(0.9f, settings.decay));
    settings_.damping = std::max(0.0f, std::min(0.95f, settings.damping));
    settings_.inputLowpass = std::max(0.05f, std::min(1.0f, settings.inputLowpass));
}

void PlateReverb::clear() noexcept {
    predelay_.clear();
    in1_.clear();
    in2_.clear();
    in3_.clear();
    in4_.clear();
    modLeft_.clear();
    delay1Left_.clear();
    allpass2Left_.clear();
    delay2Left_.clear();
    modRight_.clear();
    delay1Right_.clear();
    allpass2Right_.clear();
    delay2Right_.clear();
    inputState_ = 0.0f;
    dampLeft_ = 0.0f;
    dampRight_ = 0.0f;
    lfoSin_ = 0.0f;
    lfoCos_ = 1.0f;
    pairSum_ = 0.0f;
    previousOutput_ = 0.0f;
    currentOutput_ = 0.0f;
    oddFrame_ = false;
}

float PlateReverb::process(float input) noexcept {
    pairSum_ += input;
    if (!oddFrame_) {
        oddFrame_ = true;
        return currentOutput_;
    }
    oddFrame_ = false;
    previousOutput_ = currentOutput_;
    currentOutput_ = tick(pairSum_ * 0.5f);
    pairSum_ = 0.0f;
    // Linear interpolation back to 32 kHz: the midpoint now, the new value on
    // the next frame.
    return 0.5f * (previousOutput_ + currentOutput_);
}

float PlateReverb::tick(float input) noexcept {
    inputState_ = flushed(inputState_ + settings_.inputLowpass * (input - inputState_));
    const float delayedInput = predelay_.read(kPredelay);
    predelay_.write(inputState_);

    float diffused = allpass(in1_, kIn1, delayedInput, kInputDiffusion1);
    diffused = allpass(in2_, kIn2, diffused, kInputDiffusion1);
    diffused = allpass(in3_, kIn3, diffused, kInputDiffusion2);
    diffused = allpass(in4_, kIn4, diffused, kInputDiffusion2);

    lfoSin_ += kLfoStep * lfoCos_;
    lfoCos_ -= kLfoStep * lfoSin_;
    const float sweepLeft = std::max(-1.0f, std::min(1.0f, lfoSin_));
    const float sweepRight = std::max(-1.0f, std::min(1.0f, lfoCos_));
    const float excursion = static_cast<float>(kExcursion);

    // Each half feeds the other: read both cross-feeds before either write.
    const float fromRight = delay2Right_.read(kDelay2Right);
    const float fromLeft = delay2Left_.read(kDelay2Left);
    const float damp = 1.0f - settings_.damping;

    float left = diffused + settings_.decay * fromRight;
    left = modulatedAllpass(modLeft_,
        static_cast<float>(kModLeft) + excursion * sweepLeft, left, kDecayDiffusion1);
    const float leftDelayed = delay1Left_.read(kDelay1Left);
    delay1Left_.write(left);
    dampLeft_ = flushed(dampLeft_ + damp * (leftDelayed - dampLeft_));
    left = allpass(allpass2Left_, kAllpass2Left, dampLeft_ * settings_.decay,
                   kDecayDiffusion2);
    delay2Left_.write(left);

    float right = diffused + settings_.decay * fromLeft;
    right = modulatedAllpass(modRight_,
        static_cast<float>(kModRight) + excursion * sweepRight, right, kDecayDiffusion1);
    const float rightDelayed = delay1Right_.read(kDelay1Right);
    delay1Right_.write(right);
    dampRight_ = flushed(dampRight_ + damp * (rightDelayed - dampRight_));
    right = allpass(allpass2Right_, kAllpass2Right, dampRight_ * settings_.decay,
                    kDecayDiffusion2);
    delay2Right_.write(right);

    // The paper's left and right output taps, summed for the mono output.
    float output = 0.0f;
    output += delay1Right_.read(scaled(266) + 1u);
    output += delay1Right_.read(scaled(2974) + 1u);
    output -= allpass2Right_.read(scaled(1913) + 1u);
    output += delay2Right_.read(scaled(1996) + 1u);
    output -= delay1Left_.read(scaled(1990) + 1u);
    output -= allpass2Left_.read(scaled(187) + 1u);
    output -= delay2Left_.read(scaled(1066) + 1u);
    output += delay1Left_.read(scaled(353) + 1u);
    output += delay1Left_.read(scaled(3627) + 1u);
    output -= allpass2Left_.read(scaled(1228) + 1u);
    output += delay2Left_.read(scaled(2673) + 1u);
    output -= delay1Right_.read(scaled(2111) + 1u);
    output -= allpass2Right_.read(scaled(335) + 1u);
    output -= delay2Right_.read(scaled(121) + 1u);
    return output * 0.6f;
}

} // namespace lofi
