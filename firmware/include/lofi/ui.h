#pragma once

#include <cstddef>
#include <cstdint>

namespace lofi {

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 135;
constexpr int kPaletteSize = 16;

// The shared renderer consumes this small, platform-neutral snapshot.  The
// controls/platform task owns the strings and keeps them NUL-terminated.
enum class Screen : std::uint8_t {
    Radio = 0,
    Moods,
    Favorites,
    Settings,
    Help,
    Diagnostics,
};

struct View {
    Screen screen = Screen::Radio;
    std::uint64_t timeMs = 0;
    std::uint32_t seed = 0;
    int mood = 0;
    int bpm = 72;
    int volume = 70;
    int batteryPercent = -1;
    int selection = 0;
    int itemCount = 0;
    bool playing = true;
    bool clean = false;
    bool sdReady = false;
    bool favorite = false;
    bool pending = false;
    // 0 = still scene, 1 = reduced motion, 2 = full motion.
    std::uint8_t motion = 2;
    float beatPhase = 0.0f;
    float level = 0.0f;
    char notice[40] = {};
    char items[8][32] = {};
};

// A fixed 4-bit indexed canvas.  Two pixels are packed into each byte,
// with the even x coordinate in the high nibble.  The backing store is
// deliberately exposed only through accessors so callers cannot accidentally
// introduce an RGB565-sized framebuffer on the ADV.
class Frame {
public:
    static constexpr int width = kScreenWidth;
    static constexpr int height = kScreenHeight;
    static constexpr std::size_t packedBytes =
        static_cast<std::size_t>(width * height / 2);

    Frame();

    void clear(std::uint8_t colour = 0);
    std::uint8_t get(int x, int y) const;
    void set(int x, int y, std::uint8_t colour);

    void hLine(int x0, int x1, int y, std::uint8_t colour);
    void vLine(int x, int y0, int y1, std::uint8_t colour);
    void fillRect(int x, int y, int w, int h, std::uint8_t colour);
    void rect(int x, int y, int w, int h, std::uint8_t colour);
    void line(int x0, int y0, int x1, int y1, std::uint8_t colour);

    // Convert one indexed row to the format expected by M5GFX/SDL.  `out`
    // must point to at least `width` uint16_t values.
    void rowRgb565(int y, std::uint16_t* out) const;
    const std::uint8_t* packed() const { return pixels_; }

    static std::uint16_t paletteRgb565(std::uint8_t colour);
    static const std::uint16_t* paletteRgb565();

private:
    std::uint8_t pixels_[packedBytes]{};
};

// Draw one complete frame.  No allocation, file access, display calls or
// mutable global state occurs here; the same function is used by native and
// device front ends.
void render(Frame& frame, const View& view);

} // namespace lofi
