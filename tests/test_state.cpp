#include "lofi/state.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace {

bool sameFavorite(const lofi::Favorite& left, const lofi::Favorite& right) {
    return left.seed == right.seed && left.bankFingerprint == right.bankFingerprint &&
           left.mood == right.mood && left.engine == right.engine &&
           left.texture == right.texture && left.schema == right.schema &&
           left.bpm == right.bpm;
}

bool sameState(const lofi::SavedState& left, const lofi::SavedState& right) {
    if (left.count != right.count || left.settings.volume != right.settings.volume ||
        left.settings.bpm != right.settings.bpm ||
        left.settings.brightness != right.settings.brightness ||
        left.settings.texture != right.settings.texture ||
        left.settings.motion != right.settings.motion ||
        left.settings.engine != right.settings.engine || left.settings.mood != right.settings.mood) {
        return false;
    }
    for (unsigned i = 0; i < lofi::kMaxFavorites; ++i) {
        if (!sameFavorite(left.favorites[i], right.favorites[i])) {
            return false;
        }
    }
    return true;
}

void put32(std::uint8_t* output, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        output[i] = static_cast<std::uint8_t>(value >> (8u * i));
    }
}

void finishCrc(std::array<std::uint8_t, lofi::kStateBytes>& data) {
    put32(data.data() + 156, lofi::crc32(data.data(), 156));
}

std::array<std::uint8_t, lofi::kStateBytes> makeLegacyState() {
    std::array<std::uint8_t, lofi::kStateBytes> data{};
    std::memcpy(data.data(), "LOFI", 4);
    data[4] = lofi::kStateFormatLegacy;
    data[5] = static_cast<std::uint8_t>(lofi::kStateBytes);
    data[8] = 65;  // Format 1 volume is one byte and migrates losslessly.
    data[9] = 70;
    data[10] = 15;
    data[11] = 2;
    data[12] = 0;
    data[13] = 1;
    data[14] = 1;
    const std::uint64_t seed = UINT64_C(0x123456789abcdef0);
    put32(data.data() + 16, static_cast<std::uint32_t>(seed));
    put32(data.data() + 20, static_cast<std::uint32_t>(seed >> 32u));
    put32(data.data() + 24, 42);
    data[28] = 2;
    data[29] = 1;
    data[30] = 15;
    data[31] = 1;
    finishCrc(data);
    return data;
}

} // namespace

int main() {
    using namespace lofi;

    SavedState state;
    state.settings.volume = 300;
    state.settings.bpm = 123;
    state.settings.mood = 2;
    Favorite first;
    first.seed = UINT64_C(0x0123456789abcdef);
    first.bankFingerprint = 42;
    first.mood = 2;
    first.engine = 1;
    first.texture = 15;
    first.schema = kSessionSchema;
    first.bpm = 123;
    assert(addFavorite(state, first));

    std::array<std::uint8_t, kStateBytes> data{};
    assert(encodeState(state, data));
    assert(data[4] == kStateFormatCurrent && data.size() == kStateBytes);
    assert(data[6] == 1 && data[8] == 44 && data[15] == 123 && data[144] == 123);
    assert(data[28] == first.mood && data[29] == first.engine &&
           data[30] == first.texture && data[31] == first.schema);
    SavedState decoded;
    assert(decodeState(data.data(), data.size(), decoded));
    assert(sameState(state, decoded));
    assert(decoded.settings.volume == 300 && decoded.favorites[0].bpm == 123);

    // Manual tempo is part of favorite identity; a different tempo is a
    // distinct replay, while an exact duplicate is rejected.
    Favorite tempoVariant = first;
    tempoVariant.bpm = 124;
    assert(findFavorite(state, tempoVariant) < 0);
    assert(addFavorite(state, tempoVariant));
    assert(!addFavorite(state, tempoVariant));
    assert(encodeState(state, data));
    assert(decodeState(data.data(), data.size(), decoded));
    assert(decoded.favorites[1].bpm == 124);

    SavedState legacyDecoded;
    const auto legacy = makeLegacyState();
    assert(decodeState(legacy.data(), legacy.size(), legacyDecoded));
    assert(legacyDecoded.settings.volume == 65 && legacyDecoded.settings.bpm == 0);
    assert(legacyDecoded.favorites[0].seed == UINT64_C(0x123456789abcdef0));
    assert(legacyDecoded.favorites[0].bpm == 0);
    assert(encodeState(legacyDecoded, data)); // Legacy input upgrades to format 2.
    assert(data[4] == kStateFormatCurrent);
    auto malformedLegacyVolume = legacy;
    malformedLegacyVolume[8] = 101;
    finishCrc(malformedLegacyVolume);
    assert(!decodeState(malformedLegacyVolume.data(), malformedLegacyVolume.size(), decoded));

    // Invalid wide values and tempos are rejected before serialization.
    state.settings.volume = 301;
    assert(!encodeState(state, data));
    state.settings.volume = 300;
    state.settings.bpm = static_cast<std::uint16_t>(kMusicMinBpm - 1);
    assert(!encodeState(state, data));
    state.settings.bpm = static_cast<std::uint16_t>(kMusicMaxBpm + 1);
    assert(!encodeState(state, data));
    state.settings.bpm = 123;
    state.favorites[0].bpm = static_cast<std::uint16_t>(kMusicMaxBpm + 1);
    assert(!encodeState(state, data));
    state.favorites[0].bpm = 123;
    assert(encodeState(state, data));

    // A failed decode leaves its destination unchanged, including malformed
    // format-2 metadata with a repaired CRC.
    const auto unchanged = decoded;
    auto corrupt = data;
    corrupt[12] = 0xff; // motion is outside its allowed range.
    finishCrc(corrupt);
    assert(!decodeState(corrupt.data(), corrupt.size(), decoded));
    assert(sameState(decoded, unchanged));
    corrupt = data;
    corrupt[144] = static_cast<std::uint8_t>(kMusicMaxBpm + 1);
    finishCrc(corrupt);
    assert(!decodeState(corrupt.data(), corrupt.size(), decoded));
    assert(sameState(decoded, unchanged));
    corrupt = data;
    corrupt[152] = 1; // Reserved tail must remain zero in format 2.
    finishCrc(corrupt);
    assert(!decodeState(corrupt.data(), corrupt.size(), decoded));
    assert(sameState(decoded, unchanged));

    SavedState bounded;
    for (unsigned i = 0; i < 8; ++i) {
        Favorite extra;
        extra.seed = 1000 + i;
        extra.mood = static_cast<std::uint8_t>(i % 3);
        extra.bpm = 40;
        assert(addFavorite(bounded, extra));
    }
    Favorite ninth;
    ninth.seed = 9999;
    assert(!addFavorite(bounded, ninth));
    assert(removeFavorite(bounded, 0));
    assert(bounded.count == 7);
    assert(!removeFavorite(bounded, 8));

    std::cout << "state: format1 migration, format2 wide volume/BPM, identity, corruption and bounds passed\n";
}
