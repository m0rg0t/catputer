#pragma once

#include <cstdint>

namespace lofi {

// Compact, stable values stored by the engine settings and favorites.
enum class Tone : std::uint8_t {
    ElectricPiano = 0,
    FeltPiano = 1,
    NylonGuitar = 2,
    Vibraphone = 3,
    WarmPad = 4,
    SoftFlute = 5,
};

enum class BassTone : std::uint8_t {
    Round = 0,
    Upright = 1,
    Sub = 2,
};

struct ToneEnvelope {
    float attackIncrement;
    float sustain;
    float decay;
    float release;
    float gain;
};

const char* toneName(Tone tone) noexcept;
const char* bassToneName(BassTone tone) noexcept;
bool validTone(Tone tone) noexcept;
bool validBassTone(BassTone tone) noexcept;

// One full cycle occupies uint32_t's range. This is the same inexpensive,
// bounded approximation historically used by the music engine.
float phaseSine(std::uint32_t phase) noexcept;

// These are intentionally compact synthetic profiles, not acoustic models.
// age is the number of rendered frames since note-on. Both functions are pure,
// allocation-free and bounded to [-1, 1].
float renderTone(Tone tone, std::uint32_t phase, std::uint32_t age) noexcept;
float renderBassTone(BassTone tone, std::uint32_t phase, std::uint32_t age) noexcept;

ToneEnvelope toneEnvelope(Tone tone, bool lead = false) noexcept;
ToneEnvelope bassEnvelope(BassTone tone) noexcept;

} // namespace lofi
