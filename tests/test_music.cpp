#include "lofi/music.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": "  \
                      << #condition << '\n';                                           \
            std::exit(1);                                                              \
        }                                                                              \
    } while (false)

using lofi::Config;
using lofi::Engine;
using lofi::MusicMeter;
using lofi::Mood;
using lofi::SoundEngine;

bool sameConfig(const Config& left, const Config& right) {
    return left.seed == right.seed && left.mood == right.mood &&
           left.soundEngine == right.soundEngine && left.volume == right.volume &&
           left.texture == right.texture && left.bpm == right.bpm &&
           left.meter == right.meter && left.keysTone == right.keysTone &&
           left.leadTone == right.leadTone && left.bassTone == right.bassTone;
}

std::vector<std::int16_t> renderFrames(Engine& engine, std::size_t total,
                                       std::size_t blockSize) {
    std::vector<std::int16_t> result(total);
    std::size_t offset = 0;
    while (offset < result.size()) {
        const std::size_t count = std::min(blockSize, result.size() - offset);
        engine.render(result.data() + offset, count);
        offset += count;
    }
    return result;
}

std::uint64_t energy(const std::vector<std::int16_t>& audio) {
    std::uint64_t total = 0;
    for (const std::int16_t sample : audio) {
        total += static_cast<std::uint64_t>(std::abs(static_cast<int>(sample)));
    }
    return total;
}

std::uint64_t pcmHash(const std::vector<std::int16_t>& audio) {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const std::int16_t sample : audio) {
        const std::uint16_t bits = static_cast<std::uint16_t>(sample);
        hash = (hash ^ static_cast<std::uint8_t>(bits)) * UINT64_C(1099511628211);
        hash = (hash ^ static_cast<std::uint8_t>(bits >> 8)) * UINT64_C(1099511628211);
    }
    return hash;
}

bool sameScoreBar(const lofi::ScoreBar& left, const lofi::ScoreBar& right) {
    if (left.seed != right.seed || left.bar != right.bar || left.bpm != right.bpm ||
        left.meterNumerator != right.meterNumerator ||
        left.meterDenominator != right.meterDenominator ||
        left.beatsPerBar != right.beatsPerBar ||
        left.stepsPerBar != right.stepsPerBar ||
        left.stepsPerBeat != right.stepsPerBeat ||
        left.barStartSample != right.barStartSample ||
        left.barEndSample != right.barEndSample ||
        left.keyPitchClass != right.keyPitchClass || left.minor != right.minor ||
        left.chordRoot != right.chordRoot || left.noteCount != right.noteCount ||
        !std::equal(std::begin(left.chordNotes), std::end(left.chordNotes),
                    std::begin(right.chordNotes))) {
        return false;
    }
    for (std::size_t index = 0; index < left.noteCount; ++index) {
        const lofi::ScoreNote& a = left.notes[index];
        const lofi::ScoreNote& b = right.notes[index];
        if (a.startSample != b.startSample || a.durationSamples != b.durationSamples ||
            a.instrument != b.instrument || a.note != b.note ||
            a.velocity != b.velocity) {
            return false;
        }
    }
    return true;
}

bool sameScoreContent(const lofi::ScoreBar& left, const lofi::ScoreBar& right) {
    if (left.seed != right.seed || left.bar != right.bar ||
        left.meterNumerator != right.meterNumerator ||
        left.meterDenominator != right.meterDenominator ||
        left.beatsPerBar != right.beatsPerBar ||
        left.stepsPerBar != right.stepsPerBar ||
        left.stepsPerBeat != right.stepsPerBeat ||
        left.keyPitchClass != right.keyPitchClass || left.minor != right.minor ||
        left.chordRoot != right.chordRoot || left.noteCount != right.noteCount ||
        !std::equal(std::begin(left.chordNotes), std::end(left.chordNotes),
                    std::begin(right.chordNotes))) {
        return false;
    }
    for (std::size_t index = 0; index < left.noteCount; ++index) {
        const lofi::ScoreNote& a = left.notes[index];
        const lofi::ScoreNote& b = right.notes[index];
        if (a.instrument != b.instrument || a.note != b.note ||
            a.velocity != b.velocity) {
            return false;
        }
    }
    return true;
}

bool scaleContains(const lofi::ScoreBar& bar, std::uint8_t note) {
    static constexpr std::uint8_t major[] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr std::uint8_t minor[] = {0, 2, 3, 5, 7, 8, 10};
    const std::uint8_t relative = static_cast<std::uint8_t>(
        (note + 12u - bar.keyPitchClass) % 12u);
    const std::uint8_t* scale = bar.minor ? minor : major;
    return std::find(scale, scale + 7, relative) != scale + 7;
}

bool chordContains(const lofi::ScoreBar& bar, std::uint8_t note) {
    return std::any_of(std::begin(bar.chordNotes), std::end(bar.chordNotes),
                       [note](std::uint8_t chordNote) {
        return chordNote % 12u == note % 12u;
    });
}

std::uint64_t signatureValue(std::uint64_t hash, std::uint64_t value) {
    return (hash ^ value) * UINT64_C(1099511628211);
}

std::size_t distinctCount(const std::uint64_t* values, std::size_t count) {
    std::size_t distinct = 0;
    for (std::size_t index = 0; index < count; ++index) {
        bool seen = false;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            seen = seen || values[earlier] == values[index];
        }
        distinct += seen ? 0u : 1u;
    }
    return distinct;
}

void renderToNextBar(Engine& engine, std::uint32_t currentBar) {
    std::int16_t scratch[509]{};
    for (int attempt = 0; attempt < 1000 && engine.snapshot().bar == currentBar;
         ++attempt) {
        engine.render(scratch, std::size(scratch));
    }
    CHECK(engine.snapshot().bar == currentBar + 1u);
}

void renderUntilSeed(Engine& engine, std::uint64_t seed) {
    std::vector<std::int16_t> block(509);
    for (int attempt = 0; attempt < 1000 && engine.config().seed != seed; ++attempt) {
        engine.render(block.data(), block.size());
    }
    CHECK(engine.config().seed == seed);
}

void renderUntilTransition(Engine& engine, std::uint32_t transitions) {
    std::int16_t scratch[509]{};
    for (int attempt = 0;
         attempt < 1000 && engine.diagnostics().sessionTransitions < transitions;
         ++attempt) {
        engine.render(scratch, std::size(scratch));
    }
    CHECK(engine.diagnostics().sessionTransitions == transitions);
}

std::uint64_t stepQ32(std::uint16_t bpm, std::uint8_t stepsPerBeat = 4) {
    return (static_cast<std::uint64_t>(lofi::kMusicSampleRate) * 60u << 32) /
           (static_cast<std::uint64_t>(bpm) * stepsPerBeat);
}

struct ExpectedMeter {
    std::uint8_t numerator;
    std::uint8_t denominator;
    std::uint8_t beats;
    std::uint8_t steps;
    std::uint8_t stepsPerBeat;
};

constexpr ExpectedMeter expectedMeter(MusicMeter meter) {
    return meter == MusicMeter::ThreeFour ? ExpectedMeter{3, 4, 3, 12, 4} :
           meter == MusicMeter::SixEight ? ExpectedMeter{6, 8, 2, 12, 6} :
                                           ExpectedMeter{4, 4, 4, 16, 4};
}

void checkMeter(const lofi::ScoreBar& bar, MusicMeter meter) {
    const ExpectedMeter expected = expectedMeter(meter);
    CHECK(bar.meterNumerator == expected.numerator);
    CHECK(bar.meterDenominator == expected.denominator);
    CHECK(bar.beatsPerBar == expected.beats);
    CHECK(bar.stepsPerBar == expected.steps);
    CHECK(bar.stepsPerBeat == expected.stepsPerBeat);
}

void checkMeter(const lofi::Snapshot& snapshot, MusicMeter meter) {
    const ExpectedMeter expected = expectedMeter(meter);
    CHECK(snapshot.meterNumerator == expected.numerator);
    CHECK(snapshot.meterDenominator == expected.denominator);
    CHECK(snapshot.beatsPerBar == expected.beats);
    CHECK(snapshot.stepsPerBar == expected.steps);
    CHECK(snapshot.stepsPerBeat == expected.stepsPerBeat);
    CHECK(snapshot.beat < expected.beats);
    CHECK(snapshot.sixteenth < expected.steps);
}

