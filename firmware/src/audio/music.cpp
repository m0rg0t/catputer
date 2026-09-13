#include "lofi/music.h"

#include "lofi/sample_bank.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <type_traits>

namespace lofi {
namespace {

constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);
constexpr std::uint32_t kFadeFrames = 640;
constexpr std::size_t kMaxBarEvents = kMusicMaxBarNotes;
constexpr std::size_t kDelayFrames = 2720; // 85 ms at 32 kHz.
constexpr int kBassLow = 32;
constexpr int kBassHigh = 48;
constexpr float kInvInt16 = 1.0f / 32768.0f;
constexpr float kPhaseScale = 4294967296.0f / static_cast<float>(kMusicSampleRate);

using Instrument = MusicInstrument;

static_assert(static_cast<std::size_t>(Instrument::Rim) + 1u ==
              kMusicInstrumentCount, "instrument diagnostics must cover every voice");

enum class EnvelopeStage : std::uint8_t {
    Attack,
    Decay,
    Release,
};

struct Rng {
    std::uint64_t state = UINT64_C(0x9e3779b97f4a7c15);

    void seed(std::uint64_t value) noexcept {
        state = value != 0 ? value : UINT64_C(0x6a09e667f3bcc909);
        next64();
    }

    std::uint64_t next64() noexcept {
        std::uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * UINT64_C(2685821657736338717);
    }

    std::uint32_t next32() noexcept {
        return static_cast<std::uint32_t>(next64() >> 32);
    }

    std::uint32_t bounded(std::uint32_t upper) noexcept {
        return upper == 0 ? 0 : next32() % upper;
    }

    bool chance(std::uint32_t numerator, std::uint32_t denominator) noexcept {
        return bounded(denominator) < numerator;
    }

    int centered(int radius) noexcept {
        const std::uint32_t span = static_cast<std::uint32_t>(radius * 2 + 1);
        return static_cast<int>(bounded(span)) - radius;
    }
};

std::uint64_t mix64(std::uint64_t value) noexcept {
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

Config sanitized(Config config) noexcept {
    const auto mood = static_cast<std::uint8_t>(config.mood);
    const auto soundEngine = static_cast<std::uint8_t>(config.soundEngine);
    if (mood > static_cast<std::uint8_t>(Mood::Night)) {
        config.mood = Mood::Cozy;
    }
    if (soundEngine > static_cast<std::uint8_t>(SoundEngine::Hybrid)) {
        config.soundEngine = SoundEngine::Synth;
    }
    config.volume = std::min<std::uint8_t>(config.volume, 100);
    config.texture = std::min<std::uint8_t>(config.texture, 100);
    if (config.bpm != 0) {
        config.bpm = std::max<std::uint16_t>(
            kMusicMinBpm, std::min<std::uint16_t>(config.bpm, kMusicMaxBpm));
    }
    if (static_cast<std::uint8_t>(config.meter) >
        static_cast<std::uint8_t>(MusicMeter::SixEight)) {
        config.meter = MusicMeter::Auto;
    }
    if (static_cast<std::uint8_t>(config.keysTone) >
        static_cast<std::uint8_t>(Tone::SoftFlute)) {
        config.keysTone = Tone::ElectricPiano;
    }
    if (static_cast<std::uint8_t>(config.leadTone) >
        static_cast<std::uint8_t>(Tone::SoftFlute)) {
        config.leadTone = Tone::Vibraphone;
    }
    if (static_cast<std::uint8_t>(config.bassTone) >
        static_cast<std::uint8_t>(BassTone::Sub)) {
        config.bassTone = BassTone::Round;
    }
    return config;
}

bool sameExceptBpm(const Config& left, const Config& right) noexcept {
    return left.seed == right.seed && left.mood == right.mood &&
           left.soundEngine == right.soundEngine && left.volume == right.volume &&
           left.texture == right.texture && left.meter == right.meter &&
           left.keysTone == right.keysTone && left.leadTone == right.leadTone &&
           left.bassTone == right.bassTone;
}


float pitchRatio(int semitones) noexcept {
    constexpr float kSemitone = 1.0594630943592953f;
    float ratio = 1.0f;
    if (semitones >= 0) {
        for (int i = 0; i < semitones; ++i) {
            ratio *= kSemitone;
        }
    } else {
        for (int i = 0; i > semitones; --i) {
            ratio /= kSemitone;
        }
    }
    return ratio;
}

std::uint32_t midiPhaseIncrement(std::uint8_t midi) noexcept {
    const float frequency = 440.0f * pitchRatio(static_cast<int>(midi) - 69);
    const float increment = frequency * kPhaseScale;
    if (increment <= 1.0f) {
        return 1;
    }
    return static_cast<std::uint32_t>(increment);
}

std::uint32_t samplePhaseIncrement(const Sample& sample, int semitones,
                                   float variation = 1.0f) noexcept {
    const float increment = static_cast<float>(sample.sampleRate) /
                            static_cast<float>(kMusicSampleRate) *
                            pitchRatio(semitones) * variation * 65536.0f;
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(increment));
}

float softClip(float value) noexcept {
    const float magnitude = std::fabs(value);
    return value / (1.0f + 0.38f * magnitude);
}

std::uint8_t instrumentPriority(Instrument instrument) noexcept {
    switch (instrument) {
    case Instrument::Bass:
        return 7;
    case Instrument::Keys:
        return 6;
    case Instrument::Lead:
        return 5;
    case Instrument::Kick:
        return 4;
    case Instrument::Snare:
        return 3;
    case Instrument::Rim:
        return 2;
    case Instrument::Hat:
        return 1;
    }
    return 1;
}

bool isDrum(Instrument instrument) noexcept {
    return instrument == Instrument::Kick || instrument == Instrument::Snare ||
           instrument == Instrument::Hat || instrument == Instrument::Rim;
}

struct Event {
    std::uint64_t start = 0;
    std::uint32_t duration = 0;
    Instrument instrument = Instrument::Keys;
    std::uint8_t note = 60;
    std::uint8_t velocity = 80;
};

struct Voice {
    bool active = false;
    Instrument instrument = Instrument::Keys;
    EnvelopeStage stage = EnvelopeStage::Attack;
    std::uint8_t note = 60;
    std::uint8_t priority = 0;
    std::uint64_t releaseAt = 0;
    std::uint32_t serial = 0;
    std::uint32_t age = 0;
    std::uint32_t phase = 0;
    std::uint32_t phaseIncrement = 1;
    std::uint32_t noiseState = 1;
    std::uint32_t samplePhaseQ16 = 0;
    std::uint32_t sampleIncrementQ16 = 1;
    const Sample* sample = nullptr;
    bool sampleActive = false;
    float envelope = 0.0f;
    float attackIncrement = 1.0f;
    float sustain = 0.0f;
    float decay = 0.999f;
    float release = 0.995f;
    float gain = 0.1f;
    float velocity = 0.6f;
    float filter = 0.0f;
    float lastOutput = 0.0f;
    float stealTail = 0.0f;
};

std::uint32_t noiseNext(std::uint32_t& state) noexcept {
    std::uint32_t x = state != 0 ? state : 1;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

float signedNoise(std::uint32_t& state) noexcept {
    return static_cast<float>(static_cast<std::int32_t>(noiseNext(state))) /
           2147483648.0f;
}

float readSample(Voice& voice) noexcept {
    if (!voice.sampleActive || voice.sample == nullptr || voice.sample->data == nullptr ||
        voice.sample->frames == 0) {
        return 0.0f;
    }

    const Sample& sample = *voice.sample;
    std::uint32_t index = voice.samplePhaseQ16 >> 16;
    if (index >= sample.frames) {
        voice.sampleActive = false;
        return 0.0f;
    }
    const std::uint32_t nextIndex = std::min<std::uint32_t>(index + 1, sample.frames - 1);
    const float fraction = static_cast<float>(voice.samplePhaseQ16 & 0xffffu) / 65536.0f;
    const float a = static_cast<float>(sample.data[index]);
    const float b = static_cast<float>(sample.data[nextIndex]);
    const float result = (a + (b - a) * fraction) * kInvInt16;

    voice.samplePhaseQ16 += voice.sampleIncrementQ16;
    index = voice.samplePhaseQ16 >> 16;
    const bool canLoop = sample.loopEnd > sample.loopStart &&
                         sample.loopEnd <= sample.frames &&
                         sample.loopStart + 1 < sample.loopEnd;
    if (canLoop && voice.stage != EnvelopeStage::Release && index >= sample.loopEnd) {
        const std::uint32_t loopLengthQ16 = (sample.loopEnd - sample.loopStart) << 16;
        const std::uint32_t overshoot = voice.samplePhaseQ16 - (sample.loopEnd << 16);
        voice.samplePhaseQ16 = (sample.loopStart << 16) +
                               (loopLengthQ16 != 0 ? overshoot % loopLengthQ16 : 0);
    } else if (index >= sample.frames) {
        voice.sampleActive = false;
    }
    return result;
}

std::uint64_t hashByte(std::uint64_t hash, std::uint8_t value) noexcept {
    return (hash ^ value) * kFnvPrime;
}

template <typename T>
std::uint64_t hashInteger(std::uint64_t hash, T value) noexcept {
    using U = typename std::make_unsigned<T>::type;
    std::uint64_t bits = static_cast<std::uint64_t>(static_cast<U>(value));
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        hash = hashByte(hash, static_cast<std::uint8_t>(bits & 0xffu));
        bits >>= 8;
    }
    return hash;
}

ArrangementSection sectionForBar(std::uint32_t bar, std::uint32_t sessionBars) noexcept {
    if (bar < 4) {
        return ArrangementSection::Intro;
    }
    if (bar < 12) {
        return ArrangementSection::Groove;
    }
    if (bar < 28) {
        return ArrangementSection::Melody;
    }
    if (bar < 36) {
        return ArrangementSection::Breakdown;
    }
    if (bar + 4 >= sessionBars) {
        return ArrangementSection::Outro;
    }
    return ArrangementSection::Return;
}

struct ChordShape {
    std::uint8_t third;
    std::uint8_t seventh;
};

constexpr ChordShape kMajor7{4, 11};
constexpr ChordShape kMinor7{3, 10};
constexpr ChordShape kDominant7{4, 10};

struct Harmony {
    std::uint8_t degree;
    ChordShape shape;
};

struct MeterSpec {
    std::uint8_t numerator;
    std::uint8_t denominator;
    std::uint8_t beatsPerBar;
    std::uint8_t stepsPerBar;
    std::uint8_t stepsPerBeat;
};

constexpr MeterSpec meterSpec(MusicMeter meter) noexcept {
    switch (meter) {
    case MusicMeter::ThreeFour:
        return MeterSpec{3, 4, 3, 12, 4};
    case MusicMeter::SixEight:
        return MeterSpec{6, 8, 2, 12, 6};
    case MusicMeter::Auto:
    case MusicMeter::FourFour:
        return MeterSpec{4, 4, 4, 16, 4};
    }
    return MeterSpec{4, 4, 4, 16, 4};
}

} // namespace

