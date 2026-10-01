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
constexpr std::size_t kDelayFrames = 2704; // 84.5 ms at 32 kHz.
constexpr int kBassLow = 32;
constexpr int kBassHigh = 48;
constexpr int kLeadLow = 64;
constexpr int kLeadHigh = 83;
constexpr int kLeadMaxLeap = 5;
constexpr std::size_t kLeadCellNotes = 6;
// Shared tape-style pitch movement: slow wow plus a little faster flutter.
constexpr std::uint32_t kWowIncrement = 73820u;      // 0.55 Hz at 32 kHz.
constexpr std::uint32_t kFlutterIncrement = 845555u; // 6.3 Hz at 32 kHz.
constexpr float kWowDepth = 0.0026f;
constexpr float kFlutterDepth = 0.0005f;
constexpr float kCentsPerRatio = 1731.234f; // 1200 / ln(2).
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
    if (mood >= kMoodCount) {
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

float readSample(Voice& voice, float drift = 0.0f) noexcept {
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

    voice.samplePhaseQ16 += voice.sampleIncrementQ16 +
        static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<float>(voice.sampleIncrementQ16) * drift));
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

// One bar of melody rhythm on the session's sixteenth grid. Each gate ends at
// or before the next onset; swing can still trim it by a few samples.
struct LeadCell {
    std::uint8_t count;
    std::uint8_t steps[kLeadCellNotes];
    std::uint8_t gates[kLeadCellNotes];
};

