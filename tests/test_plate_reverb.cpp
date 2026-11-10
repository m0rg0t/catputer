#include "lofi/plate_reverb.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": "  \
                      << #condition << '\n';                                           \
            std::exit(1);                                                              \
        }                                                                              \
    } while (false)

namespace {

using lofi::PlateReverb;
constexpr std::size_t kRate = 32000;

// A 10 ms burst of deterministic noise, then silence.
std::vector<float> burstResponse(PlateReverb& reverb, std::size_t frames) {
    std::vector<float> output(frames);
    std::uint32_t state = 1;
    for (std::size_t index = 0; index < frames; ++index) {
        float input = 0.0f;
        if (index < kRate / 100u) {
            state = state * 1664525u + 1013904223u;
            input = (static_cast<float>((state >> 8) % 2001u) - 1000.0f) / 2000.0f;
        }
        output[index] = reverb.process(input);
    }
    return output;
}

std::size_t lastSounding(const std::vector<float>& audio) {
    std::size_t last = 0;
    for (std::size_t index = 0; index < audio.size(); ++index) {
        if (audio[index] != 0.0f) {
            last = index;
        }
    }
    return last;
}

double energy(const std::vector<float>& audio, std::size_t from, std::size_t to) {
    double total = 0.0;
    for (std::size_t index = from; index < to; ++index) {
        total += static_cast<double>(audio[index]) * audio[index];
    }
    return total;
}

void testSilenceStaysSilent() {
    auto reverb = std::make_unique<PlateReverb>();
    for (std::size_t index = 0; index < kRate; ++index) {
        CHECK(reverb->process(0.0f) == 0.0f);
    }
}

void testTailDecaysToExactSilence() {
    auto reverb = std::make_unique<PlateReverb>();
    PlateReverb::Settings settings{};
    settings.decay = 0.55f;
    settings.damping = 0.45f;
    reverb->configure(settings);
    const std::vector<float> tail = burstResponse(*reverb, kRate * 8u);
    // Nothing before the 20 ms predelay; a real tail after it.
    CHECK(energy(tail, 0, kRate / 50u) == 0.0);
    CHECK(energy(tail, kRate / 50u, kRate) > 0.01);
    // The tail keeps falling and ends in true digital silence, so an idle
    // reverb costs nothing audible and holds no denormals.
    CHECK(energy(tail, kRate, kRate * 2u) < energy(tail, 0, kRate) * 0.1);
    const std::size_t last = lastSounding(tail);
    CHECK(last > kRate / 2u);
    CHECK(last < kRate * 4u);
}

void testDecaySettingLengthensTheTail() {
    auto shortRoom = std::make_unique<PlateReverb>();
    auto longRoom = std::make_unique<PlateReverb>();
    PlateReverb::Settings settings{};
    settings.decay = 0.48f;
    shortRoom->configure(settings);
    settings.decay = 0.72f;
    longRoom->configure(settings);
    const std::vector<float> brief = burstResponse(*shortRoom, kRate * 8u);
    const std::vector<float> lasting = burstResponse(*longRoom, kRate * 8u);
    CHECK(lastSounding(lasting) > lastSounding(brief) + kRate / 4u);
    CHECK(energy(lasting, kRate, kRate * 8u) > energy(brief, kRate, kRate * 8u) * 2.0);
}

void testLoudInputStaysBoundedAtExtremeSettings() {
    auto reverb = std::make_unique<PlateReverb>();
    PlateReverb::Settings settings{};
    settings.decay = 5.0f;     // Clamped to 0.9.
    settings.damping = -1.0f;  // Clamped to 0.
    settings.inputLowpass = 9.0f;
    reverb->configure(settings);
    float peak = 0.0f;
    for (std::size_t index = 0; index < kRate * 20u; ++index) {
        const float square = (index / 73u) % 2u != 0u ? 1.0f : -1.0f;
        const float output = reverb->process(square);
        CHECK(std::isfinite(output));
        peak = std::max(peak, std::fabs(output));
    }
    CHECK(peak < 16.0f);
    // And it still rings out once the input stops.
    float late = 0.0f;
    for (std::size_t index = 0; index < kRate * 60u; ++index) {
        late = reverb->process(0.0f);
    }
    CHECK(late == 0.0f);
}

void testClearRestartsIdentically() {
    auto reverb = std::make_unique<PlateReverb>();
    const std::vector<float> first = burstResponse(*reverb, kRate);
    reverb->clear();
    const std::vector<float> second = burstResponse(*reverb, kRate);
    CHECK(first == second);
}

void testFitsTheDeviceBudget() {
    // int16 lines at half rate: the whole reverb stays near 25 KB.
    CHECK(sizeof(PlateReverb) <= 26u * 1024u);
}

} // namespace

int main() {
    testSilenceStaysSilent();
    testTailDecaysToExactSilence();
    testDecaySettingLengthensTheTail();
    testLoudInputStaysBoundedAtExtremeSettings();
    testClearRestartsIdentically();
    testFitsTheDeviceBudget();
    std::cout << "plate reverb tests passed\n";
    return 0;
}
