#include "lofi/state.h"

#include <cstring>

namespace lofi {
namespace {

// Both historical 160-byte formats were last emitted by music schema 3.
constexpr std::uint8_t kLegacyMaxMusicSchema = 3;
// Format 3 was superseded before generation schema 5. Keeping this bound
// prevents a corrupted old save from making a schema-5 favorite replayable.
constexpr std::uint8_t kFormat3MaxMusicSchema = 4;

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

bool validAutoDimSeconds(std::uint8_t seconds) {
    return seconds == 0 || seconds == 30 || seconds == 60 || seconds == 120;
}

bool validSettingsValues(const Settings& settings) {
    return settings.volume <= 300 && validStoredBpm(settings.bpm) &&
           settings.brightness >= 10 && settings.brightness <= 100 &&
           settings.texture <= 100 && settings.motion <= 2 && settings.engine < 2 &&
           settings.mood < 3 && validAutoDimSeconds(settings.autoDimSeconds) &&
           validMeter(settings.meter) &&
           validTone(settings.keysTone) && validTone(settings.leadTone) &&
           validBassTone(settings.bassTone);
}

// Preserve known older favorite records when loading settings. The controller
// marks them unavailable rather than silently replaying a different score.
bool validFavorite(const Favorite& favorite) {
    return favorite.mood < 3 && favorite.engine < 2 && favorite.texture <= 100 &&
           favorite.schema >= 1 && favorite.schema <= kSessionSchema &&
           validStoredBpm(favorite.bpm) && validMeter(favorite.meter) &&
           validTone(favorite.keysTone) && validTone(favorite.leadTone) &&
           validBassTone(favorite.bassTone);
}

bool sameFavorite(const Favorite& left, const Favorite& right) {
    return left.seed == right.seed && left.bankFingerprint == right.bankFingerprint &&
           left.mood == right.mood && left.engine == right.engine &&
           left.texture == right.texture && left.schema == right.schema &&
           left.bpm == right.bpm && left.meter == right.meter &&
           left.keysTone == right.keysTone && left.leadTone == right.leadTone &&
           left.bassTone == right.bassTone;
}

bool hasDuplicateFavorite(const SavedState& state, unsigned index) {
    for (unsigned previous = 0; previous < index; ++previous) {
        if (sameFavorite(state.favorites[previous], state.favorites[index])) {
            return true;
        }
    }
    return false;
}

std::size_t crcOffsetFor(std::uint8_t format) {
    return format == kStateFormatInstruments || format == kStateFormatCurrent
               ? kStateBytes - 4
               : kStateLegacyBytes - 4;
}

bool validHeaderAndCrc(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 6 || std::memcmp(data, "LOFI", 4) != 0) {
        return false;
    }
    const std::uint8_t format = data[4];
    const bool legacy = format == kStateFormatLegacy || format == kStateFormatBpm;
    const bool extended = format == kStateFormatInstruments || format == kStateFormatCurrent;
    const std::size_t expected = extended ? kStateBytes : kStateLegacyBytes;
    if ((!legacy && !extended) || size != expected || data[5] != expected) {
        return false;
    }
    const std::size_t crcOffset = crcOffsetFor(format);
    return get32(data + crcOffset) == crc32(data, crcOffset);
}

void readFavoriteBase(const std::uint8_t* data, unsigned index, Favorite& favorite) {
    const std::uint8_t* record = data + 16 + index * 16;
    favorite.seed = static_cast<std::uint64_t>(get32(record)) |
                    (static_cast<std::uint64_t>(get32(record + 4)) << 32u);
    favorite.bankFingerprint = get32(record + 8);
    favorite.mood = record[12];
    favorite.engine = record[13];
    favorite.texture = record[14];
    favorite.schema = record[15];
}

void writeFavoriteBase(std::uint8_t* data, unsigned index, const Favorite& favorite) {
    std::uint8_t* record = data + 16 + index * 16;
    put32(record, static_cast<std::uint32_t>(favorite.seed));
    put32(record + 4, static_cast<std::uint32_t>(favorite.seed >> 32u));
    put32(record + 8, favorite.bankFingerprint);
    record[12] = favorite.mood;
    record[13] = favorite.engine;
    record[14] = favorite.texture;
    record[15] = favorite.schema;
}

void setNewDefaults(Settings& settings) {
    settings.autoDimSeconds = 60;
    settings.meter = MusicMeter::Auto;
    settings.keysTone = Tone::ElectricPiano;
    settings.leadTone = Tone::Vibraphone;
    settings.bassTone = BassTone::Round;
}

void setNewDefaults(Favorite& favorite) {
    favorite.meter = MusicMeter::Auto;
    favorite.keysTone = Tone::ElectricPiano;
    favorite.leadTone = Tone::Vibraphone;
    favorite.bassTone = BassTone::Round;
}

bool unusedRecordsAreZero(const std::uint8_t* data, std::uint8_t count) {
    const std::size_t used = 16 + static_cast<std::size_t>(count) * 16;
    for (std::size_t i = used; i < 144; ++i) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

bool validSettings(const Settings& settings) {
    return validSettingsValues(settings);
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
    if (!validSettingsValues(state.settings) || state.count > kMaxFavorites) {
        return false;
    }
    for (unsigned i = 0; i < state.count; ++i) {
        if (!validFavorite(state.favorites[i]) || hasDuplicateFavorite(state, i)) {
            return false;
        }
    }

    // Format 4 retains every legacy 16-byte favorite record exactly. The
    // trailing area adds one BPM byte and four typed bytes per favorite, then
    // four global typed bytes:
    //   144..151 favorite BPM, 152..183 favorite meter/tones,
    //   184..187 global meter/tones, 188..191 CRC.
    output.fill(0);
    std::memcpy(output.data(), "LOFI", 4);
    output[4] = kStateFormatCurrent;
    output[5] = static_cast<std::uint8_t>(kStateBytes);
    const Settings& settings = state.settings;
    output[6] = static_cast<std::uint8_t>(settings.volume >> 8u);
    output[7] = settings.autoDimSeconds;
    output[8] = static_cast<std::uint8_t>(settings.volume & 0xffu);
    output[9] = settings.brightness;
    output[10] = settings.texture;
    output[11] = settings.motion;
    output[12] = settings.engine;
    output[13] = settings.mood;
    output[14] = state.count;
    output[15] = static_cast<std::uint8_t>(settings.bpm);
    output[184] = static_cast<std::uint8_t>(settings.meter);
    output[185] = static_cast<std::uint8_t>(settings.keysTone);
    output[186] = static_cast<std::uint8_t>(settings.leadTone);
    output[187] = static_cast<std::uint8_t>(settings.bassTone);

    for (unsigned i = 0; i < state.count; ++i) {
        const Favorite& favorite = state.favorites[i];
        writeFavoriteBase(output.data(), i, favorite);
        output[144 + i] = static_cast<std::uint8_t>(favorite.bpm);
        const std::size_t extra = 152 + static_cast<std::size_t>(i) * 4;
        output[extra] = static_cast<std::uint8_t>(favorite.meter);
        output[extra + 1] = static_cast<std::uint8_t>(favorite.keysTone);
        output[extra + 2] = static_cast<std::uint8_t>(favorite.leadTone);
        output[extra + 3] = static_cast<std::uint8_t>(favorite.bassTone);
    }
    put32(output.data() + 188, crc32(output.data(), 188));
    return true;
}

bool decodeState(const std::uint8_t* data, std::size_t size, SavedState& destination) {
    if (!validHeaderAndCrc(data, size)) {
        return false;
    }

    SavedState decoded;
    const std::uint8_t format = data[4];
    setNewDefaults(decoded.settings);
    decoded.settings.volume = data[8];
    decoded.settings.brightness = data[9];
    decoded.settings.texture = data[10];
    decoded.settings.motion = data[11];
    decoded.settings.engine = data[12];
    decoded.settings.mood = data[13];
    decoded.count = data[14];
    if (decoded.count > kMaxFavorites) {
        return false;
    }

    if (format == kStateFormatLegacy) {
        // Format 1 only had a byte-wide volume and no tempo or typed fields.
        if (data[6] != 0 || data[7] != 0 || data[15] != 0 || data[8] > 100 ||
            !unusedRecordsAreZero(data, decoded.count)) {
            return false;
        }
        decoded.settings.bpm = 0;
        for (std::size_t i = 144; i < 156; ++i) {
            if (data[i] != 0) {
                return false;
            }
        }
        for (unsigned i = 0; i < decoded.count; ++i) {
            Favorite& favorite = decoded.favorites[i];
            readFavoriteBase(data, i, favorite);
            favorite.bpm = 0;
            setNewDefaults(favorite);
            if (!validFavorite(favorite) || favorite.schema > kLegacyMaxMusicSchema ||
                hasDuplicateFavorite(decoded, i)) {
                return false;
            }
        }
    } else {
        if ((format != kStateFormatCurrent && data[7] != 0) ||
            !unusedRecordsAreZero(data, decoded.count)) {
            return false;
        }
        if (format == kStateFormatCurrent) {
            decoded.settings.autoDimSeconds = data[7];
        }
        decoded.settings.volume = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[8]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[6]) << 8u));
        decoded.settings.bpm = data[15];
        if (format == kStateFormatBpm) {
            // Format 2 added only wide volume and manual BPM. Typed values
            // remain the documented defaults and its old reserved tail must
            // stay zero.
            for (std::size_t i = 152; i < 156; ++i) {
                if (data[i] != 0) {
                    return false;
                }
            }
        } else {
            decoded.settings.meter = static_cast<MusicMeter>(data[184]);
            decoded.settings.keysTone = static_cast<Tone>(data[185]);
            decoded.settings.leadTone = static_cast<Tone>(data[186]);
            decoded.settings.bassTone = static_cast<BassTone>(data[187]);
            if (data[185] > static_cast<std::uint8_t>(Tone::SoftFlute) ||
                data[186] > static_cast<std::uint8_t>(Tone::SoftFlute) ||
                data[187] > static_cast<std::uint8_t>(BassTone::Sub) ||
                data[184] > static_cast<std::uint8_t>(MusicMeter::SixEight)) {
                return false;
            }
        }
        if (!validSettingsValues(decoded.settings)) {
            return false;
        }
        for (unsigned i = 0; i < decoded.count; ++i) {
            Favorite& favorite = decoded.favorites[i];
            readFavoriteBase(data, i, favorite);
            favorite.bpm = data[144 + i];
            if (format == kStateFormatInstruments || format == kStateFormatCurrent) {
                const std::size_t extra = 152 + static_cast<std::size_t>(i) * 4;
                favorite.meter = static_cast<MusicMeter>(data[extra]);
                favorite.keysTone = static_cast<Tone>(data[extra + 1]);
                favorite.leadTone = static_cast<Tone>(data[extra + 2]);
                favorite.bassTone = static_cast<BassTone>(data[extra + 3]);
            } else {
                setNewDefaults(favorite);
            }
            if (!validFavorite(favorite) ||
                ((format == kStateFormatLegacy || format == kStateFormatBpm) &&
                 favorite.schema > kLegacyMaxMusicSchema) ||
                (format == kStateFormatInstruments &&
                 favorite.schema > kFormat3MaxMusicSchema) ||
                hasDuplicateFavorite(decoded, i)) {
                return false;
            }
        }
        for (unsigned i = static_cast<unsigned>(decoded.count); i < kMaxFavorites; ++i) {
            if (data[144 + i] != 0) {
                return false;
            }
            if (format == kStateFormatInstruments || format == kStateFormatCurrent) {
                const std::size_t extra = 152 + static_cast<std::size_t>(i) * 4;
                for (std::size_t j = 0; j < 4; ++j) {
                    if (data[extra + j] != 0) {
                        return false;
                    }
                }
            }
        }
    }

    if (!validSettingsValues(decoded.settings)) {
        return false;
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
