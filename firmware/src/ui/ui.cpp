#include "lofi/ui.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace lofi {

#include "generated_scene_data.inc"

namespace {

// A small warm night palette.  The order is part of the renderer contract:
// indexed scene code only ever emits these sixteen values.
enum Colour : std::uint8_t {
    Ink = 0,
    Night,
    Wall,
    Slate,
    Haze,
    Moon,
    Cream,
    Rain,
    Brick,
    Wood,
    Amber,
    Gold,
    Cat,
    CatLight,
    Leaf,
    Glow,
};

const std::uint16_t* const kPalette = kGeneratedScenePalette;

inline int clampInt(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

inline std::uint8_t safeColour(std::uint8_t value) {
    return value < kPaletteSize ? value : Ink;
}

std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16U;
    x *= 0x7feb352dU;
    x ^= x >> 15U;
    x *= 0x846ca68bU;
    x ^= x >> 16U;
    return x;
}

std::uint32_t hashSeed(std::uint32_t seed, std::uint32_t salt) {
    return hash32(seed ^ (salt * 0x9e3779b9U));
}

char upperAscii(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

// Five by seven glyphs.  Each row uses the low five bits, leftmost pixel in
// bit four.  Keeping the table in source makes the tiny UI editable without
// a font dependency or a runtime asset loader.
const std::uint8_t kFont5x7[36][7] = {
    {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, // A
    {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}, // B
    {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f}, // C
    {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}, // D
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}, // E
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}, // F
    {0x0f, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0f}, // G
    {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, // H
    {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}, // I
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c}, // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}, // L
    {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
    {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, // O
    {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}, // P
    {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}, // Q
    {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}, // R
    {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}, // S
    {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, // U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}, // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11}, // W
    {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}, // X
    {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}, // Y
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}, // Z
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, // 0
    {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}, // 1
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}, // 2
    {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}, // 3
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, // 4
    {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}, // 5
    {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e}, // 6
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, // 8
    {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e}, // 9
};

int glyph5Index(char c) {
    c = upperAscii(c);
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= '0' && c <= '9') {
        return 26 + c - '0';
    }
    return -1;
}

std::uint8_t glyph5Row(char c, int row) {
    const int index = glyph5Index(c);
    if (index >= 0 && row >= 0 && row < 7) {
        return kFont5x7[index][row];
    }
    switch (c) {
    case '-': return row == 3 ? 0x1f : 0;
    case '_': return row == 6 ? 0x1f : 0;
    case '.': return row == 6 ? 0x04 : 0;
    case ',': return row == 5 ? 0x04 : (row == 6 ? 0x08 : 0);
    case ';': return row == 2 || row == 5 ? 0x04 : (row == 6 ? 0x08 : 0);
    case ':': return (row == 2 || row == 5) ? 0x04 : 0;
    case '!': return (row < 5) ? 0x04 : (row == 6 ? 0x04 : 0);
    case '?': return row == 0 ? 0x0e : (row == 1 ? 0x11 :
                         (row == 2 ? 0x01 : (row == 3 ? 0x02 :
                         (row == 5 || row == 6 ? 0x04 : 0))));
    case '+': return (row == 3 ? 0x1f : (row == 1 || row == 2 || row == 4 ? 0x04 : 0));
    case '/': return row == 0 ? 0x01 : (row == 1 ? 0x02 : (row == 2 ? 0x04 :
                         (row == 3 ? 0x08 : (row == 4 ? 0x10 : 0))));
    case '%': return row == 0 ? 0x19 : (row == 1 ? 0x06 : (row == 2 ? 0x08 :
                         (row == 3 ? 0x04 : (row == 4 ? 0x02 : (row == 5 ? 0x18 : 0)))));
    case '[': return (row == 0 || row == 6) ? 0x1c : 0x10;
    case ']': return (row == 0 || row == 6) ? 0x07 : 0x01;
    case '(': return row == 0 || row == 6 ? 0x02 : (row == 1 || row == 5 ? 0x04 : 0x08);
    case ')': return row == 0 || row == 6 ? 0x08 : (row == 1 || row == 5 ? 0x04 : 0x02);
    case '*': return row == 3 ? 0x0e : (row == 1 || row == 5 ? 0x04 :
                         (row == 2 || row == 4 ? 0x0a : 0));
    case '=': return row == 2 || row == 4 ? 0x1f : 0;
    case ' ': return 0;
    default: return row == 3 ? 0x0a : 0;
    }
}

// Three by five glyphs keep the status/footer legible while leaving room for
// the real labels on the 240-pixel display.
std::uint8_t glyph3Row(char c, int row) {
    c = upperAscii(c);
    if (row < 0 || row >= 5) {
        return 0;
    }
    static const std::uint8_t letters[26][5] = {
        {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3},
        {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4},
        {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7},
        {1, 1, 1, 5, 2}, {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7},
        {5, 7, 7, 5, 5}, {5, 7, 7, 7, 5}, {2, 5, 5, 5, 2},
        {6, 5, 6, 4, 4}, {2, 5, 5, 7, 3}, {6, 5, 6, 5, 5},
        {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2}, {5, 5, 5, 5, 2},
        {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5},
        {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7},
    };
    static const std::uint8_t digits[10][5] = {
        {2, 5, 7, 5, 2}, {2, 6, 2, 2, 7}, {6, 1, 2, 4, 7},
        {6, 1, 2, 1, 6}, {5, 5, 7, 1, 1}, {7, 4, 6, 1, 6},
        {3, 4, 6, 5, 2}, {7, 1, 2, 2, 2}, {2, 5, 2, 5, 2},
        {2, 5, 3, 1, 6},
    };
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'][row];
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'][row];
    }
    switch (c) {
    case '-': return row == 2 ? 7 : 0;
    case '/': return row == 0 ? 1 : (row == 1 ? 2 : (row == 2 ? 2 : (row == 3 ? 4 : 4)));
    case ':': return (row == 1 || row == 3) ? 2 : 0;
    case '.': return row == 4 ? 2 : 0;
    case ',': return row == 4 ? 2 : 0;
    case '+': return row == 2 ? 7 : ((row == 1 || row == 3) ? 2 : 0);
    case '=': return (row == 1 || row == 3) ? 7 : 0;
    case '%': return row == 0 ? 5 : (row == 4 ? 5 : (row == 2 ? 2 : 0));
    case '!': return row < 4 ? 2 : (row == 4 ? 2 : 0);
    case ' ': return 0;
    default: return row == 2 ? 2 : 0;
    }
}

