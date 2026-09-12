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
constexpr std::size_t kMaxBarEvents = 48;
constexpr std::size_t kDelayFrames = 2720; // 85 ms at 32 kHz.
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
    return config;
}

float fastSin(std::uint32_t phase) noexcept {
    const float x = static_cast<float>(static_cast<std::int32_t>(phase)) /
                    2147483648.0f;
    float y = 4.0f * x * (1.0f - std::fabs(x));
    y += 0.225f * (y * std::fabs(y) - y);
    return y;
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

} // namespace

struct Engine::Impl {
    Config current{};
    Config pendingConfig{};
    bool pendingChange = false;
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
    float duck = 1.0f;
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
    std::uint8_t swingPercent = 56;
    std::uint8_t keyPitchClass = 0;
    bool minorSession = false;
    std::uint8_t progressionDegrees[4]{};
    ChordShape progressionShapes[4]{};
    std::uint8_t chordVoicings[4][4]{};
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
    std::uint8_t maxActiveVoices = 0;

    bool automaticTransitionDue() const noexcept {
        return barIndex + 1 >= sessionBars;
    }

    bool pendingTransitionDue() const noexcept {
        return pendingChange && barIndex >= pendingApplyBar;
    }

    std::uint64_t barStartSample() const noexcept {
        return barStartQ32 >> 32;
    }

    std::uint64_t barEndSample() const noexcept {
        return barEndQ32 >> 32;
    }

    std::uint64_t stepSample(std::uint8_t step) const noexcept {
        std::uint64_t valueQ32 = barStartQ32 + stepQ32 * step;
        if ((step & 3u) == 2u) {
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

    void clearAudioState() noexcept {
        for (Voice& voice : voices) {
            voice = Voice{};
        }
        std::fill(std::begin(delay), std::end(delay), 0);
        delayIndex = 0;
        toneState = 0.0f;
        dcInput = 0.0f;
        dcOutput = 0.0f;
        duck = 1.0f;
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
                {kMinor7, kMinor7, kMajor7, kDominant7},
                {kMinor7, kMajor7, kMinor7, kMinor7},
            };
            const std::size_t selection = scoreRng.bounded(3);
            std::copy(std::begin(degrees[selection]), std::end(degrees[selection]),
                      progressionDegrees);
            std::copy(std::begin(shapes[selection]), std::end(shapes[selection]),
                      progressionShapes);
        }
    }

    void buildVoicings() noexcept {
        std::uint8_t previous[4] = {57, 60, 64, 67};
        for (std::size_t chord = 0; chord < 4; ++chord) {
            const int root = 60 + keyPitchClass + progressionDegrees[chord];
            const ChordShape shape = progressionShapes[chord];
            const int raw[4] = {root, root + shape.third, root + 7,
                                root + shape.seventh};
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
                        score += std::abs(pitch - static_cast<int>(previous[voice]));
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
            std::copy(std::begin(best), std::end(best), chordVoicings[chord]);
            std::copy(std::begin(best), std::end(best), previous);
        }
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
            bpm = static_cast<std::uint16_t>(76 + scoreRng.bounded(9));
            break;
        case Mood::Rainy:
            bpm = static_cast<std::uint16_t>(68 + scoreRng.bounded(9));
            break;
        case Mood::Night:
            bpm = static_cast<std::uint16_t>(72 + scoreRng.bounded(9));
            break;
        }
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
        buildVoicings();
        buildMotif();

