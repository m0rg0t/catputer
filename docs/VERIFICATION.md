# Implementation and verification

This is the local development candidate for the accepted plan, version **0.1.4-dev**. It keeps both sound candidates available for listening and physical testing. The production engine has not been selected.

## Implemented

- Shared C++17 composer and renderer with deterministic 64-bit seeds, three moods, six arrangement sections and automatic session changes.
- Twelve bounded voices; pure synth and hybrid instruments; seven locally generated hybrid one-shots totaling 58,240 PCM bytes.
- Offline 32 kHz mono PCM16 output. The ADV adapter rotates three 512-frame buffers through the pinned M5Unified speaker API; commands are copied to the audio task and SD work runs separately.
- AUTO or manual 40–180 BPM, with tempo-only changes at bar boundaries. Shared 0–300% output gain, 10 ms volume ramps and a soft limiter before the normalized device output path.
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
- Recheck the 0.1.4-dev display, manual tempo and boosted output on the ADV, verify Home/return behavior and neighboring applications, and exercise physical SD failure/recovery. The user ran 0.1.2-dev on the device and supplied a photo; that establishes startup, not complete installation or hardware acceptance.
- Implement external SD sample/scene packs later. The current optional SD feature is settings/favorites only.
- Select the public project license and remote repository, then publish only when requested. The local Git repository and README are prepared; the site is local.

## Practical behavior

Mood/engine/replay requests received within the last 20 ms of a bar may wait one extra bar, allowing a complete fade. Tempo-only changes take effect at the next bar without a restart. Favorites replay from the beginning and are version/bank dependent. Rapid changes are coalesced while preserving accepted musical parameters. SD write failures keep changes in RAM and stop saves until restart; hot-plug recovery is not implemented.

## Manual BPM and output gain · 2026-09-13

Settings now has eight rows, including BPM and volume up to 300%. BPM uses AUTO or 40–180 in one-beat-per-minute steps; Enter on the BPM row toggles AUTO/manual. Settings and favorite records preserve the selected mode. Format 2 remains exactly 160 bytes: the original favorite record bytes are retained, volume's high byte occupies header byte 6, global BPM byte 15, and favorite BPM bytes 144–151. The decoder migrates valid format-1 files to AUTO and rejects their previously invalid volume values above 100. Format 2 validates the wider volume, BPM ranges, reserved bytes and CRC. No private SD state is included in test fixtures or packages.

Tempo changes preserve the current session, notes and fractional sample clock. Coalescing back to the active tempo cancels the pending change; reaching a control limit does not restart playback. Favorite replay explicitly requests a restart even at the same seed. AUTO consumes the same random draws as before; its regression reference (Rainy/Synth, seed `0x0123456789abcdef`, texture 23, 14 seconds) remains BPM 74, 84 note starts, score hash `e2ebee7135b74f78` and PCM16 hash `4ee8918a12e09f44`.

At manual tempos above the session's AUTO tempo, keys, bass and lead release tails shorten proportionally in seconds to retain their approximate length in beats. This keeps the existing twelve-voice pool usable at faster tempos. The 180 BPM Cozy/Synth regression improved from five dropped incoming notes to zero in 60 seconds. The final [tempo and gain sweep](evidence/tempo-gain-check.json) covers 40, 120 and 180 BPM, all three moods and both engines at 300%: 18 × 60 seconds, 34,560,000 frames, zero clipped samples, zero dropped note events and matching A/B score hashes/counts. The largest output peak was 31,050; AUTO audio retains its previous regression hash. These finite host checks do not establish all-seed behavior or device audio deadlines.

The output-stage tests cover every PCM16 input at 300%, volume values spanning 255/256, soft-limiter bounds, monotonic gain, block-size independence, mute and ramp retargeting. Review against pinned M5Unified caught its additional mono gain after synthesis. The corrected adapter uses magnification 8 and moves the previous 2× pre-gain ahead of the limiter; its remaining digital gain is about 0.98447, so the 32,700 limiter ceiling stays below 32,193 at the final library conversion. Below 100%, the prior level curve is preserved apart from rounding and limited peaks. This proves a digital bound, not speaker distortion or perceived loudness on hardware.