void checkScheduledBar(const lofi::ScoreBar& bar, std::uint64_t& previousBarEnd,
                       std::uint64_t& previousLeadEnd, int& previousLead,
                       int& previousBass, std::uint32_t& passingNotes) {
    CHECK(bar.noteCount > 0);
    CHECK(bar.noteCount <= lofi::kMusicMaxBarNotes);
    CHECK(bar.barEndSample > bar.barStartSample);
    if (bar.bar != 0) {
        CHECK(bar.barStartSample == previousBarEnd);
    }
    previousBarEnd = bar.barEndSample;

    const double stepSamples = static_cast<double>(lofi::kMusicSampleRate) * 60.0 /
        (static_cast<double>(bar.bpm) * bar.stepsPerBeat);
    const double expectedBarSamples = stepSamples * bar.stepsPerBar;
    CHECK(std::fabs(static_cast<double>(bar.barEndSample - bar.barStartSample) -
                    expectedBarSamples) <= 1.01);

    std::uint64_t previousStart = bar.barStartSample;
    for (std::size_t index = 0; index < bar.noteCount; ++index) {
        const lofi::ScoreNote& note = bar.notes[index];
        CHECK(note.startSample >= bar.barStartSample);
        CHECK(note.startSample < bar.barEndSample);
        CHECK(note.startSample >= previousStart);
        CHECK(note.durationSamples > 0);
        CHECK(note.durationSamples <= bar.barEndSample - note.startSample);
        previousStart = note.startSample;

        const bool pitched = note.instrument == lofi::MusicInstrument::Keys ||
            note.instrument == lofi::MusicInstrument::Bass ||
            note.instrument == lofi::MusicInstrument::Lead;
        if (pitched) {
            CHECK(scaleContains(bar, note.note));
        }
        if (note.instrument == lofi::MusicInstrument::Keys) {
            CHECK(chordContains(bar, note.note));
        } else if (note.instrument == lofi::MusicInstrument::Bass) {
            CHECK(note.note >= 32 && note.note <= 48);
            if (previousBass >= 0) {
                CHECK(std::abs(static_cast<int>(note.note) - previousBass) <= 7);
            }
            previousBass = note.note;
        } else if (note.instrument == lofi::MusicInstrument::Lead) {
            CHECK(note.note >= 64 && note.note <= 83);
            CHECK(note.startSample >= previousLeadEnd);
            previousLeadEnd = note.startSample + note.durationSamples;
            if (previousLead >= 0) {
                CHECK(std::abs(static_cast<int>(note.note) - previousLead) <= 8);
            }
            previousLead = note.note;

            const int step = static_cast<int>(std::llround(
                static_cast<double>(note.startSample - bar.barStartSample) /
                stepSamples));
            const int durationSteps = std::max(1, static_cast<int>(std::llround(
                static_cast<double>(note.durationSamples) / stepSamples)));
            const bool chordTone = chordContains(bar, note.note);
            if (step % bar.stepsPerBeat == 0 || durationSteps >= 3) {
                CHECK(chordTone);
            }
            if (!chordTone) {
                ++passingNotes;
                const lofi::ScoreNote* resolution = nullptr;
                for (std::size_t next = index + 1; next < bar.noteCount; ++next) {
                    if (bar.notes[next].instrument == lofi::MusicInstrument::Lead) {
                        resolution = &bar.notes[next];
                        break;
                    }
                }
                CHECK(resolution != nullptr);
                CHECK(chordContains(bar, resolution->note));
                const int distance = std::abs(static_cast<int>(resolution->note) -
                                              static_cast<int>(note.note));
                CHECK(distance == 1 || distance == 2);
                CHECK(resolution->startSample - note.startSample <=
                      static_cast<std::uint64_t>(stepSamples * 3.01));
            }
        }
    }
}

void testBpmValidationAndSanitizing() {
    CHECK(lofi::validBpm(0));
    CHECK(lofi::validBpm(lofi::kMusicMinBpm));
    CHECK(lofi::validBpm(lofi::kMusicMaxBpm));
    CHECK(!lofi::validBpm(1));
    CHECK(!lofi::validBpm(lofi::kMusicMinBpm - 1));
    CHECK(!lofi::validBpm(lofi::kMusicMaxBpm + 1));
    CHECK(!lofi::validBpm(UINT16_MAX));
    CHECK(lofi::validMeter(MusicMeter::Auto));
    CHECK(lofi::validMeter(MusicMeter::FourFour));
    CHECK(lofi::validMeter(MusicMeter::ThreeFour));
    CHECK(lofi::validMeter(MusicMeter::SixEight));
    CHECK(!lofi::validMeter(static_cast<MusicMeter>(4)));
    CHECK(std::strcmp(lofi::meterName(MusicMeter::Auto), "AUTO") == 0);
    CHECK(std::strcmp(lofi::meterName(MusicMeter::SixEight), "6/8") == 0);

    Config invalid{};
    invalid.bpm = 1;
    CHECK(!lofi::validConfig(invalid));
    Engine clampedLow(invalid);
    CHECK(clampedLow.config().bpm == lofi::kMusicMinBpm);
    CHECK(clampedLow.snapshot().bpm == lofi::kMusicMinBpm);

    invalid.bpm = UINT16_MAX;
    Engine clampedHigh(invalid);
    CHECK(clampedHigh.config().bpm == lofi::kMusicMaxBpm);
    CHECK(clampedHigh.snapshot().bpm == lofi::kMusicMaxBpm);

    Config rejected = clampedLow.config();
    rejected.bpm = 39;
    CHECK(!clampedLow.requestConfig(rejected));
    CHECK(!clampedLow.snapshot().changePending);

    invalid = Config{};
    invalid.meter = static_cast<MusicMeter>(99);
    CHECK(!lofi::validConfig(invalid));
    invalid = Config{};
    invalid.keysTone = static_cast<lofi::Tone>(99);
    CHECK(!lofi::validConfig(invalid));
    invalid = Config{};
    invalid.leadTone = static_cast<lofi::Tone>(99);
    CHECK(!lofi::validConfig(invalid));
    invalid = Config{};
    invalid.bassTone = static_cast<lofi::BassTone>(99);
    CHECK(!lofi::validConfig(invalid));
}

void testFavoriteRoundTrip() {
    Config source{};
    source.seed = UINT64_C(0xfedcba9876543210);
    source.mood = Mood::Night;
    source.soundEngine = SoundEngine::Hybrid;
    source.volume = 100;
    source.texture = 0;
    Engine engine(source);

    char code[Engine::kFavoriteCodeCapacity]{};
    const std::size_t length = engine.writeFavoriteCode(code, sizeof(code));
    CHECK(length > 0);
    CHECK(std::strcmp(code,
                      "lofi4-fedcba9876543210-2-1-100-0-0-0-0-3-0") == 0);

    Config parsed{};
    CHECK(Engine::parseFavoriteCode(code, parsed));
    CHECK(sameConfig(source, parsed));

    source.bpm = lofi::kMusicMaxBpm;
    source.meter = MusicMeter::SixEight;
    source.keysTone = lofi::Tone::SoftFlute;
    source.leadTone = lofi::Tone::NylonGuitar;
    source.bassTone = lofi::BassTone::Sub;
    Engine manual(source);
    CHECK(manual.writeFavoriteCode(code, sizeof(code)) > 0);
    CHECK(std::strcmp(code,
                      "lofi4-fedcba9876543210-2-1-100-0-180-3-5-2-2") == 0);
    CHECK(Engine::parseFavoriteCode(code, parsed));
    CHECK(sameConfig(source, parsed));

    CHECK(!Engine::parseFavoriteCode("lofi3-fedcba9876543210-2-1-100-0", parsed));
    CHECK(!Engine::parseFavoriteCode(
        "lofi4-fedcba987654321-2-1-100-0-0-0-0-3-0", parsed));
    CHECK(!Engine::parseFavoriteCode(
        "lofi4-fedcba9876543210-3-1-100-0-0-0-0-3-0", parsed));
    CHECK(!Engine::parseFavoriteCode(
        "lofi4-fedcba9876543210-2-1-101-0-0-0-0-3-0", parsed));
    CHECK(!Engine::parseFavoriteCode(
        "lofi4-fedcba9876543210-2-1-100-0-39-0-0-3-0", parsed));
    CHECK(!Engine::parseFavoriteCode(
        "lofi4-fedcba9876543210-2-1-100-0-181-0-0-3-0", parsed));
    CHECK(!Engine::parseFavoriteCode(
        "lofi4-fedcba9876543210-2-1-100-0-120-4-0-3-0", parsed));
    CHECK(!Engine::parseFavoriteCode(
        "lofi4-fedcba9876543210-2-1-100-0-120-1-6-3-0", parsed));
    CHECK(!Engine::parseFavoriteCode(
        "lofi4-fedcba9876543210-2-1-100-0-120-1-0-6-0", parsed));
    CHECK(!Engine::parseFavoriteCode(
        "lofi4-fedcba9876543210-2-1-100-0-120-1-0-3-3", parsed));

    char tooSmall[8] = {'x'};
    CHECK(engine.writeFavoriteCode(tooSmall, sizeof(tooSmall)) == 0);
    CHECK(tooSmall[0] == '\0');
}