void drawText(Frame& frame, int x, int y, const char* text, std::uint8_t colour,
              int maxWidth = kScreenWidth) {
    if (text == nullptr) {
        return;
    }
    const int start = x;
    for (int i = 0; i < 64 && text[i] != '\0'; ++i) {
        if (x + 5 > start + maxWidth) {
            break;
        }
        const char c = upperAscii(text[i]);
        for (int row = 0; row < 7; ++row) {
            const std::uint8_t bits = glyph5Row(c, row);
            for (int col = 0; col < 5; ++col) {
                if ((bits & static_cast<std::uint8_t>(1U << (4 - col))) != 0U) {
                    frame.set(x + col, y + row, colour);
                }
            }
        }
        x += 6;
    }
}

void drawTinyText(Frame& frame, int x, int y, const char* text,
                  std::uint8_t colour, int maxWidth = kScreenWidth) {
    if (text == nullptr) {
        return;
    }
    const int start = x;
    for (int i = 0; i < 96 && text[i] != '\0'; ++i) {
        if (x + 3 > start + maxWidth) {
            break;
        }
        for (int row = 0; row < 5; ++row) {
            const std::uint8_t bits = glyph3Row(text[i], row);
            for (int col = 0; col < 3; ++col) {
                if ((bits & static_cast<std::uint8_t>(1U << (2 - col))) != 0U) {
                    frame.set(x + col, y + row, colour);
                }
            }
        }
        x += 4;
    }
}

void drawCenteredText(Frame& frame, int x, int width, int y, const char* text,
                      std::uint8_t colour) {
    int length = 0;
    if (text != nullptr) {
        while (length < 64 && text[length] != '\0') {
            ++length;
        }
    }
    const int textWidth = length > 0 ? length * 6 - 1 : 0;
    drawText(frame, x + (width - textWidth) / 2, y, text, colour,
             width > 0 ? width : kScreenWidth);
}

