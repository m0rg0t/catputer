#include "lofi/output_gain.h"
#include <algorithm>
#include <cmath>

namespace lofi {

float OutputGain::gainFor(unsigned percent) noexcept {
    if (percent > 100) return 2.0f * static_cast<float>(percent) / 100.0f;
    const unsigned oldSpeakerLevel = percent * 255 / 100;
    return 2.0f * static_cast<float>(oldSpeakerLevel * oldSpeakerLevel) / (255.0f * 255.0f);
}

void OutputGain::reset(unsigned percent) noexcept {
    percent_ = std::min(percent, kMaxPercent);
    gain_ = target_ = gainFor(percent_);
    remaining_ = 0;
    step_ = 0;
    limited_ = 0;
}

void OutputGain::setVolume(unsigned percent) noexcept {
    percent = std::min(percent, kMaxPercent);
    if (percent == percent_) return;
    percent_ = percent;
    target_ = gainFor(percent);
    remaining_ = kRampFrames;
    step_ = (target_ - gain_) / static_cast<float>(remaining_);
}

void OutputGain::process(std::int16_t* samples, std::size_t frames) noexcept {
    if (!samples) return;
    for (std::size_t i = 0; i < frames; ++i) {
        if (remaining_) {
            gain_ += step_;
            if (--remaining_ == 0) gain_ = target_;
        }
        const float input = static_cast<float>(samples[i]) * gain_;
        float magnitude = std::fabs(input);
        if (magnitude > kLimiterKnee) {
            const float excess = magnitude - kLimiterKnee;
            constexpr float room = kLimiterCeiling - kLimiterKnee;
            // Unity slope at the knee, bounded smoothly below PCM full scale.
            magnitude = kLimiterKnee + room * (excess / (room + excess));
            ++limited_;
        }
        samples[i] = static_cast<std::int16_t>(input < 0 ? -magnitude : magnitude);
    }
}

} // namespace lofi
