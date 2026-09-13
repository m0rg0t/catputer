#include "lofi/state.h"

#include <cstring>

namespace lofi {
namespace {

void put32(std::uint8_t* out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        out[i] = static_cast<std::uint8_t>(value >> (8u * i));
    }
}

std::uint32_t get32(const std::uint8_t* in) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(in[i]) << (8u * i);
    }
    return value;
}

bool validStoredBpm(std::uint16_t bpm) {
    return bpm == 0 || validBpm(bpm);
}

// Preserve known older favorite records when loading settings. The controller
// marks them unavailable rather than silently replaying a different score.
bool validFavorite(const Favorite& favorite) {
    return favorite.mood < 3 && favorite.engine < 2 && favorite.texture <= 100 &&
           favorite.schema >= 1 && favorite.schema <= kSessionSchema &&
           validStoredBpm(favorite.bpm);
}

bool sameFavorite(const Favorite& left, const Favorite& right) {
    return left.seed == right.seed && left.bankFingerprint == right.bankFingerprint &&
           left.mood == right.mood && left.engine == right.engine &&
           left.texture == right.texture && left.schema == right.schema &&
           left.bpm == right.bpm;
}

bool hasDuplicateFavorite(const SavedState& state, unsigned index) {
    for (unsigned previous = 0; previous < index; ++previous) {
        if (sameFavorite(state.favorites[previous], state.favorites[index])) {
            return true;
        }
    }
    return false;
}

bool validHeaderAndCrc(const std::uint8_t* data, std::size_t size) {
    return data != nullptr && size == kStateBytes &&
           std::memcmp(data, "LOFI", 4) == 0 && data[5] == kStateBytes &&
           (data[4] == kStateFormatLegacy || data[4] == kStateFormatCurrent) &&
           get32(data + 156) == crc32(data, 156);
}

} // namespace

bool validSettings(const Settings& settings) {
    return settings.volume <= 300 && validStoredBpm(settings.bpm) &&
           settings.brightness >= 10 && settings.brightness <= 100 &&
           settings.texture <= 100 && settings.motion <= 2 && settings.engine < 2 &&
           settings.mood < 3;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1u) ^ ((0u - (crc & 1u)) & 0xedb88320u);
        }
    }
    return ~crc;
}

std::uint32_t fingerprint(const char* text) {
    std::uint32_t hash = 2166136261u;
    if (text != nullptr) {
        while (*text != '\0') {
            hash ^= static_cast<unsigned char>(*text++);
            hash *= 16777619u;
        }
    }
    return hash;
}

bool encodeState(const SavedState& state, std::array<std::uint8_t, kStateBytes>& output) {
    if (!validSettings(state.settings) || state.count > kMaxFavorites) {
        return false;
    }
    for (unsigned i = 0; i < state.count; ++i) {
        if (!validFavorite(state.favorites[i]) || hasDuplicateFavorite(state, i)) {
            return false;
        }
    }

    // Format 2 keeps the exact 160-byte file footprint and every legacy
    // favorite record byte-for-byte. The header bytes are:
    //  0..3 magic, 4 format, 5 length, 6 volume high byte, 7 reserved,
    //  8 volume low byte, 9 brightness, 10 texture, 11 motion, 12 engine,
    //  13 mood, 14 count, 15 global BPM byte. Bytes 144..151 hold one BPM
    // byte for each favorite, 152..155 remain reserved, and the CRC covers
    // bytes 0..155 at 156..159.
    output.fill(0);
    std::memcpy(output.data(), "LOFI", 4);
    output[4] = kStateFormatCurrent;
    output[5] = static_cast<std::uint8_t>(kStateBytes);
    const Settings& settings = state.settings;
    output[6] = static_cast<std::uint8_t>(settings.volume >> 8u);
    output[7] = 0;
    output[8] = static_cast<std::uint8_t>(settings.volume & 0xffu);
    output[9] = settings.brightness;
    output[10] = settings.texture;
    output[11] = settings.motion;
    output[12] = settings.engine;
    output[13] = settings.mood;
    output[14] = state.count;
    output[15] = static_cast<std::uint8_t>(settings.bpm);

    for (unsigned i = 0; i < state.count; ++i) {
        std::uint8_t* record = output.data() + 16 + i * 16;
        const Favorite& favorite = state.favorites[i];
        put32(record, static_cast<std::uint32_t>(favorite.seed));
        put32(record + 4, static_cast<std::uint32_t>(favorite.seed >> 32u));
        put32(record + 8, favorite.bankFingerprint);
        record[12] = favorite.mood;
        record[13] = favorite.engine;
        record[14] = favorite.texture;
        record[15] = favorite.schema;
        output[144 + i] = static_cast<std::uint8_t>(favorite.bpm);
    }
    put32(output.data() + 156, crc32(output.data(), 156));
    return true;
}

