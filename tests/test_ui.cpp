#include "lofi/ui.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    using namespace lofi;
    // The display path must preserve every entry after expanding beyond the
    // original sixteen-color canvas, including the final pixel of each row.
    static_assert(Frame::packedBytes == 240U * 135U);
    static_assert(kPaletteSize == 64);
    Frame frame;
    for (int y = 0; y < Frame::height; ++y) {
        for (int x = 0; x < Frame::width; ++x) {
            frame.set(x, y, static_cast<std::uint8_t>((x + y) % kPaletteSize));
        }
    }
    std::array<std::uint16_t, Frame::width + 2> row{};
    for (int y = 0; y < Frame::height; ++y) {
        row.front() = 0xabcd;
        row.back() = 0xdcba;
        frame.rowRgb565(y, row.data() + 1);
        assert(row.front() == 0xabcd && row.back() == 0xdcba);
        for (int x = 0; x < Frame::width; ++x) {
            auto color = static_cast<std::uint8_t>((x + y) % kPaletteSize);
            assert(frame.get(x, y) == color);
            assert(row[x + 1] == Frame::paletteRgb565(color));
        }
    }
    Frame before = frame;
    frame.set(-1, 0, 63);
    frame.set(Frame::width, 0, 63);
    frame.set(0, -1, 63);
    frame.set(0, Frame::height, 63);
    assert(std::memcmp(before.packed(), frame.packed(), Frame::packedBytes) == 0);

    View view;
    view.clean = true;
    view.motion = 0;
    view.seed = 0xca7cafe;
    view.volume = 100;
    view.instrumentLevels[0] = 200;
    view.instrumentLevels[1] = 100;
    view.level = 0.9f;
    render(before, view);
    view.timeMs = 9876543210123ULL;
    view.beatPhase = 0.8f;
    render(frame, view);
    assert(std::memcmp(before.packed(), frame.packed(), Frame::packedBytes) == 0);

    // Full motion exposes actual role activity and meter phase. Pausing or
    // muting suppresses those dynamic marks, while keeping the room and text.
    view.motion = 2;
    view.clean = false;
    view.playing = true;
    view.beatPhase = 0.25f;
    view.instrumentLevels[0] = 200;
    view.instrumentLevels[1] = 100;
    render(before, view);
    const auto active = before;
    view.timeMs += 417;
    view.beatPhase = 0.75f;
    view.instrumentLevels[0] = 30;
    render(frame, view);
    assert(std::memcmp(active.packed(), frame.packed(), Frame::packedBytes) != 0);

    view.playing = false;
    view.level = 0.9f; // Renderer must still suppress beat/level animation.
    view.instrumentLevels[0] = 255;
    view.beatPhase = 0.1f;
    render(before, view);
    view.instrumentLevels[0] = 0;
    view.level = 0.0f;
    view.beatPhase = 0.9f;
    render(frame, view);
    assert(std::memcmp(before.packed(), frame.packed(), Frame::packedBytes) == 0);

    // Ten-item settings use a scrolling eight-row viewport and the sleep
    // state remains renderable at both ends of the menu.
    view.screen = Screen::Settings;
    view.itemCount = 10;
    for (int i = 0; i < view.itemCount; ++i) {
        std::snprintf(view.items[i], sizeof(view.items[i]), "SETTING %d", i + 1);
    }
    view.sleepTimerActive = true;
    view.sleepSecondsRemaining = 1799;
    view.selection = 0;
    render(before, view);
    view.selection = 9;
    render(frame, view);
    assert(std::memcmp(before.packed(), frame.packed(), Frame::packedBytes) != 0);
    view.playing = true;
    view.volume = 0;
    view.instrumentLevels[0] = 255;
    view.level = 0.9f;
    view.beatPhase = 0.2f;
    render(before, view);
    view.instrumentLevels[0] = 0;
    view.level = 0.0f;
    view.beatPhase = 0.8f;
    render(frame, view);
    assert(std::memcmp(before.packed(), frame.packed(), Frame::packedBytes) == 0);

    // Every exported screen must contain displayable indices, never the
    // sprite-only transparent sentinel, even at a long-running timestamp.
    for (int screen = 0; screen <= static_cast<int>(Screen::Diagnostics); ++screen) {
        view.screen = static_cast<Screen>(screen);
        view.clean = false;
        view.motion = 2;
        render(frame, view);
        for (std::size_t i = 0; i < Frame::packedBytes; ++i) {
            assert(frame.packed()[i] < kPaletteSize);
        }
    }
}