struct Engine::Impl {
    Config current{};
    Config pendingConfig{};
    bool pendingChange = false;
    bool pendingRestartRequested = false;
    std::uint32_t pendingApplyBar = 0;

    mutable std::atomic_flag commandLock = ATOMIC_FLAG_INIT;
    bool commandConfigPending = false;
    Config commandConfig{};
    std::uint64_t commandConfigSequence = 0;
    bool commandNextPending = false;
    bool commandNextExplicit = false;
    std::uint64_t commandSeed = 0;
    std::uint64_t commandNextSequence = 0;
    std::uint64_t commandSequence = 0;
    bool pauseCommandPending = false;
    bool pauseCommand = false;

    mutable std::atomic_flag publishLock = ATOMIC_FLAG_INIT;
    Snapshot publishedSnapshot{};
    Diagnostics publishedDiagnostics{};

    Rng scoreRng{};
    Rng timbreRng{};
    Rng textureRng{};
    const Sample* samples = nullptr;
    std::size_t sampleCount = 0;

    Event events[kMaxBarEvents]{};
    std::uint8_t eventCount = 0;
    std::uint8_t nextEvent = 0;
    Voice voices[kMusicVoiceCapacity]{};
    std::uint32_t voiceSerial = 0;

    std::int16_t delay[kDelayFrames]{};
    std::size_t delayIndex = 0;
    float toneState = 0.0f;
    float dcInput = 0.0f;
    float dcOutput = 0.0f;
    float kickDuck = 1.0f;
    float keysGain = 1.0f;
    float pauseGain = 1.0f;
    float transitionGain = 1.0f;
    std::uint32_t transitionFadeIn = 0;
    bool pauseTarget = false;
    bool pauseSettled = false;

    std::uint64_t renderedFrames = 0;
    std::uint64_t transportSample = 0;
    std::uint64_t sessionSample = 0;
    std::uint64_t barStartQ32 = 0;
    std::uint64_t barEndQ32 = 0;
    std::uint64_t stepQ32 = 0;
    std::uint32_t barIndex = 0;
    std::uint32_t sessionBars = 64;
    std::uint16_t bpm = 76;
    std::uint16_t autoBpm = 76;
    MusicMeter meter = MusicMeter::FourFour;
    std::uint8_t meterNumerator = 4;
    std::uint8_t meterDenominator = 4;
    std::uint8_t beatsPerBar = 4;
    std::uint8_t stepsPerBar = 16;
    std::uint8_t stepsPerBeat = 4;
    std::uint8_t swingPercent = 56;
    std::uint8_t keyPitchClass = 0;
    bool minorSession = false;
    std::uint8_t progressionDegrees[4]{};
    ChordShape progressionShapes[4]{};
    std::uint8_t chordNotes[4]{};
    std::uint8_t currentBassRoot = 36;
    std::uint8_t lastBassNote = 36;
    std::uint8_t lastLeadNote = 72;
    std::int8_t motif[8]{};
    std::uint64_t scoreHash = kFnvOffset;
    std::uint32_t scoreEvents = 0;
    std::uint32_t stolenVoices = 0;
    std::uint32_t droppedNoteEvents = 0;
    std::uint32_t sessionTransitions = 0;
    std::uint32_t noteEventsByInstrument[kMusicInstrumentCount]{};
    std::uint64_t firstNoteSamples[kMusicInstrumentCount]{};
    std::uint16_t absolutePeak = 0;
    std::uint16_t recentPeak = 0;
    float recentInstrumentPeaks[kMusicInstrumentCount]{};
    std::uint8_t maxActiveVoices = 0;
    float minimumKeysGain = 1.0f;

    bool automaticTransitionDue() const noexcept {
        return barIndex + 1 >= sessionBars;
    }

    bool pendingChangeDue() const noexcept {
        return pendingChange && barIndex >= pendingApplyBar;
    }

    bool pendingTempoOnly() const noexcept {
        return pendingChange && !pendingRestartRequested &&
               pendingConfig.bpm != current.bpm &&
               sameExceptBpm(pendingConfig, current);
    }

    bool pendingRestartDue() const noexcept {
        return pendingChangeDue() && !pendingTempoOnly();
    }

    std::uint64_t barStartSample() const noexcept {
        return barStartQ32 >> 32;
    }

    std::uint64_t barEndSample() const noexcept {
        return barEndQ32 >> 32;
    }

    std::uint64_t stepSample(std::uint8_t step) const noexcept {
        std::uint64_t valueQ32 = barStartQ32 + stepQ32 * step;
        if (meter != MusicMeter::SixEight && (step & 3u) == 2u) {
            const std::uint64_t delayQ32 = stepQ32 *
                static_cast<std::uint64_t>((swingPercent - 50u) * 4u) / 100u;
            valueQ32 += delayQ32;
        }
        return valueQ32 >> 32;
    }