void drawTinyNumber(Frame& frame, int x, int y, int value, std::uint8_t colour,
                    int width = kScreenWidth) {
    char text[12] = {};
    int index = 11;
    bool negative = value < 0;
    unsigned number = static_cast<unsigned>(negative ? -value : value);
    do {
        text[--index] = static_cast<char>('0' + (number % 10U));
        number /= 10U;
    } while (number != 0U && index > 0);
    if (negative && index > 0) {
        text[--index] = '-';
    }
    drawTinyText(frame, x, y, text + index, colour, width);
}

void drawHex(Frame& frame, int x, int y, std::uint32_t value, std::uint8_t colour) {
    char text[9] = {};
    for (int i = 7; i >= 0; --i) {
        const std::uint8_t digit = static_cast<std::uint8_t>((value >> (i * 4)) & 0x0fU);
        text[7 - i] = digit < 10 ? static_cast<char>('0' + digit)
                                 : static_cast<char>('A' + digit - 10);
    }
    drawTinyText(frame, x, y, text, colour);
}

const char* moodName(int mood) {
    switch (mood % 4 < 0 ? mood % 4 + 4 : mood % 4) {
    case 1: return "RAIN";
    case 2: return "NIGHT";
    default: return "COZY";
    }
}

int generatedCatFrame(const View& view) {
    if (view.motion == 0) {
        return 0;
    }
    const std::uint32_t time = static_cast<std::uint32_t>(view.timeMs);
    const std::uint32_t blinkPeriod = 4000U + (hashSeed(view.seed, 0x4b10U) % 3001U);
    const std::uint32_t blinkPhase =
        (time + (hashSeed(view.seed, 0x4b11U) % blinkPeriod)) % blinkPeriod;
    if (view.motion >= 2U && blinkPhase < 320U) {
        // Four short frames leave the eyes relaxed for several seconds, then
        // close and reopen at roughly 80ms per authored frame.
        return static_cast<int>(blinkPhase / 80U);
    }

    const std::uint32_t tailPeriod = 5800U + (hashSeed(view.seed, 0x4b12U) % 3201U);
    const std::uint32_t tailPhase =
        (time + (hashSeed(view.seed, 0x4b13U) % tailPeriod)) % tailPeriod;
    if (tailPhase < 1000U) {
        // Tail frames are a rarer two-step gesture.  Hold each authored pose
        // long enough to read as a wave rather than a one-frame flicker.
        return tailPhase < 600U ? 4 : 5;
    }
    return 0;
}

void drawGeneratedRain(Frame& frame, const View& view) {
    if (view.motion == 0) {
        return;
    }
    const std::uint32_t time = static_cast<std::uint32_t>(view.timeMs);
    const int count = view.motion >= 2U ? 8 : 4;
    for (int i = 0; i < count; ++i) {
        const std::uint32_t drop = hashSeed(view.seed, 0x530U + static_cast<std::uint32_t>(i));
        const int x = 7 + static_cast<int>(drop % 72U);
        const int baseY = 18 + static_cast<int>((drop >> 8U) % 49U);
        const int speed = 260 + static_cast<int>((drop >> 17U) % 90U);
        const int y = 19 + (baseY + static_cast<int>(time / static_cast<std::uint32_t>(speed))) % 55;
        const int length = 2 + static_cast<int>((drop >> 24U) & 1U);
        frame.line(x, y, x - 1, y + length, ((time / 260U + static_cast<std::uint32_t>(i)) & 3U) == 0U ? Moon : Rain);
    }
}

void drawGeneratedSteam(Frame& frame, const View& view) {
    if (view.motion == 0) {
        return;
    }
    const std::uint32_t time = static_cast<std::uint32_t>(view.timeMs);
    const int drift = static_cast<int>((time / 420U) % 6U);
    frame.set(130 + ((drift + 1) & 1), 66 - drift, Haze);
    if (view.motion >= 2U) {
        frame.set(134 + (drift & 1), 63 - ((drift + 2) % 5), Haze);
        if (((time / 640U) & 1U) != 0U) {
            frame.set(132, 59 - ((drift + 1) % 4), Slate);
        }
    }
}

