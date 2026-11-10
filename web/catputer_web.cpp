// C interface to the shared music engine for the browser build.
//
// The same Engine that runs on the Cardputer ADV is compiled to WebAssembly
// and driven by an AudioWorklet (web/worklet.js). This file adds no musical
// behavior: it owns one Engine, renders into a fixed buffer and exposes a few
// read-only status values. It allocates nothing after catputer_init().
#include "lofi/music.h"

#include <cstdint>
#include <new>

namespace {

constexpr std::uint32_t kBufferFrames = 1024;

alignas(lofi::Engine) unsigned char engineStorage[sizeof(lofi::Engine)];
lofi::Engine* engine = nullptr;
std::int16_t buffer[kBufferFrames];
std::uint8_t levels[lofi::kMusicInstrumentCount];

std::uint64_t joined(std::uint32_t high, std::uint32_t low) {
    return (static_cast<std::uint64_t>(high) << 32) | low;
}

lofi::Mood moodFrom(std::uint32_t value) {
    return value < lofi::kMoodCount ? static_cast<lofi::Mood>(value) : lofi::Mood::Cozy;
}

} // namespace

extern "C" {

// Starts (or restarts) playback from the beginning of a session.
void catputer_init(std::uint32_t seedHigh, std::uint32_t seedLow, std::uint32_t mood) {
    lofi::Config config{};
    config.seed = joined(seedHigh, seedLow);
    config.mood = moodFrom(mood);
    if (engine == nullptr) {
        engine = new (engineStorage) lofi::Engine(config);
    } else {
        engine->reset(config);
    }
}

std::uint32_t catputer_sample_rate() { return lofi::kMusicSampleRate; }
std::uint32_t catputer_buffer_frames() { return kBufferFrames; }
const std::int16_t* catputer_buffer() { return buffer; }

// Renders up to catputer_buffer_frames() mono frames into catputer_buffer().
std::uint32_t catputer_render(std::uint32_t frames) {
    if (engine == nullptr) {
        return 0;
    }
    if (frames > kBufferFrames) {
        frames = kBufferFrames;
    }
    engine->render(buffer, frames);
    return frames;
}

// Both changes take effect at the next bar, as on the device.
void catputer_set_mood(std::uint32_t mood) {
    if (engine == nullptr) {
        return;
    }
    lofi::Config config = engine->config();
    config.mood = moodFrom(mood);
    engine->requestConfig(config);
}

void catputer_next(std::uint32_t seedHigh, std::uint32_t seedLow) {
    if (engine != nullptr) {
        engine->requestNext(joined(seedHigh, seedLow));
    }
}

void catputer_pause(std::uint32_t paused) {
    if (engine != nullptr) {
        engine->pause(paused != 0);
    }
}

std::uint32_t catputer_bpm() { return engine ? engine->snapshot().bpm : 0; }
std::uint32_t catputer_bar() { return engine ? engine->snapshot().bar : 0; }
std::uint32_t catputer_mood() {
    return engine ? static_cast<std::uint32_t>(engine->snapshot().config.mood) : 0;
}
std::uint32_t catputer_meter() {
    if (engine == nullptr) {
        return 0;
    }
    const lofi::Snapshot snapshot = engine->snapshot();
    return static_cast<std::uint32_t>(snapshot.meterNumerator) * 256u +
           snapshot.meterDenominator;
}
std::uint32_t catputer_seed_high() {
    return engine ? static_cast<std::uint32_t>(engine->snapshot().config.seed >> 32) : 0;
}
std::uint32_t catputer_seed_low() {
    return engine ? static_cast<std::uint32_t>(engine->snapshot().config.seed) : 0;
}

// Seven activity levels (0..255) in MusicInstrument order.
const std::uint8_t* catputer_levels() {
    if (engine != nullptr) {
        const lofi::Snapshot snapshot = engine->snapshot();
        for (std::size_t index = 0; index < lofi::kMusicInstrumentCount; ++index) {
            levels[index] = snapshot.instrumentLevels[index];
        }
    }
    return levels;
}

} // extern "C"