    std::uint32_t durationSamples(std::uint8_t steps) const noexcept {
        return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(
            (stepQ32 * static_cast<std::uint64_t>(steps)) >> 32));
    }

    void updateEffectiveTempo() noexcept {
        bpm = current.bpm == 0 ? autoBpm : current.bpm;
        stepQ32 = (static_cast<std::uint64_t>(kMusicSampleRate) * 60u << 32) /
                  (static_cast<std::uint64_t>(bpm) * stepsPerBeat);
    }

    void setMeter(MusicMeter selected) noexcept {
        meter = selected;
        const MeterSpec spec = meterSpec(selected);
        meterNumerator = spec.numerator;
        meterDenominator = spec.denominator;
        beatsPerBar = spec.beatsPerBar;
        stepsPerBar = spec.stepsPerBar;
        stepsPerBeat = spec.stepsPerBeat;
    }

    float harmonicRelease(float automaticCoefficient) const noexcept {
        if (current.bpm == 0 || bpm <= autoBpm) {
            return automaticCoefficient;
        }
        // Envelope coefficients are per sample. At a faster manual tempo,
        // shorten harmonic tails by the same tempo ratio so they retain their
        // musical length and do not crowd percussion out of the fixed pool.
        return std::pow(automaticCoefficient,
                        static_cast<float>(bpm) / static_cast<float>(autoBpm));
    }

    void clearAudioState() noexcept {
        for (Voice& voice : voices) {
            voice = Voice{};
        }
        std::fill(std::begin(delay), std::end(delay), 0);
        delayIndex = 0;
        toneState = 0.0f;
        dcInput = 0.0f;
        dcOutput = 0.0f;
        kickDuck = 1.0f;
        keysGain = 1.0f;
    }

    void chooseProgression() noexcept {
        minorSession = current.mood != Mood::Cozy || scoreRng.chance(1, 4);
        if (!minorSession) {
            static constexpr std::uint8_t degrees[][4] = {
                {0, 9, 2, 7},
                {0, 4, 5, 7},
                {0, 5, 9, 7},
            };
            static constexpr ChordShape shapes[][4] = {
                {kMajor7, kMinor7, kMinor7, kDominant7},
                {kMajor7, kMinor7, kMajor7, kDominant7},
                {kMajor7, kMajor7, kMinor7, kDominant7},
            };
            const std::size_t selection = scoreRng.bounded(3);
            std::copy(std::begin(degrees[selection]), std::end(degrees[selection]),
                      progressionDegrees);
            std::copy(std::begin(shapes[selection]), std::end(shapes[selection]),
                      progressionShapes);
        } else {
            static constexpr std::uint8_t degrees[][4] = {
                {0, 8, 3, 10},
                {0, 5, 8, 7},
                {0, 3, 5, 7},
            };
            static constexpr ChordShape shapes[][4] = {
                {kMinor7, kMajor7, kMajor7, kDominant7},
                {kMinor7, kMinor7, kMajor7, kMinor7},
                {kMinor7, kMajor7, kMinor7, kMinor7},
            };
            const std::size_t selection = scoreRng.bounded(3);
            std::copy(std::begin(degrees[selection]), std::end(degrees[selection]),
                      progressionDegrees);
            std::copy(std::begin(shapes[selection]), std::end(shapes[selection]),
                      progressionShapes);
        }
    }

    ChordShape shapeForDegree(std::uint8_t degree) const noexcept {
        if (minorSession) {
            if (degree == 3 || degree == 8) {
                return kMajor7;
            }
            if (degree == 10) {
                return kDominant7;
            }
            return kMinor7;
        }
        if (degree == 0 || degree == 5) {
            return kMajor7;
        }
        if (degree == 7) {
            return kDominant7;
        }
        return kMinor7;
    }

    Harmony harmonyForBar(std::uint32_t bar) const noexcept {
        const std::size_t slot = bar & 3u;
        const std::uint32_t arc = (bar / 8u) & 3u;
        if (arc == 0) {
            return Harmony{progressionDegrees[slot], progressionShapes[slot]};
        }

        static constexpr std::uint8_t majorVariants[][4] = {
            {0, 9, 5, 7}, // I - vi - IV - V
            {0, 4, 9, 7}, // I - iii - vi - V
            {0, 5, 2, 7}, // I - IV - ii - V
        };
        static constexpr std::uint8_t minorVariants[][4] = {
            {0, 3, 8, 10}, // i - III - VI - VII
            {0, 5, 3, 10}, // i - iv - III - VII
            {0, 8, 5, 10}, // i - VI - iv - VII
        };
        const std::size_t seedOffset = static_cast<std::size_t>(
            mix64(current.seed ^ UINT64_C(0x4841524d4f4e595f)) % 3u);
        const std::size_t variant = (seedOffset + arc - 1u) % 3u;
        const std::uint8_t degree = minorSession ? minorVariants[variant][slot] :
                                                   majorVariants[variant][slot];
        return Harmony{degree, shapeForDegree(degree)};
    }

    bool scaleContains(int midi) const noexcept {
        static constexpr std::uint8_t majorScale[] = {0, 2, 4, 5, 7, 9, 11};
        static constexpr std::uint8_t minorScale[] = {0, 2, 3, 5, 7, 8, 10};
        const int relative = (midi - static_cast<int>(keyPitchClass) + 120) % 12;
        const std::uint8_t* scale = minorSession ? minorScale : majorScale;
        return std::find(scale, scale + 7, relative) != scale + 7;
    }

    bool chordContains(int midi) const noexcept {
        const int pitchClass = midi % 12;
        for (const std::uint8_t note : chordNotes) {
            if (note % 12 == pitchClass) {
                return true;
            }
        }
        return false;
    }

    static std::uint8_t nearestPitch(std::uint8_t pitchClass, int reference,
                                     int minimum, int maximum) noexcept {
        int best = minimum;
        int bestDistance = std::numeric_limits<int>::max();
        for (int note = minimum; note <= maximum; ++note) {
            if (note % 12 != pitchClass) {
                continue;
            }
            const int distance = std::abs(note - reference);
            if (distance < bestDistance) {
                best = note;
                bestDistance = distance;
            }
        }
        return static_cast<std::uint8_t>(best);
    }

    std::uint8_t bassPitchForDegree(std::uint8_t degree, int reference) const noexcept {
        const std::uint8_t pitchClass = static_cast<std::uint8_t>(
            (keyPitchClass + degree) % 12u);
        return nearestPitch(pitchClass, reference, kBassLow, kBassHigh);
    }

    std::uint8_t nearestLeadChordPitch(std::uint8_t voice,
                                       int reference) const noexcept {
        const std::uint8_t pitchClass = chordNotes[voice & 3u] % 12u;
        std::uint8_t note = nearestPitch(pitchClass, reference, 64, 83);
        if (std::abs(static_cast<int>(note) - reference) > 5) {
            int bestDistance = std::numeric_limits<int>::max();
            for (const std::uint8_t chordNote : chordNotes) {
                const std::uint8_t alternative = nearestPitch(
                    chordNote % 12u, reference, 64, 83);
                const int distance = std::abs(static_cast<int>(alternative) - reference);
                if (distance < bestDistance) {
                    note = alternative;
                    bestDistance = distance;
                }
            }
        }
        return note;
    }

    std::uint8_t leadPitchForChordVoice(std::uint8_t voice) noexcept {
        const std::uint8_t note = nearestLeadChordPitch(voice, lastLeadNote);
        lastLeadNote = note;
        return note;
    }

    std::uint8_t passingPitchTo(std::uint8_t target, int preferredDirection,
                                int minimum, int maximum) const noexcept {
        const int directions[2] = {preferredDirection, -preferredDirection};
        std::uint8_t best = target;
        int bestMovement = std::numeric_limits<int>::max();
        for (const int direction : directions) {
            for (int distance = 1; distance <= 2; ++distance) {
                const int candidate = static_cast<int>(target) + direction * distance;
                if (candidate >= minimum && candidate <= maximum &&
                    scaleContains(candidate) && !chordContains(candidate)) {
                    const int movement = std::abs(candidate - static_cast<int>(lastLeadNote));
                    if (movement < bestMovement) {
                        best = static_cast<std::uint8_t>(candidate);
                        bestMovement = movement;
                    }
                    break;
                }
            }
        }
        return best;
    }

    std::uint8_t scaleBridgeToward(std::uint8_t target, int reference,
                                   int minimum, int maximum) const noexcept {
        int best = target;
        int bestScore = std::numeric_limits<int>::max();
        for (int candidate = minimum; candidate <= maximum; ++candidate) {
            if (!scaleContains(candidate) || candidate == target) {
                continue;
            }
            const int movement = std::abs(candidate - reference);
            const int resolution = std::abs(static_cast<int>(target) - candidate);
            const int score = std::max(movement, resolution) * 32 +
                              movement + resolution;
            if (score < bestScore) {
                best = candidate;
                bestScore = score;
            }
        }
        return static_cast<std::uint8_t>(best);
    }

    void buildChord(Harmony harmony) noexcept {
        const int root = 60 + (keyPitchClass + harmony.degree) % 12u;
        const int raw[4] = {root, root + harmony.shape.third, root + 7,
                            root + harmony.shape.seventh};
        int bestScore = std::numeric_limits<int>::max();
        std::uint8_t best[4]{};
        for (int inversion = 0; inversion < 4; ++inversion) {
            int candidate[4]{};
            for (int voice = 0; voice < 4; ++voice) {
                const int source = (voice + inversion) & 3;
                candidate[voice] = raw[source] + ((voice + inversion) >= 4 ? 12 : 0);
            }
            for (int octave = -12; octave <= 0; octave += 12) {
                int score = 0;
                for (int voice = 0; voice < 4; ++voice) {
                    const int pitch = candidate[voice] + octave;
                    score += std::abs(pitch - static_cast<int>(chordNotes[voice]));
                    if (pitch < 52 || pitch > 76) {
                        score += 30;
                    }
                }
                if (score < bestScore) {
                    bestScore = score;
                    for (int voice = 0; voice < 4; ++voice) {
                        best[voice] = static_cast<std::uint8_t>(candidate[voice] + octave);
                    }
                }
            }
        }
        std::copy(std::begin(best), std::end(best), chordNotes);
        currentBassRoot = bassPitchForDegree(harmony.degree, lastBassNote);
    }

    void buildMotif() noexcept {
        static constexpr std::int8_t templates[][8] = {
            {0, -1, 1, 2, -1, 3, 2, -1},
            {2, -1, 1, -1, 0, 1, -1, 3},
            {-1, 0, 2, -1, 1, -1, 3, 2},
            {0, 1, -1, 2, -1, 1, 0, -1},
        };
        const std::size_t selected = scoreRng.bounded(4);
        std::copy(std::begin(templates[selected]), std::end(templates[selected]), motif);
    }

    void startSession(Config config, bool resetCounters) noexcept {
        current = sanitized(config);
        scoreRng.seed(mix64(current.seed ^
                            (static_cast<std::uint64_t>(current.mood) << 56) ^
                            kMusicSchemaVersion));
        timbreRng.seed(mix64(current.seed ^ UINT64_C(0x54494d4252455f31) ^
                             static_cast<std::uint64_t>(current.soundEngine)));
        textureRng.seed(mix64(current.seed ^ UINT64_C(0x544558545552455f)));

        switch (current.mood) {
        case Mood::Cozy:
            autoBpm = static_cast<std::uint16_t>(76 + scoreRng.bounded(9));
            break;
        case Mood::Rainy:
            autoBpm = static_cast<std::uint16_t>(68 + scoreRng.bounded(9));
            break;
        case Mood::Night:
            autoBpm = static_cast<std::uint16_t>(72 + scoreRng.bounded(9));
            break;
        }
        const std::uint32_t meterDraw = scoreRng.bounded(10);
        const MusicMeter automaticMeter = meterDraw < 7u ? MusicMeter::FourFour :
            meterDraw < 9u ? MusicMeter::ThreeFour : MusicMeter::SixEight;
        setMeter(current.meter == MusicMeter::Auto ? automaticMeter : current.meter);
        swingPercent = static_cast<std::uint8_t>(55 + scoreRng.bounded(5));
        static constexpr std::uint8_t cozyKeys[] = {0, 2, 5, 7, 9};
        static constexpr std::uint8_t darkKeys[] = {0, 2, 3, 5, 7, 9, 10};
        if (current.mood == Mood::Cozy) {
            keyPitchClass = cozyKeys[scoreRng.bounded(5)];
        } else {
            keyPitchClass = darkKeys[scoreRng.bounded(7)];
        }
        sessionBars = 64 + scoreRng.bounded(7) * 8;
        chooseProgression();
        buildMotif();
        const std::uint8_t voicingReference[4] = {57, 60, 64, 67};
        std::copy(std::begin(voicingReference), std::end(voicingReference), chordNotes);
        currentBassRoot = nearestPitch(keyPitchClass, 42, kBassLow, kBassHigh);
        lastBassNote = currentBassRoot;
        lastLeadNote = nearestPitch(keyPitchClass, 72, 64, 83);

        updateEffectiveTempo();
        sessionSample = 0;
        barIndex = 0;
        barStartQ32 = 0;
        barEndQ32 = stepQ32 * stepsPerBar;
        eventCount = 0;
        nextEvent = 0;
        pendingChange = false;
        pendingRestartRequested = false;
        pendingApplyBar = 0;
        transitionGain = resetCounters ? 1.0f : 0.0f;
        transitionFadeIn = resetCounters ? 0 : kFadeFrames;
        voiceSerial = 0;
        clearAudioState();
        std::fill(std::begin(recentInstrumentPeaks),
                  std::end(recentInstrumentPeaks), 0.0f);
        generateBar();

        if (resetCounters) {
            renderedFrames = 0;
            transportSample = 0;
            scoreHash = kFnvOffset;
            scoreEvents = 0;
            stolenVoices = 0;
            droppedNoteEvents = 0;
            sessionTransitions = 0;
            std::fill(std::begin(noteEventsByInstrument),
                      std::end(noteEventsByInstrument), 0u);
            std::fill(std::begin(firstNoteSamples), std::end(firstNoteSamples),
                      kMusicNoNoteSample);
            absolutePeak = 0;
            recentPeak = 0;
            maxActiveVoices = 0;
            minimumKeysGain = 1.0f;
            pauseGain = 1.0f;
            pauseTarget = false;
            pauseSettled = false;
        }
    }

    void insertEvent(Event event) noexcept {
        if (eventCount >= kMaxBarEvents) {
            ++droppedNoteEvents;
            return;
        }
        std::size_t position = eventCount;
        while (position > 0 && events[position - 1].start > event.start) {
            events[position] = events[position - 1];
            --position;
        }
        events[position] = event;
        ++eventCount;
    }

    void addEvent(std::uint8_t step, int timingOffset, std::uint8_t durationSteps,
                  Instrument instrument, int note, int velocity) noexcept {
        if (step >= stepsPerBar) {
            return;
        }
        durationSteps = std::max<std::uint8_t>(
            1, std::min<std::uint8_t>(durationSteps,
                                      static_cast<std::uint8_t>(stepsPerBar - step)));
        const std::uint64_t start = stepSample(step);
        const std::uint64_t minimum = barStartSample();
        const std::uint64_t maximum = barEndSample() > minimum ? barEndSample() - 1 : minimum;
        std::uint64_t adjusted = start;
        if (timingOffset < 0) {
            const std::uint64_t amount = static_cast<std::uint64_t>(-timingOffset);
            adjusted = adjusted > minimum + amount ? adjusted - amount : minimum;
        } else {
            adjusted = std::min<std::uint64_t>(maximum,
                                               adjusted + static_cast<std::uint64_t>(timingOffset));
        }
        Event event{};
        event.start = adjusted;
        const std::uint64_t available = std::max<std::uint64_t>(1, barEndSample() - adjusted);
        event.duration = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            durationSamples(durationSteps), available));
        event.instrument = instrument;
        event.note = static_cast<std::uint8_t>(std::max(0, std::min(127, note)));
        event.velocity = static_cast<std::uint8_t>(std::max(1, std::min(127, velocity)));
        insertEvent(event);
    }

    void addHumanizedEvent(std::uint8_t step, int timingBias, int timingRadius,
                           std::uint8_t durationSteps, Instrument instrument,
                           int note, int velocity, int velocityRadius) noexcept {
        // Keep these draws in schema order. Function argument evaluation order is
        // unspecified, so drawing inside addEvent(...) diverged between compilers.
        const int timingOffset = timingBias + scoreRng.centered(timingRadius);
        const int adjustedVelocity = velocity + scoreRng.centered(velocityRadius);
        addEvent(step, timingOffset, durationSteps, instrument, note,
                 adjustedVelocity);
    }

    void generateBar() noexcept {
        eventCount = 0;
        nextEvent = 0;
        const ArrangementSection section = sectionForBar(barIndex, sessionBars);
        const Harmony harmony = harmonyForBar(barIndex);
        buildChord(harmony);
        const int timingRadius = current.mood == Mood::Rainy ? 34 : 48;
        const std::uint8_t phraseBar = static_cast<std::uint8_t>(barIndex & 7u);
        const std::uint8_t pattern = static_cast<std::uint8_t>(
            (barIndex + (barIndex / 8u) + (current.seed >> 9)) & 3u);

        const int chordVelocity = section == ArrangementSection::Breakdown ? 58 :
                                  section == ArrangementSection::Intro ?
                                      62 + static_cast<int>(barIndex) * 2 : 72;
        for (int voice = 0; voice < 4; ++voice) {
            const int source = pattern == 1u ? 3 - voice : voice;
            std::uint8_t onset = 0;
            if (pattern == 2u && source >= 2) {
                onset = static_cast<std::uint8_t>(stepsPerBeat);
            } else if (pattern == 3u && source == 3) {
                onset = meter == MusicMeter::FourFour ? 12u :
                        meter == MusicMeter::ThreeFour ? 8u : 6u;
            }
            const int strum = voice * static_cast<int>(18 + scoreRng.bounded(19));
            const std::uint8_t tailSpace =
                section == ArrangementSection::Intro ? 3u : 2u;
            const std::uint8_t remaining = static_cast<std::uint8_t>(stepsPerBar - onset);
            const std::uint8_t gate = remaining > tailSpace ?
                static_cast<std::uint8_t>(remaining - tailSpace) : 1u;
            addEvent(onset, strum, gate, Instrument::Keys, chordNotes[source],
                     chordVelocity + scoreRng.centered(5));
        }

        std::uint8_t secondBassStep = 0;
        if (meter == MusicMeter::FourFour) {
            static constexpr std::uint8_t steps[] = {8, 10, 8, 6};
            secondBassStep = steps[pattern];
        } else if (meter == MusicMeter::ThreeFour) {
            static constexpr std::uint8_t steps[] = {6, 8, 6, 4};
            secondBassStep = steps[pattern];
        } else {
            static constexpr std::uint8_t steps[] = {6, 8, 6, 4};
            secondBassStep = steps[pattern];
        }
        const std::uint8_t approachStep = static_cast<std::uint8_t>(stepsPerBar - 2u);
        const int bassRoot = currentBassRoot;
        const std::uint8_t rootGate = std::max<std::uint8_t>(
            1, std::min<std::uint8_t>(static_cast<std::uint8_t>(secondBassStep - 1u),
                                      stepsPerBeat + 2u));
        addHumanizedEvent(0, 52, timingRadius, rootGate, Instrument::Bass,
                          bassRoot,
                          section == ArrangementSection::Intro ? 68 : 74, 5);
        lastBassNote = currentBassRoot;

        const bool flowingBass = section == ArrangementSection::Intro ||
            section == ArrangementSection::Groove ||
            section == ArrangementSection::Melody ||
            section == ArrangementSection::Return;
        const Harmony nextHarmony = harmonyForBar(barIndex + 1u);
        if (flowingBass) {
            const std::uint8_t chordInterval = pattern == 0u ? 0u :
                pattern == 2u ? harmony.shape.third : 7u;
            int second = bassPitchForDegree(
                static_cast<std::uint8_t>(harmony.degree + chordInterval), bassRoot);
            if (std::abs(second - bassRoot) > 7) {
                second = bassPitchForDegree(
                    static_cast<std::uint8_t>(harmony.degree + 7u), bassRoot);
            }
            if (std::abs(second - bassRoot) > 7) {
                second = bassRoot;
            }
            const std::uint8_t nextRootFromSecond = bassPitchForDegree(
                nextHarmony.degree, second);
            const bool needsBridge =
                std::abs(static_cast<int>(nextRootFromSecond) - second) > 7;
            if (needsBridge) {
                second = bassRoot;
            }
            const bool approach = (barIndex & 3u) == 3u ||
                (section != ArrangementSection::Intro && pattern == 2u) ||
                needsBridge;
            const std::uint8_t secondGap = approach ?
                static_cast<std::uint8_t>(approachStep - secondBassStep) :
                static_cast<std::uint8_t>(stepsPerBar - secondBassStep);
            const std::uint8_t secondGate = std::max<std::uint8_t>(
                1, static_cast<std::uint8_t>(secondGap - 1u));
            addHumanizedEvent(secondBassStep, 34, timingRadius, secondGate,
                              Instrument::Bass, second,
                              section == ArrangementSection::Intro ? 57 : 65, 5);
            lastBassNote = static_cast<std::uint8_t>(second);
            if (approach) {
                const std::uint8_t target = bassPitchForDegree(nextHarmony.degree,
                                                                lastBassNote);
                const std::uint8_t approachNote = scaleBridgeToward(
                    target, lastBassNote, kBassLow, kBassHigh);
                addHumanizedEvent(approachStep, 0, 24, 2, Instrument::Bass,
                                  approachNote, 48, 4);
                lastBassNote = approachNote;
            }
        } else {
            const std::uint8_t target = bassPitchForDegree(nextHarmony.degree,
                                                            lastBassNote);
            if (std::abs(static_cast<int>(target) - bassRoot) > 7) {
                const std::uint8_t approachNote = scaleBridgeToward(
                    target, bassRoot, kBassLow, kBassHigh);
                addHumanizedEvent(approachStep, 0, 22, 2, Instrument::Bass,
                                  approachNote, 43, 3);
                lastBassNote = approachNote;
            }
        }

        const bool fullDrums = section == ArrangementSection::Groove ||
                               section == ArrangementSection::Melody ||
                               section == ArrangementSection::Return;
        if (meter == MusicMeter::FourFour) {
            if (section == ArrangementSection::Intro || fullDrums) {
                const int kick = section == ArrangementSection::Intro ? 69 : 89;
                const int snare = section == ArrangementSection::Intro ? 50 : 75;
                addHumanizedEvent(0, 0, 16, 3, Instrument::Kick, 36, kick, 5);
                static constexpr std::uint8_t secondKicks[] = {8, 10, 8, 7};
                addHumanizedEvent(secondKicks[pattern], 0, 22, 2,
                                  Instrument::Kick, 36, kick - 12, 5);
                addHumanizedEvent(4, 0, 24, 2, Instrument::Snare, 38, snare, 6);
                addHumanizedEvent(12, 0, 24, 2, Instrument::Snare, 38,
                                  snare + 3, 6);
                for (std::uint8_t step = 2; step < 16; step += 2) {
                    const bool rest = fullDrums && pattern == 1u && step == 6u;
                    if (!rest) {
                        addHumanizedEvent(step, 0, 18, 1, Instrument::Hat, 42,
                                          (step & 3u) == 0u ? 37 : 44, 4);
                    }
                }
                if ((barIndex & 1u) != 0u) {
                    addHumanizedEvent(15, 0, 10, 1, Instrument::Rim, 37, 38, 4);
                }
            } else if (section == ArrangementSection::Breakdown) {
                addEvent(4, scoreRng.centered(18), 2, Instrument::Rim, 37, 42);
                addEvent(12, scoreRng.centered(18), 2, Instrument::Rim, 37, 45);
                for (std::uint8_t step = 2; step < 16; step += 4) {
                    addHumanizedEvent(step, 0, 16, 1, Instrument::Hat, 42, 31, 3);
                }
            } else {
                addEvent(4, scoreRng.centered(18), 2, Instrument::Rim, 37, 37);
                addEvent(12, scoreRng.centered(18), 2, Instrument::Rim, 37, 34);
            }
        } else if (meter == MusicMeter::ThreeFour) {
            if (section == ArrangementSection::Intro || fullDrums) {
                const int kick = section == ArrangementSection::Intro ? 66 : 86;
                const int snare = section == ArrangementSection::Intro ? 48 : 70;
                addHumanizedEvent(0, 0, 16, 3, Instrument::Kick, 36, kick, 4);
                if (pattern != 0u || fullDrums) {
                    addHumanizedEvent(pattern == 3u ? 6u : 8u, 0, 18, 2,
                                      Instrument::Kick, 36, kick - 18, 4);
                }
                addHumanizedEvent(4, 0, 20, 2, Instrument::Snare, 38, snare, 5);
                addHumanizedEvent(8, 0, 20, 2, Instrument::Snare, 38,
                                  snare - 3, 5);
                for (std::uint8_t step = 2; step < 12; step += 2) {
                    addHumanizedEvent(step, 0, 16, 1, Instrument::Hat, 42,
                                      step == 2u ? 35 : 42, 4);
                }
                if ((barIndex & 1u) != 0u) {
                    addHumanizedEvent(11, 0, 9, 1, Instrument::Rim, 37, 36, 3);
                }
            } else if (section == ArrangementSection::Breakdown) {
                addEvent(4, scoreRng.centered(18), 2, Instrument::Rim, 37, 41);
                addEvent(8, scoreRng.centered(18), 2, Instrument::Rim, 37, 44);
                for (std::uint8_t step = 2; step < 12; step += 4) {
                    addEvent(step, scoreRng.centered(14), 1, Instrument::Hat, 42, 31);
                }
            } else {
                addEvent(4, scoreRng.centered(16), 2, Instrument::Rim, 37, 36);
                addEvent(8, scoreRng.centered(16), 2, Instrument::Rim, 37, 33);
            }
        } else {
            if (section == ArrangementSection::Intro || fullDrums) {
                const int kick = section == ArrangementSection::Intro ? 67 : 88;
                const int snare = section == ArrangementSection::Intro ? 50 : 73;
                addHumanizedEvent(0, 0, 14, 3, Instrument::Kick, 36, kick, 4);
                if (pattern != 0u || fullDrums) {
                    addHumanizedEvent(pattern == 3u ? 9u : 8u, 0, 16, 2,
                                      Instrument::Kick, 36, kick - 15, 4);
                }
                addHumanizedEvent(6, 0, 20, 2, Instrument::Snare, 38, snare, 5);
                for (std::uint8_t step = 2; step < 12; step += 2) {
                    addHumanizedEvent(step, 0, 14, 1, Instrument::Hat, 42,
                                      step == 2u || step == 8u ? 36 : 43, 4);
                }
                if ((barIndex & 1u) != 0u) {
                    addHumanizedEvent(11, 0, 8, 1, Instrument::Rim, 37, 38, 3);
                }
            } else if (section == ArrangementSection::Breakdown) {
                addEvent(6, scoreRng.centered(16), 2, Instrument::Rim, 37, 43);
                for (std::uint8_t step : {2u, 4u, 8u, 10u}) {
                    addEvent(step, scoreRng.centered(12), 1, Instrument::Hat, 42, 31);
                }
            } else {
                addEvent(6, scoreRng.centered(16), 2, Instrument::Rim, 37, 36);
            }
        }

        const auto addChordLead = [this](std::uint8_t step, std::uint8_t voice,
                                         std::uint8_t duration,
                                         int velocity) noexcept {
            const std::uint8_t note = leadPitchForChordVoice(voice);
            addEvent(step, 0, duration, Instrument::Lead, note,
                     velocity + scoreRng.centered(5));
        };
        const auto addHook = [this, &addChordLead](std::uint8_t maximumNotes,
                                                   int velocity,
                                                   std::uint8_t delay = 0) noexcept {
            static constexpr std::uint8_t fourFourSteps[] = {0, 2, 4, 6, 8, 10, 12, 14};
            static constexpr std::uint8_t shortSteps[] = {0, 2, 4, 6, 8, 10};
            const std::uint8_t* hookSteps = meter == MusicMeter::FourFour ?
                fourFourSteps : shortSteps;
            const std::uint8_t slots = meter == MusicMeter::FourFour ? 8u : 6u;
            std::uint8_t added = 0;
            for (std::uint8_t slot = 0; slot < slots && added < maximumNotes; ++slot) {
                if (motif[slot] < 0) {
                    continue;
                }
                const std::uint8_t step = static_cast<std::uint8_t>(
                    hookSteps[slot] + delay);
                if (step >= stepsPerBar) {
                    continue;
                }
                const std::uint8_t duration = step % stepsPerBeat == 0u ? 2u : 1u;
                addChordLead(step, static_cast<std::uint8_t>(motif[slot]), duration,
                             velocity + (added == 0 ? 3 : 0));
                ++added;
            }
        };
        const auto addCadence = [this, &harmony](std::uint8_t step, bool useThird,
                                                 std::uint8_t duration,
                                                 int velocity) noexcept {
            const std::uint8_t rootClass = currentBassRoot % 12u;
            const std::uint8_t thirdClass = static_cast<std::uint8_t>(
                (rootClass + harmony.shape.third) % 12u);
            const std::uint8_t root = nearestPitch(rootClass, lastLeadNote, 64, 83);
            const std::uint8_t third = nearestPitch(thirdClass, lastLeadNote, 64, 83);
            std::uint8_t note = useThird ? third : root;
            const std::uint8_t alternative = useThird ? root : third;
            if (std::abs(static_cast<int>(note) - lastLeadNote) > 5 &&
                std::abs(static_cast<int>(alternative) - lastLeadNote) <
                    std::abs(static_cast<int>(note) - lastLeadNote)) {
                note = alternative;
            }
            addEvent(step, 0, duration, Instrument::Lead, note,
                     velocity + scoreRng.centered(4));
            lastLeadNote = note;
        };
        const auto addResponse = [this, &addChordLead, &addCadence](
                                     int velocity,
                                     std::uint8_t maximumNotes) noexcept {
            const std::uint8_t responseSteps[3] = {
                static_cast<std::uint8_t>(stepsPerBeat + 1u),
                static_cast<std::uint8_t>(stepsPerBar - 3u),
                static_cast<std::uint8_t>(stepsPerBar - 1u),
            };
            std::uint8_t added = 0;
            for (int slot = 7; slot >= 0 && added < maximumNotes; --slot) {
                if (motif[slot] < 0) {
                    continue;
                }
                if (added == 2u) {
                    addCadence(responseSteps[added], (barIndex & 2u) != 0u,
                               1, velocity - 4);
                } else {
                    addChordLead(responseSteps[added],
                                 static_cast<std::uint8_t>(motif[slot]), 2,
                                 velocity - added * 2);
                }
                ++added;
            }
        };
        const auto addPassingResolution = [this](std::uint8_t passingStep,
                                                  std::uint8_t chordVoice,
                                                  int velocity) noexcept {
            std::uint8_t target = nearestLeadChordPitch(chordVoice, lastLeadNote);
            const int direction = lastLeadNote < target ? -1 :
                                  lastLeadNote > target ? 1 :
                                  ((barIndex + chordVoice + current.seed) & 1u) != 0u ?
                                      1 : -1;
            std::uint8_t passing = passingPitchTo(target, direction, 64, 83);
            if (passing == target) {
                for (std::uint8_t alternativeVoice = 0; alternativeVoice < 4;
                     ++alternativeVoice) {
                    const std::uint8_t alternative = nearestLeadChordPitch(
                        alternativeVoice, lastLeadNote);
                    const std::uint8_t alternativePassing = passingPitchTo(
                        alternative, direction, 64, 83);
                    if (alternativePassing != alternative) {
                        target = alternative;
                        passing = alternativePassing;
                        break;
                    }
                }
            }
            const std::uint8_t pitchClass = target % 12u;
            addEvent(passingStep, 0, 1, Instrument::Lead, passing,
                     velocity - 5 + scoreRng.centered(3));
            lastLeadNote = passing;
            target = nearestPitch(pitchClass, lastLeadNote, 64, 83);
            addEvent(static_cast<std::uint8_t>(passingStep + 1u), 0, 2,
                     Instrument::Lead, target, velocity + scoreRng.centered(4));
            lastLeadNote = target;
        };

        if (section == ArrangementSection::Outro) {
            if ((barIndex & 1u) == 0u) {
                addCadence(static_cast<std::uint8_t>(stepsPerBeat), false,
                           stepsPerBeat, 35);
            }
        } else if (section == ArrangementSection::Breakdown) {
            if (phraseBar == 0u || phraseBar == 4u) {
                addChordLead(static_cast<std::uint8_t>(stepsPerBeat),
                             static_cast<std::uint8_t>(phraseBar / 4u),
                             stepsPerBeat, 37);
            } else if (phraseBar == 3u || phraseBar == 7u) {
                addCadence(static_cast<std::uint8_t>(stepsPerBar - stepsPerBeat),
                           false, static_cast<std::uint8_t>(stepsPerBeat - 1u), 34);
            }
        } else {
            switch (phraseBar) {
            case 0:
                addHook(8, section == ArrangementSection::Intro ? 40 : 45);
                break;
            case 1:
                addResponse(section == ArrangementSection::Intro ? 40 : 44, 3);
                break;
            case 2:
                addPassingResolution(meter == MusicMeter::SixEight ? 3u : 5u,
                                     1, 42);
                break;
            case 3:
                addCadence(static_cast<std::uint8_t>(stepsPerBar / 2u),
                           (barIndex & 8u) != 0u, stepsPerBeat, 43);
                break;
            case 4:
                // The motif returns as a delayed echo after one clear beat.
                // Its full, on-the-downbeat form comes back at the next
                // eight-bar boundary.
                addHook(3, 41, stepsPerBeat);
                break;
            case 5:
                addResponse(42, 2);
                break;
            case 6:
                addPassingResolution(meter == MusicMeter::SixEight ? 8u : 9u,
                                     2, 40);
                break;
            case 7:
                addChordLead(static_cast<std::uint8_t>(stepsPerBar - 6u), 2, 2, 40);
                addCadence(static_cast<std::uint8_t>(stepsPerBar - 2u), false, 2, 37);
                break;
            }
        }
    }

    void consumeCommands() noexcept {
        if (commandLock.test_and_set(std::memory_order_acquire)) {
            return;
        }
        const bool hasConfig = commandConfigPending;
        const Config requestedConfig = commandConfig;
        const std::uint64_t configSequence = commandConfigSequence;
        const bool hasNext = commandNextPending;
        const bool nextExplicit = commandNextExplicit;
        const std::uint64_t requestedSeed = commandSeed;
        const std::uint64_t nextSequence = commandNextSequence;
        const bool hasPause = pauseCommandPending;
        const bool requestedPause = pauseCommand;
        commandConfigPending = false;
        commandNextPending = false;
        pauseCommandPending = false;
        commandLock.clear(std::memory_order_release);

        if (hasPause) {
            pauseTarget = requestedPause;
            if (!pauseTarget) {
                pauseSettled = false;
            }
        }

        const bool hadPendingChange = pendingChange;
        const bool wasTempoOnly = pendingTempoOnly();
        if (!hadPendingChange) {
            pendingRestartRequested = false;
        }
        if (hasConfig) {
            pendingConfig = requestedConfig;
            pendingChange = true;
        }
        if (hasNext) {
            if (!pendingChange) {
                pendingConfig = current;
            }
            pendingChange = true;
            pendingRestartRequested = true;
            // Configuration and next-session requests are independent. Keep
            // all config fields, then let the later request choose the seed.
            if (!hasConfig || nextSequence > configSequence) {
                pendingConfig.seed = nextExplicit ? requestedSeed :
                    mix64(pendingConfig.seed ^ UINT64_C(0x4e4558545f534553));
            }
        }
        if (pendingChange && !pendingRestartRequested &&
            pendingConfig.bpm == current.bpm &&
            sameExceptBpm(pendingConfig, current)) {
            // Coalescing a pending manual/AUTO selection back to the active
            // configuration cancels it. An independent Next request keeps
            // pendingRestartRequested set and therefore still restarts.
            pendingChange = false;
            pendingApplyBar = 0;
            return;
        }
        const bool isTempoOnly = pendingTempoOnly();
        if (pendingChange && (!hadPendingChange || wasTempoOnly != isTempoOnly)) {
            if (isTempoOnly) {
                // Tempo changes do not restart the score. They take effect at
                // the immediate next edge, using that edge as the new Q32
                // timing origin.
                pendingApplyBar = barIndex;
                return;
            }
            const std::uint64_t end = barEndSample();
            const std::uint64_t remaining = end > sessionSample ? end - sessionSample : 0;
            // Never jump into the middle of a transition envelope. A request
            // received too late waits one extra bar for a complete fade.
            pendingApplyBar = barIndex +
                ((remaining < kFadeFrames && !automaticTransitionDue()) ? 1u : 0u);
        }
    }

    Voice* allocateVoice(const Event& event) noexcept {
        for (Voice& voice : voices) {
            if (!voice.active) {
                return &voice;
            }
        }

        const auto released = [this](const Voice& voice) noexcept {
            return voice.stage == EnvelopeStage::Release ||
                   sessionSample >= voice.releaseAt;
        };
        const auto betterVictim = [&released](const Voice& candidate,
                                              const Voice& incumbent) noexcept {
            const bool candidateReleased = released(candidate);
            const bool incumbentReleased = released(incumbent);
            if (candidateReleased != incumbentReleased) {
                return candidateReleased;
            }
            return candidate.priority < incumbent.priority ||
                   (candidate.priority == incumbent.priority &&
                    (candidate.envelope < incumbent.envelope ||
                     (candidate.envelope == incumbent.envelope &&
                      candidate.serial < incumbent.serial)));
        };
        Voice* candidate = nullptr;
        if (isDrum(event.instrument)) {
            // Percussion may recycle percussion tails, but it never takes a
            // sounding harmonic voice.
            for (Voice& voice : voices) {
                if (isDrum(voice.instrument) &&
                    (candidate == nullptr || betterVictim(voice, *candidate))) {
                    candidate = &voice;
                }
            }
        } else {
            // Harmonic voices get first claim on a percussion slot. This keeps
            // a hat or kick from clipping a chord, bass line, or lead answer.
            for (Voice& voice : voices) {
                if (isDrum(voice.instrument) &&
                    (candidate == nullptr || betterVictim(voice, *candidate))) {
                    candidate = &voice;
                }
            }
            if (candidate == nullptr) {
                for (Voice& voice : voices) {
                    if (candidate == nullptr || betterVictim(voice, *candidate)) {
                        candidate = &voice;
                    }
                }
            }
        }
        if (candidate == nullptr) {
            return nullptr;
        }
        const std::uint8_t incomingPriority = instrumentPriority(event.instrument);
        const bool harmonicTakingDrum = !isDrum(event.instrument) &&
                                        isDrum(candidate->instrument);
        if (!harmonicTakingDrum && !released(*candidate) &&
            candidate->priority > incomingPriority &&
            candidate->envelope > 0.08f) {
            return nullptr;
        }
        candidate->stealTail = candidate->lastOutput;
        ++stolenVoices;
        return candidate;
    }

    const Sample* keySampleFor(std::uint8_t note) const noexcept {
        if (samples == nullptr || sampleCount < 3) {
            return nullptr;
        }
        const Sample* best = &samples[0];
        int distance = std::abs(static_cast<int>(note) - static_cast<int>(best->rootMidi));
        for (std::size_t i = 1; i < 3; ++i) {
            const int nextDistance = std::abs(static_cast<int>(note) -
                                              static_cast<int>(samples[i].rootMidi));
            if (nextDistance < distance) {
                distance = nextDistance;
                best = &samples[i];
            }
        }
        return best;
    }

    const Sample* drumSampleFor(Instrument instrument) const noexcept {
        std::size_t index = sampleCount;
        switch (instrument) {
        case Instrument::Kick:
            index = 3;
            break;
        case Instrument::Snare:
            index = 4;
            break;
        case Instrument::Hat:
            index = 5;
            break;
        case Instrument::Rim:
            index = 6;
            break;
        default:
            break;
        }
        return samples != nullptr && index < sampleCount ? &samples[index] : nullptr;
    }

    void startVoice(const Event& event) noexcept {
        Voice* voice = allocateVoice(event);
        if (voice == nullptr) {
            ++droppedNoteEvents;
            return;
        }
        const float oldTail = voice->stealTail;
        *voice = Voice{};
        voice->active = true;
        voice->instrument = event.instrument;
        voice->note = event.note;
        voice->priority = instrumentPriority(event.instrument);
        voice->releaseAt = sessionSample + event.duration;
        voice->serial = ++voiceSerial;
        voice->velocity = static_cast<float>(event.velocity) / 127.0f;
        voice->phase = timbreRng.next32();
        voice->phaseIncrement = midiPhaseIncrement(event.note);
        const int detune = timbreRng.centered(7);
        voice->phaseIncrement += static_cast<std::uint32_t>(
            static_cast<std::int64_t>(voice->phaseIncrement) * detune / 10000);
        voice->noiseState = timbreRng.next32() | 1u;
        voice->stealTail = oldTail;

        switch (event.instrument) {
        case Instrument::Keys:
        case Instrument::Lead: {
            const bool lead = event.instrument == Instrument::Lead;
            const Tone tone = lead ? current.leadTone : current.keysTone;
            const ToneEnvelope profile = toneEnvelope(tone, lead);
            voice->attackIncrement = profile.attackIncrement;
            voice->sustain = profile.sustain;
            voice->decay = tone == Tone::ElectricPiano && !lead && current.mood == Mood::Rainy
                ? 0.99988f : profile.decay;
            voice->release = harmonicRelease(profile.release);
            voice->gain = profile.gain;
            break;
        }
        case Instrument::Bass: {
            const ToneEnvelope profile = bassEnvelope(current.bassTone);
            voice->attackIncrement = profile.attackIncrement;
            voice->sustain = profile.sustain;
            voice->decay = profile.decay;
            voice->release = harmonicRelease(profile.release);
            voice->gain = profile.gain;
            break;
        }
        case Instrument::Kick:
            voice->attackIncrement = 1.0f;
            voice->sustain = 0.0f;
            voice->decay = 0.9989f;
            voice->release = 0.991f;
            voice->gain = 0.43f;
            voice->phaseIncrement = static_cast<std::uint32_t>(112.0f * kPhaseScale);
            kickDuck = std::min(kickDuck, 0.91f);
            break;
        case Instrument::Snare:
            voice->attackIncrement = 1.0f;
            voice->sustain = 0.0f;
            voice->decay = 0.9977f;
            voice->release = 0.989f;
            voice->gain = 0.22f;
            break;
        case Instrument::Hat:
            voice->attackIncrement = 1.0f;
            voice->sustain = 0.0f;
            voice->decay = 0.989f;
            voice->release = 0.965f;
            voice->gain = 0.075f;
            break;
        case Instrument::Rim:
            voice->attackIncrement = 1.0f;
            voice->sustain = 0.0f;
            voice->decay = 0.982f;
            voice->release = 0.94f;
            voice->gain = 0.115f;
            voice->phaseIncrement = static_cast<std::uint32_t>(1520.0f * kPhaseScale);
            break;
        }

        if (current.soundEngine == SoundEngine::Hybrid) {
            if (event.instrument == Instrument::Keys || event.instrument == Instrument::Lead) {
                // Keep the electric-piano source specific to that tone. The
                // other selected pitched profiles are procedural in both engines.
                const Tone tone = event.instrument == Instrument::Keys
                    ? current.keysTone : current.leadTone;
                voice->sample = tone == Tone::ElectricPiano ? keySampleFor(event.note) : nullptr;
                if (voice->sample != nullptr && voice->sample->data != nullptr &&
                    voice->sample->frames != 0) {
                    voice->sampleActive = true;
                    voice->sampleIncrementQ16 = samplePhaseIncrement(
                        *voice->sample,
                        static_cast<int>(event.note) - static_cast<int>(voice->sample->rootMidi));
                }
            } else {
                voice->sample = drumSampleFor(event.instrument);
                if (voice->sample != nullptr && voice->sample->data != nullptr &&
                    voice->sample->frames != 0) {
                    voice->sampleActive = true;
                    const float pitchVariation = 0.985f +
                        static_cast<float>(timbreRng.bounded(31)) * 0.001f;
                    voice->sampleIncrementQ16 = samplePhaseIncrement(
                        *voice->sample, 0, pitchVariation);
                }
            }
        }

        const std::size_t instrument = static_cast<std::size_t>(event.instrument);
        ++noteEventsByInstrument[instrument];
        if (firstNoteSamples[instrument] == kMusicNoNoteSample) {
            firstNoteSamples[instrument] = transportSample;
        }
    }

    void dispatchEvents() noexcept {
        while (nextEvent < eventCount && events[nextEvent].start <= sessionSample) {
            const Event& event = events[nextEvent];
            scoreHash = hashInteger(scoreHash, transportSample);
            scoreHash = hashInteger(scoreHash, static_cast<std::uint8_t>(event.instrument));
            scoreHash = hashInteger(scoreHash, event.note);
            scoreHash = hashInteger(scoreHash, event.velocity);
            scoreHash = hashInteger(scoreHash, event.duration);
            ++scoreEvents;
            startVoice(event);
            ++nextEvent;
        }
    }

    float renderVoice(Voice& voice) noexcept {
        if (!voice.active) {
            return 0.0f;
        }
        if (sessionSample >= voice.releaseAt && voice.stage != EnvelopeStage::Release) {
            voice.stage = EnvelopeStage::Release;
        }
        if (voice.stage == EnvelopeStage::Attack) {
            voice.envelope += voice.attackIncrement;
            if (voice.envelope >= 1.0f) {
                voice.envelope = 1.0f;
                voice.stage = EnvelopeStage::Decay;
            }
        } else if (voice.stage == EnvelopeStage::Decay) {
            voice.envelope = voice.sustain +
                             (voice.envelope - voice.sustain) * voice.decay;
        } else {
            voice.envelope *= voice.release;
        }

        float signal = 0.0f;
        const float fundamental = isDrum(voice.instrument) ? phaseSine(voice.phase) : 0.0f;
        switch (voice.instrument) {
        case Instrument::Keys:
        case Instrument::Lead: {
            const Tone tone = voice.instrument == Instrument::Keys
                ? current.keysTone : current.leadTone;
            const float tonal = renderTone(tone, voice.phase, voice.age);
            signal = tonal;
            if (current.soundEngine == SoundEngine::Hybrid && voice.sample != nullptr) {
                // The synthesized bed stays at its original blend level even
                // after the one-shot ends; otherwise it jumps from 0.32 to 1.0.
                signal = readSample(voice) * 0.78f + tonal * 0.32f;
            }
            break;
        }
        case Instrument::Bass: {
            const float tonal = renderBassTone(current.bassTone, voice.phase, voice.age);
            voice.filter += 0.075f * (tonal - voice.filter);
            signal = voice.filter;
            break;
        }
        case Instrument::Kick:
            if (current.soundEngine == SoundEngine::Hybrid && voice.sampleActive) {
                signal = readSample(voice) * 0.93f + fundamental * 0.12f;
            } else {
                const float click = voice.age < 24 ?
                    (1.0f - static_cast<float>(voice.age) / 24.0f) *
                        signedNoise(voice.noiseState) * 0.18f : 0.0f;
                signal = fundamental + click;
            }
            voice.phaseIncrement = static_cast<std::uint32_t>(
                std::max(49.0f * kPhaseScale,
                         static_cast<float>(voice.phaseIncrement) * 0.99972f));
            break;
        case Instrument::Snare:
            if (current.soundEngine == SoundEngine::Hybrid && voice.sampleActive) {
                signal = readSample(voice);
            } else {
                const float noise = signedNoise(voice.noiseState);
                voice.filter += 0.14f * (noise - voice.filter);
                signal = (noise - voice.filter) * 0.82f + fundamental * 0.18f;
            }
            break;
        case Instrument::Hat:
            if (current.soundEngine == SoundEngine::Hybrid && voice.sampleActive) {
                signal = readSample(voice);
            } else {
                const float noise = signedNoise(voice.noiseState);
                voice.filter += 0.06f * (noise - voice.filter);
                signal = noise - voice.filter;
            }
            break;
        case Instrument::Rim:
            if (current.soundEngine == SoundEngine::Hybrid && voice.sampleActive) {
                signal = readSample(voice);
            } else {
                signal = fundamental * 0.72f + signedNoise(voice.noiseState) * 0.28f;
            }
            break;
        }

        voice.phase += voice.phaseIncrement;
        ++voice.age;
        float output = signal * voice.envelope * voice.velocity * voice.gain;
        if (std::fabs(voice.stealTail) > 0.00005f) {
            output += voice.stealTail;
            voice.stealTail *= 0.985f;
        } else {
            voice.stealTail = 0.0f;
        }
        voice.lastOutput = output;
        if (voice.stage == EnvelopeStage::Release && voice.envelope < 0.00035f) {
            // The envelope also multiplies samples. A faded-out source must
            // release its pool slot even if the one-shot has unread frames.
            voice.active = false;
            voice.sampleActive = false;
        }
        return output;
    }

    float renderTexture() noexcept {
        if (current.texture == 0) {
            return 0.0f;
        }
        const float amount = static_cast<float>(current.texture) / 100.0f;
        const float dust = static_cast<float>(static_cast<std::int32_t>(textureRng.next32())) /
                           2147483648.0f * 0.0022f * amount;
        float crackle = 0.0f;
        if ((textureRng.next32() & 0x3ffffu) == 0u) {
            crackle = static_cast<float>(static_cast<std::int32_t>(textureRng.next32())) /
                      2147483648.0f * 0.035f * amount;
        }
        return dust + crackle;
    }

    std::uint8_t activeVoiceCount() const noexcept {
        std::uint8_t count = 0;
        for (const Voice& voice : voices) {
            count += voice.active ? 1 : 0;
        }
        return count;
    }

    void advanceBar() noexcept {
        const bool automatic = automaticTransitionDue();
        if (pendingChangeDue() && pendingTempoOnly()) {
            current.bpm = pendingConfig.bpm;
            pendingChange = false;
            pendingRestartRequested = false;
            pendingApplyBar = 0;

            if (automatic) {
                Config next = current;
                next.seed = mix64(current.seed ^ UINT64_C(0x4e4558545f534553));
                ++sessionTransitions;
                startSession(next, false);
                return;
            }

            ++barIndex;
            barStartQ32 = barEndQ32;
            updateEffectiveTempo();
            barEndQ32 = barStartQ32 + stepQ32 * stepsPerBar;
            generateBar();
            return;
        }

        const bool transition = pendingRestartDue() || automatic;
        if (transition) {
            Config next = pendingChange ? pendingConfig : current;
            if (!pendingChange) {
                next.seed = mix64(current.seed ^ UINT64_C(0x4e4558545f534553));
            }
            ++sessionTransitions;
            startSession(next, false);
            return;
        }
        ++barIndex;
        barStartQ32 = barEndQ32;
        barEndQ32 += stepQ32 * stepsPerBar;
        generateBar();
    }

    void publish() noexcept {
        if (publishLock.test_and_set(std::memory_order_acquire)) {
            return;
        }
        Snapshot snap{};
        snap.config = current;
        snap.transportSample = transportSample;
        snap.sessionSample = sessionSample;
        snap.bar = barIndex;
        snap.bpm = bpm;
        snap.meterNumerator = meterNumerator;
        snap.meterDenominator = meterDenominator;
        snap.beatsPerBar = beatsPerBar;
        snap.stepsPerBar = stepsPerBar;
        snap.stepsPerBeat = stepsPerBeat;
        const std::uint64_t start = barStartSample();
        const std::uint64_t end = std::max<std::uint64_t>(start + 1, barEndSample());
        const std::uint64_t within = sessionSample > start ?
            std::min<std::uint64_t>(sessionSample - start, end - start) : 0;
        snap.barPhaseQ16 = static_cast<std::uint16_t>(
            within * UINT64_C(65535) / (end - start));
        snap.sixteenth = static_cast<std::uint8_t>(
            std::min<std::uint64_t>(stepsPerBar - 1u,
                                    within * stepsPerBar / (end - start)));
        snap.beat = static_cast<std::uint8_t>(
            std::min<std::uint8_t>(beatsPerBar - 1u,
                                   snap.sixteenth / stepsPerBeat));
        snap.section = sectionForBar(barIndex, sessionBars);
        snap.paused = pauseTarget;
        snap.changePending = pendingChange || automaticTransitionDue();
        snap.activeVoices = activeVoiceCount();
        snap.voiceCapacity = kMusicVoiceCapacity;
        snap.keysGainQ15 = static_cast<std::uint16_t>(
            std::max(0.0f, std::min(1.0f, keysGain)) * 32767.0f + 0.5f);
        for (std::size_t instrument = 0; instrument < kMusicInstrumentCount;
             ++instrument) {
            const float level = pauseSettled && pauseTarget ? 0.0f :
                std::max(0.0f, std::min(1.0f, recentInstrumentPeaks[instrument]));
            snap.instrumentLevels[instrument] = static_cast<std::uint8_t>(
                level * 255.0f + 0.5f);
        }
        snap.recentPeak = recentPeak;
        snap.scoreEventHash = scoreHash;
        snap.scoreEventCount = scoreEvents;
        publishedSnapshot = snap;

        Diagnostics diag{};
        diag.renderedFrames = renderedFrames;
        diag.transportSample = transportSample;
        diag.scoreEventHash = scoreHash;
        diag.scoreEventCount = scoreEvents;
        diag.stolenVoices = stolenVoices;
        diag.droppedNoteEvents = droppedNoteEvents;
        diag.sessionTransitions = sessionTransitions;
        std::copy(std::begin(noteEventsByInstrument),
                  std::end(noteEventsByInstrument), diag.noteEventsByInstrument);
        std::copy(std::begin(firstNoteSamples), std::end(firstNoteSamples),
                  diag.firstNoteSamples);
        diag.absolutePeak = absolutePeak;
        diag.maxActiveVoices = maxActiveVoices;
        diag.minimumKeysGainQ15 = static_cast<std::uint16_t>(
            std::max(0.0f, std::min(1.0f, minimumKeysGain)) * 32767.0f + 0.5f);
        publishedDiagnostics = diag;
        publishLock.clear(std::memory_order_release);
    }
};