void drawGeneratedScene(Frame& frame, const View& view, bool /*muted*/) {
    for (int y = 0; y < kScreenHeight; ++y) {
        const std::size_t row = static_cast<std::size_t>(y * kScreenWidth);
        for (int x = 0; x < kScreenWidth; ++x) {
            frame.set(x, y, kGeneratedSceneBackground[row + static_cast<std::size_t>(x)]);
        }
    }

    drawGeneratedRain(frame, view);
    drawGeneratedSteam(frame, view);

    const int catFrame = generatedCatFrame(view);
    const int originX = kGeneratedSceneOriginX;
    const int originY = kGeneratedSceneOriginY;
    for (int y = 0; y < kSceneFrameHeight; ++y) {
        for (int x = 0; x < kSceneFrameWidth; ++x) {
            const std::uint8_t colour = kGeneratedSceneCat[catFrame][
                static_cast<std::size_t>(y * kSceneFrameWidth + x)];
            if (colour != kSceneTransparentIndex) {
                frame.set(originX + x, originY + y, colour);
            }
        }
    }

    // Keep the room alive on a beat without washing the authored palette out.
    if (view.motion != 0U && view.playing && view.level > 0.65f) {
        frame.set(106, 56, Gold);
        frame.set(109, 57, Amber);
    }
}

void drawBattery(Frame& frame, int x, int y, int percent) {
    frame.rect(x, y, 15, 7, Haze);
    frame.fillRect(x + 15, y + 2, 2, 3, Haze);
    const int fill = percent < 0 ? 0 : (percent * 11) / 100;
    if (fill > 0) {
        frame.fillRect(x + 2, y + 2, fill, 3, percent < 20 ? Brick : Gold);
    }
    if (percent < 0) {
        frame.hLine(x + 4, x + 10, y + 3, Slate);
    }
}

void drawStar(Frame& frame, int cx, int cy, std::uint8_t colour) {
    frame.set(cx, cy - 4, colour);
    frame.set(cx, cy + 4, colour);
    frame.set(cx - 4, cy, colour);
    frame.set(cx + 4, cy, colour);
    frame.set(cx - 2, cy - 2, colour);
    frame.set(cx + 2, cy - 2, colour);
    frame.set(cx - 2, cy + 2, colour);
    frame.set(cx + 2, cy + 2, colour);
}

void drawStatus(Frame& frame, const View& view, const char* title) {
    frame.fillRect(0, 0, 240, 14, Ink);
    frame.fillRect(0, 13, 240, 1, Brick);
    drawText(frame, 4, 3, title, Cream, 35);
    drawText(frame, 40, 3, moodName(view.mood), Cream, 29);
    char bpm[4], volume[5];
    std::snprintf(bpm, sizeof(bpm), "%d", clampInt(view.bpm, 0, 999));
    std::snprintf(volume, sizeof(volume), "V%d", clampInt(view.volume, 0, 100));
    drawText(frame, 76, 3, bpm, Gold, 17);
    drawText(frame, 96, 3, "BPM", Moon, 17);
    drawText(frame, 120, 3, view.playing ? "PLAY" : "PAUSE", Cream, 29);
    if (view.favorite) {
        drawStar(frame, 158, 7, Gold);
    }
    drawText(frame, 176, 3, volume, view.volume == 0 ? Amber : Cream, 23);
    drawBattery(frame, 219, 3, view.batteryPercent);
}

void drawFooter(Frame& frame, const View& view) {
    // Two seven-pixel rows, with solid backing and a two-pixel line gap.
    // Keep the essentials here; the help screen carries the full key list.
    frame.fillRect(0, 115, 240, 20, Ink);
    frame.fillRect(0, 115, 240, 1, Brick);
    if (view.notice[0] != '\0') {
        drawText(frame, 4, 118, view.notice, Gold, 232);
        drawText(frame, 4, 127, "SPACE PLAY   -/= VOL   H HELP", Cream, 232);
    } else if (view.pending) {
        drawText(frame, 4, 118, "NEXT SESSION QUEUED", Gold, 232);
        drawText(frame, 4, 127, "SPACE PLAY   -/= VOL   H HELP", Cream, 232);
    } else {
        drawText(frame, 4, 118, "SPACE PLAY   -/= VOL   M MOOD", Cream, 232);
        drawText(frame, 4, 127, "N NEXT   F FAV   V CLEAN   H HELP", Cream, 232);
    }
}

