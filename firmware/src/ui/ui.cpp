#include "lofi/ui.h"

#include <cstddef>
#include <cstdint>

namespace lofi {
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

constexpr std::uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
    return static_cast<std::uint16_t>(((r >> 3U) << 11U) |
                                      ((g >> 2U) << 5U) | (b >> 3U));
}

const std::uint16_t kPalette[kPaletteSize] = {
    rgb565(8, 15, 28),     // Ink
    rgb565(14, 28, 49),    // Night
    rgb565(24, 43, 64),    // Wall
    rgb565(42, 60, 82),    // Slate
    rgb565(89, 113, 135),  // Haze
    rgb565(183, 206, 196), // Moon
    rgb565(246, 223, 178), // Cream
    rgb565(102, 183, 187), // Rain
    rgb565(152, 73, 65),   // Brick
    rgb565(102, 60, 49),   // Wood
    rgb565(208, 126, 65),  // Amber
    rgb565(242, 184, 87),  // Gold
    rgb565(174, 105, 76),  // Cat
    rgb565(230, 158, 116), // CatLight
    rgb565(112, 161, 116), // Leaf
    rgb565(255, 221, 157), // Glow
};

const std::uint8_t kDimPalette[kPaletteSize] = {
    Ink, Night, Night, Wall, Slate, Slate, Haze, Haze,
    Brick, Wood, Amber, Amber, Brick, Brick, Leaf, Gold,
};

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

int triangleWave(std::uint32_t tick, int period, int amplitude) {
    if (period <= 1 || amplitude <= 0) {
        return 0;
    }
    const int phase = static_cast<int>(tick % static_cast<std::uint32_t>(period));
    const int half = period / 2;
    if (phase < half) {
        return (phase * amplitude) / (half > 0 ? half : 1);
    }
    return amplitude - ((phase - half) * amplitude) /
                             ((period - half) > 0 ? period - half : 1);
}