const char* moodName(Mood mood) noexcept {
    switch (mood) {
    case Mood::Cozy:
        return "Cozy";
    case Mood::Rainy:
        return "Rainy";
    case Mood::Night:
        return "Night";
    }
    return "Unknown";
}

const char* soundEngineName(SoundEngine engine) noexcept {
    switch (engine) {
    case SoundEngine::Synth:
        return "Synth";
    case SoundEngine::Hybrid:
        return "Hybrid";
    }
    return "Unknown";
}

const char* meterName(MusicMeter meter) noexcept {
    switch (meter) {
    case MusicMeter::Auto:
        return "AUTO";
    case MusicMeter::FourFour:
        return "4/4";
    case MusicMeter::ThreeFour:
        return "3/4";
    case MusicMeter::SixEight:
        return "6/8";
    }
    return "Unknown";
}

bool validMeter(MusicMeter meter) noexcept {
    return static_cast<std::uint8_t>(meter) <=
           static_cast<std::uint8_t>(MusicMeter::SixEight);
}

bool validBpm(std::uint16_t bpm) noexcept {
    return bpm == 0 || (bpm >= kMusicMinBpm && bpm <= kMusicMaxBpm);
}

bool validConfig(const Config& config) noexcept {
    return static_cast<std::uint8_t>(config.mood) <= static_cast<std::uint8_t>(Mood::Night) &&
           static_cast<std::uint8_t>(config.soundEngine) <=
               static_cast<std::uint8_t>(SoundEngine::Hybrid) &&
           validBpm(config.bpm) && validMeter(config.meter) &&
           validTone(config.keysTone) && validTone(config.leadTone) &&
           validBassTone(config.bassTone);
}