void testReplayAndBlockDeterminism() {
    Config config{};
    config.seed = UINT64_C(0x0123456789abcdef);
    config.mood = Mood::Rainy;
    config.soundEngine = SoundEngine::Synth;
    config.texture = 23;

    Engine smallBlocks(config);
    Engine unevenBlocks(config);
    constexpr std::size_t frames = lofi::kMusicSampleRate * 14u;
    const std::vector<std::int16_t> reference = renderFrames(smallBlocks, frames, 64);
    const std::vector<std::int16_t> uneven = renderFrames(unevenBlocks, frames, 997);
    CHECK(reference == uneven);

    const lofi::Diagnostics left = smallBlocks.diagnostics();
    const lofi::Diagnostics right = unevenBlocks.diagnostics();
    CHECK(left.scoreEventHash == right.scoreEventHash);
    CHECK(left.scoreEventCount == right.scoreEventCount);
    CHECK(left.stolenVoices == right.stolenVoices);
    CHECK(left.droppedNoteEvents == right.droppedNoteEvents);
    CHECK(std::equal(std::begin(left.noteEventsByInstrument),
                     std::end(left.noteEventsByInstrument),
                     std::begin(right.noteEventsByInstrument)));
    CHECK(std::equal(std::begin(left.firstNoteSamples),
                     std::end(left.firstNoteSamples),
                     std::begin(right.firstNoteSamples)));
    CHECK(left.transportSample == frames);
    CHECK(left.renderedFrames == frames);
    CHECK(left.scoreEventCount > 20);
    CHECK(energy(reference) > UINT64_C(10000000));
    // Pin the schema-4 AUTO score and PCM stream, including its weighted meter
    // draw, so host and device builds cannot silently diverge.
    CHECK(smallBlocks.snapshot().bpm == 68);
    CHECK(smallBlocks.snapshot().meterNumerator == 4);
    CHECK(smallBlocks.snapshot().meterDenominator == 4);
    CHECK(left.scoreEventCount == 82);
    CHECK(left.scoreEventHash == UINT64_C(0xf0d5794f6cab7f00));
    CHECK(pcmHash(reference) == UINT64_C(0xdee9e5a94acbcedb));

    Config manualConfig = config;
    manualConfig.bpm = 151;
    Engine automaticScore(config);
    Engine manualScore(manualConfig);
    for (std::uint32_t bar = 0; bar < 8; ++bar) {
        CHECK(sameScoreContent(automaticScore.scoreBar(), manualScore.scoreBar()));
        renderToNextBar(automaticScore, bar);
        renderToNextBar(manualScore, bar);
    }
}

void testBackendIndependentScore() {
    Config synthConfig{};
    synthConfig.seed = UINT64_C(0xbadc0ffee0ddf00d);
    synthConfig.mood = Mood::Cozy;
    synthConfig.soundEngine = SoundEngine::Synth;
    synthConfig.texture = 0;
    synthConfig.bpm = 137;
    Config hybridConfig = synthConfig;
    hybridConfig.soundEngine = SoundEngine::Hybrid;

    Engine synth(synthConfig);
    Engine hybrid(hybridConfig);
    CHECK(sameScoreBar(synth.scoreBar(), hybrid.scoreBar()));
    constexpr std::size_t frames = lofi::kMusicSampleRate * 18u;
    const std::vector<std::int16_t> synthAudio = renderFrames(synth, frames, 511);
    const std::vector<std::int16_t> hybridAudio = renderFrames(hybrid, frames, 1024);
    const lofi::Diagnostics synthDiagnostics = synth.diagnostics();
    const lofi::Diagnostics hybridDiagnostics = hybrid.diagnostics();

    CHECK(synthDiagnostics.scoreEventCount == hybridDiagnostics.scoreEventCount);
    CHECK(synthDiagnostics.scoreEventHash == hybridDiagnostics.scoreEventHash);
    CHECK(std::equal(std::begin(synthDiagnostics.noteEventsByInstrument),
                     std::end(synthDiagnostics.noteEventsByInstrument),
                     std::begin(hybridDiagnostics.noteEventsByInstrument)));
    CHECK(synthDiagnostics.droppedNoteEvents == hybridDiagnostics.droppedNoteEvents);
    CHECK(synth.snapshot().bar == hybrid.snapshot().bar);
    CHECK(synth.snapshot().bpm == hybrid.snapshot().bpm);
    CHECK(sameScoreBar(synth.scoreBar(), hybrid.scoreBar()));
    CHECK(synthAudio != hybridAudio);
    CHECK(energy(synthAudio) > UINT64_C(10000000));
    CHECK(energy(hybridAudio) > UINT64_C(10000000));

    for (const std::uint16_t bpm : {lofi::kMusicMinBpm, lofi::kMusicMaxBpm}) {
        synthConfig.bpm = bpm;
        hybridConfig = synthConfig;
        hybridConfig.soundEngine = SoundEngine::Hybrid;
        Engine boundedSynth(synthConfig);
        Engine boundedHybrid(hybridConfig);
        renderFrames(boundedSynth, lofi::kMusicSampleRate * 5u, 251);
        renderFrames(boundedHybrid, lofi::kMusicSampleRate * 5u, 1024);
        CHECK(boundedSynth.snapshot().bpm == bpm);
        CHECK(boundedHybrid.snapshot().bpm == bpm);
        CHECK(boundedSynth.diagnostics().scoreEventHash ==
              boundedHybrid.diagnostics().scoreEventHash);
        CHECK(boundedSynth.diagnostics().scoreEventCount ==
              boundedHybrid.diagnostics().scoreEventCount);
        CHECK(sameScoreBar(boundedSynth.scoreBar(), boundedHybrid.scoreBar()));
    }
}

void testMeterSchedulingAndBackendDeterminism() {
    constexpr MusicMeter meters[] = {
        MusicMeter::FourFour,
        MusicMeter::ThreeFour,
        MusicMeter::SixEight,
    };
    constexpr std::uint16_t bpms[] = {
        lofi::kMusicMinBpm,
        120,
        lofi::kMusicMaxBpm,
    };

    for (std::size_t meterIndex = 0; meterIndex < std::size(meters); ++meterIndex) {
        for (std::size_t bpmIndex = 0; bpmIndex < std::size(bpms); ++bpmIndex) {
            Config synthConfig{};
            synthConfig.seed = UINT64_C(0x6d65746572000000) +
                meterIndex * 0x101u + bpmIndex;
            synthConfig.mood = static_cast<Mood>((meterIndex + bpmIndex) % 3u);
            synthConfig.soundEngine = SoundEngine::Synth;
            synthConfig.texture = 0;
            synthConfig.bpm = bpms[bpmIndex];
            synthConfig.meter = meters[meterIndex];
            Config hybridConfig = synthConfig;
            hybridConfig.soundEngine = SoundEngine::Hybrid;

            Engine synth(synthConfig);
            Engine hybrid(hybridConfig);
            std::uint64_t previousBarEnd = 0;
            std::uint64_t previousLeadEnd = 0;
            int previousLead = -1;
            int previousBass = -1;
            std::uint32_t passingNotes = 0;

            for (std::uint32_t barIndex = 0; barIndex < 8; ++barIndex) {
                const lofi::ScoreBar synthBar = synth.scoreBar();
                const lofi::ScoreBar hybridBar = hybrid.scoreBar();
                CHECK(sameScoreBar(synthBar, hybridBar));
                CHECK(synthBar.bar == barIndex);
                CHECK(synthBar.bpm == bpms[bpmIndex]);
                checkMeter(synthBar, meters[meterIndex]);
                checkMeter(synth.snapshot(), meters[meterIndex]);
                checkMeter(hybrid.snapshot(), meters[meterIndex]);
                for (const std::uint8_t chordNote : synthBar.chordNotes) {
                    CHECK(scaleContains(synthBar, chordNote));
                }
                checkScheduledBar(synthBar, previousBarEnd, previousLeadEnd,
                                  previousLead, previousBass, passingNotes);

                if (barIndex + 1u < 8u) {
                    renderToNextBar(synth, barIndex);
                    renderToNextBar(hybrid, barIndex);
                }
            }

            CHECK(passingNotes >= 2);
            const lofi::Diagnostics synthDiagnostics = synth.diagnostics();
            const lofi::Diagnostics hybridDiagnostics = hybrid.diagnostics();
            CHECK(synthDiagnostics.scoreEventCount ==
                  hybridDiagnostics.scoreEventCount);
            CHECK(synthDiagnostics.scoreEventHash == hybridDiagnostics.scoreEventHash);
            CHECK(synthDiagnostics.droppedNoteEvents == 0);
            CHECK(hybridDiagnostics.droppedNoteEvents == 0);
            CHECK(synthDiagnostics.maxActiveVoices <= lofi::kMusicVoiceCapacity);
            CHECK(hybridDiagnostics.maxActiveVoices <= lofi::kMusicVoiceCapacity);
        }
    }

    // AUTO consumes a deterministic meter draw and is weighted toward 4/4.
    std::uint16_t autoCounts[3]{};
    for (std::uint64_t seed = 1; seed <= 128; ++seed) {
        Config config{};
        config.seed = seed;
        config.meter = MusicMeter::Auto;
        Engine engine(config);
        const lofi::Snapshot snapshot = engine.snapshot();
        const std::size_t bucket = snapshot.meterNumerator == 4 ? 0u :
                                   snapshot.meterNumerator == 3 ? 1u : 2u;
        ++autoCounts[bucket];
    }
    CHECK(autoCounts[0] > autoCounts[1]);
    CHECK(autoCounts[0] > autoCounts[2]);
    CHECK(autoCounts[1] > 0);
    CHECK(autoCounts[2] > 0);
}

