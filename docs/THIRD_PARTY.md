# Sources and attribution

The music composer, synthesis code and local instrument-bank generator were authored for this implementation. The original room and cat artwork was created for this project with the built-in OpenAI imagegen tool, then resized and palette-converted for the shared renderer. Source images, exact prompts and conversion provenance are retained in `assets/source/imagegen-v1` and `assets/runtime/imagegen-v1`. No Lofi Girl recordings or artwork, and no Lofi Cat music or image assets, are bundled.

Reference projects informed the design and delivery workflow:

- [Lofi Cat](https://github.com/m0rg0t/lofi_cat): radio experience and scene structure.
- Local Cardputer ADV Hermes Terminal, Cardputer Oracle and Cardputer Agent: board/toolchain, audio shutdown, SD, M5Apps and real-code screenshot practices. See [reference notes](REFERENCES.md) for inspected sources.

Published methods implemented from their papers, with no third-party code:

- Plate reverb: Jon Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", *Journal of the Audio Engineering Society* 45(9), September 1997, pp. 660–684. The topology, delay lengths and diffusion constants come from the paper; lengths are rescaled to a 16 kHz network.
- [fm1-amb](https://github.com/charlesvestal/fm1-amb) by Charles Vestal (MPL-2.0) was read as a design reference for the reverb's role and for slow random-target modulation. None of its files or code are included.

Firmware dependencies retain their own licenses; consult the pinned source distributions:

| Dependency | Pin | License/source |
| --- | --- | --- |
| M5Unified | 0.2.17 | MIT; https://github.com/m5stack/M5Unified |
| M5GFX | 0.2.22 | MIT; https://github.com/m5stack/M5GFX |
| M5Cardputer | 1.1.1 | MIT; https://github.com/m5stack/M5Cardputer |
| Arduino-ESP32 | 2.0.17 through PlatformIO espressif32 7.0.1 | LGPL-2.1-or-later with bundled components; https://github.com/espressif/arduino-esp32 |
| ESP-IDF components | Bundled with Arduino | Apache-2.0 and component licenses; https://github.com/espressif/esp-idf |
| IRremote (M5Cardputer dependency) | 4.7.1 | MIT; https://github.com/Arduino-IRremote/Arduino-IRremote |
| Emscripten (browser player build only) | 7.0.0 | MIT / University of Illinois-NCSA; https://github.com/emscripten-core/emscripten |
| SDL2 (desktop only) | Host dependency | zlib; https://github.com/libsdl-org/SDL |

Development-time ElevenLabs sound effects remain private, unapproved auditions. The built-in bank contains only the reproducible local sounds. Public distribution of a future generated pack requires a source/provenance and rights review for that pack.

Project code and documentation are under the MIT License (`LICENSE`); artwork and sounds are under CC BY 4.0 (`LICENSE-ASSETS.md`). Dependency notices should be reviewed again before a public release.