Engine::Engine(const Config& config) noexcept {
    static_assert(sizeof(Impl) <= kStorageBytes, "Engine storage is too small");
    static_assert(alignof(Impl) <= alignof(std::max_align_t), "Engine storage alignment is too small");
    new (storage_) Impl{};
    std::size_t count = 0;
    impl().samples = builtinSamples(count);
    impl().sampleCount = count;
    impl().startSession(config, true);
    impl().publish();
}

Engine::~Engine() {
    impl().~Impl();
}

Engine::Impl& Engine::impl() noexcept {
    return *std::launder(reinterpret_cast<Impl*>(storage_));
}

const Engine::Impl& Engine::impl() const noexcept {
    return *std::launder(reinterpret_cast<const Impl*>(storage_));
}

void Engine::reset(const Config& config) noexcept {
    Impl& state = impl();
    while (state.commandLock.test_and_set(std::memory_order_acquire)) {
    }
    state.commandConfigPending = false;
    state.commandNextPending = false;
    state.pauseCommandPending = false;
    state.commandLock.clear(std::memory_order_release);
    state.startSession(config, true);
    state.publish();
}

bool Engine::requestConfig(const Config& config) noexcept {
    if (!validConfig(config)) {
        return false;
    }
    Impl& state = impl();
    while (state.commandLock.test_and_set(std::memory_order_acquire)) {
    }
    state.commandConfig = sanitized(config);
    state.commandConfigSequence = ++state.commandSequence;
    state.commandConfigPending = true;
    state.commandLock.clear(std::memory_order_release);
    return true;
}

