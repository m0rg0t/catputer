# Sources and attribution

The music composer, synthesis code, local instrument-bank generator and pixel-art scene in this project were authored for this implementation. No Lofi Girl recordings or artwork, and no Lofi Cat music or image assets, are bundled.

Reference projects informed the design and delivery workflow:

- [Lofi Cat](https://github.com/m0rg0t/lofi_cat): radio experience and scene structure.
- Local Cardputer ADV Hermes Terminal, Cardputer Oracle and Cardputer Agent: board/toolchain, audio shutdown, SD, M5Apps and real-code screenshot practices. See [reference notes](REFERENCES.md) for inspected sources.

Firmware dependencies retain their own licenses; consult the pinned source distributions:

| Dependency | Pin | License/source |
| --- | --- | --- |
| M5Unified | 0.2.17 | MIT; https://github.com/m5stack/M5Unified |
| M5GFX | 0.2.22 | MIT; https://github.com/m5stack/M5GFX |
| M5Cardputer | 1.1.1 | MIT; https://github.com/m5stack/M5Cardputer |
| Arduino-ESP32 | 2.0.17 through PlatformIO espressif32 7.0.1 | LGPL-2.1-or-later with bundled components; https://github.com/espressif/arduino-esp32 |
| ESP-IDF components | Bundled with Arduino | Apache-2.0 and component licenses; https://github.com/espressif/esp-idf |
| IRremote (M5Cardputer dependency) | 4.7.1 | MIT; https://github.com/Arduino-IRremote/Arduino-IRremote |
| SDL2 (desktop only) | Host dependency | zlib; https://github.com/libsdl-org/SDL |

Development-time ElevenLabs sound effects remain private, unapproved auditions. The built-in bank contains only the reproducible local sounds. Public distribution of a future generated pack requires a source/provenance and rights review for that pack.

A project-wide public license has not been selected yet. Dependency notices and the project license should be finalized before a public release.