void drawMenuFrame(Frame& frame, const char* title, const View& view) {
    frame.fillRect(22, 18, 196, 101, Ink);
    frame.rect(22, 18, 196, 101, Haze);
    frame.rect(25, 21, 190, 95, Brick);
    drawText(frame, 34, 26, title, Cream, 164);
    frame.fillRect(34, 36, 170, 1, Brick);
    if (view.itemCount > 0) {
        const int count = clampInt(view.itemCount, 0, 8);
        for (int i = 0; i < count; ++i) {
            const int y = 42 + i * 9;
            const bool selected = i == clampInt(view.selection, 0, count - 1);
            if (selected) {
                frame.fillRect(32, y - 2, 174, 9, Slate);
                frame.fillRect(34, y, 2, 5, Gold);
            }
            drawText(frame, 41, y, view.items[i], selected ? Cream : Moon, 158);
        }
    } else {
        drawCenteredText(frame, 34, 170, 54, "NO ITEMS YET", Haze);
        drawCenteredText(frame, 34, 170, 66, "PRESS ESC TO RETURN", Haze);
    }
    // Navigation hints live in the footer, outside the eight menu rows.
}

void drawHelp(Frame& frame, const View& view) {
    frame.fillRect(10, 15, 220, 105, Ink);
    frame.rect(10, 15, 220, 105, Haze);
    frame.rect(13, 18, 214, 99, Brick);
    drawText(frame, 20, 22, "POCKET LOFI KEYS", Cream, 200);
    static const char* const lines[] = {
        "SPACE PLAY/PAUSE", "- = VOLUME", "N NEXT SESSION", "M MOODS",
        "F FAVORITE", "L FAVORITES", "V CLEAN VIEW", "S SETTINGS",
        "E SYNTH/HYBRID", "H HELP", "ENTER SELECT", "ESC BACK",
        "; . UP/DOWN", ", / ADJUST",
    };
    for (int i = 0; i < 14; ++i) {
        const int x = (i & 1) == 0 ? 20 : 128;
        const int y = 34 + (i / 2) * 11;
        drawText(frame, x, y, lines[i], (i == 0 || i == 8) ? Gold : Cream, 101);
    }
    (void)view;
}

void drawDiagnostics(Frame& frame, const View& view) {
    frame.fillRect(21, 18, 198, 101, Ink);
    frame.rect(21, 18, 198, 101, Haze);
    frame.rect(24, 21, 192, 95, Brick);
    drawText(frame, 33, 26, "DIAGNOSTICS", Cream, 170);
    const int count = clampInt(view.itemCount, 0, 8);
    if (count > 0) {
        // Diagnostics is a report, rather than a compact summary.  The
        // controller supplies bounded, already formatted rows here; retaining
        // the rows preserves the complete 16-digit seed and engine details.
        for (int i = 0; i < count; ++i) {
            const int y = 39 + i * 9;
            const bool selected = i == clampInt(view.selection, 0, count - 1);
            if (selected) {
                frame.fillRect(31, y - 2, 177, 9, Slate);
            }
            drawText(frame, 34, y, view.items[i], selected ? Cream : Moon, 171);
        }
    } else {
        // Keep a useful fallback for a platform snapshot that has not yet
        // populated its rows.
        drawTinyText(frame, 34, 42, "NO DIAGNOSTIC ROWS", Amber, 171);
        drawTinyText(frame, 34, 55, "SEED", Haze);
        drawHex(frame, 70, 55, view.seed, Gold);
        drawTinyText(frame, 34, 65, "BPM", Haze);
        drawTinyNumber(frame, 70, 65, view.bpm, Moon, 135);
        drawTinyText(frame, 34, 78, view.sdReady ? "SD READY" : "NO SD / RAM ONLY",
                     view.sdReady ? Leaf : Amber, 171);
        drawTinyText(frame, 34, 91, view.clean ? "CLEAN VIEW ON" : "OVERLAY VIEW ON", Rain, 171);
    }
    drawText(frame, 34, 108, "ESC BACK", Moon, 178);
}

