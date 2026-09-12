#include "lofi/ui.h"

#include <array>
#include <cassert>
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
    render(before, view);
    view.timeMs = 9876543210123ULL;
    view.beatPhase = 0.8f;
    view.level = 0.9f;
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