The native player uses the same output stage. WAV exports remain raw engine comparisons unless `--volume` is explicitly supplied; metadata records whether the stage was applied. `--bpm auto|40..180` controls native playback and exports. The updated 18-screen contact sheet was visually reviewed, including 300%, manual BPM and AUTO BPM. Seven native suites pass in normal and UBSAN builds; a dummy SDL audio/video run at 180 BPM/300% and the screenshot exporter also pass under UBSAN.

## LCD correction · 2026-09-13

The user's first device photo showed recognizable scene geometry with scrambled colors and unreadable text. The native exporter bypasses M5GFX, so its earlier screenshots did not validate the physical transfer. The renderer emits host-order RGB565 words; M5GFX 0.2.22 defaults to treating `uint16_t` image data as already byte-swapped for the LCD. Explicit `Display.setSwapBytes(true)` makes the pinned library convert those words to the LCD byte order. A probe against the actual library reproduced the red-pixel difference: the old path sends `00 F8`, and the corrected path sends `F8 00`. The checked-in `display_rgb565` regression exercises the pinned pixel conversion for all 64 palette entries: the corrected conversion passes all 64, and the old raw path fails 63. Enable this optional CTest target by configuring `LOFI_M5GFX_SOURCE_DIR` with the path to an M5GFX 0.2.22 checkout; it checks library conversion, not physical SPI or the LCD.

Status labels, control hints, clean-view status and help now use 5 × 7 glyphs instead of 3 × 5. The radio footer has two separated rows on a solid dark background and fewer simultaneous shortcuts; help lists the remaining controls, including engine selection and the distinct movement/adjustment keys. The 15 updated native screenshots were visually reviewed. This remains host evidence until the corrected build is checked on the same device.

The 0.1.3-dev LCD update left the composer, samples, schema and save format unchanged. Its six 180-second demos retained their previous score hashes and zero clipped samples. The older score-audit and two-hour soak records below retain their original version metadata and establish the AUTO-mode baseline.

## Recorded comparison · 2026-09-13

All current examples use generation schema 3, seed `0xCA7CAFE`, **180 seconds**, volume 78 in the core, texture 18 and the same score within each mood. Values are PCM16 units before the preview/device master volume. Every render has zero clipped samples, zero dropped note events and at most twelve active voices.

| Mood | BPM | Synth RMS / peak | Hybrid RMS / peak | Events | Shared score hash |
| --- | ---: | ---: | ---: | ---: | --- |
| Cozy | 82 | 1148 / 9538 | 1035 / 6802 | 1173 | `2999fdd6804daf28` |
| Rainy | 76 | 1111 / 9384 | 1004 / 7733 | 1080 | `7114bfa2295533a6` |
| Night | 78 | 1067 / 9000 | 961 / 7386 | 1113 | `f1c0237d0c5771e6` |

The earlier eight-voice composer left the lead out until bar 24 and withheld full drums until bar 8. The current first bar includes keys, bass, a quiet kick/snare/hat groove and the seed's melody hook. The intro remains four bars. The wider voice pool and revised allocation favor completed note tails and prevent percussion from taking harmonic voices.

| Mood | First 10s events, schema 1 → schema 3 | Current lead entry | Voice steals over current 180s |
| --- | ---: | ---: | ---: |
| Cozy | 18 → 64 | First beat | 7 |
| Rainy | 14 → 61 | First beat | 7 |
| Night | 15 → 62 | 0.42 s | 2 |

Every opening has zero steals or dropped notes during its first ten seconds. These are combined arrangement/polyphony changes at the same seed; changing schema changes the score and tempo. [Baseline evidence](evidence/opening-before-schema1.json) records the original source revision. A voice steal replaces an existing voice with a smoothed tail; it is distinct from a dropped incoming note.