void testToneSelectionDoesNotChangeScore() {
    Config referenceConfig{};
    referenceConfig.seed = UINT64_C(0x746f6e655f73636f);
    referenceConfig.mood = Mood::Night;
    referenceConfig.soundEngine = SoundEngine::Synth;
    referenceConfig.texture = 0;
    referenceConfig.bpm = 120;
    referenceConfig.meter = MusicMeter::ThreeFour;
    const Engine referenceInitial(referenceConfig);
    const lofi::ScoreBar referenceBar = referenceInitial.scoreBar();

    for (std::uint8_t value = 0; value <= 5; ++value) {
        Config selected = referenceConfig;
        selected.keysTone = static_cast<lofi::Tone>(value);
        Engine engine(selected);
        CHECK(sameScoreBar(referenceBar, engine.scoreBar()));

        selected = referenceConfig;
        selected.leadTone = static_cast<lofi::Tone>(value);
        Engine leadEngine(selected);
        CHECK(sameScoreBar(referenceBar, leadEngine.scoreBar()));
    }
    for (std::uint8_t value = 0; value <= 2; ++value) {
        Config selected = referenceConfig;
        selected.bassTone = static_cast<lofi::BassTone>(value);
        Engine engine(selected);
        CHECK(sameScoreBar(referenceBar, engine.scoreBar()));
    }

    Config alternateConfig = referenceConfig;
    alternateConfig.keysTone = lofi::Tone::WarmPad;
    alternateConfig.leadTone = lofi::Tone::SoftFlute;
    alternateConfig.bassTone = lofi::BassTone::Upright;
    Engine reference(referenceConfig);
    Engine alternate(alternateConfig);
    for (std::uint32_t bar = 0; bar < 8; ++bar) {
        CHECK(sameScoreBar(reference.scoreBar(), alternate.scoreBar()));
        if (bar + 1u < 8u) {
            renderToNextBar(reference, bar);
            renderToNextBar(alternate, bar);
        }
    }
    CHECK(reference.diagnostics().scoreEventCount ==
          alternate.diagnostics().scoreEventCount);
    CHECK(reference.diagnostics().scoreEventHash ==
          alternate.diagnostics().scoreEventHash);

    Engine referenceAudio(referenceConfig);
    Engine alternateAudio(alternateConfig);
    constexpr std::size_t frames = lofi::kMusicSampleRate * 2u;
    CHECK(renderFrames(referenceAudio, frames, 257) !=
          renderFrames(alternateAudio, frames, 997));
}

void testInstrumentLevelTelemetry() {
    Config config{};
    config.seed = UINT64_C(0x6c6576656c735f31);
    config.mood = Mood::Cozy;
    config.soundEngine = SoundEngine::Synth;
    config.volume = 100;
    config.texture = 0;
    config.bpm = 120;
    config.meter = MusicMeter::FourFour;

    constexpr std::size_t frames = lofi::kMusicSampleRate * 6u;
    Engine smallBlocks(config);
    std::vector<std::int16_t> smallAudio(frames);
    std::uint8_t observed[lofi::kMusicInstrumentCount]{};
    for (std::size_t offset = 0; offset < frames;) {
        const std::size_t count = std::min<std::size_t>(257, frames - offset);
        smallBlocks.render(smallAudio.data() + offset, count);
        const lofi::Snapshot snapshot = smallBlocks.snapshot();
        for (std::size_t instrument = 0; instrument < lofi::kMusicInstrumentCount;
             ++instrument) {
            observed[instrument] = std::max(observed[instrument],
                                            snapshot.instrumentLevels[instrument]);
        }
        offset += count;
    }

    Engine largeBlocks(config);
    const std::vector<std::int16_t> largeAudio = renderFrames(largeBlocks, frames, 997);
    CHECK(smallAudio == largeAudio);
    const lofi::Snapshot smallSnapshot = smallBlocks.snapshot();
    const lofi::Snapshot largeSnapshot = largeBlocks.snapshot();
    CHECK(std::equal(std::begin(smallSnapshot.instrumentLevels),
                     std::end(smallSnapshot.instrumentLevels),
                     std::begin(largeSnapshot.instrumentLevels)));
    CHECK(std::all_of(std::begin(observed), std::end(observed),
                      [](std::uint8_t level) { return level > 0; }));

    smallBlocks.pause(true);
    std::int16_t pauseAudio[2048]{};
    smallBlocks.render(pauseAudio, std::size(pauseAudio));
    const lofi::Snapshot paused = smallBlocks.snapshot();
    CHECK(paused.paused);
    CHECK(std::all_of(std::begin(paused.instrumentLevels),
                      std::end(paused.instrumentLevels),
                      [](std::uint8_t level) { return level == 0; }));
}

void testOpeningHasEveryLayerAndHeadroom() {
    constexpr std::uint64_t seeds[] = {
        UINT64_C(0x000000000ca7cafe),
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xbadc0ffee0ddf00d),
    };
    for (std::size_t moodIndex = 0; moodIndex < 3; ++moodIndex) {
        lofi::Diagnostics byEngine[2]{};
        for (std::size_t engineIndex = 0; engineIndex < 2; ++engineIndex) {
            Config config{};
            config.seed = seeds[moodIndex];
            config.mood = static_cast<Mood>(moodIndex);
            config.soundEngine = static_cast<SoundEngine>(engineIndex);
            config.texture = 0;
            Engine engine(config);

            const lofi::Diagnostics initial = engine.diagnostics();
            CHECK(std::all_of(std::begin(initial.firstNoteSamples),
                              std::end(initial.firstNoteSamples), [](std::uint64_t sample) {
                return sample == lofi::kMusicNoNoteSample;
            }));

            const std::uint64_t firstBarEnd = engine.scoreBar().barEndSample;
            renderToNextBar(engine, 0);
            const std::uint64_t secondBarEnd = engine.scoreBar().barEndSample;
            renderToNextBar(engine, 1);

            const lofi::Diagnostics diagnostics = engine.diagnostics();
            const auto count = [&diagnostics](lofi::MusicInstrument instrument) {
                return diagnostics.noteEventsByInstrument[
                    static_cast<std::size_t>(instrument)];
            };
            const auto first = [&diagnostics](lofi::MusicInstrument instrument) {
                return diagnostics.firstNoteSamples[static_cast<std::size_t>(instrument)];
            };

            CHECK(count(lofi::MusicInstrument::Keys) >= 8);
            CHECK(count(lofi::MusicInstrument::Bass) >= 4);
            CHECK(count(lofi::MusicInstrument::Lead) >= 6);
            CHECK(count(lofi::MusicInstrument::Kick) >= 4);
            CHECK(count(lofi::MusicInstrument::Snare) >= 2);
            CHECK(count(lofi::MusicInstrument::Hat) >= 8);
            CHECK(count(lofi::MusicInstrument::Rim) >= 1);
            CHECK(first(lofi::MusicInstrument::Keys) < firstBarEnd);
            CHECK(first(lofi::MusicInstrument::Bass) < firstBarEnd);
            CHECK(first(lofi::MusicInstrument::Lead) < firstBarEnd);
            CHECK(first(lofi::MusicInstrument::Kick) < firstBarEnd);
            CHECK(first(lofi::MusicInstrument::Snare) < firstBarEnd);
            CHECK(first(lofi::MusicInstrument::Hat) < firstBarEnd);
            CHECK(first(lofi::MusicInstrument::Rim) < secondBarEnd);

            const std::uint32_t started = std::accumulate(
                std::begin(diagnostics.noteEventsByInstrument),
                std::end(diagnostics.noteEventsByInstrument), std::uint32_t{0});
            CHECK(started + diagnostics.droppedNoteEvents == diagnostics.scoreEventCount);
            CHECK(diagnostics.droppedNoteEvents == 0);
            CHECK(diagnostics.stolenVoices == 0);
            CHECK(diagnostics.maxActiveVoices >= 7);
            CHECK(diagnostics.maxActiveVoices <= lofi::kMusicVoiceCapacity);
            byEngine[engineIndex] = diagnostics;
        }

        CHECK(byEngine[0].scoreEventCount == byEngine[1].scoreEventCount);
        CHECK(byEngine[0].scoreEventHash == byEngine[1].scoreEventHash);
        CHECK(std::equal(std::begin(byEngine[0].noteEventsByInstrument),
                         std::end(byEngine[0].noteEventsByInstrument),
                         std::begin(byEngine[1].noteEventsByInstrument)));
        CHECK(std::equal(std::begin(byEngine[0].firstNoteSamples),
                         std::end(byEngine[0].firstNoteSamples),
                         std::begin(byEngine[1].firstNoteSamples)));
    }

    CHECK(lofi::kMusicVoiceCapacity == 12);
    CHECK(lofi::kMusicSchemaVersion == 4);
}

