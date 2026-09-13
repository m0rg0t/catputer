#include "lofi/timbre.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr std::array<lofi::Tone, 6> kTones{{
    lofi::Tone::ElectricPiano, lofi::Tone::FeltPiano,
    lofi::Tone::NylonGuitar, lofi::Tone::Vibraphone,
    lofi::Tone::WarmPad, lofi::Tone::SoftFlute,
}};

constexpr std::array<lofi::BassTone, 3> kBassTones{{
    lofi::BassTone::Round, lofi::BassTone::Upright, lofi::BassTone::Sub,
}};

std::uint32_t phaseIncrement(unsigned midi) {
    double frequency = 440.0;
    for (int semitone = 69; semitone < static_cast<int>(midi); ++semitone) {
        frequency *= 1.0594630943592953;
    }
    for (int semitone = 69; semitone > static_cast<int>(midi); --semitone) {
        frequency /= 1.0594630943592953;
    }
    return static_cast<std::uint32_t>(frequency * 4294967296.0 / 32000.0);
}

template <typename Kind, std::size_t Size, typename Render>
void checkDistinct(const std::array<Kind, Size>& kinds, Render render) {
    std::array<std::array<float, 512>, Size> signatures{};
    const std::uint32_t increment = phaseIncrement(60);
    for (std::size_t kind = 0; kind < Size; ++kind) {
        std::uint32_t phase = 0x13579bdfu;
        for (std::size_t frame = 0; frame < signatures[kind].size(); ++frame) {
            signatures[kind][frame] = render(kinds[kind], phase,
                                              static_cast<std::uint32_t>(frame * 17));
            phase += increment;
        }
    }
    for (std::size_t left = 0; left < Size; ++left) {
        for (std::size_t right = left + 1; right < Size; ++right) {
            double difference = 0.0;
            for (std::size_t frame = 0; frame < signatures[left].size(); ++frame) {
                difference += std::fabs(signatures[left][frame] - signatures[right][frame]);
            }
            assert(difference / signatures[left].size() > 0.018);
        }
    }
}

template <typename Kind, typename Render>
std::array<double, 4> harmonicFingerprint(Kind kind, Render render,
                                          std::uint32_t age) {
    std::array<double, 4> harmonics{};
    constexpr unsigned kFrames = 4096;
    for (unsigned frame = 0; frame < kFrames; ++frame) {
        const std::uint32_t phase = frame << 20u;
        const double sample = render(kind, phase, age);
        for (unsigned harmonic = 1; harmonic <= harmonics.size(); ++harmonic) {
            harmonics[harmonic - 1] +=
                sample * lofi::phaseSine(phase * harmonic) * (2.0 / kFrames);
        }
    }
    return harmonics;
}

template <typename Kind, typename Render>
double meanMagnitude(Kind kind, Render render, std::uint32_t age) {
    double total = 0.0;
    constexpr unsigned kFrames = 4096;
    for (unsigned frame = 0; frame < kFrames; ++frame) {
        total += std::fabs(render(kind, frame << 20u, age + frame));
    }
    return total / kFrames;
}

template <typename Kind, typename Render>
double peakMagnitude(Kind kind, Render render, std::uint32_t age) {
    double peak = 0.0;
    constexpr unsigned kFrames = 4096;
    for (unsigned frame = 0; frame < kFrames; ++frame) {
        peak = std::max(peak, std::fabs(static_cast<double>(
            render(kind, frame << 20u, age))));
    }
    return peak;
}

void checkEnvelope(const lofi::ToneEnvelope& envelope) {
    assert(std::isfinite(envelope.attackIncrement) && envelope.attackIncrement > 0.0f &&
           envelope.attackIncrement <= 1.0f);
    assert(std::isfinite(envelope.sustain) && envelope.sustain >= 0.0f &&
           envelope.sustain <= 1.0f);
    assert(std::isfinite(envelope.decay) && envelope.decay > 0.0f && envelope.decay < 1.0f);
    assert(std::isfinite(envelope.release) && envelope.release > 0.0f &&
           envelope.release < 1.0f);
    assert(std::isfinite(envelope.gain) && envelope.gain > 0.0f && envelope.gain <= 1.0f);
}

} // namespace