Schema 3 adds related progressions every eight bars, the original progression's return every 32 bars, carried chord voicings, scale-aware bass approaches, a returning melody hook and varied responses/endings. Bass uses MIDI 32–48 and lead MIDI 64–83. Keys, bass and drums rotate their rhythm/articulation patterns with phrases. The notes remain composed on the device; neither backend plays a recorded song or backing loop.

The independent [score audit](evidence/score-analysis.json) covers three starting seeds (`0xCA7CAFE`, `0x0123456789ABCDEF`, `0xBADC0FFEE0DDF00D`) in all three moods and both engines: **18 × 480 seconds**, 52,080 scheduled notes including automatic session changes. All nine A/B CSV pairs match byte-for-byte. There are zero scale/chord-anchor/passing-resolution/register violations, with at most 25 scheduled notes in a bar against the 48-note capacity. Each eight-minute example contains 20–25 distinct complete four-bar lead phrases and 5–9 distinct four-bar harmonic sequences across its sessions; velocity and microtiming alone do not count as new phrases. Maximum observed consecutive movement is ten semitones in the bass and nine in the lead. These are rule and variety measurements, not subjective listening scores.

The C++ behavior suite additionally checks sustained/strong lead chord tones, stepwise passing-note resolution, voice movement, recurring hook rhythm, harmony return and rhythmic variants. CSV validation checks current schema, timestamps, complete-phrase horizons, note bounds, duplicate/missing events and consistent automatic-session offsets. Timed exports ending before a passing note's target mark that continuation as unobserved.

The two-hour host renders use Rainy seed `0x6a09e667f3bcc909`: 230,400,000 frames each, 41,103 identical events, score hash `e82aeca769ce3b82`, zero clips, zero dropped note events and twelve voices maximum. Both have 277 voice steals over two hours. Synth RMS/peak: 1107/10328; hybrid: 1001/8235. These are offline throughput checks, not two hours of device operation. [Synth evidence](evidence/soak-synth.json), [hybrid evidence](evidence/soak-hybrid.json).

The native and firmware builds read the same VERSION. Audio demo exports bind each MP3 to its WAV and metadata with SHA-256. The site requires the current version/schema, correct mood/engine, matching duration/score and matching MP3 hash; new file content gets a new browser URL. Earlier `lofi1-` and `lofi2-` favorites remain stored and labeled `OLD`, with replay blocked under schema 3 instead of silently producing different music. Existing settings are retained.

The local website layout and sample playback progress were verified in the browser. Native browser audio controls caused an embedded-browser crash during review; the site now uses small accessible play/pause controls and MP3 downloads, and playback was rechecked successfully. Only one sample plays at a time.

The pinned M5GFX tag `0.2.22` was verified locally and used through an ignored local build config because the dependency download was unavailable. The public PlatformIO config retains the upstream tag URL and pinned dependency versions.

## Final firmware artifact

Application BIN: 681,168 bytes; 629,552 bytes below the `0x140000` compact limit. Linker static RAM: 67,208 bytes of the 327,680-byte linker budget; dynamic heap/DMA/task overhead still needs device measurements. The project/version marker and ESP application descriptor, image checksum and appended hash are validated. The package contains only the application, install guide, dependency notes, manifest and checksums.

BIN SHA-256: `c5467348447f9f29bbffabfbfefc642fdb1309e793b700c6eff6fe4559f20400`.

The 0.1.4-dev update passes seven native CTest suites (including output gain and the optional M5GFX conversion check), the same seven under UBSAN, and 36 Python tests. Its application image, release archive and 18 native-renderer screenshots are validated. The preceding harmony update's evidence includes 18 score exports, six three-minute A/B demos and two two-hour offline renders; those AUTO-mode baselines remain distinct from the new tempo/gain sweep above.