void testFastTempoVoicePressure() {
    for (std::uint8_t meterValue = 1; meterValue <= 3; ++meterValue) {
        for (std::size_t moodIndex = 0; moodIndex < 3; ++moodIndex) {
            lofi::Diagnostics byEngine[2]{};
            for (std::size_t engineIndex = 0; engineIndex < 2; ++engineIndex) {
                Config config{};
                config.seed = UINT64_C(0x000000000ca7cafe);
                config.mood = static_cast<Mood>(moodIndex);
                config.soundEngine = static_cast<SoundEngine>(engineIndex);
                config.bpm = lofi::kMusicMaxBpm;
                config.meter = static_cast<MusicMeter>(meterValue);
                Engine engine(config);
                renderFrames(engine, lofi::kMusicSampleRate * 60u,
                             347u + engineIndex * 164u);

                const lofi::Diagnostics diagnostics = engine.diagnostics();
                CHECK(diagnostics.droppedNoteEvents == 0);
                CHECK(diagnostics.maxActiveVoices <= lofi::kMusicVoiceCapacity);
                byEngine[engineIndex] = diagnostics;
            }
            CHECK(byEngine[0].scoreEventCount == byEngine[1].scoreEventCount);
            CHECK(byEngine[0].scoreEventHash == byEngine[1].scoreEventHash);
            CHECK(std::equal(std::begin(byEngine[0].noteEventsByInstrument),
                             std::end(byEngine[0].noteEventsByInstrument),
                             std::begin(byEngine[1].noteEventsByInstrument)));
        }
    }

    // Regression: short sampled tails used to occupy all twelve Hybrid slots
    // in this dense 6/8 palette and drop the incoming hat at bar 25.
    Config sampledTailRegression{};
    sampledTailRegression.seed = UINT64_C(0x000000000ca7cafe);
    sampledTailRegression.mood = Mood::Cozy;
    sampledTailRegression.soundEngine = SoundEngine::Hybrid;
    sampledTailRegression.bpm = lofi::kMusicMaxBpm;
    sampledTailRegression.meter = MusicMeter::SixEight;
    sampledTailRegression.keysTone = lofi::Tone::Vibraphone;
    sampledTailRegression.leadTone = lofi::Tone::ElectricPiano;
    sampledTailRegression.bassTone = lofi::BassTone::Round;
    Engine regression(sampledTailRegression);
    renderFrames(regression, lofi::kMusicSampleRate * 30u, 401);
    const lofi::Diagnostics regressionDiagnostics = regression.diagnostics();
    CHECK(regressionDiagnostics.scoreEventCount == 737);
    CHECK(regressionDiagnostics.scoreEventHash == UINT64_C(0x5a32afd229110c9b));
    CHECK(regressionDiagnostics.droppedNoteEvents == 0);
    CHECK(regressionDiagnostics.maxActiveVoices <= lofi::kMusicVoiceCapacity);
}

