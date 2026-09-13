#pragma once
#include "lofi/music.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace lofi {
constexpr std::size_t kMaxFavorites = 8;
// The first two formats are exactly 160 bytes. Formats 3 through 5 append typed
// instrument/meter settings while retaining the original favorite records.
// Format 4 assigns the previously reserved header byte to auto-dim.
// Format 5 permits Sunny as mood 3; all byte positions stay unchanged.
// Keep the legacy size public so platform readers can accept both lengths.
constexpr std::size_t kStateLegacyBytes = 160;
constexpr std::size_t kStateBytes = 192;
constexpr std::uint8_t kStateFormatLegacy = 1;
constexpr std::uint8_t kStateFormatBpm = 2;
constexpr std::uint8_t kStateFormatInstruments = 3;
constexpr std::uint8_t kStateFormatAutoDim = 4;
constexpr std::uint8_t kStateFormatCurrent = 5;
static_assert(kMusicMaxBpm <= 255, "Persisted BPM bytes must hold the full manual range");
static_assert(kStateBytes <= 255, "Persisted state length must fit the header byte");
static_assert(kMusicSchemaVersion <= 255, "Favorite schema must fit the saved format");
constexpr std::uint8_t kSessionSchema = static_cast<std::uint8_t>(kMusicSchemaVersion);
struct Settings {
    // The settings volume is the user-facing master gain and intentionally has
    // a wider range than Config::volume, which remains the audio engine's
    // normalized session parameter.
    std::uint16_t volume = 35;
    std::uint8_t brightness = 70;
    std::uint8_t texture = 15;
    std::uint8_t motion = 2;
    std::uint8_t engine = 0;
    std::uint8_t mood = 0;
    // Appended after the original fields so existing aggregate initializers
    // continue to mean the same thing. 0 = AUTO; otherwise
    // kMusicMinBpm..kMusicMaxBpm.
    std::uint16_t bpm = 0;
    MusicMeter meter = MusicMeter::Auto;
    Tone keysTone = Tone::ElectricPiano;
    Tone leadTone = Tone::Vibraphone;
    BassTone bassTone = BassTone::Round;
    // 0 disables idle dimming; the supported delays are 30, 60 and 120 seconds.
    std::uint8_t autoDimSeconds = 60;
};
struct Favorite {
    std::uint64_t seed = 0;
    std::uint32_t bankFingerprint = 0;
    std::uint8_t mood = 0;
    std::uint8_t engine = 0;
    std::uint8_t texture = 15;
    std::uint8_t schema = kSessionSchema;
    std::uint16_t bpm = 0; // 0 = AUTO; otherwise kMusicMinBpm..kMusicMaxBpm.
    MusicMeter meter = MusicMeter::Auto;
    Tone keysTone = Tone::ElectricPiano;
    Tone leadTone = Tone::Vibraphone;
    BassTone bassTone = BassTone::Round;
};
struct SavedState {
    Settings settings{};
    std::uint8_t count = 0;
    std::array<Favorite, kMaxFavorites> favorites{};
};
bool validSettings(const Settings& settings);
std::uint32_t crc32(const std::uint8_t* data, std::size_t size);
std::uint32_t fingerprint(const char* text);
bool encodeState(const SavedState& state, std::array<std::uint8_t,kStateBytes>& output);
// A failed decode leaves destination unchanged.
bool decodeState(const std::uint8_t* data, std::size_t size, SavedState& destination);
int findFavorite(const SavedState& state, const Favorite& favorite);
bool addFavorite(SavedState& state, const Favorite& favorite);
bool removeFavorite(SavedState& state, std::size_t index);
}
