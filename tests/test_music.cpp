#include "lofi/music.h"

#include <algorithm>
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
using lofi::Mood;
using lofi::SoundEngine;

bool sameConfig(const Config& left, const Config& right) {
    return left.seed == right.seed && left.mood == right.mood &&
           left.soundEngine == right.soundEngine && left.volume == right.volume &&
           left.texture == right.texture;
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

void renderUntilSeed(Engine& engine, std::uint64_t seed) {
    std::vector<std::int16_t> block(509);
    for (int attempt = 0; attempt < 1000 && engine.config().seed != seed; ++attempt) {
        engine.render(block.data(), block.size());
    }
    CHECK(engine.config().seed == seed);
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
    CHECK(std::strcmp(code, "lofi1-fedcba9876543210-2-1-100-0") == 0);

    Config parsed{};
    CHECK(Engine::parseFavoriteCode(code, parsed));
    CHECK(sameConfig(source, parsed));
    CHECK(!Engine::parseFavoriteCode("lofi1-fedcba987654321-2-1-100-0", parsed));
    CHECK(!Engine::parseFavoriteCode("lofi1-fedcba9876543210-3-1-100-0", parsed));
    CHECK(!Engine::parseFavoriteCode("lofi1-fedcba9876543210-2-1-101-0", parsed));

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
    CHECK(left.transportSample == frames);
    CHECK(left.renderedFrames == frames);
    CHECK(left.scoreEventCount > 20);
    CHECK(energy(reference) > UINT64_C(10000000));
}

void testBackendIndependentScore() {
    Config synthConfig{};
    synthConfig.seed = UINT64_C(0xbadc0ffee0ddf00d);
    synthConfig.mood = Mood::Cozy;
    synthConfig.soundEngine = SoundEngine::Synth;
    synthConfig.texture = 0;
    Config hybridConfig = synthConfig;
    hybridConfig.soundEngine = SoundEngine::Hybrid;

    Engine synth(synthConfig);
    Engine hybrid(hybridConfig);
    constexpr std::size_t frames = lofi::kMusicSampleRate * 18u;
    const std::vector<std::int16_t> synthAudio = renderFrames(synth, frames, 511);
    const std::vector<std::int16_t> hybridAudio = renderFrames(hybrid, frames, 1024);
    const lofi::Diagnostics synthDiagnostics = synth.diagnostics();
    const lofi::Diagnostics hybridDiagnostics = hybrid.diagnostics();

    CHECK(synthDiagnostics.scoreEventCount == hybridDiagnostics.scoreEventCount);
    CHECK(synthDiagnostics.scoreEventHash == hybridDiagnostics.scoreEventHash);
    CHECK(synth.snapshot().bar == hybrid.snapshot().bar);
    CHECK(synth.snapshot().bpm == hybrid.snapshot().bpm);
    CHECK(synthAudio != hybridAudio);
    CHECK(energy(synthAudio) > UINT64_C(10000000));
    CHECK(energy(hybridAudio) > UINT64_C(10000000));
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
    CHECK(after.bpm >= 68 && after.bpm <= 76);
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

    // When both intents are still in the mailbox, the later full config owns
    // the seed while the earlier next intent is retained as a transition.
    Engine nextThenConfig(initial);
    nextThenConfig.requestNext(laterSeed);
    CHECK(nextThenConfig.requestConfig(changed));
    nextThenConfig.render(&one, 1);
    renderUntilSeed(nextThenConfig, changed.seed);
    CHECK(sameConfig(nextThenConfig.config(), changed));
}

void testLateRequestWaitsForFullFade() {
    Config initial{};
    initial.seed = UINT64_C(0x4142434445464748);
    initial.mood = Mood::Cozy;
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
    std::fill(audio.begin(), audio.end(), static_cast<std::int16_t>(7));
    engine.render(audio.data(), audio.size());
    CHECK(engine.snapshot().transportSample == frozenTransport);
    CHECK(engine.snapshot().sessionSample == frozenSession);
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
    CHECK(std::all_of(std::begin(sections), std::end(sections), [](bool seen) {
        return seen;
    }));
    CHECK(engine.diagnostics().transportSample > UINT64_C(0x003fffff));
}

} // namespace

int main() {
    testFavoriteRoundTrip();
    testReplayAndBlockDeterminism();
    testBackendIndependentScore();
    testBoundsAndRenderContract();
    testBarBoundaryTransition();
    testConfigAndNextRequestsMerge();
    testLateRequestWaitsForFullFade();
    testQueuedNextDoesNotFreezeFadeIn();
    testPauseFreezesTransport();
    testResetAndSilentVolume();
    testArrangementAndAutomaticNextSession();
    std::cout << "music tests passed\n";
    return 0;
}