void testScheduledHarmonyMovementAndPhraseVariety() {
    constexpr std::uint64_t seeds[] = {
        UINT64_C(0x000000000ca7cafe),
        UINT64_C(0xbadc0ffee0ddf00d),
    };
    constexpr std::size_t kBars = 36;
    constexpr std::size_t kPhrases = kBars / 4;

    for (std::size_t moodIndex = 0; moodIndex < 3; ++moodIndex) {
        for (const std::uint64_t seed : seeds) {
            Config config{};
            config.seed = seed;
            config.mood = static_cast<Mood>(moodIndex);
            config.soundEngine = SoundEngine::Synth;
            config.texture = 0;
            config.meter = MusicMeter::FourFour;
            Engine engine(config);

            const lofi::Snapshot snapshotBefore = engine.snapshot();
            const lofi::Diagnostics diagnosticsBefore = engine.diagnostics();
            const lofi::ScoreBar firstCopy = engine.scoreBar();
            const lofi::ScoreBar secondCopy = engine.scoreBar();
            CHECK(sameScoreBar(firstCopy, secondCopy));
            CHECK(engine.snapshot().transportSample == snapshotBefore.transportSample);
            CHECK(engine.diagnostics().scoreEventHash == diagnosticsBefore.scoreEventHash);

            std::uint64_t harmonySignatures[kPhrases]{};
            std::uint64_t leadSignatures[kPhrases]{};
            std::uint64_t keysPatterns[kPhrases]{};
            std::uint64_t bassPatterns[kPhrases]{};
            std::uint64_t drumPatterns[kPhrases]{};
            for (std::size_t phrase = 0; phrase < kPhrases; ++phrase) {
                harmonySignatures[phrase] = UINT64_C(14695981039346656037);
                leadSignatures[phrase] = UINT64_C(14695981039346656037);
                keysPatterns[phrase] = UINT64_C(14695981039346656037);
                bassPatterns[phrase] = UINT64_C(14695981039346656037);
                drumPatterns[phrase] = UINT64_C(14695981039346656037);
            }

            int previousBass = -1;
            int previousLead = -1;
            std::uint8_t previousChord[4]{};
            std::uint64_t hookRhythm[2] = {
                UINT64_C(14695981039346656037),
                UINT64_C(14695981039346656037),
            };
            std::uint8_t hookNotes[2]{};
            std::uint32_t passingNotes = 0;
            std::uint32_t strongLeadNotes = 0;

            for (std::size_t expectedBar = 0; expectedBar < kBars; ++expectedBar) {
                const lofi::ScoreBar bar = engine.scoreBar();
                CHECK(bar.seed == seed);
                CHECK(bar.bar == expectedBar);
                CHECK(bar.noteCount <= lofi::kMusicMaxBarNotes);
                CHECK(bar.noteCount >= 5);
                CHECK(bar.chordRoot >= 32 && bar.chordRoot <= 48);
                CHECK(scaleContains(bar, bar.chordRoot));

                const std::uint64_t barStart = bar.barStartSample;
                const std::uint64_t barEnd = bar.barEndSample;
                const double stepSamples = static_cast<double>(lofi::kMusicSampleRate) *
                    60.0 / (static_cast<double>(bar.bpm) * bar.stepsPerBeat);
                const std::size_t phrase = expectedBar / 4u;
                harmonySignatures[phrase] = signatureValue(
                    harmonySignatures[phrase], expectedBar & 3u);
                harmonySignatures[phrase] = signatureValue(
                    harmonySignatures[phrase],
                    (bar.chordRoot + 12u - bar.keyPitchClass) % 12u);

                for (std::size_t voice = 0; voice < 4; ++voice) {
                    CHECK(scaleContains(bar, bar.chordNotes[voice]));
                    if (expectedBar != 0) {
                        CHECK(std::abs(static_cast<int>(bar.chordNotes[voice]) -
                                       static_cast<int>(previousChord[voice])) <= 12);
                    }
                    previousChord[voice] = bar.chordNotes[voice];
                }

                std::uint64_t previousStart = barStart;
                for (std::size_t index = 0; index < bar.noteCount; ++index) {
                    const lofi::ScoreNote& note = bar.notes[index];
                    CHECK(note.startSample >= barStart);
                    CHECK(note.startSample < barEnd);
                    CHECK(note.startSample >= previousStart);
                    CHECK(note.durationSamples > 0);
                    CHECK(note.velocity > 0 && note.velocity <= 127);
                    previousStart = note.startSample;

                    const int step = static_cast<int>(std::llround(
                        static_cast<double>(note.startSample - barStart) / stepSamples));
                    const int durationSteps = std::max(1, static_cast<int>(std::llround(
                        static_cast<double>(note.durationSamples) / stepSamples)));
                    const std::uint64_t rhythm = static_cast<std::uint64_t>(
                        (expectedBar & 3u) * 128u + static_cast<std::size_t>(step) * 8u +
                        static_cast<std::size_t>(durationSteps));
                    const bool pitched = note.instrument == lofi::MusicInstrument::Keys ||
                        note.instrument == lofi::MusicInstrument::Bass ||
                        note.instrument == lofi::MusicInstrument::Lead;
                    if (pitched) {
                        CHECK(scaleContains(bar, note.note));
                    }

                    if (note.instrument == lofi::MusicInstrument::Keys) {
                        CHECK(chordContains(bar, note.note));
                        keysPatterns[phrase] = signatureValue(keysPatterns[phrase], rhythm);
                    } else if (note.instrument == lofi::MusicInstrument::Bass) {
                        CHECK(note.note >= 32 && note.note <= 48);
                        if (previousBass >= 0) {
                            CHECK(std::abs(static_cast<int>(note.note) - previousBass) <= 7);
                        }
                        previousBass = note.note;
                        bassPatterns[phrase] = signatureValue(bassPatterns[phrase], rhythm);
                    } else if (note.instrument == lofi::MusicInstrument::Lead) {
                        CHECK(note.note >= 64 && note.note <= 83);
                        if (previousLead >= 0) {
                            CHECK(std::abs(static_cast<int>(note.note) - previousLead) <= 8);
                        }
                        previousLead = note.note;
                        const bool chordTone = chordContains(bar, note.note);
                        const bool strong = step % bar.stepsPerBeat == 0 ||
                                            durationSteps >= 3;
                        if (strong) {
                            ++strongLeadNotes;
                            CHECK(chordTone);
                        }
                        if (!chordTone) {
                            ++passingNotes;
                            const lofi::ScoreNote* resolution = nullptr;
                            for (std::size_t next = index + 1; next < bar.noteCount; ++next) {
                                if (bar.notes[next].instrument ==
                                    lofi::MusicInstrument::Lead) {
                                    resolution = &bar.notes[next];
                                    break;
                                }
                            }
                            CHECK(resolution != nullptr);
                            CHECK(chordContains(bar, resolution->note));
                            const int resolutionDistance = std::abs(
                                static_cast<int>(resolution->note) - note.note);
                            CHECK(resolutionDistance == 1 || resolutionDistance == 2);
                            CHECK(resolution->startSample - note.startSample <=
                                  static_cast<std::uint64_t>(stepSamples * 3.1));
                        }
                        leadSignatures[phrase] = signatureValue(
                            leadSignatures[phrase], rhythm);
                        leadSignatures[phrase] = signatureValue(
                            leadSignatures[phrase],
                            (note.note + 12u - bar.keyPitchClass) % 12u);
                        if (expectedBar == 0u || expectedBar == 8u) {
                            const std::size_t hook = expectedBar == 0u ? 0u : 1u;
                            hookRhythm[hook] = signatureValue(hookRhythm[hook],
                                static_cast<std::uint64_t>(step * 8 + durationSteps));
                            ++hookNotes[hook];
                        }
                    } else {
                        drumPatterns[phrase] = signatureValue(
                            drumPatterns[phrase],
                            rhythm * 8u + static_cast<std::uint8_t>(note.instrument));
                    }
                }

                if (expectedBar + 1u < kBars) {
                    renderToNextBar(engine, static_cast<std::uint32_t>(expectedBar));
                }
            }

            CHECK(harmonySignatures[0] == harmonySignatures[1]);
            CHECK(harmonySignatures[2] == harmonySignatures[3]);
            CHECK(harmonySignatures[4] == harmonySignatures[5]);
            CHECK(harmonySignatures[6] == harmonySignatures[7]);
            CHECK(harmonySignatures[0] == harmonySignatures[8]);
            CHECK(distinctCount(harmonySignatures, kPhrases) >= 4);
            CHECK(distinctCount(leadSignatures, kPhrases) >= 5);
            CHECK(distinctCount(keysPatterns, kPhrases) >= 3);
            CHECK(distinctCount(bassPatterns, kPhrases) >= 3);
            CHECK(distinctCount(drumPatterns, kPhrases) >= 4);
            CHECK(hookNotes[0] >= 4 && hookNotes[0] == hookNotes[1]);
            CHECK(hookRhythm[0] == hookRhythm[1]);
            CHECK(passingNotes >= 6);
            CHECK(strongLeadNotes >= 12);
            const lofi::Diagnostics diagnostics = engine.diagnostics();
            CHECK(diagnostics.droppedNoteEvents == 0);
            CHECK(diagnostics.maxActiveVoices <= lofi::kMusicVoiceCapacity);
        }
    }
}

void testBoundsAndRenderContract() {
    Config config{};
    config.seed = UINT64_C(0x7777444422221111);
    config.mood = Mood::Night;
    config.soundEngine = SoundEngine::Hybrid;
    config.volume = 100;
    config.texture = 100;
    Engine engine(config);

    constexpr std::size_t frames = lofi::kMusicSampleRate * 24u;
    std::vector<std::int16_t> guarded(frames + 2, static_cast<std::int16_t>(12345));
    engine.render(guarded.data() + 1, frames);
    CHECK(guarded.front() == 12345);
    CHECK(guarded.back() == 12345);
    const auto minMax = std::minmax_element(guarded.begin() + 1, guarded.end() - 1);
    CHECK(*minMax.first >= -32767);
    CHECK(*minMax.second <= 32767);
    CHECK(*minMax.first < -100);
    CHECK(*minMax.second > 100);

    const lofi::Diagnostics diagnostics = engine.diagnostics();
    CHECK(diagnostics.maxActiveVoices <= lofi::kMusicVoiceCapacity);
    CHECK(engine.snapshot().activeVoices <= lofi::kMusicVoiceCapacity);
    CHECK(engine.snapshot().voiceCapacity == lofi::kMusicVoiceCapacity);
    CHECK(diagnostics.absolutePeak > 1000);
    CHECK(diagnostics.absolutePeak <= 32767);
    CHECK(diagnostics.scoreEventCount > 30);
}

void testBarBoundaryTransition() {
    Config initial{};
    initial.seed = UINT64_C(0x1111222233334444);
    initial.mood = Mood::Cozy;
    initial.soundEngine = SoundEngine::Synth;
    Engine engine(initial);

    std::vector<std::int16_t> scratch(257);
    engine.render(scratch.data(), scratch.size());
    const std::uint16_t firstBpm = engine.snapshot().bpm;

    Config requested{};
    requested.seed = UINT64_C(0x9999aaaabbbbcccc);
    requested.mood = Mood::Rainy;
    requested.soundEngine = SoundEngine::Hybrid;
    requested.volume = 67;
    requested.texture = 9;
    requested.bpm = 120;
    requested.meter = MusicMeter::SixEight;
    requested.keysTone = lofi::Tone::FeltPiano;
    requested.leadTone = lofi::Tone::SoftFlute;
    requested.bassTone = lofi::BassTone::Upright;
    CHECK(engine.requestConfig(requested));

    bool changed = false;
    std::uint64_t sampleBeforeChange = 0;
    for (int block = 0; block < 1000; ++block) {
        sampleBeforeChange = engine.snapshot().transportSample;
        engine.render(scratch.data(), scratch.size());
        const Config active = engine.config();
        if (active.seed == requested.seed) {
            changed = true;
            break;
        }
        CHECK(active.seed == initial.seed);
    }
    CHECK(changed);
    const lofi::Snapshot after = engine.snapshot();
    CHECK(sameConfig(after.config, requested));
    CHECK(after.sessionSample <= scratch.size());
    CHECK(after.transportSample > sampleBeforeChange);
    CHECK(after.bpm == requested.bpm);
    checkMeter(after, requested.meter);
    CHECK(firstBpm >= 76 && firstBpm <= 84);
    CHECK(engine.diagnostics().sessionTransitions == 1);

    const std::uint64_t explicitSeed = UINT64_C(0x8000000000000042);
    engine.requestNext(explicitSeed);
    for (int block = 0; block < 1000 && engine.config().seed != explicitSeed; ++block) {
        engine.render(scratch.data(), scratch.size());
    }
    CHECK(engine.config().seed == explicitSeed);
    CHECK(engine.config().mood == requested.mood);
    CHECK(engine.config().soundEngine == requested.soundEngine);
    CHECK(engine.config().meter == requested.meter);
    checkMeter(engine.snapshot(), requested.meter);
    CHECK(engine.diagnostics().sessionTransitions == 2);
}