void Engine::requestNext() noexcept {
    Impl& state = impl();
    while (state.commandLock.test_and_set(std::memory_order_acquire)) {
    }
    state.commandNextExplicit = false;
    state.commandNextSequence = ++state.commandSequence;
    state.commandNextPending = true;
    state.commandLock.clear(std::memory_order_release);
}

void Engine::requestNext(std::uint64_t seed) noexcept {
    Impl& state = impl();
    while (state.commandLock.test_and_set(std::memory_order_acquire)) {
    }
    state.commandSeed = seed;
    state.commandNextExplicit = true;
    state.commandNextSequence = ++state.commandSequence;
    state.commandNextPending = true;
    state.commandLock.clear(std::memory_order_release);
}

void Engine::pause(bool paused) noexcept {
    Impl& state = impl();
    while (state.commandLock.test_and_set(std::memory_order_acquire)) {
    }
    state.pauseCommand = paused;
    state.pauseCommandPending = true;
    state.commandLock.clear(std::memory_order_release);
}

void Engine::render(std::int16_t* output, std::size_t frames) noexcept {
    if (output == nullptr || frames == 0) {
        return;
    }
    Impl& state = impl();
    state.consumeCommands();
    state.recentPeak = 0;

    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (state.pauseSettled && state.pauseTarget) {
            output[frame] = 0;
            ++state.renderedFrames;
            continue;
        }

        if (state.sessionSample >= state.barEndSample()) {
            state.advanceBar();
        }

        state.dispatchEvents();
        float instrumentMix[kMusicInstrumentCount]{};
        bool leadActive = false;
        for (Voice& voice : state.voices) {
            const float voiceOutput = state.renderVoice(voice);
            instrumentMix[static_cast<std::size_t>(voice.instrument)] += voiceOutput;
            leadActive = leadActive ||
                (voice.active && voice.instrument == Instrument::Lead &&
                 voice.envelope > 0.025f);
        }

        // Give the lead a narrow pocket by turning down only the chord bed.
        // The attack and release are intentionally smoothed to avoid pumping;
        // bass, drums, texture and delay remain outside this attenuation.
        constexpr float kLeadKeysGain = 0.88f;
        constexpr float kKeysDuckAttack = 0.00155f;  // ~20 ms at 32 kHz.
        constexpr float kKeysDuckRelease = 0.00026f; // ~120 ms at 32 kHz.
        const float keysTarget = leadActive ? kLeadKeysGain : 1.0f;
        const float keysSmoothing = leadActive ? kKeysDuckAttack : kKeysDuckRelease;
        state.keysGain += (keysTarget - state.keysGain) * keysSmoothing;
        state.keysGain = std::max(kLeadKeysGain, std::min(1.0f, state.keysGain));
        state.minimumKeysGain = std::min(state.minimumKeysGain, state.keysGain);
        instrumentMix[static_cast<std::size_t>(Instrument::Keys)] *= state.keysGain;

        float mix = 0.0f;
        for (float contribution : instrumentMix) {
            mix += contribution;
        }
        mix += state.renderTexture();

        const std::uint8_t active = state.activeVoiceCount();
        state.maxActiveVoices = std::max(state.maxActiveVoices, active);
        state.kickDuck += (1.0f - state.kickDuck) * 0.00042f;
        mix *= state.kickDuck;

        const float delayed = static_cast<float>(state.delay[state.delayIndex]) * kInvInt16;
        const float delayWrite = std::max(-0.98f, std::min(0.98f, mix + delayed * 0.16f));
        state.delay[state.delayIndex] = static_cast<std::int16_t>(delayWrite * 32767.0f);
        ++state.delayIndex;
        if (state.delayIndex == kDelayFrames) {
            state.delayIndex = 0;
        }
        mix += delayed * 0.105f;

        const float toneCoefficient = state.current.mood == Mood::Night ? 0.105f :
                                      state.current.mood == Mood::Rainy ? 0.125f : 0.145f;
        state.toneState += toneCoefficient * (mix - state.toneState);
        const float dcBlocked = state.toneState - state.dcInput + 0.995f * state.dcOutput;
        state.dcInput = state.toneState;
        state.dcOutput = dcBlocked;
        mix = softClip(dcBlocked * 1.32f);

        const bool wantsTransition = state.pendingRestartDue() ||
                                     state.automaticTransitionDue();
        const std::uint64_t end = state.barEndSample();
        const std::uint64_t remaining = end > state.sessionSample ?
            end - state.sessionSample : 0;
        const bool fadeOutNow = wantsTransition && remaining <= kFadeFrames;
        if (fadeOutNow) {
            state.transitionGain = std::min(
                state.transitionGain,
                static_cast<float>(remaining) / static_cast<float>(kFadeFrames));
        } else if (state.transitionFadeIn > 0) {
            state.transitionGain = 1.0f -
                static_cast<float>(state.transitionFadeIn) / static_cast<float>(kFadeFrames);
            --state.transitionFadeIn;
        } else {
            state.transitionGain = 1.0f;
        }

        if (state.pauseTarget) {
            state.pauseGain = std::max(0.0f, state.pauseGain - 1.0f / kFadeFrames);
            if (state.pauseGain <= 0.0f) {
                state.pauseGain = 0.0f;
                state.pauseSettled = true;
                std::fill(std::begin(state.recentInstrumentPeaks),
                          std::end(state.recentInstrumentPeaks), 0.0f);
            }
        } else {
            state.pauseGain = std::min(1.0f, state.pauseGain + 1.0f / kFadeFrames);
        }

        const float volume = static_cast<float>(state.current.volume) / 100.0f;
        const float master = volume * volume * 0.94f;
        const float instrumentScale = master * state.pauseGain *
                                      state.transitionGain * state.kickDuck * 1.32f;
        for (std::size_t instrument = 0; instrument < kMusicInstrumentCount;
             ++instrument) {
            state.recentInstrumentPeaks[instrument] = std::max(
                state.recentInstrumentPeaks[instrument] * 0.9997f,
                std::fabs(instrumentMix[instrument]) * instrumentScale);
        }
        const float scaled = mix * master * state.pauseGain * state.transitionGain;
        const float limited = std::max(-1.0f, std::min(1.0f, scaled));
        const std::int32_t pcm = static_cast<std::int32_t>(limited * 32767.0f);
        output[frame] = static_cast<std::int16_t>(pcm);
        const std::uint16_t magnitude = static_cast<std::uint16_t>(
            std::min<std::int32_t>(32767, std::abs(pcm)));
        state.recentPeak = std::max(state.recentPeak, magnitude);
        state.absolutePeak = std::max(state.absolutePeak, magnitude);

        ++state.renderedFrames;
        ++state.transportSample;
        ++state.sessionSample;
    }
    state.publish();
}

