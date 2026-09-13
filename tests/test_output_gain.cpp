#include "lofi/output_gain.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>

namespace {

using lofi::OutputGain;

float legacyGain(unsigned percent) {
    const unsigned oldSpeakerLevel = percent * 255 / 100;
    return 2.0f * static_cast<float>(oldSpeakerLevel * oldSpeakerLevel) /
        (255.0f * 255.0f);
}

std::int16_t legacyOutput(unsigned percent, std::int16_t sample) {
    const float input = static_cast<float>(sample) * legacyGain(percent);
    float magnitude = std::fabs(input);
    if (magnitude > OutputGain::kLimiterKnee) {
        const float excess = magnitude - OutputGain::kLimiterKnee;
        constexpr float room = OutputGain::kLimiterCeiling - OutputGain::kLimiterKnee;
        const float ratio = excess / (room + excess);
        // lofi_core is compiled with -ffp-contract=off. Force the same rounded
        // multiply before the add in this independent compatibility reference.
        volatile float shaped = room * ratio;
        magnitude = OutputGain::kLimiterKnee + shaped;
    }
    return static_cast<std::int16_t>(input < 0 ? -magnitude : magnitude);
}

template<std::size_t N>
void assertAdjacentDeltaAtMost(const std::array<std::int16_t, N>& samples, int limit) {
    for (std::size_t i = 1; i < samples.size(); ++i) {
        assert(std::abs(int(samples[i]) - int(samples[i - 1])) <= limit);
    }
}

void testGainCurveAndLegacyCompatibility() {
    // Quiet material exposes the pre-limiter curve directly: 100% is the
    // previous ADV 2x level and 300% is 12x core / 6x the nominal level.
    std::array<std::int16_t, 5> quiet{{-2000, -1000, 0, 1000, 2000}};
    auto nominal = quiet;
    auto boosted = quiet;
    OutputGain normal(100), boost(300);
    normal.process(nominal.data(), nominal.size());
    boost.process(boosted.data(), boosted.size());
    for (std::size_t i = 0; i < quiet.size(); ++i) {
        assert(nominal[i] == quiet[i] * 2);
        assert(boosted[i] == quiet[i] * 12);
        assert(boosted[i] == nominal[i] * 6);
    }
    assert(boost.limitedSamples() == 0);

    // Every old setting, including its integer 0..255 volume quantization and
    // soft-limiter extremes, remains bit-exact through 100%.
    constexpr std::array<std::int16_t, 9> probes{{
        INT16_MIN, -20000, -12000, -1, 0, 1, 12000, 20000, INT16_MAX
    }};
    for (unsigned volume = 0; volume <= 100; ++volume) {
        OutputGain stage(volume);
        auto actual = probes;
        stage.process(actual.data(), actual.size());
        for (std::size_t i = 0; i < probes.size(); ++i) {
            assert(actual[i] == legacyOutput(volume, probes[i]));
        }
    }

    // Values above the public ceiling clamp instead of narrowing to uint8_t.
    int previous = -1;
    for (unsigned volume = 0; volume <= 400; ++volume) {
        OutputGain stage(volume);
        std::int16_t sample = 1000;
        stage.process(&sample, 1);
        const unsigned clamped = std::min(volume, OutputGain::kMaxPercent);
        const float expectedGain = clamped <= 100
            ? legacyGain(clamped)
            : 2.0f + static_cast<float>(clamped - 100) * 0.05f;
        assert(sample == static_cast<std::int16_t>(1000.0f * expectedGain));
        assert(sample >= previous);
        assert(stage.volume() == clamped);
        previous = sample;
    }
}

void testDriverAndPcmBounds() {
    // Pinned M5Unified 0.2.17 duplicates mono L/R, squares master/channel
    // volume, and shifts its mixer by 8. Magnification8 keeps its final PCM
    // conversion below unity; magnification16 would be about 1.97x.
    constexpr double driverGain = 2.0 * OutputGain::kAdvSpeakerMagnification *
        255.0 * 255.0 * 255.0 * 255.0 / 68719476736.0;
    static_assert(driverGain < 1.0);
    static_assert(driverGain * OutputGain::kLimiterCeiling < INT16_MAX);
    static_assert(driverGain * OutputGain::kPeakCeiling < INT16_MAX);

    // Reset for each input because the peak limiter deliberately has memory.
    // The first sample must catch either signed PCM extreme without wrapping,
    // and the independent transfer remains monotonic.
    OutputGain stage(300);
    int previous = -OutputGain::kPeakCeiling;
    int maxRegression = 0;
    for (int value = INT16_MIN; value <= INT16_MAX; ++value) {
        stage.reset(300);
        auto sample = static_cast<std::int16_t>(value);
        stage.process(&sample, 1);
        // Float division at the flat ceiling can alternate by one truncated
        // PCM unit while preserving the monotonic envelope.
        maxRegression = std::max(maxRegression, previous - int(sample));
        assert(int(sample) + 1 >= previous);
        assert(std::abs(int(sample)) <= OutputGain::kPeakCeiling);
        assert(value == 0 ? sample == 0 : (sample < 0) == (value < 0));
        previous = sample;
    }
    assert(maxRegression <= 1);

    // A single peak is bounded on its own first frame, with no lookahead.
    stage.reset(300);
    std::array<std::int16_t, 5> isolated{{1000, 1000, INT16_MAX, 1000, 1000}};
    stage.process(isolated.data(), isolated.size());
    assert(isolated[0] == 12000 && isolated[1] == 12000);
    assert(isolated[2] > 0 && isolated[2] <= OutputGain::kPeakCeiling);
    assert(isolated[3] > 0 && isolated[3] < 12000);
    assert(stage.limitedSamples() == 3);

    stage.reset(300);
    std::int16_t negativePeak = INT16_MIN;
    stage.process(&negativePeak, 1);
    assert(negativePeak < 0);
    assert(std::abs(int(negativePeak)) <= OutputGain::kPeakCeiling);
    assert(stage.limitedSamples() == 1);

    // The crossfade retains the legacy ceiling while entering the peak mode;
    // even full-scale material remains safe for the downstream driver.
    stage.reset(100);
    stage.setVolume(300);
    std::array<std::int16_t, 640> transition{};
    transition.fill(INT16_MAX);
    stage.process(transition.data(), transition.size());
    assert(std::all_of(transition.begin(), transition.end(), [](std::int16_t value) {
        return value > 0 && value < OutputGain::kLimiterCeiling;
    }));
}

void testPeakHoldReleaseAndSustainedBound() {
    OutputGain stage(300);
    std::int16_t peak = INT16_MAX;
    stage.process(&peak, 1);
    assert(peak <= OutputGain::kPeakCeiling);

    // Gain holds for exactly the configured number of following frames.
    std::array<std::int16_t, OutputGain::kPeakHoldFrames> held{};
    held.fill(1000);
    stage.process(held.data(), held.size());
    assert(std::all_of(held.begin(), held.end(), [&](std::int16_t value) {
        return value == held.front();
    }));
    assert(held.front() > 0 && held.front() < 12000);

    std::int16_t firstRelease = 1000;
    stage.process(&firstRelease, 1);
    assert(firstRelease > held.back() && firstRelease < 12000);

    // Release is monotonic on quiet material and approaches unattenuated 12x.
    std::array<std::int16_t, 24000> recovery{};
    recovery.fill(1000);
    stage.process(recovery.data(), recovery.size());
    for (std::size_t i = 1; i < recovery.size(); ++i) {
        assert(recovery[i] >= recovery[i - 1]);
        assert(recovery[i] <= 12000);
    }
    assert(recovery.back() >= 11998);

    // Under a sustained overload, release may not overshoot the per-sample
    // required gain or alternate above/below the ceiling after the hold.
    stage.reset(300);
    std::array<std::int16_t, OutputGain::kPeakHoldFrames + 4096> sustained{};
    sustained.fill(INT16_MAX);
    stage.process(sustained.data(), sustained.size());
    assert(std::all_of(sustained.begin(), sustained.end(), [&](std::int16_t value) {
        return value == sustained.front() && value <= OutputGain::kPeakCeiling;
    }));
    assert(stage.limitedSamples() == sustained.size());
}

void testBlockSplitDeterminism() {
    std::array<std::int16_t, 4096> whole{}, split{};
    for (std::size_t i = 0; i < whole.size(); ++i) {
        int value = static_cast<int>((i * 7919u) % 60001u) - 30000;
        if (i == 31 || i == 997 || i == 2048) value = INT16_MAX;
        if (i == 511 || i == 3001) value = INT16_MIN;
        whole[i] = split[i] = static_cast<std::int16_t>(value);
    }

    OutputGain oneCall(300), manyCalls(300);
    oneCall.process(whole.data(), whole.size());
    constexpr std::array<std::size_t, 7> chunks{{1, 17, 32, 255, 3, 511, 64}};
    std::size_t offset = 0, chunk = 0;
    while (offset < split.size()) {
        const auto count = std::min(chunks[chunk++ % chunks.size()], split.size() - offset);
        manyCalls.process(split.data() + offset, count);
        offset += count;
    }
    assert(whole == split);
    assert(oneCall.limitedSamples() == manyCalls.limitedSamples());

    // A null buffer is a no-op, including for ramp and envelope time.
    OutputGain nullCall(100), reference(100);
    nullCall.setVolume(300);
    reference.setVolume(300);
    nullCall.process(nullptr, 123);
    std::int16_t afterNull = 1000, expected = 1000;
    nullCall.process(&afterNull, 1);
    reference.process(&expected, 1);
    assert(afterNull == expected);
}

void testVolumeTransitionsAndRetargeting() {
    std::array<std::int16_t, OutputGain::kRampFrames> ramp{};

    // Crossing 100/101 crossfades limiter modes over the same 10 ms ramp.
    OutputGain crossing(100);
    std::int16_t before = 16000;
    crossing.process(&before, 1);
    crossing.setVolume(101);
    ramp.fill(16000);
    crossing.process(ramp.data(), ramp.size());
    assert(std::abs(int(ramp.front()) - int(before)) < 64);
    assert(ramp.back() == OutputGain::kPeakCeiling);
    for (std::size_t i = 1; i < ramp.size(); ++i) {
        assert(ramp[i] >= ramp[i - 1]);
        assert(ramp[i] <= OutputGain::kPeakCeiling);
    }
    assertAdjacentDeltaAtMost(ramp, 64);

    const int highSide = ramp.back();
    crossing.setVolume(100);
    ramp.fill(16000);
    crossing.process(ramp.data(), ramp.size());
    assert(std::abs(int(ramp.front()) - highSide) < 64);
    assert(ramp.back() == legacyOutput(100, 16000));
    for (std::size_t i = 1; i < ramp.size(); ++i) assert(ramp[i] <= ramp[i - 1]);
    assertAdjacentDeltaAtMost(ramp, 64);

    // Repeated setters do not restart ramps, and call boundaries do not alter
    // a volume transition.
    std::array<std::int16_t, 640> full{}, split{};
    full.fill(1000);
    split.fill(1000);
    OutputGain a(0), b(0);
    a.setVolume(300);
    b.setVolume(300);
    a.process(full.data(), full.size());
    for (std::size_t offset = 0; offset < split.size(); offset += 32) {
        b.setVolume(300);
        b.process(split.data() + offset, 32);
    }
    assert(full == split);
    for (std::size_t i = 1; i < OutputGain::kRampFrames; ++i) {
        assert(full[i] >= full[i - 1]);
    }
    assertAdjacentDeltaAtMost(full, 38);
    assert(full[OutputGain::kRampFrames - 1] == 12000);
    assert(full.back() == 12000);

    // Retarget toward mute from the current interpolated gain, with no jump or
    // negative overshoot. Volume zero stays silent for signed extremes.
    a.reset(0);
    a.setVolume(300);
    full.fill(1000);
    a.process(full.data(), 128);
    const int beforeRetarget = full[127];
    a.setVolume(0);
    full.fill(1000);
    a.process(full.data(), full.size());
    assert(full[0] <= beforeRetarget && beforeRetarget - full[0] <= 16);
    for (std::size_t i = 1; i < full.size(); ++i) {
        assert(full[i] >= 0 && full[i] <= full[i - 1]);
    }
    assert(full[OutputGain::kRampFrames - 1] == 0 && full.back() == 0);
    std::array<std::int16_t, 2> extremes{{INT16_MIN, INT16_MAX}};
    a.process(extremes.data(), extremes.size());
    assert(extremes[0] == 0 && extremes[1] == 0);
}

void testSleepFadeAndReset() {
    std::array<std::int16_t, 640> full{}, split{};

    // Sleep attenuation follows limiting, leaves user volume intact, and
    // reaches exactly half then mute without defeating the peak ceiling.
    OutputGain awake(300), sleepy(300);
    std::int16_t loud = 30000;
    awake.process(&loud, 1);
    sleepy.setSleepGain(16384);
    full.fill(30000);
    sleepy.process(full.data(), full.size());
    assert(full.front() < loud);
    assert(std::abs(int(full.back()) * 2 - int(loud)) <= 1);
    assert(sleepy.volume() == 300);
    for (std::size_t i = 1; i < full.size(); ++i) assert(full[i] <= full[i - 1]);

    sleepy.setSleepGain(0);
    full.fill(30000);
    sleepy.process(full.data(), full.size());
    assert(full[OutputGain::kRampFrames - 1] == 0 && full.back() == 0);
    assert(sleepy.volume() == 300);

    sleepy.setSleepGain(32768);
    full.fill(30000);
    sleepy.process(full.data(), full.size());
    for (std::size_t i = 1; i < full.size(); ++i) assert(full[i] >= full[i - 1]);
    assert(full.back() == loud);

    // Sleep ramps are block-split deterministic and repeated setters do not
    // restart them.
    OutputGain a(300), b(300);
    a.setSleepGain(0);
    b.setSleepGain(0);
    full.fill(30000);
    split.fill(30000);
    a.process(full.data(), full.size());
    for (std::size_t offset = 0; offset < split.size(); offset += 32) {
        b.setSleepGain(0);
        b.process(split.data() + offset, 32);
    }
    assert(full == split);
    assert(full.back() == 0);

    // Retargeting a partial sleep fade starts at its current value. Reset
    // restores an awake limiter with fresh envelope/counter state.
    a.reset(300);
    a.setSleepGain(0);
    full.fill(30000);
    a.process(full.data(), 128);
    const int beforeWake = full[127];
    a.setSleepGain(32768);
    full.fill(30000);
    a.process(full.data(), full.size());
    assert(full[0] >= beforeWake && full[0] - beforeWake < 128);
    assert(full.back() == loud && a.volume() == 300);

    a.setSleepGain(0);
    a.reset(300);
    assert(a.limitedSamples() == 0);
    std::int16_t afterReset = 30000;
    a.process(&afterReset, 1);
    assert(afterReset == loud);
    assert(a.limitedSamples() == 1);

    // Q15 values above unity clamp to unity and cannot amplify.
    a.reset(300);
    a.setSleepGain(UINT16_MAX);
    afterReset = 30000;
    a.process(&afterReset, 1);
    assert(afterReset == loud);
}

} // namespace

int main() {
    testGainCurveAndLegacyCompatibility();
    testDriverAndPcmBounds();
    testPeakHoldReleaseAndSustainedBound();
    testBlockSplitDeterminism();
    testVolumeTransitionsAndRetargeting();
    testSleepFadeAndReset();
}