void testConfigAndNextRequestsMerge() {
    Config initial{};
    initial.seed = UINT64_C(0x1000000000000001);

    Config changed = initial;
    changed.seed = UINT64_C(0x2000000000000002);
    changed.mood = Mood::Night;
    changed.soundEngine = SoundEngine::Hybrid;
    changed.volume = 61;
    changed.texture = 7;
    changed.bpm = 105;
    const std::uint64_t laterSeed = UINT64_C(0x3000000000000003);

    // This also covers a config already consumed by the audio side before the
    // later next request arrives.
    Engine configThenNext(initial);
    CHECK(configThenNext.requestConfig(changed));
    std::int16_t one = 0;
    configThenNext.render(&one, 1);
    configThenNext.requestNext(laterSeed);
    configThenNext.render(&one, 1);
    renderUntilSeed(configThenNext, laterSeed);
    const Config merged = configThenNext.config();
    CHECK(merged.seed == laterSeed);
    CHECK(merged.mood == changed.mood);
    CHECK(merged.soundEngine == changed.soundEngine);
    CHECK(merged.volume == changed.volume);
    CHECK(merged.texture == changed.texture);
    CHECK(merged.bpm == changed.bpm);

    // When both intents are still in the mailbox, the later full config owns
    // the seed while the earlier next intent is retained as a transition.
    Engine nextThenConfig(initial);
    nextThenConfig.requestNext(laterSeed);
    CHECK(nextThenConfig.requestConfig(changed));
    nextThenConfig.render(&one, 1);
    renderUntilSeed(nextThenConfig, changed.seed);
    CHECK(sameConfig(nextThenConfig.config(), changed));

    // A tempo-only command that has already reached the audio owner remains
    // attached to a later Next request and therefore becomes one restart.
    Config tempo = initial;
    tempo.bpm = 123;
    Engine tempoThenNext(initial);
    CHECK(tempoThenNext.requestConfig(tempo));
    tempoThenNext.render(&one, 1);
    tempoThenNext.requestNext(laterSeed);
    tempoThenNext.render(&one, 1);
    renderUntilSeed(tempoThenNext, laterSeed);
    CHECK(tempoThenNext.config().bpm == tempo.bpm);
    CHECK(tempoThenNext.snapshot().bpm == tempo.bpm);
    CHECK(tempoThenNext.diagnostics().sessionTransitions == 1);

    // If Next arrives first, a later tempo command owns the latest seed but
    // cannot erase the already-requested session transition.
    Engine nextThenTempo(initial);
    nextThenTempo.requestNext(laterSeed);
    CHECK(nextThenTempo.requestConfig(tempo));
    nextThenTempo.render(&one, 1);
    renderUntilTransition(nextThenTempo, 1);
    CHECK(nextThenTempo.config().seed == tempo.seed);
    CHECK(nextThenTempo.config().bpm == tempo.bpm);

    // A same-seed Next remains an explicit restart even when the accompanying
    // Config is otherwise identical to the active favorite.
    Engine explicitRestart(initial);
    CHECK(explicitRestart.requestConfig(initial));
    explicitRestart.requestNext(initial.seed);
    explicitRestart.render(&one, 1);
    renderUntilTransition(explicitRestart, 1);
    CHECK(sameConfig(explicitRestart.config(), initial));
}

void testTempoOnlyChangesAtBarEdges() {
    Config initial{};
    initial.seed = UINT64_C(0x0123456789abcdef);
    initial.mood = Mood::Rainy;
    initial.soundEngine = SoundEngine::Synth;
    initial.texture = 23;
    initial.meter = MusicMeter::FourFour;

    Engine cancellation(initial);
    Config cancelledManual = initial;
    cancelledManual.bpm = 120;
    std::int16_t sample = 0;
    CHECK(cancellation.requestConfig(cancelledManual));
    cancellation.render(&sample, 1);
    CHECK(cancellation.snapshot().changePending);
    CHECK(cancellation.requestConfig(initial));
    cancellation.render(&sample, 1);
    CHECK(!cancellation.snapshot().changePending);
    renderToNextBar(cancellation, 0);
    CHECK(cancellation.config().bpm == 0);
    CHECK(cancellation.diagnostics().sessionTransitions == 0);

    Engine engine(initial);
    const std::uint16_t automaticBpm = engine.snapshot().bpm;
    CHECK(automaticBpm == 68);

    const std::uint64_t automaticStep = stepQ32(automaticBpm);
    const std::uint64_t firstBarEnd = (automaticStep * 16u) >> 32;
    renderFrames(engine, 733, 127);

    Config manual = initial;
    manual.bpm = lofi::kMusicMaxBpm;
    CHECK(engine.requestConfig(manual));
    engine.render(&sample, 1);
    CHECK(engine.snapshot().changePending);
    CHECK(engine.config().bpm == 0);

    const std::uint64_t beforeEdgeTransport = engine.snapshot().transportSample;
    const std::size_t toEdge = static_cast<std::size_t>(
        firstBarEnd - engine.snapshot().sessionSample);
    renderFrames(engine, toEdge, 997);
    CHECK(engine.snapshot().bar == 0);
    CHECK(engine.config().bpm == 0);
    engine.render(&sample, 1);

    const lofi::Snapshot manualSnapshot = engine.snapshot();
    CHECK(manualSnapshot.bar == 1);
    CHECK(manualSnapshot.sessionSample == firstBarEnd + 1u);
    CHECK(manualSnapshot.transportSample == beforeEdgeTransport + toEdge + 1u);
    CHECK(manualSnapshot.config.bpm == lofi::kMusicMaxBpm);
    CHECK(manualSnapshot.bpm == lofi::kMusicMaxBpm);
    CHECK(manualSnapshot.config.seed == initial.seed);
    CHECK(engine.diagnostics().sessionTransitions == 0);

    const lofi::ScoreBar manualBar = engine.scoreBar();
    CHECK(manualBar.bar == 1);
    CHECK(manualBar.bpm == lofi::kMusicMaxBpm);
    for (std::size_t note = 0; note < manualBar.noteCount; ++note) {
        CHECK(manualBar.notes[note].startSample >= firstBarEnd);
        if (note > 0) {
            CHECK(manualBar.notes[note - 1].startSample <=
                  manualBar.notes[note].startSample);
        }
    }

    // Returning to AUTO uses the automatic BPM draw retained from session
    // start. The score RNG and session continue from the next bar.
    Config automatic = manual;
    automatic.bpm = 0;
    CHECK(engine.requestConfig(automatic));
    const std::uint64_t secondBarEnd =
        (automaticStep * 16u + stepQ32(manual.bpm) * 16u) >> 32;
    const std::size_t toSecondEdge = static_cast<std::size_t>(
        secondBarEnd - engine.snapshot().sessionSample);
    renderFrames(engine, toSecondEdge, 383);
    CHECK(engine.snapshot().bar == 1);
    engine.render(&sample, 1);
    CHECK(engine.snapshot().bar == 2);
    CHECK(engine.config().bpm == 0);
    CHECK(engine.snapshot().bpm == automaticBpm);
    CHECK(engine.snapshot().sessionSample == secondBarEnd + 1u);
    CHECK(engine.diagnostics().sessionTransitions == 0);
    const lofi::ScoreBar autoBar = engine.scoreBar();
    for (std::size_t note = 0; note < autoBar.noteCount; ++note) {
        CHECK(autoBar.notes[note].startSample >= secondBarEnd);
    }
}

