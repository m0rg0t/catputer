# Implementation and verification

This is the first local feasibility implementation of the accepted plan, version **0.1.0-dev**. It keeps both sound candidates available for listening and physical testing. The production engine has not been selected.

## Implemented

- Shared C++17 composer and renderer with deterministic 64-bit seeds, three moods, six arrangement sections and automatic session changes.
- Eight bounded voices; pure synth and hybrid instruments; seven locally generated hybrid one-shots totaling 58,240 PCM bytes.
- Offline 32 kHz mono PCM16 output. The ADV adapter rotates three 512-frame buffers through the pinned M5Unified speaker API; commands are copied to the audio task and SD work runs separately.
- Original 240 × 135 pixel-art cat/room renderer. Packed scene storage is 16,200 bytes plus a 480-byte output row. Engine inline storage is 8,192 bytes.
- Menus, help, settings, favorites, clean scene and motion levels. Fixed 160-byte versioned/CRC-checked optional SD state with valid backup/temp recovery. No-SD state lives in RAM.
- Native SDL preview and WAV/PPM exports, deterministic sample tooling, a media exporter, allowlisted static site builder and app-only package builder.
- Arduino generic NVS initialization is wrapped out; ELF symbol inspection confirmed the wrapper is linked. This app has no NVS clients.

## Host checks

Normal CMake/CTest and an UndefinedBehaviorSanitizer build pass the music, state, controller and native storage suites. Tests cover deterministic PCM across block sizes, identical A/B score hashes, bounded voices, favorite parsing, pause/resume, arrangement/session progression, malformed saves, CRC corruption, menu boundaries and recovery from a corrupt primary save. Audio command sequencing and transition regressions are included after review.

The SDL preview completed a bounded run with dummy audio/video drivers and optional local state. All exported UI scenarios also ran under UBSAN. The actual renderer was visually inspected at an integer scale: cat/room, main HUD, menus and full 64-bit diagnostic seed are legible.

Python tests cover the selected bank, format/order/hash/loop/size rejection and application-image checksum/hash/target/bounds validation. AddressSanitizer's runtime stalls during initialization on this host; no ASAN pass is claimed.

Audio comparison measurements are recorded below. The completed firmware build is below the compact profile; final image identity and checksums are in the distribution manifest. Desktop timings are not a device real-time guarantee; clipping/energy checks do not establish pleasant sound.

## Remaining physical and later milestone work

- Listen through the ADV speaker and jack; compare the two candidates, then select/tune the production sound.
- Measure startup time, internal free heap/largest block, task stack margins, render deadlines and audible dropouts while animation/keys/SD saves are active. Serial `queue_empty` counts are source-queue observations, not verified DMA underruns.
- Perform the planned two-hour **device** soak and battery/runtime measurements.
- Install through the intended M5Apps layout, verify Home/return behavior and neighboring applications, and exercise physical SD failure/recovery. No device has been flashed by this task.
- Implement external SD sample/scene packs later. The current optional SD feature is settings/favorites only.
- Select the public project license and remote repository, then publish only when requested. The local Git repository and README are prepared; the site is local.

## Practical behavior

Controls received within the last 20 ms of a bar may wait one extra bar, allowing a complete fade. Favorites replay from the beginning and are version/bank dependent. Rapid changes are coalesced while preserving accepted musical parameters. SD write failures keep changes in RAM and stop saves until restart; hot-plug recovery is not implemented.

## Recorded comparison · 2026-09-13

All short examples use seed `0xCA7CAFE`, 90 seconds, volume 78 in the core, texture 18 and the same score within each mood. Values are PCM16 units before the preview/device master volume. Every render has zero clipped samples and at most eight active voices.

| Mood | Synth RMS / peak | Hybrid RMS / peak | Events | Shared score hash |
| --- | ---: | ---: | ---: | --- |
| Cozy | 1060 / 9522 | 948 / 7941 | 471 | `78df7553ed0a92f` |
| Rainy | 963 / 8533 | 877 / 7568 | 391 | `20f13e2fe0362668` |
| Night | 963 / 8829 | 882 / 6984 | 418 | `4c6bc6a3e68c1b21` |

Two-hour host renders use Rainy seed `0x6a09e667f3bcc909`: 230,400,000 frames each, 38,084 identical events, score hash `7e8a54196eec670b`, zero clips, eight voices maximum. Synth RMS/peak: 1032/10387; hybrid: 935/7941. They rendered in approximately 8.18 and 8.47 seconds on this Mac. These are offline throughput checks, not two hours of device operation. [Synth evidence](evidence/soak-synth.json), [hybrid evidence](evidence/soak-hybrid.json).

The local website layout and sample playback progress were verified in the browser. Native browser audio controls caused an embedded-browser crash during review; the site now uses small accessible play/pause controls and MP3 downloads, and playback was rechecked successfully. Only one sample plays at a time.

The pinned M5GFX tag `0.2.22` was verified locally and used through an ignored local build config because the dependency download was unavailable. The public PlatformIO config retains the upstream tag URL and pinned dependency versions.

## Final firmware artifact

Application BIN: 616384 bytes; 694336 bytes below the `0x140000` compact limit. Linker static RAM: 50,936 bytes of the 327,680-byte linker budget; dynamic heap/DMA/task overhead still needs device measurements. The project/version marker and ESP application descriptor, image checksum and appended hash are validated. The package contains only the application, install guide, dependency notes, manifest and checksums.

BIN SHA-256: `13644d9ddead1d61160a7b4b667fbb9b370d26b6f84b8ec31b00c9fdbcf86553`.

Final test inventory: four native CTest suites (normal and UBSAN), 22 Python tests, real bootloader rejection, validated release archive, SDL smoke run and native-renderer screenshot exports.