int centeredWave(std::uint32_t tick, int period, int amplitude) {
    return triangleWave(tick, period, amplitude * 2) - amplitude;
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

void drawTriangle(Frame& frame, int x0, int y0, int x1, int y1, int x2, int y2,
                  std::uint8_t colour) {
    if (y0 > y1) { const int tx = x0; const int ty = y0; x0 = x1; y0 = y1; x1 = tx; y1 = ty; }
    if (y1 > y2) { const int tx = x1; const int ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
    if (y0 > y1) { const int tx = x0; const int ty = y0; x0 = x1; y0 = y1; x1 = tx; y1 = ty; }
    if (y0 == y2) {
        frame.hLine(x0 < x1 ? x0 : x1, x0 > x1 ? x0 : x1, y0, colour);
        return;
    }
    for (int y = y0; y <= y2; ++y) {
        const bool second = y > y1 || y1 == y0;
        const int segmentStart = second ? y1 : y0;
        const int segmentEnd = second ? y2 : y1;
        const int segmentX = second ? x2 : x1;
        const int edgeA = x0 + ((x2 - x0) * (y - y0)) / (y2 - y0);
        const int edgeB = segmentEnd == segmentStart
                              ? segmentX
                              : x0 + ((segmentX - x0) * (y - y0)) / (segmentEnd - y0);
        frame.hLine(edgeA < edgeB ? edgeA : edgeB, edgeA > edgeB ? edgeA : edgeB, y, colour);
    }
}

void drawDisc(Frame& frame, int cx, int cy, int radius, std::uint8_t colour) {
    for (int y = -radius; y <= radius; ++y) {
        const int span = radius * radius - y * y;
        int dx = 0;
        while ((dx + 1) * (dx + 1) <= span) {
            ++dx;
        }
        frame.hLine(cx - dx, cx + dx, cy + y, colour);
    }
}

struct ScenePainter {
    Frame& frame;
    bool muted;

    std::uint8_t colour(std::uint8_t value) const {
        return muted ? kDimPalette[safeColour(value)] : safeColour(value);
    }
    void fill(int x, int y, int w, int h, std::uint8_t c) { frame.fillRect(x, y, w, h, colour(c)); }
    void rect(int x, int y, int w, int h, std::uint8_t c) { frame.rect(x, y, w, h, colour(c)); }
    void hLine(int x0, int x1, int y, std::uint8_t c) { frame.hLine(x0, x1, y, colour(c)); }
    void vLine(int x, int y0, int y1, std::uint8_t c) { frame.vLine(x, y0, y1, colour(c)); }
    void line(int x0, int y0, int x1, int y1, std::uint8_t c) {
        frame.line(x0, y0, x1, y1, colour(c));
    }
    void triangle(int x0, int y0, int x1, int y1, int x2, int y2, std::uint8_t c) {
        drawTriangle(frame, x0, y0, x1, y1, x2, y2, colour(c));
    }
    void disc(int cx, int cy, int radius, std::uint8_t c) {
        drawDisc(frame, cx, cy, radius, colour(c));
    }
};

const char* moodName(int mood) {
    switch (mood % 4 < 0 ? mood % 4 + 4 : mood % 4) {
    case 1: return "RAIN";
    case 2: return "NIGHT";
    default: return "COZY";
    }
}

void drawPlant(ScenePainter& p, int x, int y, std::uint32_t seed) {
    p.fill(x - 5, y + 10, 12, 7, Wood);
    p.fill(x - 3, y + 16, 8, 2, Ink);
    p.line(x, y + 10, x - 4, y - 2, Leaf);
    p.line(x + 1, y + 10, x + 7, y - 5, Leaf);
    p.line(x, y + 8, x + 11, y + 2, Leaf);
    p.fill(x - 7, y - 5, 6, 4, Leaf);
    p.fill(x + 6, y - 8, 6, 4, Leaf);
    p.fill(x + 9, y, 6, 4, Leaf);
    if ((hashSeed(seed, 0x42U) & 1U) != 0U) {
        p.fill(x - 9, y + 1, 5, 3, Leaf);
    }
}

void drawScene(Frame& frame, const View& view, bool muted) {
    ScenePainter p{frame, muted};
    const std::uint32_t seed = view.seed;
    const bool animate = view.motion != 0;
    // Keeping the clock at a fixed origin makes still mode deterministic and
    // also gates every audio-linked visual below (beat, level and play state
    // remain available to the information bars).
    const std::uint32_t time = animate ? static_cast<std::uint32_t>(view.timeMs) : 0U;

    // Wall, moulding and a wood floor give the room a strong horizontal read
    // behind the deliberately quieter rain and furniture details.
    p.fill(0, 0, 240, 88, Wall);
    p.fill(0, 0, 240, 17, Night);
    p.fill(0, 84, 240, 4, Slate);
    p.fill(0, 88, 240, 47, Wood);
    for (int y = 95; y < 135; y += 13) {
        p.hLine(0, 239, y, Ink);
    }
    for (int x = 17; x < 240; x += 39) {
        p.vLine(x, 89, 134, Slate);
    }
    p.hLine(0, 239, 117, Ink);
    p.hLine(0, 239, 119, Slate);

    // Low-contrast wall panels and little picture frames establish depth even
    // when the status bars are hidden by clean view.
    p.hLine(0, 239, 20, Slate);
    p.hLine(0, 239, 21, Night);
    p.vLine(81, 18, 82, Slate);
    p.vLine(82, 18, 82, Night);
    p.fill(92, 25, 18, 12, Night);
    p.rect(92, 25, 18, 12, Haze);
    p.fill(95, 28, 12, 6, Brick);

    // Rainy window: a moonlit pane, distant skyline and deterministic drops.
    p.fill(8, 21, 74, 62, Ink);
    p.fill(11, 24, 68, 53, Slate);
    p.fill(14, 27, 62, 47, Night);
    p.fill(14, 49, 62, 25, Slate);
    p.fill(14, 64, 62, 10, Night);
    p.disc(56, 39, 9, Moon);
    p.disc(60, 36, 8, Night);
    p.fill(56, 31, 6, 2, Moon);
    p.fill(21, 33, 1, 1, Glow);
    p.fill(30, 42, 1, 1, Glow);
    p.fill(68, 29, 1, 1, Moon);
    p.fill(42, 27, 1, 1, Haze);
    for (int i = 0; i < 11; ++i) {
        const std::uint32_t drop = hashSeed(seed, static_cast<std::uint32_t>(0x100) +
                                                    static_cast<std::uint32_t>(i));
        const int x = 16 + static_cast<int>(drop % 58U);
        const int baseY = 27 + static_cast<int>((drop >> 8U) % 43U);
        const int drift = static_cast<int>(time / (170U + (drop & 3U) * 14U));
        const int y = 26 + (baseY + drift) % 47;
        const int length = 2 + static_cast<int>((drop >> 16U) & 3U);
        p.line(x, y, x - 1, y + length,
               ((time / 300U + static_cast<std::uint32_t>(i)) & 3U) == 0U ? Moon : Rain);
    }
    for (int i = 0; i < 8; ++i) {
        const std::uint32_t city = hashSeed(seed, static_cast<std::uint32_t>(0x180) +
                                                    static_cast<std::uint32_t>(i));
        const int x = 15 + i * 8;
        const int height = 5 + static_cast<int>((city >> 5U) % 11U);
        p.fill(x, 73 - height, 5 + static_cast<int>(city & 2U), height, Ink);
        if ((city & 1U) != 0U) {
            p.fill(x + 2, 72 - height, 1, 1,
                   ((time / 650U + static_cast<std::uint32_t>(i)) & 1U) != 0U ? Gold : Amber);
        }
    }
    p.fill(7, 77, 76, 5, Wood);
    p.fill(5, 81, 80, 3, Ink);
    p.fill(10, 78, 69, 2, Amber);

    // A shelf and a plant keep the left third of the room textured but low in
    // contrast, so the cat remains the visual anchor.
    p.fill(83, 48, 38, 4, Wood);
    p.fill(85, 51, 3, 31, Ink);
    p.fill(115, 51, 3, 31, Ink);
    p.fill(87, 78, 29, 4, Wood);
    p.fill(88, 57, 7, 16, Brick);
    p.fill(97, 54, 6, 19, Amber);
    p.fill(105, 60, 8, 13, Slate);
    p.fill(106, 58, 7, 2, Gold);
    drawPlant(p, 102, 42, seed);
    p.fill(22, 91, 24, 3, Ink);
    p.fill(23, 87, 7, 4, Amber);
    p.fill(31, 84, 6, 7, Brick);
    p.fill(38, 86, 7, 5, Gold);

    // Lamp and cup.  The small light response follows the audio level while
    // remaining bounded to a couple of pixels, avoiding a full-screen pulse.
    const int flicker = static_cast<int>((hashSeed(seed, time / 180U) >> 4U) & 3U);
    p.fill(121, 58, 4, 23, Wood);
    p.fill(116, 78, 15, 4, Wood);
    p.triangle(115, 45, 137, 45, 144, 59, flicker == 0 ? Amber : Gold);
    p.fill(120, 58, 19, 3, Glow);
    p.fill(127, 42, 4, 4, Glow);
    p.fill(128, 37, 2, 6, Amber);
    p.fill(126, 34, 6, 3, Gold);
    p.fill(128, 35, 2, 2, Glow);
    const int glowPulse = animate ? clampInt(static_cast<int>(view.level * 3.0f), 0, 3) : 0;
    if (animate && view.playing && glowPulse > 0) {
        p.fill(110 - glowPulse, 60, 2, 2, Amber);
        p.fill(144 + glowPulse, 59, 2, 2, Amber);
    }
    p.fill(143, 72, 12, 3, Wood);
    p.fill(145, 68, 8, 5, Cream);
    p.fill(146, 67, 6, 2, Haze);
    p.line(149, 67, 149, 63, Haze);
    p.line(153, 67, 153, 62, Haze);
    if (((time / 480U) & 1U) != 0U) {
        p.fill(149, 60, 1, 2, Haze);
        p.fill(153, 58, 1, 2, Haze);
    }

    // Rug and cat cushion.  The cat is about sixty pixels from ear tips to
    // paws and has a clean silhouette at the native display size.
    p.fill(70, 103, 121, 27, Brick);
    p.fill(76, 106, 109, 21, Amber);
    p.fill(84, 109, 93, 2, Gold);
    p.fill(84, 121, 93, 2, Brick);
    p.fill(156, 94, 70, 17, Ink);
    p.fill(158, 92, 65, 16, Wood);
    p.fill(164, 90, 53, 16, Brick);
    p.fill(169, 91, 43, 13, CatLight);

    const int beatNudge = animate && view.playing
                              ? static_cast<int>(view.beatPhase * 2.0f + 0.5f)
                              : 0;
    const int breath = animate && triangleWave(time / 40U, 22, 2) == 1 ? 1 : 0;
    const int nod = animate && view.playing ? ((beatNudge & 1) != 0 ? 1 : 0) : 0;
    const int catX = 168 + nod;
    const int catY = 45 + breath;

    // Tail behind the body, with a slower independent period than breathing.
    const int tailWave = centeredWave(time / 20U + (hashSeed(seed, 0x220U) & 31U),
                                      115, 4);
    p.line(198, 91, 210, 86 + tailWave / 3, Cat);
    p.line(199, 92, 211, 87 + tailWave / 3, Cat);
    p.line(210, 86 + tailWave / 3, 218, 76 + tailWave / 4, Cat);
    p.line(211, 87 + tailWave / 3, 219, 77 + tailWave / 4, Cat);
    p.fill(217, 74 + tailWave / 4, 5, 5, CatLight);

    // Ears, head, neck and body.
    p.triangle(catX - 7, catY + 16, catX - 3, catY - 2, catX + 7, catY + 7, Cat);
    p.triangle(catX + 22, catY + 7, catX + 32, catY - 2, catX + 34, catY + 17, Cat);
    p.triangle(catX - 2, catY + 5, catX, catY + 1, catX + 4, catY + 8, Brick);
    p.triangle(catX + 25, catY + 8, catX + 31, catY + 1, catX + 31, catY + 10, Brick);
    p.fill(catX - 5, catY + 8, 38, 25, Cat);
    p.fill(catX - 1, catY + 15, 30, 16, CatLight);
    p.fill(catX + 4, catY + 31, 28, 30, Cat);
    p.fill(catX + 9, catY + 36, 19, 22, CatLight);
    p.fill(catX + 3, catY + 56, 12, 5, Cat);
    p.fill(catX + 22, catY + 55, 12, 6, Cat);
    p.fill(catX + 1, catY + 58, 14, 4, Cream);
    p.fill(catX + 21, catY + 58, 14, 4, Cream);
    p.fill(catX + 4, catY + 29, 29, 4, Brick);
    p.fill(catX + 15, catY + 29, 6, 3, Gold);

    const std::uint32_t blinkClock = (time + hashSeed(seed, 0x2a0U) % 1400U) % 4300U;
    const bool blink = blinkClock > 3650U && blinkClock < 3790U;
    if (blink) {
        p.hLine(catX + 3, catX + 9, catY + 18, Ink);
        p.hLine(catX + 20, catX + 26, catY + 18, Ink);
    } else {
        p.fill(catX + 5, catY + 16, 5, 6, Ink);
        p.fill(catX + 21, catY + 16, 5, 6, Ink);
        p.fill(catX + 6, catY + 17, 2, 3, Gold);
        p.fill(catX + 22, catY + 17, 2, 3, Gold);
    }
    p.fill(catX + 13, catY + 23, 6, 4, Brick);
    p.fill(catX + 15, catY + 23, 2, 2, Ink);
    p.line(catX + 15, catY + 27, catX + 11, catY + 29, Ink);
    p.line(catX + 17, catY + 27, catX + 21, catY + 29, Ink);
    p.line(catX + 1, catY + 25, catX - 7, catY + 23, Haze);
    p.line(catX + 1, catY + 28, catX - 8, catY + 29, Haze);
    p.line(catX + 30, catY + 25, catX + 38, catY + 23, Haze);
    p.line(catX + 30, catY + 28, catX + 39, catY + 30, Haze);

    // A little book in the paws gives the character a calm listening/study
    // action without needing another sprite asset.
    p.fill(catX + 9, catY + 43, 22, 12, Slate);
    p.fill(catX + 10, catY + 44, 10, 10, Moon);
    p.fill(catX + 21, catY + 44, 9, 10, Cream);
    p.vLine(catX + 20, catY + 44, catY + 54, Brick);
    p.hLine(catX + 12, catX + 18, catY + 47, Haze);
    p.hLine(catX + 23, catX + 28, catY + 47, Amber);
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
    drawTinyText(frame, 42, 4, moodName(view.mood), Rain, 34);
    drawTinyNumber(frame, 82, 4, clampInt(view.bpm, 0, 999), Gold, 13);
    drawTinyText(frame, 98, 4, "BPM", Haze, 20);
    if (view.playing) {
        drawTinyText(frame, 126, 4, "PLAY", Leaf, 24);
        frame.fillRect(120, 4, 3, 6, Leaf);
    } else {
        drawTinyText(frame, 126, 4, "PAUSE", Amber, 28);
        frame.fillRect(120, 4, 2, 6, Amber);
        frame.fillRect(123, 4, 2, 6, Amber);
    }
    if (view.favorite) {
        drawStar(frame, 153, 7, Gold);
    }
    drawTinyText(frame, 164, 4, "VOL", Haze, 18);
    frame.fillRect(181, 5, clampInt(view.volume, 0, 100) / 10, 4, view.volume == 0 ? Brick : Gold);
    drawBattery(frame, 219, 3, view.batteryPercent);
}

void drawFooter(Frame& frame, const View& view) {
    frame.fillRect(0, 118, 240, 17, Ink);
    frame.fillRect(0, 118, 240, 1, Brick);
    if (view.notice[0] != '\0') {
        drawTinyText(frame, 4, 121, view.notice, Gold, 232);
        drawTinyText(frame, 4, 128, "SPACE PLAY   -/= VOL   H HELP", Haze, 232);
    } else if (view.pending) {
        drawTinyText(frame, 4, 121, "NEXT SESSION QUEUED", Gold, 130);
        drawTinyText(frame, 4, 128, "SPACE PLAY   -/= VOL   N NEXT", Haze, 232);
    } else {
        drawTinyText(frame, 4, 121, "SPACE PLAY   -/= VOL   N NEXT   M MOOD", Haze, 232);
        drawTinyText(frame, 4, 128, "F FAVORITE   L LIST   V CLEAN   S SET   H HELP", Haze, 232);
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
    drawTinyText(frame, 34, 108, "ENTER SELECT   ESC BACK   ,/. MOVE", Rain, 168);
}

void drawHelp(Frame& frame, const View& view) {
    frame.fillRect(10, 15, 220, 105, Ink);
    frame.rect(10, 15, 220, 105, Haze);
    frame.rect(13, 18, 214, 99, Brick);
    drawText(frame, 20, 22, "POCKET LOFI KEYS", Cream, 200);
    static const char* const lines[] = {
        "SPACE PLAY / PAUSE", "- = VOLUME", "N NEXT SESSION", "M MOODS",
        "F FAVORITE", "L FAVORITES", "V CLEAN VIEW", "S SETTINGS",
        "H HELP", "ENTER SELECT", "ESC BACK", ", /. MOVE",
    };
    for (int i = 0; i < 12; ++i) {
        const int x = (i & 1) == 0 ? 20 : 126;
        const int y = 34 + (i / 2) * 11;
        drawTinyText(frame, x, y + 1, lines[i], (i == 0 || i == 8) ? Gold : Moon, 96);
    }
    drawTinyText(frame, 20, 108, view.notice[0] != '\0' ? view.notice : "SMALL KEYS KEEP THE ROOM QUIET", Rain, 198);
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
    drawTinyText(frame, 34, 108, "ESC BACK", Rain, 178);
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
    const std::uint8_t c = static_cast<std::uint8_t>(safeColour(colour) * 0x11U);
    for (std::size_t i = 0; i < packedBytes; ++i) {
        pixels_[i] = c;
    }
}

std::uint8_t Frame::get(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return Ink;
    }
    const std::uint8_t packed = pixels_[static_cast<std::size_t>(y * width + x) >> 1U];
    return (x & 1) == 0 ? static_cast<std::uint8_t>(packed >> 4U)
                        : static_cast<std::uint8_t>(packed & 0x0fU);
}

void Frame::set(int x, int y, std::uint8_t colour) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(y * width + x) >> 1U;
    const std::uint8_t c = safeColour(colour);
    if ((x & 1) == 0) {
        pixels_[index] = static_cast<std::uint8_t>((pixels_[index] & 0x0fU) |
                                                   static_cast<std::uint8_t>(c << 4U));
    } else {
        pixels_[index] = static_cast<std::uint8_t>((pixels_[index] & 0xf0U) | c);
    }
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
    drawScene(frame, view, showOverlay);
    if (view.screen == Screen::Radio) {
        if (!view.clean) {
            drawStatus(frame, view, "RADIO");
            drawFooter(frame, view);
        } else if (!view.playing || view.pending) {
            frame.fillRect(4, 4, 68, 10, Ink);
            drawTinyText(frame, 8, 6, view.playing ? "NEXT QUEUED" : "PAUSED", view.pending ? Gold : Amber, 60);
        }
        return;
    }
    drawStatus(frame, view, view.screen == Screen::Help ? "HELP" :
                            view.screen == Screen::Diagnostics ? "INFO" : "MENU");
    drawOverlay(frame, view);
    frame.fillRect(0, 120, 240, 15, Ink);
    frame.fillRect(0, 120, 240, 1, Brick);
    drawTinyText(frame, 4, 125, "ENTER SELECT   ESC BACK   ,/. MOVE", Haze, 232);
}

} // namespace lofi
