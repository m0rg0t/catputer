#include "lofi/output_gain.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>

int main() {
    using lofi::OutputGain;
    // Quiet samples retain the ADV's nominal 2x pre-gain and gain by exactly
    // another 3x at 300%, before limiting and the now-normalized driver.
    std::array<std::int16_t, 5> quiet{{-3000, -1000, 0, 1000, 3000}};
    auto unity = quiet, boosted = quiet;
    OutputGain normal(100), boost(300);
    normal.process(unity.data(), unity.size());
    boost.process(boosted.data(), boosted.size());
    for (unsigned i=0; i<quiet.size(); ++i) {
        assert(unity[i] == quiet[i] * 2);
        assert(boosted[i] == unity[i] * 3);
    }
    assert(boost.limitedSamples() == 0);

    // Pinned M5Unified 0.2.17: mono duplicates L/R, squares master/channel
    // volume, and shifts its 32-bit mixer by 8 at PCM output. ADV defaults to
    // magnification16 (~1.97x), which would clip AFTER our limiter. Firmware
    // must use8 (~0.984x), with the former2x moved ahead of the limiter.
    constexpr double driverGain = 2.0 * OutputGain::kAdvSpeakerMagnification *
        255.0 * 255.0 * 255.0 * 255.0 / 68719476736.0;
    static_assert(driverGain < 1.0);
    static_assert(driverGain * OutputGain::kLimiterCeiling < INT16_MAX);

    // All PCM16 inputs remain sign-correct and bounded at 300%; no wraparound
    // at INT16_MIN, no flat hard-clipped range, and a monotonic transfer.
    int previous = -OutputGain::kLimiterCeiling;
    for (int value = INT16_MIN; value <= INT16_MAX; ++value) {
        auto sample = static_cast<std::int16_t>(value);
        boost.process(&sample, 1);
        assert(sample >= previous);
        assert(std::abs(int(sample)) < OutputGain::kLimiterCeiling);
        assert(value == 0 ? sample == 0 : (sample < 0) == (value < 0));
        previous = sample;
    }
    assert(boost.limitedSamples() > 0);

    // 255/256 and the requested 300% ceiling must never narrow to uint8_t.
    previous = -1;
    for (unsigned volume = 0; volume <= 400; ++volume) {
        OutputGain stage(volume);
        std::int16_t sample = 1000;
        stage.process(&sample, 1);
        assert(sample >= previous);
        assert(stage.volume() == std::min(volume, 300u));
        if (volume > 100) assert(std::abs(int(sample) - int(std::min(volume, 300u) * 20)) <= 1);
        else {
            const double oldLevel = volume * 255 / 100;
            const double oldDriverOutput = 1000.0 * (oldLevel * oldLevel / (255.0 * 255.0)) * driverGain * 2;
            assert(std::abs(double(sample) * driverGain - oldDriverOutput) <= 1.0);
        }
        previous = sample;
    }

    // Repeated setters must not restart the ramp. Block size must not alter it.
    std::array<std::int16_t, 640> full, split;
    full.fill(1000); split.fill(1000);
    OutputGain a(0), b(0); a.setVolume(300); b.setVolume(300);
    a.process(full.data(), full.size());
    for (unsigned offset=0; offset<split.size(); offset+=32) {
        b.setVolume(300);
        b.process(split.data()+offset, 32);
    }
    assert(full == split);
    for (unsigned i=1; i<OutputGain::kRampFrames; ++i) {
        assert(full[i] >= full[i-1] && full[i]-full[i-1] <= 19);
    }
    assert(full[OutputGain::kRampFrames-1] == 6000 && full.back() == 6000);
    a.setVolume(0); full.fill(1000); a.process(full.data(), full.size());
    assert(full[OutputGain::kRampFrames-1] == 0 && full.back() == 0);

    // A second key press during a ramp starts from the current gain, with no
    // discontinuity or negative overshoot when retargeted toward mute.
    a.reset(0);a.setVolume(300);full.fill(1000);a.process(full.data(),128);
    const int halfway=full[127];
    a.setVolume(0);full.fill(1000);a.process(full.data(),full.size());
    assert(full[0]<=halfway && halfway-full[0]<=8);
    for(unsigned i=1;i<full.size();++i) assert(full[i]>=0 && full[i]<=full[i-1]);
    assert(full.back()==0);

    // Sleep fade is independent of the user volume and follows the limiter.
    OutputGain awake(300), sleepy(300);
    std::int16_t loud = 30000;
    awake.process(&loud, 1);
    sleepy.setSleepGain(16384);
    full.fill(30000); sleepy.process(full.data(), full.size());
    assert(std::abs(int(full.back()) * 2 - int(loud)) <= 1);
    assert(sleepy.volume() == 300);
    for (unsigned i=1; i<full.size(); ++i) assert(full[i] <= full[i-1]);
    sleepy.setSleepGain(0);
    full.fill(30000); sleepy.process(full.data(), full.size());
    assert(full.back() == 0 && sleepy.volume() == 300);
    sleepy.setSleepGain(32768);
    full.fill(30000); sleepy.process(full.data(), full.size());
    for (unsigned i=1; i<full.size(); ++i) assert(full[i] >= full[i-1]);
    assert(full.back() == loud);

    a.reset(300); b.reset(300); a.setSleepGain(0); b.setSleepGain(0);
    full.fill(30000); split.fill(30000);
    a.process(full.data(), full.size());
    for (unsigned offset=0; offset<split.size(); offset+=32) {
        b.setSleepGain(0); b.process(split.data()+offset, 32);
    }
    assert(full == split && full.back() == 0);
    a.reset(300); a.setSleepGain(0); full.fill(30000); a.process(full.data(), 128);
    const int beforeWake = full[127];
    a.setSleepGain(32768); full.fill(30000); a.process(full.data(), full.size());
    assert(full[0] >= beforeWake && full[0]-beforeWake < 100);
    assert(full.back() == loud && a.volume() == 300);
    a.setSleepGain(0); a.reset(300); loud = 30000; a.process(&loud, 1);
    assert(loud == full.back()); // Reset returns to awake, full output.
}