bool decodeState(const std::uint8_t* data, std::size_t size, SavedState& destination) {
    if (!validHeaderAndCrc(data, size)) {
        return false;
    }

    SavedState decoded;
    const std::uint8_t format = data[4];
    if (format == kStateFormatLegacy) {
        // Format 1 used one byte per setting and four bytes for the trailing
        // favorite attributes. It has no persisted manual tempo, so migration
        // deliberately initializes both settings and favorite BPM to AUTO.
        if (data[6] != 0 || data[7] != 0 || data[15] != 0) {
            return false;
        }
        for (std::size_t i = 144; i < 156; ++i) {
            if (data[i] != 0) {
                return false;
            }
        }
        decoded.settings.volume = data[8];
        decoded.settings.bpm = 0;
        decoded.settings.brightness = data[9];
        decoded.settings.texture = data[10];
        decoded.settings.motion = data[11];
        decoded.settings.engine = data[12];
        decoded.settings.mood = data[13];
        decoded.count = data[14];
        // Format 1 could only represent the original 0..100 setting range;
        // values 101..255 are malformed legacy records, not wide volumes.
        if (decoded.settings.volume > 100 || !validSettings(decoded.settings) ||
            decoded.count > kMaxFavorites) {
            return false;
        }
        for (unsigned i = 0; i < decoded.count; ++i) {
            const std::uint8_t* record = data + 16 + i * 16;
            Favorite& favorite = decoded.favorites[i];
            favorite.seed = static_cast<std::uint64_t>(get32(record)) |
                            (static_cast<std::uint64_t>(get32(record + 4)) << 32u);
            favorite.bankFingerprint = get32(record + 8);
            favorite.mood = record[12];
            favorite.engine = record[13];
            favorite.texture = record[14];
            favorite.schema = record[15];
            favorite.bpm = 0;
            if (!validFavorite(favorite) || hasDuplicateFavorite(decoded, i)) {
                return false;
            }
        }
    } else {
        if (data[7] != 0) {
            return false;
        }
        decoded.settings.volume = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[8]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[6]) << 8u));
        decoded.settings.bpm = data[15];
        decoded.settings.brightness = data[9];
        decoded.settings.texture = data[10];
        decoded.settings.motion = data[11];
        decoded.settings.engine = data[12];
        decoded.settings.mood = data[13];
        decoded.count = data[14];
        if (!validSettings(decoded.settings) || decoded.count > kMaxFavorites) {
            return false;
        }
        for (unsigned i = 0; i < decoded.count; ++i) {
            const std::uint8_t* record = data + 16 + i * 16;
            Favorite& favorite = decoded.favorites[i];
            favorite.seed = static_cast<std::uint64_t>(get32(record)) |
                            (static_cast<std::uint64_t>(get32(record + 4)) << 32u);
            favorite.bankFingerprint = get32(record + 8);
            favorite.mood = record[12];
            favorite.engine = record[13];
            favorite.texture = record[14];
            favorite.schema = record[15];
            favorite.bpm = data[144 + i];
            if (!validFavorite(favorite) || hasDuplicateFavorite(decoded, i)) {
                return false;
            }
        }
        for (unsigned i = static_cast<unsigned>(decoded.count); i < kMaxFavorites; ++i) {
            if (data[144 + i] != 0) {
                return false;
            }
        }
        for (std::size_t i = 152; i < 156; ++i) {
            if (data[i] != 0) {
                return false;
            }
        }
        const std::size_t usedRecords = 16 + static_cast<std::size_t>(decoded.count) * 16;
        for (std::size_t i = usedRecords; i < 144; ++i) {
            if (data[i] != 0) {
                return false;
            }
        }
    }

    if (format == kStateFormatLegacy) {
        const std::size_t used = 16 + static_cast<std::size_t>(decoded.count) * 16;
        for (std::size_t i = used; i < 156; ++i) {
            if (data[i] != 0) {
                return false;
            }
        }
    }
    destination = decoded;
    return true;
}

int findFavorite(const SavedState& state, const Favorite& favorite) {
    for (unsigned i = 0; i < state.count && i < kMaxFavorites; ++i) {
        if (sameFavorite(state.favorites[i], favorite)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool addFavorite(SavedState& state, const Favorite& favorite) {
    if (state.count >= kMaxFavorites || !validFavorite(favorite) ||
        findFavorite(state, favorite) >= 0) {
        return false;
    }
    state.favorites[state.count++] = favorite;
    return true;
}

bool removeFavorite(SavedState& state, std::size_t index) {
    if (state.count > kMaxFavorites || index >= state.count) {
        return false;
    }
    for (std::size_t i = index + 1; i < state.count; ++i) {
        state.favorites[i - 1] = state.favorites[i];
    }
    state.favorites[--state.count] = {};
    return true;
}

} // namespace lofi
