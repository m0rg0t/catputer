#pragma once
#include <cstddef>
#include <cstdint>

namespace lofi {
struct Sample {
    const std::int16_t* data;
    std::uint32_t frames;
    std::uint32_t sampleRate;
    std::uint8_t rootMidi;
    std::uint32_t loopStart;
    std::uint32_t loopEnd;
};
// Stable order: keys low/mid/high, kick, snare, closed hat, rim.
const Sample* builtinSamples(std::size_t& count);
const char* builtinSampleBankId();
}
