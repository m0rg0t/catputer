// The browser build drives the engine through web/catputer_web.cpp. This runs
// that same interface natively: it must play exactly what Engine plays.
#include "lofi/music.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": "  \
                      << #condition << '\n';                                           \
            std::exit(1);                                                              \
        }                                                                              \
    } while (false)

extern "C" {
void catputer_init(std::uint32_t, std::uint32_t, std::uint32_t);
std::uint32_t catputer_sample_rate();
std::uint32_t catputer_buffer_frames();
const std::int16_t* catputer_buffer();
std::uint32_t catputer_render(std::uint32_t);
void catputer_set_mood(std::uint32_t);
void catputer_next(std::uint32_t, std::uint32_t);
void catputer_pause(std::uint32_t);
std::uint32_t catputer_bpm();
std::uint32_t catputer_bar();
std::uint32_t catputer_mood();
std::uint32_t catputer_meter();
std::uint32_t catputer_seed_high();
std::uint32_t catputer_seed_low();
const std::uint8_t* catputer_levels();
}

namespace {

std::vector<std::int16_t> renderWeb(std::size_t frames, std::uint32_t block) {
    std::vector<std::int16_t> audio;
    while (audio.size() < frames) {
        const std::uint32_t count = catputer_render(block);
        CHECK(count == block);
        audio.insert(audio.end(), catputer_buffer(), catputer_buffer() + count);
    }
    return audio;
}

void testBeforeInitIsHarmless() {
    CHECK(catputer_render(128) == 0);
    CHECK(catputer_bpm() == 0);
    catputer_set_mood(2);
    catputer_next(1, 2);
    catputer_pause(1);
}

void testMatchesTheEngine() {
    lofi::Config config{};
    config.seed = UINT64_C(0x0123456789abcdef);
    config.mood = lofi::Mood::Night;
    auto reference = std::make_unique<lofi::Engine>(config);
    constexpr std::size_t frames = 32000u * 6u;
    std::vector<std::int16_t> expected(frames);
    reference->render(expected.data(), frames);

    catputer_init(0x01234567u, 0x89abcdefu, 2);
    CHECK(catputer_sample_rate() == 32000u);
    CHECK(catputer_seed_high() == 0x01234567u);
    CHECK(catputer_seed_low() == 0x89abcdefu);
    const std::vector<std::int16_t> web = renderWeb(frames, 128); // The worklet quantum.
    CHECK(web == expected);
    CHECK(catputer_bpm() == reference->snapshot().bpm);
    CHECK(catputer_mood() == 2);
    CHECK(catputer_meter() / 256u == reference->snapshot().meterNumerator);

    bool heard = false;
    const std::uint8_t* levels = catputer_levels();
    for (std::size_t index = 0; index < lofi::kMusicInstrumentCount; ++index) {
        heard = heard || levels[index] != 0;
    }
    CHECK(heard);
}

void testControlsAndLimits() {
    catputer_init(0, 0xca7cafeu, 0);
    CHECK(catputer_render(catputer_buffer_frames() + 500u) == catputer_buffer_frames());

    catputer_set_mood(3);
    catputer_set_mood(99); // Out of range falls back to Cozy.
    catputer_set_mood(1);
    for (int index = 0; index < 400 && catputer_mood() != 1; ++index) {
        catputer_render(1024);
    }
    CHECK(catputer_mood() == 1);

    catputer_next(0x11111111u, 0x22222222u);
    for (int index = 0; index < 400 && catputer_seed_low() != 0x22222222u; ++index) {
        catputer_render(1024);
    }
    CHECK(catputer_seed_high() == 0x11111111u);
    CHECK(catputer_seed_low() == 0x22222222u);

    catputer_pause(1);
    for (int index = 0; index < 8; ++index) {
        catputer_render(1024);
    }
    const std::int16_t* silent = catputer_buffer();
    for (std::uint32_t index = 0; index < 1024; ++index) {
        CHECK(silent[index] == 0);
    }
    catputer_pause(0);

    // Restarting an existing engine replays the same session.
    catputer_init(0, 0xca7cafeu, 0);
    const std::vector<std::int16_t> first = renderWeb(32000, 128);
    catputer_init(0, 0xca7cafeu, 0);
    CHECK(first == renderWeb(32000, 128));
}

} // namespace

int main() {
    testBeforeInitIsHarmless();
    testMatchesTheEngine();
    testControlsAndLimits();
    std::cout << "web api tests passed\n";
    return 0;
}
