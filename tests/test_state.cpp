#include "lofi/state.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace {

void put32(std::uint8_t* output, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        output[i] = static_cast<std::uint8_t>(value >> (8u * i));
    }
}

void finishLegacyCrc(std::array<std::uint8_t, lofi::kStateLegacyBytes>& data) {
    put32(data.data() + 156, lofi::crc32(data.data(), 156));
}

std::array<std::uint8_t, lofi::kStateLegacyBytes> makeFormat1() {
    std::array<std::uint8_t, lofi::kStateLegacyBytes> data{};
    std::memcpy(data.data(), "LOFI", 4);
    data[4] = lofi::kStateFormatLegacy;
    data[5] = static_cast<std::uint8_t>(lofi::kStateLegacyBytes);
    data[8] = 65;
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
    data[31] = 3; // A pre-schema-4 favorite remains visible as OLD.
    finishLegacyCrc(data);
    return data;
}

bool sameFavorite(const lofi::Favorite& left, const lofi::Favorite& right) {
    return left.seed == right.seed && left.bankFingerprint == right.bankFingerprint &&
           left.mood == right.mood && left.engine == right.engine &&
           left.texture == right.texture && left.schema == right.schema &&
           left.bpm == right.bpm && left.meter == right.meter &&
           left.keysTone == right.keysTone && left.leadTone == right.leadTone &&
           left.bassTone == right.bassTone;
}