Config Engine::config() const noexcept {
    return snapshot().config;
}

Snapshot Engine::snapshot() const noexcept {
    const Impl& state = impl();
    while (state.publishLock.test_and_set(std::memory_order_acquire)) {
    }
    const Snapshot result = state.publishedSnapshot;
    state.publishLock.clear(std::memory_order_release);
    return result;
}

Diagnostics Engine::diagnostics() const noexcept {
    const Impl& state = impl();
    while (state.publishLock.test_and_set(std::memory_order_acquire)) {
    }
    const Diagnostics result = state.publishedDiagnostics;
    state.publishLock.clear(std::memory_order_release);
    return result;
}

ScoreBar Engine::scoreBar() const noexcept {
    const Impl& state = impl();
    ScoreBar result{};
    result.seed = state.current.seed;
    result.bar = state.barIndex;
    result.bpm = state.bpm;
    result.meterNumerator = state.meterNumerator;
    result.meterDenominator = state.meterDenominator;
    result.beatsPerBar = state.beatsPerBar;
    result.stepsPerBar = state.stepsPerBar;
    result.stepsPerBeat = state.stepsPerBeat;
    result.barStartSample = state.barStartSample();
    result.barEndSample = state.barEndSample();
    result.keyPitchClass = state.keyPitchClass;
    result.minor = state.minorSession;
    result.chordRoot = state.currentBassRoot;
    std::copy(std::begin(state.chordNotes), std::end(state.chordNotes),
              result.chordNotes);
    result.noteCount = state.eventCount;
    for (std::size_t index = 0; index < state.eventCount; ++index) {
        const Event& event = state.events[index];
        ScoreNote& note = result.notes[index];
        note.startSample = event.start;
        note.durationSamples = event.duration;
        note.instrument = event.instrument;
        note.note = event.note;
        note.velocity = event.velocity;
    }
    return result;
}