// Indexed by meter: 4/4, 3/4, 6/8. Hooks are the returning idea, answers
// leave a beat of space and end on a held note, contrasts open the B bars.
constexpr LeadCell kHookCells[3][4] = {
    {{5, {0, 3, 6, 8, 10}, {3, 3, 2, 2, 4}},
     {6, {0, 2, 4, 7, 8, 14}, {2, 2, 3, 1, 4, 2}},
     {5, {2, 4, 6, 11, 12}, {2, 2, 4, 1, 4}},
     {5, {0, 6, 8, 10, 12}, {4, 2, 2, 2, 3}}},
    {{4, {0, 2, 4, 8}, {2, 2, 4, 3}},
     {5, {0, 3, 4, 6, 8}, {3, 1, 2, 2, 4}},
     {4, {2, 4, 6, 8}, {2, 2, 2, 4}},
     {4, {0, 6, 8, 10}, {4, 2, 2, 2}}},
    {{4, {0, 2, 4, 6}, {2, 2, 2, 5}},
     {5, {0, 4, 6, 8, 10}, {4, 2, 2, 2, 2}},
     {4, {2, 4, 6, 10}, {2, 2, 4, 2}},
     {4, {0, 6, 8, 10}, {6, 2, 2, 2}}},
};
constexpr LeadCell kAnswerCells[3][3] = {
    {{3, {4, 6, 8}, {2, 2, 6}},
     {4, {3, 4, 10, 12}, {1, 4, 2, 4}},
     {4, {6, 8, 10, 12}, {2, 2, 2, 4}}},
    {{3, {4, 6, 8}, {2, 2, 4}},
     {2, {4, 8}, {4, 4}},
     {3, {2, 4, 6}, {2, 2, 6}}},
    {{2, {4, 6}, {2, 6}},
     {2, {3, 6}, {3, 6}},
     {3, {2, 4, 6}, {2, 2, 4}}},
};
constexpr LeadCell kContrastCells[3][3] = {
    {{4, {0, 8, 10, 12}, {6, 2, 2, 4}},
     {5, {0, 2, 4, 6, 8}, {2, 2, 2, 2, 6}},
     {3, {4, 8, 12}, {4, 4, 4}}},
    {{3, {0, 8, 10}, {6, 2, 2}},
     {4, {0, 2, 4, 6}, {2, 2, 2, 6}},
     {3, {0, 4, 8}, {4, 4, 4}}},
    {{2, {0, 6}, {6, 6}},
     {5, {0, 2, 4, 6, 8}, {2, 2, 2, 2, 4}},
     {4, {0, 4, 6, 10}, {4, 2, 4, 2}}},
};
constexpr LeadCell kCadenceCells[3] = {
    {2, {6, 8}, {2, 6}},
    {2, {2, 4}, {2, 6}},
    {2, {4, 6}, {2, 6}},
};
constexpr std::int8_t kFallingContour[kLeadCellNotes] = {0, -1, -1, -1, 1, -1};
constexpr std::int8_t kRisingContour[kLeadCellNotes] = {0, 1, 1, -1, 1, -1};

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
    float toneState2 = 0.0f;
    float hissState = 0.0f;
    float crackle = 0.0f;
    std::uint32_t wowPhase = 0;
    std::uint32_t flutterPhase = 0;
    float pitchDrift = 0.0f;
    float pumpDepth = 0.0f;
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
    std::uint8_t hookCell = 0;
    std::uint8_t answerCell = 0;
    std::uint8_t contrastCell = 0;
    std::uint8_t leadHome = 72;
    std::int8_t hookContour[kLeadCellNotes]{};
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
        toneState2 = 0.0f;
        hissState = 0.0f;
        crackle = 0.0f;
        wowPhase = 0;
        flutterPhase = 0;
        pitchDrift = 0.0f;
        pumpDepth = 0.0f;
        dcInput = 0.0f;
        dcOutput = 0.0f;
        kickDuck = 1.0f;
        keysGain = 1.0f;
    }

    void chooseProgression() noexcept {
        minorSession = current.mood == Mood::Sunny ? false :
            current.mood != Mood::Cozy || scoreRng.chance(1, 4);
        // Degrees are semitones above the tonic; shapes follow the key.
        static constexpr std::uint8_t majorDegrees[][4] = {
            {0, 9, 2, 7}, // I - vi - ii - V
            {0, 4, 5, 7}, // I - iii - IV - V
            {0, 5, 9, 7}, // I - IV - vi - V
            {2, 7, 0, 9}, // ii - V - I - vi
            {5, 4, 2, 0}, // IV - iii - ii - I
        };
        static constexpr std::uint8_t minorDegrees[][4] = {
            {0, 8, 3, 10}, // i - VI - III - VII
            {0, 5, 8, 7},  // i - iv - VI - v
            {0, 3, 5, 7},  // i - III - iv - v
            {0, 10, 8, 7}, // i - VII - VI - v
            {5, 10, 3, 8}, // iv - VII - III - VI
        };
        const std::size_t selection = scoreRng.bounded(5);
        for (std::size_t slot = 0; slot < 4; ++slot) {
            progressionDegrees[slot] = minorSession ?
                minorDegrees[selection][slot] : majorDegrees[selection][slot];
            progressionShapes[slot] = shapeForDegree(progressionDegrees[slot]);
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
        // The keys voicing is rootless; the bass root completes the harmony.
        const int pitchClass = midi % 12;
        if (currentBassRoot % 12 == pitchClass) {
            return true;
        }
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

    // A scale tone just beside the target that is not in the harmony, on the
    // side the line arrives from. Returns target when none fits.
    std::uint8_t approachPitch(std::uint8_t target, int from) const noexcept {
        const int preferred = from >= static_cast<int>(target) ? 1 : -1;
        for (const int direction : {preferred, -preferred}) {
            for (int distance = 1; distance <= 2; ++distance) {
                const int candidate = static_cast<int>(target) + direction * distance;
                if (candidate >= kLeadLow && candidate <= kLeadHigh &&
                    scaleContains(candidate) && !chordContains(candidate) &&
                    std::abs(candidate - from) <= kLeadMaxLeap) {
                    return static_cast<std::uint8_t>(candidate);
                }
            }
        }
        return target;
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
        // Rootless voicing: third, fifth, seventh and the ninth. A ninth
        // outside the session key falls back to the root an octave up.
        const int top = scaleContains(root + 14) ? root + 14 : root + 12;
        const int raw[4] = {root + harmony.shape.third, root + 7,
                            root + harmony.shape.seventh, top};
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
        hookCell = static_cast<std::uint8_t>(scoreRng.bounded(4));
        answerCell = static_cast<std::uint8_t>(scoreRng.bounded(3));
        contrastCell = static_cast<std::uint8_t>(scoreRng.bounded(3));
        // An arch (or occasionally a valley) in harmony-ladder steps: mostly
        // neighbors, sometimes a skip. Entry 0 is the offset from leadHome.
        const bool valley = scoreRng.chance(1, 3);
        hookContour[0] = static_cast<std::int8_t>(scoreRng.centered(1));
        for (std::size_t note = 1; note < kLeadCellNotes; ++note) {
            const int magnitude = scoreRng.chance(1, 5) ? 2 : 1;
            const bool rising = (note <= 2u) != valley;
            hookContour[note] = static_cast<std::int8_t>(rising ? magnitude : -magnitude);
        }
        leadHome = static_cast<std::uint8_t>(71 + scoreRng.bounded(5));
    }

    std::size_t meterIndex() const noexcept {
        return meter == MusicMeter::ThreeFour ? 1u :
               meter == MusicMeter::SixEight ? 2u : 0u;
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
        case Mood::Sunny:
            autoBpm = static_cast<std::uint16_t>(84 + scoreRng.bounded(9));
            break;
        }
        const std::uint32_t meterDraw = scoreRng.bounded(10);
        const MusicMeter automaticMeter = meterDraw < 7u ? MusicMeter::FourFour :
            meterDraw < 9u ? MusicMeter::ThreeFour : MusicMeter::SixEight;
        setMeter(current.meter == MusicMeter::Auto ? automaticMeter : current.meter);
        swingPercent = static_cast<std::uint8_t>(58 + scoreRng.bounded(5));
        static constexpr std::uint8_t cozyKeys[] = {0, 2, 5, 7, 9};
        static constexpr std::uint8_t darkKeys[] = {0, 2, 3, 5, 7, 9, 10};
        static constexpr std::uint8_t sunnyKeys[] = {0, 4, 5, 7, 9};
        if (current.mood == Mood::Cozy) {
            keyPitchClass = cozyKeys[scoreRng.bounded(5)];
        } else if (current.mood == Mood::Sunny) {
            keyPitchClass = sunnyKeys[scoreRng.bounded(5)];
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
        lastLeadNote = leadHome;

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
        // The backbeat sits about 12 ms behind the grid for a relaxed pocket.
        constexpr int kLazyBackbeat = 384;
        const std::uint8_t pattern = static_cast<std::uint8_t>(
            (barIndex + (barIndex / 8u) + (current.seed >> 9)) & 3u);

        const int chordVelocity = section == ArrangementSection::Breakdown ? 58 :
                                  section == ArrangementSection::Intro ?
                                      62 + static_cast<int>(barIndex) * 2 : 72;
        // In the full sections every other bar re-strikes the two upper chord
        // voices on a late off-beat instead of holding all four for the bar.
        const bool fullSection = section == ArrangementSection::Groove ||
                                 section == ArrangementSection::Melody ||
                                 section == ArrangementSection::Return;
        // 6/8 bars are too short for it to read as anything but clutter.
        const std::uint8_t restrikeStep = fullSection && pattern != 3u &&
            (barIndex & 1u) != 0u && meter != MusicMeter::SixEight ?
                (meter == MusicMeter::FourFour ? 10u : 6u) : 0u;
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
            std::uint8_t gate = remaining > tailSpace ?
                static_cast<std::uint8_t>(remaining - tailSpace) : 1u;
            if (restrikeStep != 0u && source >= 2) {
                gate = static_cast<std::uint8_t>(restrikeStep - onset);
            }
            const int velocity = chordVelocity + scoreRng.centered(5);
            addEvent(onset, strum, gate, Instrument::Keys, chordNotes[source],
                     velocity);
        }
        if (restrikeStep != 0u) {
            for (int source = 2; source < 4; ++source) {
                const int velocity = chordVelocity - 16 + scoreRng.centered(4);
                addEvent(restrikeStep, (source - 2) * 22,
                         static_cast<std::uint8_t>(stepsPerBar - restrikeStep - 2u),
                         Instrument::Keys, chordNotes[source], velocity);
            }
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

        const bool fullDrums = fullSection;
        if (meter == MusicMeter::FourFour) {
            if (section == ArrangementSection::Intro || fullDrums) {
                const int kick = section == ArrangementSection::Intro ? 69 : 89;
                const int snare = section == ArrangementSection::Intro ? 50 : 75;
                addHumanizedEvent(0, 0, 16, 3, Instrument::Kick, 36, kick, 5);
                static constexpr std::uint8_t secondKicks[] = {8, 10, 8, 7};
                addHumanizedEvent(secondKicks[pattern], 0, 22, 2,
                                  Instrument::Kick, 36, kick - 12, 5);
                addHumanizedEvent(4, kLazyBackbeat, 24, 2, Instrument::Snare, 38,
                                  snare, 6);
                addHumanizedEvent(12, kLazyBackbeat, 24, 2, Instrument::Snare, 38,
                                  snare + 3, 6);
                for (std::uint8_t step = 2; step < 16; step += 2) {
                    const bool rest = fullDrums && pattern == 1u && step == 6u;
                    if (!rest) {
                        addHumanizedEvent(step, 0, 18, 1, Instrument::Hat, 42,
                                          (step & 3u) == 0u ? 32 : 46, 5);
                    }
                }
                if (fullDrums && (pattern & 1u) != 0u) {
                    // A ghosted sixteenth leading into the second backbeat.
                    addHumanizedEvent(11, 0, 14, 1, Instrument::Hat, 42, 24, 3);
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
                addHumanizedEvent(4, kLazyBackbeat, 20, 2, Instrument::Snare, 38,
                                  snare, 5);
                addHumanizedEvent(8, kLazyBackbeat, 20, 2, Instrument::Snare, 38,
                                  snare - 3, 5);
                for (std::uint8_t step = 2; step < 12; step += 2) {
                    addHumanizedEvent(step, 0, 16, 1, Instrument::Hat, 42,
                                      (step & 3u) == 0u ? 32 : 44, 5);
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
                addHumanizedEvent(6, kLazyBackbeat, 20, 2, Instrument::Snare, 38,
                                  snare, 5);
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

        generateLead(section, harmony);
        trimLeadGates();
    }

    // Walks the harmony ladder (chord tones plus the bass root inside the lead
    // register) along a contour, then turns short off-beat notes into scale
    // approach tones that resolve by step into the following chord tone.
    void addLeadPhrase(const LeadCell& cell, const std::int8_t* contour,
                       bool inverted, int home, int velocity, bool cadence,
                       const Harmony& harmony) noexcept {
        std::uint8_t ladder[kLeadHigh - kLeadLow + 1]{};
        int rungs = 0;
        for (int note = kLeadLow; note <= kLeadHigh; ++note) {
            if (chordContains(note)) {
                ladder[rungs++] = static_cast<std::uint8_t>(note);
            }
        }
        if (rungs == 0 || cell.count == 0) {
            return;
        }
        int index = 0;
        for (int rung = 1; rung < rungs; ++rung) {
            if (std::abs(static_cast<int>(ladder[rung]) - home) <
                std::abs(static_cast<int>(ladder[index]) - home)) {
                index = rung;
            }
        }

        std::uint8_t pitches[kLeadCellNotes]{};
        int previous = lastLeadNote;
        for (std::uint8_t note = 0; note < cell.count; ++note) {
            const int delta = inverted ? -contour[note] : contour[note];
            index += delta;
            if (index < 0 || index >= rungs) {
                index -= 2 * delta;
            }
            index = std::max(0, std::min(rungs - 1, index));
            while (index > 0 &&
                   static_cast<int>(ladder[index]) - previous > kLeadMaxLeap) {
                --index;
            }
            while (index + 1 < rungs &&
                   previous - static_cast<int>(ladder[index]) > kLeadMaxLeap) {
                ++index;
            }
            pitches[note] = ladder[index];
            previous = pitches[note];
        }

        const std::uint8_t last = static_cast<std::uint8_t>(cell.count - 1u);
        if (cadence) {
            // Close on the root or third, whichever is nearer; later phrases
            // lean toward the third so cadences do not all sound final.
            const int reference = last > 0 ? pitches[last - 1u] : lastLeadNote;
            const std::uint8_t rootClass = currentBassRoot % 12u;
            const std::uint8_t root = nearestPitch(rootClass, reference,
                                                   kLeadLow, kLeadHigh);
            const std::uint8_t third = nearestPitch(
                static_cast<std::uint8_t>((rootClass + harmony.shape.third) % 12u),
                reference, kLeadLow, kLeadHigh);
            const int rootDistance = std::abs(static_cast<int>(root) - reference);
            const int thirdDistance = std::abs(static_cast<int>(third) - reference) -
                                      ((barIndex & 8u) != 0u ? 1 : 0);
            pitches[last] = rootDistance <= thirdDistance ? root : third;
        }

        bool approached = false;
        for (std::uint8_t note = 0; note < last; ++note) {
            const bool eligible = !approached && cell.gates[note] <= 2u &&
                cell.steps[note] % stepsPerBeat != 0u &&
                cell.steps[note + 1u] - cell.steps[note] <= 2;
            approached = false;
            if (!eligible) {
                continue;
            }
            const int from = note > 0 ? pitches[note - 1u] : lastLeadNote;
            const std::uint8_t candidate = approachPitch(pitches[note + 1u], from);
            if (candidate != pitches[note + 1u]) {
                pitches[note] = candidate;
                approached = true;
            }
        }

        for (std::uint8_t note = 0; note < cell.count; ++note) {
            const int accent = (note == 0 ? 4 : 0) + (cell.gates[note] >= 4u ? 2 : 0);
            const int adjusted = velocity + accent + scoreRng.centered(3);
            addEvent(cell.steps[note], 0, cell.gates[note], Instrument::Lead,
                     pitches[note], adjusted);
        }
        lastLeadNote = pitches[last];
    }

    void generateLead(ArrangementSection section, const Harmony& harmony) noexcept {
        const std::size_t grid = meterIndex();
        const std::uint8_t phraseBar = static_cast<std::uint8_t>(barIndex & 7u);
        const LeadCell& cadenceCell = kCadenceCells[grid];
        if (section == ArrangementSection::Outro) {
            if ((barIndex & 1u) == 0u) {
                addLeadPhrase(cadenceCell, kFallingContour, false, lastLeadNote,
                              42, true, harmony);
            }
            return;
        }
        if (section == ArrangementSection::Breakdown) {
            // One held tone per bar keeps the tune present while the beat rests.
            const LeadCell held{1, {stepsPerBeat},
                                {static_cast<std::uint8_t>(stepsPerBeat * 2u)}};
            const bool close = (phraseBar & 3u) == 3u;
            addLeadPhrase(close ? cadenceCell : held, kFallingContour,
                          (phraseBar & 1u) != 0u, close ? lastLeadNote : leadHome,
                          46, close, harmony);
            return;
        }

        // Eight bars form A - A' - B - A'': the hook and its answer, the hook
        // again with a closing answer, a contrasting idea, then the hook and a
        // cadence. Each later eight-bar block shifts the register slightly.
        static constexpr std::int8_t homeShift[4] = {0, 2, 0, -2};
        const int home = leadHome + homeShift[(barIndex / 8u) & 3u];
        const int velocity = section == ArrangementSection::Intro ? 52 : 58;
        const LeadCell& hook = kHookCells[grid][hookCell];
        const LeadCell& answer = kAnswerCells[grid][answerCell];
        const LeadCell& secondAnswer = kAnswerCells[grid][(answerCell + 1u) % 3u];
        switch (phraseBar) {
        case 0:
        case 2:
        case 6:
            addLeadPhrase(hook, hookContour, false, home, velocity, false, harmony);
            break;
        case 1:
            addLeadPhrase(answer, kFallingContour, false, lastLeadNote,
                          velocity - 2, false, harmony);
            break;
        case 3:
            addLeadPhrase(answer, kFallingContour, false, lastLeadNote,
                          velocity - 2, true, harmony);
            break;
        case 4:
            addLeadPhrase(kContrastCells[grid][contrastCell], hookContour, true,
                          home + 4, velocity, false, harmony);
            break;
        case 5:
            addLeadPhrase(secondAnswer, kRisingContour, false, lastLeadNote,
                          velocity - 2, false, harmony);
            break;
        default:
            addLeadPhrase(cadenceCell, kFallingContour, false, lastLeadNote,
                          velocity - 3, true, harmony);
            break;
        }
    }

    // Swing delays some onsets, so end every melody gate at the next onset.
    void trimLeadGates() noexcept {
        Event* previous = nullptr;
        for (std::uint8_t index = 0; index < eventCount; ++index) {
            Event& event = events[index];
            if (event.instrument != Instrument::Lead) {
                continue;
            }
            if (previous != nullptr && previous->start + previous->duration > event.start) {
                previous->duration = static_cast<std::uint32_t>(
                    std::max<std::uint64_t>(1, event.start - previous->start));
            }
            previous = &event;
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
            // Percussion recycles percussion tails first. With none left it
            // may take the quietest already-released harmonic tail, but never
            // a note that is still held.
            for (Voice& voice : voices) {
                if (isDrum(voice.instrument) &&
                    (candidate == nullptr || betterVictim(voice, *candidate))) {
                    candidate = &voice;
                }
            }
            if (candidate == nullptr) {
                for (Voice& voice : voices) {
                    if (released(voice) &&
                        (candidate == nullptr || voice.envelope < candidate->envelope)) {
                        candidate = &voice;
                    }
                }
                if (candidate != nullptr) {
                    candidate->stealTail = candidate->lastOutput;
                    ++stolenVoices;
                    return candidate;
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
        if (event.instrument == Instrument::Lead) {
            // The melody is one line: a new note hands the previous one a
            // short tail (about 80 ms) so slow tones cannot stack up.
            constexpr float kLeadHandoffRelease = 0.997f;
            for (Voice& sounding : voices) {
                if (sounding.active && sounding.instrument == Instrument::Lead) {
                    sounding.stage = EnvelopeStage::Release;
                    sounding.release = std::min(sounding.release, kLeadHandoffRelease);
                }
            }
        }
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
            pumpDepth = 0.22f;
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
                signal = readSample(voice, pitchDrift) * 0.78f + tonal * 0.32f;
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
        if (!isDrum(voice.instrument)) {
            voice.phase += static_cast<std::uint32_t>(static_cast<std::int32_t>(
                static_cast<float>(voice.phaseIncrement) * pitchDrift));
        }
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
        // Softened hiss plus irregular record pops: about two a second, most
        // of them faint, each ringing out over a few samples.
        const float white = static_cast<float>(
            static_cast<std::int32_t>(textureRng.next32())) / 2147483648.0f;
        hissState += 0.35f * (white - hissState);
        const std::uint32_t draw = textureRng.next32();
        if ((draw & 0x3fffu) == 0u) {
            const float size = static_cast<float>((draw >> 14) & 0xffu) / 255.0f;
            const float pop = size * size * 0.085f * amount;
            crackle = (draw >> 31) != 0u ? -pop : pop;
        } else {
            crackle *= 0.8f;
        }
        return hissState * 0.012f * amount + crackle;
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
        snap.pitchDriftQ8 = static_cast<std::int16_t>(
            pitchDrift * kCentsPerRatio * 256.0f);
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
    case Mood::Sunny:
        return "Sunny";
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
    return static_cast<std::uint8_t>(config.mood) < kMoodCount &&
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

        state.wowPhase += kWowIncrement;
        state.flutterPhase += kFlutterIncrement;
        state.pitchDrift = phaseSine(state.wowPhase) * kWowDepth +
                           phaseSine(state.flutterPhase) * kFlutterDepth;

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

        // Each kick dips the pitched bed and lets it swell back over roughly
        // 200 ms. The gain itself is smoothed, so the dip has no edge; drums
        // and texture stay outside it.
        state.pumpDepth -= state.pumpDepth * 0.00016f;
        state.kickDuck += ((1.0f - state.pumpDepth) - state.kickDuck) * 0.006f;
        instrumentMix[static_cast<std::size_t>(Instrument::Keys)] *= state.kickDuck;
        instrumentMix[static_cast<std::size_t>(Instrument::Lead)] *= state.kickDuck;
        instrumentMix[static_cast<std::size_t>(Instrument::Bass)] *= state.kickDuck;

        float mix = 0.0f;
        for (float contribution : instrumentMix) {
            mix += contribution;
        }
        mix += state.renderTexture();

        const std::uint8_t active = state.activeVoiceCount();
        state.maxActiveVoices = std::max(state.maxActiveVoices, active);

        const float delayed = static_cast<float>(state.delay[state.delayIndex]) * kInvInt16;
        const float delayWrite = std::max(-0.98f, std::min(0.98f, mix + delayed * 0.16f));
        state.delay[state.delayIndex] = static_cast<std::int16_t>(delayWrite * 32767.0f);
        ++state.delayIndex;
        if (state.delayIndex == kDelayFrames) {
            state.delayIndex = 0;
        }
        mix += delayed * 0.105f;

        // Tape-style stage: a slightly biased soft saturation adds even
        // harmonics, then two gentle poles round the top off at 12 dB/octave.
        // The DC blocker below removes the offset the bias introduces.
        constexpr float kTapeBias = 0.06f;
        constexpr float kTapeBiasOutput = kTapeBias / (1.0f + 0.55f * kTapeBias);
        const float driven = mix * 1.18f + kTapeBias;
        mix = driven / (1.0f + 0.55f * std::fabs(driven)) - kTapeBiasOutput;
        const float toneCoefficient = state.current.mood == Mood::Night ? 0.24f :
                                      state.current.mood == Mood::Rainy ? 0.27f : 0.31f;
        state.toneState += toneCoefficient * (mix - state.toneState);
        state.toneState2 += toneCoefficient * (state.toneState - state.toneState2);
        const float dcBlocked = state.toneState2 - state.dcInput + 0.995f * state.dcOutput;
        state.dcInput = state.toneState2;
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
                                      state.transitionGain * 1.32f;
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
    const char prefix[] = "lofi6-";
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
    if (text == nullptr || std::strncmp(text, "lofi6-", 6) != 0) {
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
    constexpr char kMaximumMoodCharacter =
        static_cast<char>('0' + kMoodCount - 1u);
    if (*cursor < '0' || *cursor > kMaximumMoodCharacter) {
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
