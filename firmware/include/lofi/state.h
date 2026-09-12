#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace lofi {
constexpr std::size_t kMaxFavorites = 8;
constexpr std::size_t kStateBytes = 160;
constexpr std::uint8_t kSessionSchema = 1;
struct Settings {
    std::uint8_t volume = 35;
    std::uint8_t brightness = 70;
    std::uint8_t texture = 15;
    std::uint8_t motion = 2;
    std::uint8_t engine = 0;
    std::uint8_t mood = 0;
};
struct Favorite {
    std::uint64_t seed = 0;
    std::uint32_t bankFingerprint = 0;
    std::uint8_t mood = 0;
    std::uint8_t engine = 0;
    std::uint8_t texture = 15;
    std::uint8_t schema = kSessionSchema;
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