void drawOverlay(Frame& frame, const View& view) {
    switch (view.screen) {
    case Screen::Moods: drawMenuFrame(frame, "CHOOSE A MOOD", view); break;
    case Screen::Favorites: drawMenuFrame(frame, "FAVORITE SESSIONS", view); break;
    case Screen::Settings: drawMenuFrame(frame, "SETTINGS", view); break;
    case Screen::Help: drawHelp(frame, view); break;
    case Screen::Diagnostics: drawDiagnostics(frame, view); break;
    case Screen::Radio: break;
    }
}

} // namespace

Frame::Frame() {
    clear(Ink);
}

void Frame::clear(std::uint8_t colour) {
    const std::uint8_t c = safeColour(colour);
    for (std::size_t i = 0; i < packedBytes; ++i) {
        pixels_[i] = c;
    }
}

std::uint8_t Frame::get(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return Ink;
    }
    return pixels_[static_cast<std::size_t>(y * width + x)];
}

void Frame::set(int x, int y, std::uint8_t colour) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }
    pixels_[static_cast<std::size_t>(y * width + x)] = safeColour(colour);
}

void Frame::hLine(int x0, int x1, int y, std::uint8_t colour) {
    if (y < 0 || y >= height) {
        return;
    }
    if (x0 > x1) {
        const int temp = x0;
        x0 = x1;
        x1 = temp;
    }
    x0 = clampInt(x0, 0, width - 1);
    x1 = clampInt(x1, 0, width - 1);
    for (int x = x0; x <= x1; ++x) {
        set(x, y, colour);
    }
}

void Frame::vLine(int x, int y0, int y1, std::uint8_t colour) {
    if (x < 0 || x >= width) {
        return;
    }
    if (y0 > y1) {
        const int temp = y0;
        y0 = y1;
        y1 = temp;
    }
    y0 = clampInt(y0, 0, height - 1);
    y1 = clampInt(y1, 0, height - 1);
    for (int y = y0; y <= y1; ++y) {
        set(x, y, colour);
    }
}

void Frame::fillRect(int x, int y, int w, int h, std::uint8_t colour) {
    if (w <= 0 || h <= 0) {
        return;
    }
    const int x0 = clampInt(x, 0, width);
    const int y0 = clampInt(y, 0, height);
    const int x1 = clampInt(x + w, 0, width);
    const int y1 = clampInt(y + h, 0, height);
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            set(px, py, colour);
        }
    }
}

void Frame::rect(int x, int y, int w, int h, std::uint8_t colour) {
    if (w <= 0 || h <= 0) {
        return;
    }
    hLine(x, x + w - 1, y, colour);
    hLine(x, x + w - 1, y + h - 1, colour);
    vLine(x, y, y + h - 1, colour);
    vLine(x + w - 1, y, y + h - 1, colour);
}

void Frame::line(int x0, int y0, int x1, int y1, std::uint8_t colour) {
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        set(x0, y0, colour);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void Frame::rowRgb565(int y, std::uint16_t* out) const {
    if (out == nullptr || y < 0 || y >= height) {
        return;
    }
    for (int x = 0; x < width; ++x) {
        out[x] = kPalette[get(x, y)];
    }
}

std::uint16_t Frame::paletteRgb565(std::uint8_t colour) {
    return kPalette[safeColour(colour)];
}

const std::uint16_t* Frame::paletteRgb565() {
    return kPalette;
}

void render(Frame& frame, const View& view) {
    const bool showOverlay = view.screen != Screen::Radio;
    drawGeneratedScene(frame, view, showOverlay);
    if (view.screen == Screen::Radio) {
        if (!view.clean) {
            drawStatus(frame, view, "RADIO");
            drawFooter(frame, view);
        } else if (!view.playing || view.pending) {
            frame.fillRect(4, 4, 68, 12, Ink);
            drawText(frame, 8, 6, view.playing ? "NEXT QUEUED" : "PAUSED", view.pending ? Gold : Cream, 60);
        }
        return;
    }
    drawStatus(frame, view, view.screen == Screen::Help ? "HELP" :
                            view.screen == Screen::Diagnostics ? "INFO" : "MENU");
    drawOverlay(frame, view);
    frame.fillRect(0, 120, 240, 15, Ink);
    frame.fillRect(0, 120, 240, 1, Brick);
    drawText(frame, 4, 125, "ENTER OK   ESC BACK   ;/. MOVE", Cream, 232);
}

} // namespace lofi
