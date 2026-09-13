#include "lofi/output_gain.h"
#include <algorithm>
#include <cmath>

namespace lofi {

float OutputGain::gainFor(unsigned percent) noexcept {
    if (percent > 100) return 2.0f + static_cast<float>(percent - 100) * 0.05f;
    const unsigned oldSpeakerLevel = percent * 255 / 100;
    return 2.0f * static_cast<float>(oldSpeakerLevel * oldSpeakerLevel) / (255.0f * 255.0f);
}

void OutputGain::reset(unsigned percent) noexcept {
    percent_ = std::min(percent, kMaxPercent);
    gain_ = target_ = gainFor(percent_);
    remaining_ = 0;
    step_ = 0;
    limiterMix_ = limiterTarget_ = percent_ > 100 ? 1.0f : 0.0f;
    limiterStep_ = 0.0f;
    peakGain_ = 1.0f;
    peakHold_ = 0;
    limited_ = 0;
    sleepQ15_ = 32768;
    sleepGain_ = sleepTarget_ = 1.0f;
    sleepStep_ = 0;
    sleepRemaining_ = 0;
}

void OutputGain::setVolume(unsigned percent) noexcept {
    percent = std::min(percent, kMaxPercent);
    if (percent == percent_) return;
    percent_ = percent;
    target_ = gainFor(percent);
    remaining_ = kRampFrames;
    step_ = (target_ - gain_) / static_cast<float>(remaining_);
    limiterTarget_ = percent > 100 ? 1.0f : 0.0f;
    limiterStep_ = (limiterTarget_ - limiterMix_) / static_cast<float>(remaining_);
}

void OutputGain::setSleepGain(std::uint16_t q15) noexcept {
    q15 = std::min<std::uint16_t>(q15, 32768);
    if (q15 == sleepQ15_) return;
    sleepQ15_ = q15;
    sleepTarget_ = static_cast<float>(q15) / 32768.0f;
    sleepRemaining_ = kRampFrames;
    sleepStep_ = (sleepTarget_ - sleepGain_) / static_cast<float>(sleepRemaining_);
}

void OutputGain::process(std::int16_t* samples, std::size_t frames) noexcept {
    if (!samples) return;
    for (std::size_t i = 0; i < frames; ++i) {
        if (remaining_) {
            gain_ += step_;
            limiterMix_ += limiterStep_;
            if (--remaining_ == 0) {
                gain_ = target_;
                limiterMix_ = limiterTarget_;
            }
        }
        const float input = static_cast<float>(samples[i]) * gain_;
        float magnitude = std::fabs(input);
        const float rawMagnitude = magnitude;
        bool limited = false;
        if (limiterMix_ < 1.0f && magnitude > kLimiterKnee) {
            const float excess = magnitude - kLimiterKnee;
            constexpr float room = kLimiterCeiling - kLimiterKnee;
            // Unity slope at the knee, bounded smoothly below PCM full scale.
            magnitude = kLimiterKnee + room * (excess / (room + excess));
            limited = true;
        }
        if (limiterMix_ > 0.0f) {
            const float required = rawMagnitude > kPeakCeiling
                ? static_cast<float>(kPeakCeiling) / rawMagnitude : 1.0f;
            if (required < peakGain_) {
                // Immediate attack bounds even an isolated full-scale input.
                peakGain_ = required;
                peakHold_ = kPeakHoldFrames;
            } else if (peakHold_) {
                --peakHold_;
            } else {
                peakGain_ = std::min(required, peakGain_ + (1.0f - peakGain_) * kPeakRelease);
            }
            const float envelopeOutput = rawMagnitude * peakGain_;
            magnitude = limiterMix_ >= 1.0f ? envelopeOutput
                : magnitude + (envelopeOutput - magnitude) * limiterMix_;
            limited = limited || peakGain_ < 0.999999f;
        } else {
            peakGain_ = 1.0f;
            peakHold_ = 0;
        }
        if (limited) ++limited_;
        if (sleepRemaining_) {
            sleepGain_ += sleepStep_;
            if (--sleepRemaining_ == 0) sleepGain_ = sleepTarget_;
        }
        // Attenuate after limiting so a high user volume cannot defeat the fade.
        magnitude *= sleepGain_;
        samples[i] = static_cast<std::int16_t>(input < 0 ? -magnitude : magnitude);
    }
}

} // namespace lofi
