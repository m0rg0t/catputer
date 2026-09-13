#include "lofi/timbre.h"

#include <cmath>

namespace lofi {
namespace {

float ageFalloff(std::uint32_t age, float rate) noexcept {
    return 1.0f / (1.0f + static_cast<float>(age) * rate);
}

ToneEnvelope leadVersion(ToneEnvelope envelope) noexcept {
    envelope.attackIncrement *= 1.25f;
    envelope.sustain *= 0.72f;
    envelope.decay = 1.0f - (1.0f - envelope.decay) * 1.65f;
    envelope.release = 1.0f - (1.0f - envelope.release) * 3.9f;
    envelope.gain *= 0.76f;
    return envelope;
}

} // namespace

const char* toneName(Tone tone) noexcept {
    switch (tone) {
    case Tone::ElectricPiano: return "ELECTRIC PIANO";
    case Tone::FeltPiano: return "FELT PIANO";
    case Tone::NylonGuitar: return "NYLON GUITAR";
    case Tone::Vibraphone: return "VIBRAPHONE";
    case Tone::WarmPad: return "WARM PAD";
    case Tone::SoftFlute: return "SOFT FLUTE";
    }
    return "UNKNOWN";
}

const char* bassToneName(BassTone tone) noexcept {
    switch (tone) {
    case BassTone::Round: return "ROUND";
    case BassTone::Upright: return "UPRIGHT";
    case BassTone::Sub: return "SUB";
    }
    return "UNKNOWN";
}

bool validTone(Tone tone) noexcept {
    return static_cast<std::uint8_t>(tone) <=
           static_cast<std::uint8_t>(Tone::SoftFlute);
}

bool validBassTone(BassTone tone) noexcept {
    return static_cast<std::uint8_t>(tone) <=
           static_cast<std::uint8_t>(BassTone::Sub);
}

float phaseSine(std::uint32_t phase) noexcept {
    const float x = static_cast<float>(static_cast<std::int32_t>(phase)) /
                    2147483648.0f;
    float value = 4.0f * x * (1.0f - std::fabs(x));
    value += 0.225f * (value * std::fabs(value) - value);
    return value;
}

float renderTone(Tone tone, std::uint32_t phase, std::uint32_t age) noexcept {
    const float fundamental = phaseSine(phase);
    switch (tone) {
    case Tone::ElectricPiano: {
        const float brightness = ageFalloff(age, 0.00009f);
        return fundamental * 0.70f +
               (phaseSine(phase * 2u) * 0.20f +
                phaseSine(phase * 3u) * 0.10f) * brightness;
    }
    case Tone::FeltPiano: {
        const float brightness = ageFalloff(age, 0.000035f);
        return fundamental * 0.86f +
               (phaseSine(phase * 2u) * 0.10f +
                phaseSine(phase * 3u) * 0.04f) * brightness;
    }
    case Tone::NylonGuitar: {
        const float pluck = ageFalloff(age, 0.00045f);
        return fundamental * 0.64f +
               (phaseSine(phase * 2u) * 0.19f +
                phaseSine(phase * 3u) * 0.11f +
                phaseSine(phase * 4u) * 0.06f) * pluck;
    }
    case Tone::Vibraphone: {
        constexpr std::uint32_t kTremoloIncrement = 697932u; // 5.2 Hz at 32 kHz.
        const float tremolo = 0.82f + phaseSine(age * kTremoloIncrement) * 0.12f;
        const float bar = ageFalloff(age, 0.00012f);
        return fundamental * tremolo + phaseSine(phase * 4u) * 0.06f * bar;
    }
    case Tone::WarmPad: {
        constexpr std::uint32_t kDetuneIncrement = 56371u; // +0.42 Hz beating.
        const float detuned = phaseSine(phase + age * kDetuneIncrement);
        return fundamental * 0.48f + detuned * 0.38f +
               phaseSine(phase * 2u) * 0.10f;
    }
    case Tone::SoftFlute: {
        constexpr std::uint32_t kVibratoIncrement = 671089u; // 5 Hz at 32 kHz.
        const float vibrato = phaseSine(age * kVibratoIncrement);
        const auto bend = static_cast<std::int32_t>(vibrato * 90000000.0f);
        const float carrier = phaseSine(phase + static_cast<std::uint32_t>(bend));
        return carrier * 0.89f + phaseSine(phase * 2u) * 0.08f +
               phaseSine(phase * 3u) * 0.03f;
    }
    }
    return 0.0f;
}

float renderBassTone(BassTone tone, std::uint32_t phase, std::uint32_t age) noexcept {
    const float fundamental = phaseSine(phase);
    switch (tone) {
    case BassTone::Round:
        return fundamental * 0.82f + phaseSine(phase * 2u) * 0.13f +
               phaseSine(phase * 3u) * 0.05f;
    case BassTone::Upright: {
        const float pluck = ageFalloff(age, 0.00070f);
        return fundamental * 0.68f +
               (phaseSine(phase * 2u) * 0.18f +
                phaseSine(phase * 3u) * 0.09f +
                phaseSine(phase * 4u) * 0.05f) * pluck;
    }
    case BassTone::Sub:
        return fundamental * 0.97f + phaseSine(phase * 2u) * 0.03f;
    }
    return 0.0f;
}

ToneEnvelope toneEnvelope(Tone tone, bool lead) noexcept {
    ToneEnvelope envelope;
    switch (tone) {
    case Tone::ElectricPiano:
        envelope = {1.0f / 224.0f, 0.23f, 0.99984f, 0.99972f, 0.185f};
        break;
    case Tone::FeltPiano:
        envelope = {1.0f / 360.0f, 0.18f, 0.99978f, 0.99960f, 0.190f};
        break;
    case Tone::NylonGuitar:
        envelope = {1.0f / 64.0f, 0.07f, 0.99948f, 0.99880f, 0.170f};
        break;
    case Tone::Vibraphone:
        envelope = {1.0f / 96.0f, 0.42f, 0.99992f, 0.99955f, 0.160f};
        break;
    case Tone::WarmPad:
        envelope = {1.0f / 1400.0f, 0.72f, 0.99998f, 0.99972f, 0.130f};
        break;
    case Tone::SoftFlute:
        envelope = {1.0f / 700.0f, 0.65f, 0.99999f, 0.99965f, 0.140f};
        break;
    default:
        envelope = {1.0f / 224.0f, 0.23f, 0.99984f, 0.99972f, 0.185f};
        break;
    }
    return lead ? leadVersion(envelope) : envelope;
}

ToneEnvelope bassEnvelope(BassTone tone) noexcept {
    switch (tone) {
    case BassTone::Round:
        return {1.0f / 112.0f, 0.54f, 0.99955f, 0.99885f, 0.250f};
    case BassTone::Upright:
        return {1.0f / 56.0f, 0.22f, 0.99895f, 0.99780f, 0.240f};
    case BassTone::Sub:
        return {1.0f / 300.0f, 0.72f, 0.99988f, 0.99935f, 0.220f};
    }
    return {1.0f / 112.0f, 0.54f, 0.99955f, 0.99885f, 0.250f};
}

} // namespace lofi
