// Host-only regression probe for the firmware's uint16_t pushImage path.
//
// The UI row converter intentionally returns host-order RGB565 words.  The
// ADV display is a byte-oriented M5GFX panel, so Display.setSwapBytes(true)
// must be enabled before pushImage(uint16_t*) converts those words to wire
// order.  This probe uses the pinned M5GFX pixelcopy implementation itself:
// it checks that the old raw path fails for non-symmetric palette words and
// that the swap-bytes conversion produces the expected two wire bytes for
// every palette entry.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "lofi/ui.h"
#include "lgfx/v1/misc/pixelcopy.hpp"

namespace {

using namespace lgfx::v1;

void convertToWire(std::uint16_t logical, std::uint8_t out[2]) {
    rgb565_t source(logical);
    swap565_t converted;
    pixelcopy_t copyParams{};
    copyParams.src_data = &source;
    copyParams.src_x32 = 0;
    copyParams.src_y32 = 0;
    copyParams.src_x32_add = 1u << pixelcopy_t::FP_SCALE;
    copyParams.src_y32_add = 0;
    copyParams.src_bitwidth = 1;
    copyParams.transp = pixelcopy_t::NON_TRANSP;

    const auto copy = pixelcopy_t::get_fp_copy_rgb_affine<rgb565_t>(rgb565_2Byte);
    assert(copy != nullptr);
    copy(&converted, 0, 1, &copyParams);
    std::memcpy(out, &converted, sizeof(converted));
}

} // namespace

int main() {
    std::uint16_t endianProbe = 1;
    assert(*reinterpret_cast<const std::uint8_t*>(&endianProbe) == 1);

    lofi::Frame frame;
    std::uint16_t row[lofi::kScreenWidth] = {};
    unsigned oldRawFailures = 0;
    for (std::uint8_t index = 0; index < lofi::kPaletteSize; ++index) {
        frame.set(0, 0, index);
        frame.rowRgb565(0, row);
        const std::uint16_t logical = row[0];
        assert(logical == lofi::Frame::paletteRgb565(index));
        const auto* oldRaw = reinterpret_cast<const std::uint8_t*>(&logical);
        const std::uint8_t expected[2] = {
            static_cast<std::uint8_t>(logical >> 8),
            static_cast<std::uint8_t>(logical & 0xffu),
        };

        // This is the behavior of pushImage while _swapBytes is false: the
        // host-order uint16_t is sent as native little-endian memory.
        if (oldRaw[0] != expected[0] || oldRaw[1] != expected[1]) {
            ++oldRawFailures;
        }

        std::uint8_t converted[2] = {};
        convertToWire(logical, converted);
        assert(converted[0] == expected[0]);
        assert(converted[1] == expected[1]);
    }

    std::printf("palette=%d old_raw_failures=%u swapped_conversion=pass\n",
                lofi::kPaletteSize, oldRawFailures);
    assert(oldRawFailures > 0);
    return 0;
}