void testLateRequestWaitsForFullFade() {
    Config initial{};
    initial.seed = UINT64_C(0x4142434445464748);
    initial.mood = Mood::Cozy;
    initial.meter = MusicMeter::FourFour;
    Engine requested(initial);
    Engine reference(initial);
    const std::uint64_t stepQ32 =
        (static_cast<std::uint64_t>(lofi::kMusicSampleRate) * 60u << 32) /
        (static_cast<std::uint64_t>(requested.snapshot().bpm) * 4u);
    const std::uint64_t firstBarEnd = (stepQ32 * 16u) >> 32;
    const std::uint64_t secondBarEnd = (stepQ32 * 32u) >> 32;
    CHECK(firstBarEnd > 641);

    const std::vector<std::int16_t> beforeRequested =
        renderFrames(requested, static_cast<std::size_t>(firstBarEnd - 1), 997);
    const std::vector<std::int16_t> beforeReference =
        renderFrames(reference, static_cast<std::size_t>(firstBarEnd - 1), 257);
    CHECK(beforeRequested == beforeReference);

    Config next = initial;
    next.seed = UINT64_C(0x5152535455565758);
    next.mood = Mood::Rainy;
    CHECK(requested.requestConfig(next));

    std::int16_t requestedSample = 0;
    std::int16_t referenceSample = 0;
    requested.render(&requestedSample, 1); // Last sample of bar zero.
    reference.render(&referenceSample, 1);
    CHECK(requestedSample == referenceSample);
    CHECK(requested.config().seed == initial.seed);
    CHECK(requested.snapshot().bar == 0);

    requested.render(&requestedSample, 1); // First sample of bar one.
    reference.render(&referenceSample, 1);
    CHECK(requestedSample == referenceSample);
    CHECK(requested.config().seed == initial.seed);
    CHECK(requested.snapshot().bar == 1);
    CHECK(requested.diagnostics().sessionTransitions == 0);

    const std::uint64_t fadeStart = secondBarEnd - 640u;
    const std::size_t identicalFrames = static_cast<std::size_t>(
        fadeStart - requested.snapshot().sessionSample + 1u);
    const std::vector<std::int16_t> requestedBeforeFade =
        renderFrames(requested, identicalFrames, 383);
    const std::vector<std::int16_t> referenceBeforeFade =
        renderFrames(reference, identicalFrames, 701);
    CHECK(requestedBeforeFade == referenceBeforeFade);
    CHECK(requested.config().seed == initial.seed);

    const std::size_t fadeRemainder = static_cast<std::size_t>(
        secondBarEnd - requested.snapshot().sessionSample);
    renderFrames(requested, fadeRemainder, 127);
    CHECK(requested.config().seed == initial.seed);
    requested.render(&requestedSample, 1);
    CHECK(requested.config().seed == next.seed);
    CHECK(requested.config().mood == next.mood);
    CHECK(requested.diagnostics().sessionTransitions == 1);
}

void testQueuedNextDoesNotFreezeFadeIn() {
    Config initial{};
    initial.seed = UINT64_C(0x6162636465666768);
    initial.meter = MusicMeter::FourFour;
    Engine engine(initial);
    const std::uint64_t stepQ32 =
        (static_cast<std::uint64_t>(lofi::kMusicSampleRate) * 60u << 32) /
        (static_cast<std::uint64_t>(engine.snapshot().bpm) * 4u);
    const std::uint64_t barEnd = (stepQ32 * 16u) >> 32;

    Config changed = initial;
    changed.seed = UINT64_C(0x7172737475767778);
    changed.mood = Mood::Night;
    CHECK(engine.requestConfig(changed));
    renderFrames(engine, static_cast<std::size_t>(barEnd), 1021);
    CHECK(engine.config().seed == initial.seed);
    std::int16_t firstNewSample = 1;
    engine.render(&firstNewSample, 1);
    CHECK(engine.config().seed == changed.seed);
    CHECK(firstNewSample == 0);

    engine.requestNext(UINT64_C(0x8182838485868788));
    const std::vector<std::int16_t> fadeIn = renderFrames(engine, 2048, 113);
    CHECK(engine.config().seed == changed.seed);
    CHECK(energy(fadeIn) > 0);
}

void testPauseFreezesTransport() {
    Config config{};
    config.seed = UINT64_C(0x55aa55aa55aa55aa);
    config.mood = Mood::Cozy;
    Engine engine(config);
    std::vector<std::int16_t> audio(2048);
    engine.render(audio.data(), audio.size());

    engine.pause(true);
    std::fill(audio.begin(), audio.end(), static_cast<std::int16_t>(7));
    engine.render(audio.data(), audio.size());
    const lofi::Snapshot paused = engine.snapshot();
    CHECK(paused.paused);
    CHECK(paused.transportSample > 2048);

    const std::uint64_t frozenTransport = paused.transportSample;
    const std::uint64_t frozenSession = paused.sessionSample;
    Config manualTempo = config;
    manualTempo.bpm = 96;
    CHECK(engine.requestConfig(manualTempo));
    std::fill(audio.begin(), audio.end(), static_cast<std::int16_t>(7));
    engine.render(audio.data(), audio.size());
    CHECK(engine.snapshot().transportSample == frozenTransport);
    CHECK(engine.snapshot().sessionSample == frozenSession);
    CHECK(engine.config().bpm == 0);
    CHECK(engine.snapshot().changePending);
    CHECK(std::all_of(audio.begin(), audio.end(), [](std::int16_t sample) {
        return sample == 0;
    }));

    engine.pause(false);
    engine.render(audio.data(), audio.size());
    const lofi::Snapshot resumed = engine.snapshot();
    CHECK(!resumed.paused);
    CHECK(resumed.transportSample == frozenTransport + audio.size());
    CHECK(resumed.sessionSample == frozenSession + audio.size());
    CHECK(energy(audio) > 0);

    for (int attempt = 0; attempt < 1000 && engine.config().bpm == 0; ++attempt) {
        engine.render(audio.data(), audio.size());
    }
    CHECK(engine.config().bpm == manualTempo.bpm);
    CHECK(engine.snapshot().bpm == manualTempo.bpm);
    CHECK(engine.diagnostics().sessionTransitions == 0);
}

void testResetAndSilentVolume() {
    Config config{};
    config.seed = UINT64_C(0x0102030405060708);
    config.volume = 0;
    Engine engine(config);
    std::vector<std::int16_t> audio(4096, 1);
    engine.render(audio.data(), audio.size());
    CHECK(std::all_of(audio.begin(), audio.end(), [](std::int16_t sample) {
        return sample == 0;
    }));
    CHECK(engine.diagnostics().scoreEventCount > 0);

    config.volume = 70;
    engine.reset(config);
    CHECK(engine.snapshot().transportSample == 0);
    CHECK(engine.diagnostics().renderedFrames == 0);
    engine.render(audio.data(), audio.size());
    CHECK(energy(audio) > 0);
}

void testArrangementAndAutomaticNextSession() {
    Config config{};
    config.seed = UINT64_C(0x2468ace013579bdf);
    config.mood = Mood::Rainy;
    config.soundEngine = SoundEngine::Synth;
    config.bpm = 96;
    Engine engine(config);
    std::vector<std::int16_t> block(512);
    bool sections[6]{};
    constexpr std::uint64_t frameLimit =
        static_cast<std::uint64_t>(lofi::kMusicSampleRate) * 410u;

    while (engine.diagnostics().sessionTransitions == 0 &&
           engine.diagnostics().renderedFrames < frameLimit) {
        engine.render(block.data(), block.size());
        const std::size_t section = static_cast<std::size_t>(engine.snapshot().section);
        CHECK(section < 6);
        sections[section] = true;
    }
    CHECK(engine.diagnostics().sessionTransitions == 1);
    CHECK(engine.config().seed != config.seed);
    CHECK(engine.config().bpm == config.bpm);
    CHECK(engine.snapshot().bpm == config.bpm);
    CHECK(std::all_of(std::begin(sections), std::end(sections), [](bool seen) {
        return seen;
    }));
    CHECK(engine.diagnostics().transportSample > UINT64_C(0x003fffff));
}

} // namespace

int main() {
    testBpmValidationAndSanitizing();
    testFavoriteRoundTrip();
    testReplayAndBlockDeterminism();
    testBackendIndependentScore();
    testMeterSchedulingAndBackendDeterminism();
    testToneSelectionDoesNotChangeScore();
    testInstrumentLevelTelemetry();
    testOpeningHasEveryLayerAndHeadroom();
    testFastTempoVoicePressure();
    testScheduledHarmonyMovementAndPhraseVariety();
    testBoundsAndRenderContract();
    testBarBoundaryTransition();
    testConfigAndNextRequestsMerge();
    testTempoOnlyChangesAtBarEdges();
    testLateRequestWaitsForFullFade();
    testQueuedNextDoesNotFreezeFadeIn();
    testPauseFreezesTransport();
    testResetAndSilentVolume();
    testArrangementAndAutomaticNextSession();
    std::cout << "music tests passed\n";
    return 0;
}