bool sameState(const lofi::SavedState& left, const lofi::SavedState& right) {
    if (left.count != right.count || left.settings.volume != right.settings.volume ||
        left.settings.bpm != right.settings.bpm ||
        left.settings.brightness != right.settings.brightness ||
        left.settings.texture != right.settings.texture ||
        left.settings.motion != right.settings.motion ||
        left.settings.engine != right.settings.engine || left.settings.mood != right.settings.mood ||
        left.settings.meter != right.settings.meter ||
        left.settings.keysTone != right.settings.keysTone ||
        left.settings.leadTone != right.settings.leadTone ||
        left.settings.bassTone != right.settings.bassTone ||
        left.settings.autoDimSeconds != right.settings.autoDimSeconds) {
        return false;
    }
    for (unsigned i = 0; i < lofi::kMaxFavorites; ++i) {
        if (!sameFavorite(left.favorites[i], right.favorites[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    using namespace lofi;

    SavedState state;
    state.settings.volume = 300;
    state.settings.bpm = 123;
    state.settings.mood = 2;
    state.settings.autoDimSeconds = 120;
    state.settings.meter = MusicMeter::SixEight;
    state.settings.keysTone = Tone::FeltPiano;
    state.settings.leadTone = Tone::SoftFlute;
    state.settings.bassTone = BassTone::Upright;
    Favorite first;
    first.seed = UINT64_C(0x0123456789abcdef);
    first.bankFingerprint = 42;
    first.mood = 2;
    first.engine = 1;
    first.texture = 15;
    first.schema = kSessionSchema;
    first.bpm = 123;
    first.meter = MusicMeter::ThreeFour;
    first.keysTone = Tone::NylonGuitar;
    first.leadTone = Tone::WarmPad;
    first.bassTone = BassTone::Sub;
    assert(addFavorite(state, first));

    std::array<std::uint8_t, kStateBytes> data{};
    assert(encodeState(state, data));
    assert(data[4] == kStateFormatCurrent && data.size() == kStateBytes);
    assert(data[5] == 192 && data[6] == 1 && data[7] == 120 &&
           data[8] == 44 && data[15] == 123);
    assert(data[144] == 123 && data[184] == static_cast<std::uint8_t>(MusicMeter::SixEight));
    assert(data[185] == static_cast<std::uint8_t>(Tone::FeltPiano));
    assert(data[186] == static_cast<std::uint8_t>(Tone::SoftFlute));
    assert(data[187] == static_cast<std::uint8_t>(BassTone::Upright));
    assert(data[152] == static_cast<std::uint8_t>(MusicMeter::ThreeFour));
    assert(data[153] == static_cast<std::uint8_t>(Tone::NylonGuitar));
    assert(data[154] == static_cast<std::uint8_t>(Tone::WarmPad));
    assert(data[155] == static_cast<std::uint8_t>(BassTone::Sub));
    assert(data[28] == first.mood && data[29] == first.engine &&
           data[30] == first.texture && data[31] == first.schema);
    SavedState decoded;
    assert(decodeState(data.data(), data.size(), decoded));
    assert(sameState(state, decoded));
    assert(decoded.settings.volume == 300 && decoded.favorites[0].bpm == 123);

    // Format 3 used the same 192-byte layout with byte 7 reserved. It migrates
    // to the 60-second default and cannot claim a schema-5 favorite.
    auto format3 = data;
    format3[4] = kStateFormatInstruments;
    format3[7] = 0;
    format3[31] = 4;
    put32(format3.data() + 188, crc32(format3.data(), 188));
    SavedState format3Decoded;
    assert(decodeState(format3.data(), format3.size(), format3Decoded));
    assert(format3Decoded.settings.autoDimSeconds == 60 &&
           format3Decoded.favorites[0].schema == 4);
    auto format3ClaimingCurrentMusic = format3;
    format3ClaimingCurrentMusic[31] = 5;
    put32(format3ClaimingCurrentMusic.data() + 188,
          crc32(format3ClaimingCurrentMusic.data(), 188));
    assert(!decodeState(format3ClaimingCurrentMusic.data(),
                        format3ClaimingCurrentMusic.size(), format3Decoded));
    auto format3BadPadding = format3;
    format3BadPadding[7] = 30;
    put32(format3BadPadding.data() + 188, crc32(format3BadPadding.data(), 188));
    assert(!decodeState(format3BadPadding.data(), format3BadPadding.size(), format3Decoded));

    // Manual tempo and instruments are part of favorite identity.
    Favorite variant = first;
    variant.bpm = 124;
    assert(findFavorite(state, variant) < 0);
    assert(addFavorite(state, variant));
    assert(!addFavorite(state, variant));
    assert(encodeState(state, data));
    assert(decodeState(data.data(), data.size(), decoded));
    assert(decoded.favorites[1].bpm == 124);
    variant = first;
    variant.keysTone = Tone::ElectricPiano;
    assert(findFavorite(state, variant) < 0);

    // Real format-1 bytes migrate with AUTO/default instrument fields.
    SavedState legacyDecoded;
    const auto legacy = makeFormat1();
    assert(decodeState(legacy.data(), legacy.size(), legacyDecoded));
    assert(legacyDecoded.settings.volume == 65 && legacyDecoded.settings.bpm == 0);
    assert(legacyDecoded.settings.autoDimSeconds == 60);
    assert(legacyDecoded.settings.meter == MusicMeter::Auto &&
           legacyDecoded.settings.keysTone == Tone::ElectricPiano);
    assert(legacyDecoded.favorites[0].seed == UINT64_C(0x123456789abcdef0));
    assert(legacyDecoded.favorites[0].bpm == 0 &&
           legacyDecoded.favorites[0].schema == 3);
    assert(encodeState(legacyDecoded, data));
    assert(data[4] == kStateFormatCurrent && data[5] == kStateBytes);

    // Format 2 is the previous wide-volume/BPM layout and still migrates.
    auto format2 = legacy;
    format2[4] = kStateFormatBpm;
    format2[6] = 1;
    format2[8] = 44; // 300 percent
    format2[15] = 123;
    format2[144] = 123;
    finishLegacyCrc(format2);
    assert(decodeState(format2.data(), format2.size(), decoded));
    assert(decoded.settings.volume == 300 && decoded.settings.bpm == 123);
    assert(decoded.settings.autoDimSeconds == 60);
    assert(decoded.favorites[0].bpm == 123);
    assert(decoded.settings.meter == MusicMeter::Auto &&
           decoded.favorites[0].bassTone == BassTone::Round);
    auto futureFavoriteInLegacy = format2;
    futureFavoriteInLegacy[31] = kSessionSchema;
    finishLegacyCrc(futureFavoriteInLegacy);
    assert(!decodeState(futureFavoriteInLegacy.data(), futureFavoriteInLegacy.size(), decoded));

    auto malformedLegacyVolume = legacy;
    malformedLegacyVolume[8] = 101;
    finishLegacyCrc(malformedLegacyVolume);
    assert(!decodeState(malformedLegacyVolume.data(), malformedLegacyVolume.size(), decoded));
    auto futureFavoriteInFormat1 = legacy;
    futureFavoriteInFormat1[31] = kSessionSchema;
    finishLegacyCrc(futureFavoriteInFormat1);
    assert(!decodeState(futureFavoriteInFormat1.data(), futureFavoriteInFormat1.size(), decoded));

    // Invalid wide values, typed values, and tempos are rejected before
    // serialization or after a repaired CRC.
    state.settings.volume = 301;
    assert(!encodeState(state, data));
    state.settings.volume = 300;
    state.settings.autoDimSeconds = 45;
    assert(!encodeState(state, data));
    state.settings.autoDimSeconds = 120;
    state.settings.bpm = static_cast<std::uint16_t>(kMusicMinBpm - 1);
    assert(!encodeState(state, data));
    state.settings.bpm = static_cast<std::uint16_t>(kMusicMaxBpm + 1);
    assert(!encodeState(state, data));
    state.settings.bpm = 123;
    state.settings.meter = static_cast<MusicMeter>(255);
    assert(!encodeState(state, data));
    state.settings.meter = MusicMeter::SixEight;
    state.favorites[0].bpm = static_cast<std::uint16_t>(kMusicMaxBpm + 1);
    assert(!encodeState(state, data));
    state.favorites[0].bpm = 123;
    assert(encodeState(state, data));

    const auto unchanged = decoded;
    auto corrupt = data;
    corrupt[12] = 0xff; // engine is outside its allowed range.
    put32(corrupt.data() + 188, crc32(corrupt.data(), 188));
    assert(!decodeState(corrupt.data(), corrupt.size(), decoded));
    assert(sameState(decoded, unchanged));
    corrupt = data;
    corrupt[7] = 45;
    put32(corrupt.data() + 188, crc32(corrupt.data(), 188));
    assert(!decodeState(corrupt.data(), corrupt.size(), decoded));
    assert(sameState(decoded, unchanged));
    corrupt = data;
    corrupt[144] = static_cast<std::uint8_t>(kMusicMaxBpm + 1);
    put32(corrupt.data() + 188, crc32(corrupt.data(), 188));
    assert(!decodeState(corrupt.data(), corrupt.size(), decoded));
    assert(sameState(decoded, unchanged));
    corrupt = data;
    corrupt[152 + 4 * 2] = 1; // Inactive favorite extension must remain zero.
    put32(corrupt.data() + 188, crc32(corrupt.data(), 188));
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

    std::cout << "state: format1/2 migration, format3 instruments, identity, corruption and bounds passed\n";
}
