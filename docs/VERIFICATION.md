# Implementation and verification

This is the local development candidate for the accepted plan, version **0.1.1-dev**. It keeps both sound candidates available for listening and physical testing. The production engine has not been selected.

## Implemented

- Shared C++17 composer and renderer with deterministic 64-bit seeds, three moods, six arrangement sections and automatic session changes.
- Twelve bounded voices; pure synth and hybrid instruments; seven locally generated hybrid one-shots totaling 58,240 PCM bytes.
- Offline 32 kHz mono PCM16 output. The ADV adapter rotates three 512-frame buffers through the pinned M5Unified speaker API; commands are copied to the audio task and SD work runs separately.
- Original imagegen room/cat artwork adapted for the shared 240 × 135 renderer. The 64-color indexed framebuffer uses 32,400 bytes plus a 480-byte output row. Background, six 64 × 72 cat frames and the RGB565 palette use 60,176 bytes of constant scene data in flash. Engine inline storage is 8,192 bytes.
- Menus, help, settings, favorites, clean scene and motion levels. Fixed 160-byte versioned/CRC-checked optional SD state with valid backup/temp recovery. No-SD state lives in RAM.
- Native SDL preview and WAV/PPM exports, deterministic sample tooling, a media exporter, allowlisted static site builder and app-only package builder.
- Arduino generic NVS initialization is wrapped out; ELF symbol inspection confirmed the wrapper is linked. This app has no NVS clients.

## Host checks

Normal CMake/CTest and an UndefinedBehaviorSanitizer build pass the music, state, controller, native storage and UI suites. Tests cover deterministic PCM across block sizes, identical A/B score hashes, bounded voices, favorite parsing, pause/resume, arrangement/session progression, malformed saves, CRC corruption, menu boundaries and recovery from a corrupt primary save. Audio command sequencing and transition regressions are included after review. The UI suite verifies all 64 indices through RGB565 row conversion, row-buffer guards, off-screen writes, still-motion behavior and valid indices in every screen.

The SDL preview completed a bounded run with dummy audio/video drivers and optional local state. All exported UI scenarios also ran under UBSAN. The actual renderer was visually inspected at an integer scale: cat/room, main HUD, menus and full 64-bit diagnostic seed are legible.

Python tests cover the selected bank, format/order/hash/loop/size rejection and application-image checksum/hash/target/bounds validation. AddressSanitizer's runtime stalls during initialization on this host; no ASAN pass is claimed.

The imagegen asset update adds deterministic scene conversion tests for palette order, transparency, dimensions and sprite-sheet scanline order. The final native preview exports 15 UI states and 144 animation frames (12 seconds at 12 FPS). The room/cat composition and UI contact sheet were visually reviewed; sprites use real indexed transparency with no magenta colors remaining in the packed palette. The six poses share a fixed bench anchor. Source masters, prompts and hashes are retained with the assets. The new framebuffer adds 16,200 bytes of static RAM, so physical heap and timing checks remain necessary.

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

All short examples use generation schema 2, seed `0xCA7CAFE`, 90 seconds, volume 78 in the core, texture 18 and the same score within each mood. Values are PCM16 units before the preview/device master volume. Every render has zero clipped samples, zero dropped note events and at most twelve active voices.

| Mood | BPM | Synth RMS / peak | Hybrid RMS / peak | Events | Shared score hash |
| --- | ---: | ---: | ---: | ---: | --- |
| Cozy | 76 | 1127 / 10807 | 1008 / 6746 | 613 | `a89ea1a234267397` |
| Rainy | 71 | 1115 / 9471 | 1000 / 7839 | 576 | `c3b63baf89db1539` |
| Night | 73 | 1078 / 9109 | 982 / 7041 | 594 | `c8d57979e095690e` |

The earlier eight-voice composer left the lead out until bar 24 and withheld full drums until bar 8. Schema 2 gives the first bar keys, bass, a quiet kick/snare/hat groove and a three-note lead answer. The intro lasts four bars, recurring melodic answers continue through Groove, and the main melodic section begins at bar 12. The wider voice pool and revised allocation favor completed note tails and prevent percussion from taking harmonic voices.

| Mood | First 10s events, before → after | Voice steals per 90s, before → after | New lead entry |
| --- | ---: | ---: | ---: |
| Cozy | 18 → 61 | 88 → 2 | 1.24 s |
| Rainy | 14 → 52 | 65 → 2 | 1.33 s |
| Night | 15 → 58 | 74 → 5 | 1.31 s |

These are combined arrangement/polyphony changes at the same seed; schema 2 also changes the generated score and tempo. [Baseline evidence](evidence/opening-before-schema1.json) records the previous source revision. A voice steal replaces an existing voice with a smoothed tail; it is distinct from a dropped incoming note. Actual-start counters prove every intended opening instrument enters within the first two bars in all mood/backend tests.

The two-hour host renders use Rainy seed `0x6a09e667f3bcc909`: 230,400,000 frames each, 42,280 identical events, score hash `e881d18200461819`, zero clips, zero dropped note events and twelve voices maximum. Both have 300 voice steals over two hours. Synth RMS/peak: 1096/10603; hybrid: 993/8641. They rendered in approximately 9.68 and 10.13 seconds on this Mac. These are offline throughput checks, not two hours of device operation. [Synth evidence](evidence/soak-synth.json), [hybrid evidence](evidence/soak-hybrid.json).

The native and firmware builds now read the same VERSION. Audio demo exports bind each MP3 to its WAV and metadata with SHA-256. The site requires the current version/schema, correct mood/engine, matching duration/score and matching MP3 hash; new file content gets a new browser URL. Earlier `lofi1-` favorites remain stored and labeled `OLD`, with replay blocked under schema 2 instead of silently producing different music.

The local website layout and sample playback progress were verified in the browser. Native browser audio controls caused an embedded-browser crash during review; the site now uses small accessible play/pause controls and MP3 downloads, and playback was rechecked successfully. Only one sample plays at a time.

The pinned M5GFX tag `0.2.22` was verified locally and used through an ignored local build config because the dependency download was unavailable. The public PlatformIO config retains the upstream tag URL and pinned dependency versions.

## Final firmware artifact

Application BIN: 675,424 bytes; 635,296 bytes below the `0x140000` compact limit. Linker static RAM: 67,136 bytes of the 327,680-byte linker budget; dynamic heap/DMA/task overhead still needs device measurements. The project/version marker and ESP application descriptor, image checksum and appended hash are validated. The package contains only the application, install guide, dependency notes, manifest and checksums.

BIN SHA-256: `d8baf4fd9ff39314f3063910a46edbb3c181af7e283188ffa5c519c85c842887`.

Final test inventory: five native CTest suites (normal and UBSAN), 29 Python tests, real bootloader rejection, validated release archive, SDL smoke run and native-renderer screenshot exports. The opening/polyphony update includes new audio exports, schema-1 save retention and stale-audio rejection tests.