        stepQ32 = (static_cast<std::uint64_t>(kMusicSampleRate) * 60u << 32) /
                  (static_cast<std::uint64_t>(bpm) * 4u);
        sessionSample = 0;
        barIndex = 0;
        barStartQ32 = 0;
        barEndQ32 = stepQ32 * 16u;
        eventCount = 0;
        nextEvent = 0;
        pendingChange = false;
        pendingApplyBar = 0;
        transitionGain = resetCounters ? 1.0f : 0.0f;
        transitionFadeIn = resetCounters ? 0 : kFadeFrames;
        voiceSerial = 0;
        clearAudioState();
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
        event.duration = durationSamples(durationSteps);
        event.instrument = instrument;
        event.note = static_cast<std::uint8_t>(std::max(0, std::min(127, note)));
        event.velocity = static_cast<std::uint8_t>(std::max(1, std::min(127, velocity)));
        insertEvent(event);
    }

    void generateBar() noexcept {
        eventCount = 0;
        nextEvent = 0;
        const ArrangementSection section = sectionForBar(barIndex, sessionBars);
        const std::size_t chord = barIndex & 3u;
        const int timingRadius = current.mood == Mood::Rainy ? 34 : 48;

        const int chordVelocity = section == ArrangementSection::Breakdown ? 58 :
                                  section == ArrangementSection::Intro ?
                                      62 + static_cast<int>(barIndex) * 2 : 72;
        const std::uint8_t chordLength = section == ArrangementSection::Intro ? 13 : 14;
        for (int voice = 0; voice < 4; ++voice) {
            const int strum = voice * static_cast<int>(20 + scoreRng.bounded(22));
            addEvent(0, strum, chordLength, Instrument::Keys,
                     chordVoicings[chord][voice],
                     chordVelocity + scoreRng.centered(5));
        }

        const int bassRoot = 36 + keyPitchClass + progressionDegrees[chord];
        addEvent(0, 52 + scoreRng.centered(timingRadius), 6, Instrument::Bass,
                 bassRoot,
                 (section == ArrangementSection::Intro ? 68 : 74) +
                     scoreRng.centered(5));
        const bool flowingBass = section == ArrangementSection::Intro ||
            section == ArrangementSection::Groove ||
            section == ArrangementSection::Melody ||
            section == ArrangementSection::Return;
        if (flowingBass) {
            const int second = scoreRng.chance(3, 4) ? bassRoot + 7 : bassRoot;
            addEvent(8, 34 + scoreRng.centered(timingRadius), 5, Instrument::Bass,
                     second,
                     (section == ArrangementSection::Intro ? 57 : 65) +
                         scoreRng.centered(5));
            if ((section != ArrangementSection::Intro || barIndex >= 2) &&
                scoreRng.chance(1, 3)) {
                const std::size_t nextChord = (chord + 1) & 3u;
                const int target = 36 + keyPitchClass + progressionDegrees[nextChord];
                addEvent(14, scoreRng.centered(28), 2, Instrument::Bass,
                         target - 1, 48 + scoreRng.centered(4));
            }
        }

        const bool fullDrums = section == ArrangementSection::Groove ||
                               section == ArrangementSection::Melody ||
                               section == ArrangementSection::Return;
        if (section == ArrangementSection::Intro) {
            // The opening is already a complete, quiet pocket. Later sections
            // add weight and subdivisions instead of waiting to reveal the beat.
            addEvent(0, scoreRng.centered(14), 3, Instrument::Kick, 36,
                     69 + scoreRng.centered(4));
            addEvent(8, scoreRng.centered(18), 2, Instrument::Kick, 36,
                     55 + scoreRng.centered(5));
            if (barIndex >= 2) {
                addEvent((barIndex & 1u) == 0u ? 10 : 6, scoreRng.centered(18), 2,
                         Instrument::Kick, 36, 43 + scoreRng.centered(4));
            }
            addEvent(4, scoreRng.centered(22), 2, Instrument::Snare, 38,
                     49 + scoreRng.centered(5));
            addEvent(12, scoreRng.centered(24), 2, Instrument::Snare, 38,
                     53 + scoreRng.centered(5));
            for (std::uint8_t step = 2; step < 16; step += 4) {
                addEvent(step, scoreRng.centered(16), 1, Instrument::Hat, 42,
                         ((step & 7u) == 2u ? 31 : 37) + scoreRng.centered(4));
            }
            if ((barIndex & 1u) != 0u) {
                addEvent(15, scoreRng.centered(10), 1, Instrument::Rim, 37,
                         35 + scoreRng.centered(4));
            }
        } else if (fullDrums) {
            addEvent(0, scoreRng.centered(18), 3, Instrument::Kick, 36,
                     91 + scoreRng.centered(5));
            addEvent(8, scoreRng.centered(22), 3, Instrument::Kick, 36,
                     77 + scoreRng.centered(6));
            if (scoreRng.chance(1, 3)) {
                addEvent(scoreRng.chance(1, 2) ? 6 : 11, scoreRng.centered(24), 2,
                         Instrument::Kick, 36, 52 + scoreRng.centered(5));
            }
            addEvent(4, scoreRng.centered(26), 3, Instrument::Snare, 38,
                     73 + scoreRng.centered(7));
            addEvent(12, scoreRng.centered(28), 3, Instrument::Snare, 38,
                     79 + scoreRng.centered(7));
            for (std::uint8_t step = 0; step < 16; step += 2) {
                if ((step == 0 || step == 8) && scoreRng.chance(1, 3)) {
                    continue;
                }
                addEvent(step, scoreRng.centered(22), 1, Instrument::Hat, 42,
                         (step & 3u) == 0u ? 39 + scoreRng.centered(5) :
                                            48 + scoreRng.centered(5));
            }
            if ((barIndex & 3u) == 3u && scoreRng.chance(1, 2)) {
                addEvent(15, scoreRng.centered(12), 1, Instrument::Rim, 37,
                         50 + scoreRng.centered(5));
            }
        } else if (section == ArrangementSection::Breakdown) {
            addEvent(4, scoreRng.centered(22), 2, Instrument::Rim, 37,
                     42 + scoreRng.centered(4));
            addEvent(12, scoreRng.centered(22), 2, Instrument::Rim, 37,
                     46 + scoreRng.centered(4));
            for (std::uint8_t step = 2; step < 16; step += 4) {
                addEvent(step, scoreRng.centered(18), 1, Instrument::Hat, 42,
                         31 + scoreRng.centered(4));
            }
        } else if (section == ArrangementSection::Outro) {
            addEvent(4, scoreRng.centered(20), 2, Instrument::Rim, 37,
                     38 + scoreRng.centered(4));
            addEvent(12, scoreRng.centered(20), 2, Instrument::Rim, 37,
                     34 + scoreRng.centered(4));
        }

        const auto addMotifEvent = [this, chord](std::uint8_t step,
                                                 std::uint8_t motifSlot,
                                                 std::uint8_t fallbackVoice,
                                                 std::uint8_t duration,
                                                 int velocity) noexcept {
            const std::int8_t selected = motif[motifSlot & 7u];
            const std::size_t chordVoice = selected >= 0 ?
                static_cast<std::size_t>(selected) :
                static_cast<std::size_t>(fallbackVoice & 3u);
            int note = chordVoicings[chord][chordVoice] + 12;
            while (note > 82) {
                note -= 12;
            }
            addEvent(step, scoreRng.centered(30), duration, Instrument::Lead,
                     note, velocity + scoreRng.centered(5));
        };

        if (section == ArrangementSection::Intro) {
            // A small call and response identifies the session before bar two.
            if (barIndex == 0) {
                addMotifEvent(6, 2, 1, 2, 40);
                addMotifEvent(10, 5, 2, 2, 43);
                addMotifEvent(14, 7, 0, 2, 38);
            } else if (barIndex == 1) {
                addMotifEvent(4, 1, 2, 2, 39);
                addMotifEvent(8, 4, 0, 3, 44);
                addMotifEvent(12, 6, 1, 2, 40);
            } else if (barIndex == 2) {
                addMotifEvent(10, 5, 2, 2, 39);
                addMotifEvent(14, 7, 0, 2, 36);
            } else {
                addMotifEvent(2, 0, 0, 2, 38);
                addMotifEvent(6, 2, 1, 2, 41);
                addMotifEvent(10, 5, 2, 2, 43);
                addMotifEvent(14, 6, 0, 2, 37);
            }
        }

        const bool melodyLayer = section == ArrangementSection::Melody ||
            (section == ArrangementSection::Groove && (barIndex & 1u) != 0u) ||
            (section == ArrangementSection::Return && (barIndex & 1u) == 0u);
        if (melodyLayer) {
            for (std::uint8_t slot = 0; slot < 8; ++slot) {
                std::int8_t motifVoice = motif[slot];
                if (motifVoice < 0 || ((barIndex & 3u) == 3u && slot == 7)) {
                    continue;
                }
                if ((barIndex & 3u) == 3u && slot >= 6) {
                    motifVoice = 0;
                }
                int note = chordVoicings[chord][static_cast<std::size_t>(motifVoice)] + 12;
                while (note > 82) {
                    note -= 12;
                }
                addEvent(static_cast<std::uint8_t>(slot * 2), scoreRng.centered(36),
                         scoreRng.chance(1, 3) ? 3 : 2, Instrument::Lead, note,
                         46 + scoreRng.centered(8));
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
        if (hasConfig) {
            pendingConfig = requestedConfig;
            pendingChange = true;
        }
        if (hasNext) {
            if (!pendingChange) {
                pendingConfig = current;
            }
            pendingChange = true;
            // Configuration and next-session requests are independent. Keep
            // all config fields, then let the later request choose the seed.
            if (!hasConfig || nextSequence > configSequence) {
                pendingConfig.seed = nextExplicit ? requestedSeed :
                    mix64(pendingConfig.seed ^ UINT64_C(0x4e4558545f534553));
            }
        }
        if (!hadPendingChange && pendingChange) {
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
            voice->attackIncrement = 1.0f / 224.0f;
            voice->sustain = 0.23f;
            voice->decay = current.mood == Mood::Rainy ? 0.99988f : 0.99984f;
            voice->release = 0.99972f;
            voice->gain = 0.185f;
            break;
        case Instrument::Bass:
            voice->attackIncrement = 1.0f / 112.0f;
            voice->sustain = 0.54f;
            voice->decay = 0.99955f;
            voice->release = 0.99885f;
            voice->gain = 0.25f;
            break;
        case Instrument::Lead:
            voice->attackIncrement = 1.0f / 180.0f;
            voice->sustain = 0.16f;
            voice->decay = 0.99968f;
            voice->release = 0.9989f;
            voice->gain = 0.14f;
            break;
        case Instrument::Kick:
            voice->attackIncrement = 1.0f;
            voice->sustain = 0.0f;
            voice->decay = 0.9989f;
            voice->release = 0.991f;
            voice->gain = 0.43f;
            voice->phaseIncrement = static_cast<std::uint32_t>(112.0f * kPhaseScale);
            duck = std::min(duck, 0.91f);
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
                voice->sample = keySampleFor(event.note);
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
        const float fundamental = fastSin(voice.phase);
        switch (voice.instrument) {
        case Instrument::Keys:
        case Instrument::Lead: {
            const float ageDecay = 1.0f /
                (1.0f + static_cast<float>(voice.age) * 0.000055f);
            const float electric = fundamental * 0.72f +
                                   fastSin(voice.phase * 2u + (voice.phase >> 5)) *
                                       0.19f * ageDecay +
                                   fastSin(voice.phase * 3u) * 0.09f * ageDecay;
            if (current.soundEngine == SoundEngine::Hybrid && voice.sampleActive) {
                const float sampleValue = readSample(voice);
                signal = sampleValue * 0.78f + electric * 0.32f;
            } else {
                signal = electric;
            }
            break;
        }
        case Instrument::Bass: {
            const float roundedTriangle = fundamental * 0.78f +
                                          fastSin(voice.phase * 2u) * 0.12f;
            voice.filter += 0.075f * (roundedTriangle - voice.filter);
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
        if (voice.stage == EnvelopeStage::Release && voice.envelope < 0.00035f &&
            !voice.sampleActive) {
            voice.active = false;
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
        const bool transition = pendingTransitionDue() || automaticTransitionDue();
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
        barEndQ32 += stepQ32 * 16u;
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
        const std::uint64_t start = barStartSample();
        const std::uint64_t end = std::max<std::uint64_t>(start + 1, barEndSample());
        const std::uint64_t within = sessionSample > start ?
            std::min<std::uint64_t>(sessionSample - start, end - start) : 0;
        snap.barPhaseQ16 = static_cast<std::uint16_t>(
            within * UINT64_C(65535) / (end - start));
        snap.sixteenth = static_cast<std::uint8_t>(
            std::min<std::uint64_t>(15, within * 16u / (end - start)));
        snap.beat = static_cast<std::uint8_t>(snap.sixteenth / 4u);
        snap.section = sectionForBar(barIndex, sessionBars);
        snap.paused = pauseTarget;
        snap.changePending = pendingChange || automaticTransitionDue();
        snap.activeVoices = activeVoiceCount();
        snap.voiceCapacity = kMusicVoiceCapacity;
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

bool validConfig(const Config& config) noexcept {
    return static_cast<std::uint8_t>(config.mood) <= static_cast<std::uint8_t>(Mood::Night) &&
           static_cast<std::uint8_t>(config.soundEngine) <=
               static_cast<std::uint8_t>(SoundEngine::Hybrid);
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
        float mix = 0.0f;
        for (Voice& voice : state.voices) {
            mix += state.renderVoice(voice);
        }
        mix += state.renderTexture();

        const std::uint8_t active = state.activeVoiceCount();
        state.maxActiveVoices = std::max(state.maxActiveVoices, active);
        state.duck += (1.0f - state.duck) * 0.00042f;
        mix *= state.duck;

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

        const bool wantsTransition = state.pendingTransitionDue() ||
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
            }
        } else {
            state.pauseGain = std::min(1.0f, state.pauseGain + 1.0f / kFadeFrames);
        }

        const float volume = static_cast<float>(state.current.volume) / 100.0f;
        const float master = volume * volume * 0.94f;
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

namespace {

char hexDigit(std::uint8_t value) noexcept {
    return value < 10 ? static_cast<char>('0' + value) :
                        static_cast<char>('a' + value - 10);
}

bool parseDecimal(const char*& cursor, std::uint8_t& value) noexcept {
    if (*cursor < '0' || *cursor > '9') {
        return false;
    }
    unsigned parsed = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        parsed = parsed * 10u + static_cast<unsigned>(*cursor - '0');
        if (parsed > 100u) {
            return false;
        }
        ++cursor;
    }
    value = static_cast<std::uint8_t>(parsed);
    return true;
}

void appendDecimal(char*& cursor, std::uint8_t value) noexcept {
    if (value >= 100) {
        *cursor++ = '1';
        *cursor++ = '0';
        *cursor++ = '0';
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
    const char prefix[] = "lofi2-";
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
    if (text == nullptr || std::strncmp(text, "lofi2-", 6) != 0) {
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
    std::uint8_t volume = 0;
    if (!parseDecimal(cursor, volume) || *cursor++ != '-') {
        return false;
    }
    std::uint8_t texture = 0;
    if (!parseDecimal(cursor, texture) || *cursor != '\0') {
        return false;
    }
    output.seed = seed;
    output.mood = mood;
    output.soundEngine = soundEngine;
    output.volume = volume;
    output.texture = texture;
    return true;
}

} // namespace lofi