namespace {

char hexDigit(std::uint8_t value) noexcept {
    return value < 10 ? static_cast<char>('0' + value) :
                        static_cast<char>('a' + value - 10);
}

bool parseDecimal(const char*& cursor, std::uint16_t maximum,
                  std::uint16_t& value) noexcept {
    if (*cursor < '0' || *cursor > '9') {
        return false;
    }
    unsigned parsed = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        parsed = parsed * 10u + static_cast<unsigned>(*cursor - '0');
        if (parsed > maximum) {
            return false;
        }
        ++cursor;
    }
    value = static_cast<std::uint16_t>(parsed);
    return true;
}

void appendDecimal(char*& cursor, std::uint16_t value) noexcept {
    if (value >= 100) {
        *cursor++ = static_cast<char>('0' + value / 100);
        value = static_cast<std::uint16_t>(value % 100);
        *cursor++ = static_cast<char>('0' + value / 10);
        *cursor++ = static_cast<char>('0' + value % 10);
    } else if (value >= 10) {
        *cursor++ = static_cast<char>('0' + value / 10);
        *cursor++ = static_cast<char>('0' + value % 10);
    } else {
        *cursor++ = static_cast<char>('0' + value);
    }
}

} // namespace

std::size_t Engine::writeFavoriteCode(char* output, std::size_t capacity) const noexcept {
    if (output == nullptr || capacity == 0) {
        return 0;
    }
    const Config saved = config();
    char local[kFavoriteCodeCapacity]{};
    char* cursor = local;
    const char prefix[] = "lofi5-";
    for (char character : prefix) {
        if (character != '\0') {
            *cursor++ = character;
        }
    }
    for (int shift = 60; shift >= 0; shift -= 4) {
        *cursor++ = hexDigit(static_cast<std::uint8_t>((saved.seed >> shift) & 0x0fu));
    }
    *cursor++ = '-';
    *cursor++ = static_cast<char>('0' + static_cast<std::uint8_t>(saved.mood));
    *cursor++ = '-';
    *cursor++ = static_cast<char>('0' + static_cast<std::uint8_t>(saved.soundEngine));
    *cursor++ = '-';
    appendDecimal(cursor, saved.volume);
    *cursor++ = '-';
    appendDecimal(cursor, saved.texture);
    *cursor++ = '-';
    appendDecimal(cursor, saved.bpm);
    *cursor++ = '-';
    *cursor++ = static_cast<char>('0' + static_cast<std::uint8_t>(saved.meter));
    *cursor++ = '-';
    *cursor++ = static_cast<char>('0' + static_cast<std::uint8_t>(saved.keysTone));
    *cursor++ = '-';
    *cursor++ = static_cast<char>('0' + static_cast<std::uint8_t>(saved.leadTone));
    *cursor++ = '-';
    *cursor++ = static_cast<char>('0' + static_cast<std::uint8_t>(saved.bassTone));
    *cursor = '\0';
    const std::size_t length = static_cast<std::size_t>(cursor - local);
    if (capacity <= length) {
        output[0] = '\0';
        return 0;
    }
    std::memcpy(output, local, length + 1);
    return length;
}

bool Engine::parseFavoriteCode(const char* text, Config& output) noexcept {
    if (text == nullptr || std::strncmp(text, "lofi5-", 6) != 0) {
        return false;
    }
    const char* cursor = text + 6;
    std::uint64_t seed = 0;
    for (int i = 0; i < 16; ++i) {
        const char character = *cursor++;
        std::uint8_t nibble = 0;
        if (character >= '0' && character <= '9') {
            nibble = static_cast<std::uint8_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            nibble = static_cast<std::uint8_t>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            nibble = static_cast<std::uint8_t>(character - 'A' + 10);
        } else {
            return false;
        }
        seed = (seed << 4) | nibble;
    }
    if (*cursor++ != '-') {
        return false;
    }
    if (*cursor < '0' || *cursor > '2') {
        return false;
    }
    const Mood mood = static_cast<Mood>(*cursor++ - '0');
    if (*cursor++ != '-') {
        return false;
    }
    if (*cursor < '0' || *cursor > '1') {
        return false;
    }
    const SoundEngine soundEngine = static_cast<SoundEngine>(*cursor++ - '0');
    if (*cursor++ != '-') {
        return false;
    }
    std::uint16_t volume = 0;
    if (!parseDecimal(cursor, 100, volume) || *cursor++ != '-') {
        return false;
    }
    std::uint16_t texture = 0;
    if (!parseDecimal(cursor, 100, texture) || *cursor++ != '-') {
        return false;
    }
    std::uint16_t bpm = 0;
    if (!parseDecimal(cursor, kMusicMaxBpm, bpm) || !validBpm(bpm) ||
        *cursor++ != '-') {
        return false;
    }
    if (*cursor < '0' || *cursor > '3') {
        return false;
    }
    const MusicMeter meter = static_cast<MusicMeter>(*cursor++ - '0');
    if (*cursor++ != '-' || *cursor < '0' || *cursor > '5') {
        return false;
    }
    const Tone keysTone = static_cast<Tone>(*cursor++ - '0');
    if (*cursor++ != '-' || *cursor < '0' || *cursor > '5') {
        return false;
    }
    const Tone leadTone = static_cast<Tone>(*cursor++ - '0');
    if (*cursor++ != '-' || *cursor < '0' || *cursor > '2') {
        return false;
    }
    const BassTone bassTone = static_cast<BassTone>(*cursor++ - '0');
    if (*cursor != '\0') {
        return false;
    }
    output.seed = seed;
    output.mood = mood;
    output.soundEngine = soundEngine;
    output.volume = static_cast<std::uint8_t>(volume);
    output.texture = static_cast<std::uint8_t>(texture);
    output.bpm = bpm;
    output.meter = meter;
    output.keysTone = keysTone;
    output.leadTone = leadTone;
    output.bassTone = bassTone;
    return true;
}

} // namespace lofi