int main() {
    using namespace lofi;

    static_assert(sizeof(Tone) == 1);
    static_assert(sizeof(BassTone) == 1);

    assert(std::strcmp(toneName(Tone::ElectricPiano), "ELECTRIC PIANO") == 0);
    assert(std::strcmp(toneName(Tone::SoftFlute), "SOFT FLUTE") == 0);
    assert(std::strcmp(bassToneName(BassTone::Round), "ROUND") == 0);
    assert(std::strcmp(bassToneName(BassTone::Sub), "SUB") == 0);
    for (const auto tone : kTones) assert(validTone(tone));
    for (const auto tone : kBassTones) assert(validBassTone(tone));
    assert(!validTone(static_cast<Tone>(6)));
    assert(!validTone(static_cast<Tone>(255)));
    assert(!validBassTone(static_cast<BassTone>(3)));
    assert(std::strcmp(toneName(static_cast<Tone>(255)), "UNKNOWN") == 0);
    assert(std::strcmp(bassToneName(static_cast<BassTone>(255)), "UNKNOWN") == 0);

    // Exercise the complete uint32 phase cycle densely. The oscillator remains
    // finite and bounded, including the signed conversion seam at half-cycle.
    for (std::uint32_t index = 0; index < 65536; ++index) {
        const float sample = phaseSine(index << 16u);
        assert(std::isfinite(sample));
        assert(sample >= -1.0f && sample <= 1.0f);
    }

    // Render the full musical register for several note ages. Explicit
    // harmonics stop at 4x: MIDI 83's highest one is about 3.95 kHz at 32 kHz.
    constexpr std::array<std::uint32_t, 6> ages{{0, 1, 127, 4096, 32000, UINT32_MAX}};
    for (const auto tone : kTones) {
        for (unsigned midi = 48; midi <= 83; ++midi) {
            const std::uint32_t increment = phaseIncrement(midi);
            std::uint32_t phase = 0x2468ace0u;
            for (const auto age : ages) {
                for (unsigned frame = 0; frame < 256; ++frame) {
                    const float sample = renderTone(tone, phase, age + frame);
                    assert(std::isfinite(sample));
                    assert(sample >= -1.00001f && sample <= 1.00001f);
                    phase += increment;
                }
            }
        }
        checkEnvelope(toneEnvelope(tone, false));
        checkEnvelope(toneEnvelope(tone, true));
        const double peak = peakMagnitude(tone, renderTone, 0);
        assert(peak > 0.45 && peak <= 1.00001);
    }
    for (const auto tone : kBassTones) {
        for (unsigned midi = 32; midi <= 52; ++midi) {
            const std::uint32_t increment = phaseIncrement(midi);
            std::uint32_t phase = 0xabcdef01u;
            for (const auto age : ages) {
                for (unsigned frame = 0; frame < 256; ++frame) {
                    const float sample = renderBassTone(tone, phase, age + frame);
                    assert(std::isfinite(sample));
                    assert(sample >= -1.00001f && sample <= 1.00001f);
                    phase += increment;
                }
            }
        }
        checkEnvelope(bassEnvelope(tone));
        const double peak = peakMagnitude(tone, renderBassTone, 0);
        assert(peak > 0.45 && peak <= 1.00001);
    }

    checkDistinct(kTones, renderTone);
    checkDistinct(kBassTones, renderBassTone);

    // Low-order additive profiles have measurably different spectra without
    // resorting to discontinuous saw/square waves. These are identity checks,
    // not claims that the compact profiles reproduce acoustic instruments.
    const auto electric = harmonicFingerprint(Tone::ElectricPiano, renderTone, 0);
    const auto felt = harmonicFingerprint(Tone::FeltPiano, renderTone, 0);
    const auto nylon = harmonicFingerprint(Tone::NylonGuitar, renderTone, 0);
    const auto round = harmonicFingerprint(BassTone::Round, renderBassTone, 0);
    const auto upright = harmonicFingerprint(BassTone::Upright, renderBassTone, 0);
    const auto sub = harmonicFingerprint(BassTone::Sub, renderBassTone, 0);
    assert(electric[1] > felt[1] + 0.06);
    assert(nylon[3] > felt[3] + 0.02);
    assert(upright[1] > round[1] + 0.02);
    assert(sub[1] < round[1] - 0.05);

    // Plucked/struck profiles lose harmonic brightness with age; sustained
    // profiles rely more heavily on their envelope or slow modulation.
    assert(meanMagnitude(Tone::NylonGuitar, renderTone, 32000) <
           meanMagnitude(Tone::NylonGuitar, renderTone, 0));
    assert(meanMagnitude(BassTone::Upright, renderBassTone, 32000) <
           meanMagnitude(BassTone::Upright, renderBassTone, 0));
    const auto feltEnvelope = toneEnvelope(Tone::FeltPiano);
    const auto nylonEnvelope = toneEnvelope(Tone::NylonGuitar);
    const auto padEnvelope = toneEnvelope(Tone::WarmPad);
    const auto fluteEnvelope = toneEnvelope(Tone::SoftFlute);
    assert(nylonEnvelope.attackIncrement > feltEnvelope.attackIncrement);
    assert(padEnvelope.attackIncrement < feltEnvelope.attackIncrement);
    assert(fluteEnvelope.sustain > feltEnvelope.sustain);
    const auto roundEnvelope = bassEnvelope(BassTone::Round);
    const auto uprightEnvelope = bassEnvelope(BassTone::Upright);
    const auto subEnvelope = bassEnvelope(BassTone::Sub);
    assert(uprightEnvelope.attackIncrement > roundEnvelope.attackIncrement);
    assert(subEnvelope.attackIncrement < roundEnvelope.attackIncrement);
    assert(subEnvelope.sustain > roundEnvelope.sustain &&
           roundEnvelope.sustain > uprightEnvelope.sustain);

    // Invalid/corrupt selectors are silent in the oscillator path and receive
    // safe defaults only if the caller asks for an envelope before validation.
    assert(renderTone(static_cast<Tone>(255), 123u, 456u) == 0.0f);
    assert(renderBassTone(static_cast<BassTone>(255), 123u, 456u) == 0.0f);
    checkEnvelope(toneEnvelope(static_cast<Tone>(255), false));
    checkEnvelope(bassEnvelope(static_cast<BassTone>(255)));
}
