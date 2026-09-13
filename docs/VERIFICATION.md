# Implementation and verification

This is the development candidate for the accepted plan, version **0.1.8-dev**. It keeps both sound candidates available for listening and physical testing. The production engine has not been selected.

## Mix, phrasing and comfort controls · 0.1.8-dev

The lead now gently reduces only the chord bed toward 88% gain; the opening retains its layers. Answering bars leave a beat of space, the midpoint uses a delayed partial echo, and the full hook rhythm returns at the next eight-bar boundary. Schema 5 intentionally changes score and PCM. The pinned 14-second Rainy/Synth regression is 82 events, score hash `5292184dcc22641f`, PCM hash `bb767a4472919295`. Floating-point contraction remains disabled and timing/velocity random draws remain explicitly ordered.

Sleep Off/30/60/90 minutes counts wall-clock time, fades during the final 30 seconds and pauses. Fake-clock tests cover the deadline, half fade, cancellation, deliberate resume, countdown while paused and a pause still queued at cancellation. The separate post-limiter fade leaves the user's 0–300% volume untouched; output tests cover smooth attenuation/recovery and block-size independence. Auto-dim Off/30/60/120 seconds defaults to 60 seconds, dims to at most 10%, and consumes the first wake key. Both adapters suppress auto-repeat from a held wake key. Save format 4 remains 192 bytes; valid formats 1/2/3 migrate with the new default, and earlier favorite schemas remain `OLD`.

The integrated Mac build passes **8/8 native suites and 47 Python tests**, and the SDL preview completes a bounded run with dummy audio/video and state saving. Thirty-one current screen captures and 144 animation frames come from the shared renderer. Sleep countdown, paused expiry and scrolling settings were inspected; the auto-dim image simulates the native adapter's relative brightness, not measured LCD brightness.

The [162-case music matrix](evidence/v018-music-matrix.json) renders 30 seconds per case across all moods/meters/engines, selected tones and 40/120/180 BPM, at 300% volume. Its 155,520,000 frames contain **zero clips, dropped note events or score-rule violations**. Maximum active voices remain 12, with at most 5 voice steals per case; output peaks range from 26,766 to 31,220. Every tested case reaches a keys gain of 28,836/32,767 (approximately 88%). Tone selection and Synth/Hybrid retain identical score hashes for matching musical parameters.

All six refreshed three-minute demos have zero clips and dropped notes. Matching Synth/Hybrid score hashes are Cozy `c341a20f185dadfc`, Rainy `701cd7e315cb4a8f`, Night `ade7d0308dded898`. [Before/after numeric evidence](evidence/v018-audio-comparison.json) preserves the schema-4 baseline and MP3 hashes; prior WAV/MP3 files are retained locally in `build/comparison-v018/before`. These checks establish musical constraints and digital bounds; listening is still needed to judge the improvement.

The local ADV application is **689,056 bytes**, leaving **621,664 bytes** under the compact profile, with **67,384 bytes** of linker static RAM. This is a build result, not runtime heap or device timing evidence. Installation and physical sleep/wake behavior remain to be checked on the ADV.

## Compiler consistency and Pages · 0.1.7-dev

The first GitHub Pages build on Linux/GCC 13 exposed a score regression hidden by the passing Mac/Clang checks. Twenty-two event calls consumed timing and velocity randomness in one function argument list, whose evaluation order is not fixed by C++17. Explicitly drawing timing before velocity preserves the intended schema-4 desktop sequence. The exact score hash assertion remains unchanged.

The next check isolated floating-point multiply/add contraction as a second difference. Native and firmware builds now use `-ffp-contract=off`. In the 14-second regression below, enabling versus disabling contraction on Mac changed 2,771 of 448,000 PCM16 samples, each by only one unit, while the score hash stayed `f0d5794f6cab7f00`. The explicit non-contracted PCM reference is `b52edd474ab7066c`; the previous `dee9e5a94acbcedb` reference used the Mac compiler's default contraction. Exact score and PCM assertions remain required checks in the Pages workflow; no tolerance or platform-specific alternate hash is accepted.

The site workflow builds the pinned ADV application, checks native and Python code, renders current screenshots and six audio demos, then publishes the allowlisted site and validated development package. Publication status is available in [GitHub Actions](https://github.com/m0rg0t/catputer/actions/workflows/pages.yml); package sizes and checksums come from the published `downloads/latest.json`. The earlier measured releases below remain versioned history. No new physical hardware verification is implied.

Local checks pass: 8/8 native tests and 47 Python tests; the exact schema-4 score/PCM regression also passes under Linux/GCC 12 and x86 Clang. All six refreshed 180-second demos retain their previous score hashes with zero clips or dropped notes. The local ADV application is 686,496 bytes, with 624,224 bytes of compact-profile headroom and 67,264 bytes of linker static RAM. The package and screenshot/audio manifests pass their hash checks.

## Translucent visualization · 0.1.6-dev

The music strip overlays the room with a 55%-opaque dark ink backing, quantized to the existing 64-color palette. A compile-time 64-byte lookup preserves the 32,400-byte framebuffer and avoids runtime color searches. The upper solid red rule was removed, and role labels use the brighter Moon color. The ordinary and clean views were visually reviewed at integer scale; screenshots remain desktop evidence.

Eight native CTest suites pass, including UI and all-palette RGB565 conversion. The UI and RGB565 suites also pass under UBSAN; all 47 Python tests pass. The firmware builds with the pinned ESP32-S3 toolchain: **686,496 bytes**, **624,224 bytes** below the compact limit, with static RAM unchanged at 67,264 bytes. BIN SHA-256: `1d8010b5cd039e66e37da2220ac2f58d863f1907194648a2dc10e75100967e19`. The application-only package passes descriptor, identity, checksum, appended hash and archive validation.

This update leaves the music engine, schema 4, save format 3 and favorites unchanged. The six refreshed three-minute demos retain the 0.1.5-dev score hashes and zero clips/dropped notes. Hardware appearance and render timing still need an ADV check; the SD transfer record below belongs to 0.1.5-dev.

## Music and instrument update · 0.1.5-dev

The bottom strip shows the real contributions of seven instrument roles, with an approximately 104 ms peak decay so brief percussion remains visible at 12 FPS. Beat markers use the resolved meter and the same transport as audio; queue-based latency correction remains an estimate on the device. Clean mode anchors the strip to the screen bottom. Pause, mute and motion OFF produce idle activity. Twenty-four native UI scenarios and 144 actual-engine animation frames were exported and visually reviewed, including the instrument menu and all three meters. Main labels and the meter use 5×7 glyphs.

The `I` menu selects chord/lead tones from electric piano, felt piano, nylon guitar, vibraphone, warm pad and soft flute, with round/upright/sub bass. Choices, meter and tempo persist in format 3 (192 bytes); valid 160-byte format 1/2 states migrate and preserve old favorites as OLD. Legacy layouts cannot claim a post-schema-3 favorite. New favorites carry schema 4 and all selectors; quick changes coalesce while the menu immediately shows the pending selection.

AUTO meter chooses 4/4 with 70%, 3/4 with 20%, or 6/8 with 10% probability and retains that meter for the session. Manual choices use the same grids: 16/12/12 sixteenth steps and 4/3/2 pulses. In 6/8 BPM counts dotted quarters. Melody and bass movement is bounded, returning hooks receive root/third cadences, passing notes resolve by step, and pitched gates stay inside the current harmony bar. Melody gates do not overlap. Tone selectors do not change the score RNG.

The first stress matrix caught one dropped hat in Cozy/Hybrid 6/8 at 180 BPM with vibraphone chords and electric-piano lead. Released sampled voices were retaining slots even after their envelopes had faded below 0.00035. Releasing those slots reduced this case from 51 to 2 voice steals and from 1 dropped note to 0, while retaining its 737 events and score hash `5a32afd229110c9b`. Review also caught a sample-end tonal jump: Hybrid's synthesized component changed from 0.32 to 1.0 after the one-shot ended. It now keeps the original blend for the entire voice.

Final verification:

- Eight CTest suites pass in the optimized build and the same eight under UBSAN; 39 Python tests pass. A bounded dummy SDL run with 6/8, 180 BPM, 300%, pad/flute/sub also passes under UBSAN. Invalid CLI tone, meter, BPM and gain values are rejected.
- [Meter/tone/gain matrix](evidence/meter-tone-check.json): 162 × 30 seconds, 155,520,000 output frames, all moods/meters/backends, all six tone choices at 180 BPM plus lower/middle tempo cases and a pad/pad/sub stress case. Zero clipped samples, dropped note events, score-rule failures, backend score mismatches or score changes caused by tone selection. Maximum 12 voices, 4 steals per 30-second case, peak 31,181 at 300%. Reproduce with `python3 tools/verify_music.py --seconds 30`.
- [Longer score audit](evidence/meter-session-score-check.json): three representative meter/mood/seed cases in both engines, 6 × 480 seconds. Each includes automatic session changes (2–4 sessions) and 22–37 distinct complete four-bar lead phrases. Paired CSVs match byte-for-byte, with zero scale/chord/gate/passing-resolution violations. These are finite rule/variety checks, not a subjective pleasantness rating.
- Schema 4 regression: Rainy/Synth, seed `0x0123456789abcdef`, texture 23, 14 seconds → 68 BPM, 4/4, 82 events, score `f0d5794f6cab7f00`, PCM16-LE FNV `dee9e5a94acbcedb`. Schema 3 hashes below are historical, not current expectations.

The six refreshed three-minute AUTO demos use seed `0xCA7CAFE`, core volume 78 and texture 18, before the optional output-gain stage. Both engines have zero clips/drops and matching scores within each mood:

| Mood | Meter / BPM | Synth RMS / peak | Hybrid RMS / peak | Events | Shared score hash |
| --- | --- | ---: | ---: | ---: | --- |
| Cozy | 4/4 / 80 | 1153 / 9689 | 956 / 7515 | 1158 | `5f2c9abb529f6121` |
| Rainy | 3/4 / 73 | 1188 / 9213 | 979 / 7445 | 1276 | `632141c81c7917a4` |
| Night | 3/4 / 76 | 1151 / 9632 | 983 / 7585 | 1329 | `af29b50c1b238d12` |

Current application BIN: **686,384 bytes**, 624,336 bytes below the compact `0x140000` limit; linker static RAM 67,264/327,680 bytes. Engine inline storage remains 8,192 bytes and the voice limit remains 12. BIN SHA-256: `15b4ebf30afd478f2191385eb007946b661f96d195095ab2ee64dcc088a51c7d`. Device render timing, acoustic quality and physical synchronization still require testing on the ADV. The earlier measurements below remain versioned history.

The 0.1.5-dev package and ZIP checksums pass. Its application BIN was copied to the connected CARDPUTER SD card, verified by reading back the complete file and matching SHA-256, and the card was safely ejected on 2026-09-13. This verifies transfer to the card, not installation on the device. The local site was refreshed to the new package, 24 screen states and six current audio demos; Cozy/Synth playback advanced in the browser and was then paused.

## Implemented

- Shared C++17 composer and renderer with deterministic 64-bit seeds, three moods, six arrangement sections and automatic session changes.
- Twelve bounded voices; pure synth and hybrid instruments; seven locally generated hybrid one-shots totaling 58,240 PCM bytes.
- Offline 32 kHz mono PCM16 output. The ADV adapter rotates three 512-frame buffers through the pinned M5Unified speaker API; commands are copied to the audio task and SD work runs separately.
- AUTO or manual 40–180 BPM, with tempo-only changes at bar boundaries. Shared 0–300% output gain, 10 ms volume ramps and a soft limiter before the normalized device output path.
- Original imagegen room/cat artwork adapted for the shared 240 × 135 renderer. The 64-color indexed framebuffer uses 32,400 bytes plus a 480-byte output row. Background, six 64 × 72 cat frames and the RGB565 palette use 60,176 bytes of constant scene data in flash. Engine inline storage is 8,192 bytes.
- Menus, help, settings, favorites, clean scene and motion levels. Fixed 192-byte versioned/CRC-checked optional SD state, with 160-byte legacy migration with valid backup/temp recovery. No-SD state lives in RAM.
- Native SDL preview and WAV/PPM exports, deterministic sample tooling, a media exporter, allowlisted static site builder and app-only package builder.
- Arduino generic NVS initialization is wrapped out; ELF symbol inspection confirmed the wrapper is linked. This app has no NVS clients.

## Host checks

Normal CMake/CTest and an UndefinedBehaviorSanitizer build pass the music, state, controller, native storage and UI suites. Tests cover deterministic PCM across block sizes, identical A/B score hashes, bounded voices, favorite parsing, pause/resume, arrangement/session progression, malformed saves, CRC corruption, menu boundaries and recovery from a corrupt primary save. Audio command sequencing and transition regressions are included after review. The UI suite verifies all 64 indices through RGB565 row conversion, row-buffer guards, off-screen writes, still-motion behavior and valid indices in every screen.

The SDL preview completed a bounded run with dummy audio/video drivers and optional local state. All exported UI scenarios also ran under UBSAN. The actual renderer was visually inspected at an integer scale: cat/room, main HUD, menus and full 64-bit diagnostic seed are legible.

Python tests cover the selected bank, format/order/hash/loop/size rejection and application-image checksum/hash/target/bounds validation. AddressSanitizer's runtime stalls during initialization on this host; no ASAN pass is claimed.

The imagegen asset update adds deterministic scene conversion tests for palette order, transparency, dimensions and sprite-sheet scanline order. The artwork update originally exported 15 UI states and 144 animation frames (12 seconds at 12 FPS). The room/cat composition and UI contact sheet were visually reviewed; sprites use real indexed transparency with no magenta colors remaining in the packed palette. The six poses share a fixed bench anchor. Source masters, prompts and hashes are retained with the assets. The new framebuffer adds 16,200 bytes of static RAM, so physical heap and timing checks remain necessary.

Audio comparison measurements are recorded below. The completed firmware build is below the compact profile; final image identity and checksums are in the distribution manifest. Desktop timings are not a device real-time guarantee; clipping/energy checks do not establish pleasant sound.

## Remaining physical and later milestone work

- Listen through the ADV speaker and jack; compare the two candidates, then select/tune the production sound.
- Measure startup time, internal free heap/largest block, task stack margins, render deadlines and audible dropouts while animation/keys/SD saves are active. Serial `queue_empty` counts are source-queue observations, not verified DMA underruns.
- Perform the planned two-hour **device** soak and battery/runtime measurements.
- Recheck the current display, instruments, meters, visualization sync, manual tempo, boosted output, sleep fade and wake behavior on the ADV; verify Home/return behavior and neighboring applications, and exercise physical SD failure/recovery. The user ran 0.1.2-dev on the device and supplied a photo; that establishes startup, not complete installation or hardware acceptance.
- Implement external SD sample/scene packs later. The current optional SD feature is settings/favorites only.
- Select the public project license. The public repository and GitHub Pages deployment are active at `m0rg0t/catputer`.

## Practical behavior

Mood/engine/replay requests received within the last 20 ms of a bar may wait one extra bar, allowing a complete fade. Tempo-only changes take effect at the next bar without a restart. Favorites replay from the beginning and are version/bank dependent. Rapid changes are coalesced while preserving accepted musical parameters. SD write failures keep changes in RAM and stop saves until restart; hot-plug recovery is not implemented.

## Previous candidate: manual BPM and output gain · 0.1.4-dev

Settings now has eight rows, including BPM and volume up to 300%. BPM uses AUTO or 40–180 in one-beat-per-minute steps; Enter on the BPM row toggles AUTO/manual. Settings and favorite records preserve the selected mode. Format 2 remains exactly 160 bytes: the original favorite record bytes are retained, volume's high byte occupies header byte 6, global BPM byte 15, and favorite BPM bytes 144–151. The decoder migrates valid format-1 files to AUTO and rejects their previously invalid volume values above 100. Format 2 validates the wider volume, BPM ranges, reserved bytes and CRC. No private SD state is included in test fixtures or packages.

Tempo changes preserve the current session, notes and fractional sample clock. Coalescing back to the active tempo cancels the pending change; reaching a control limit does not restart playback. Favorite replay explicitly requests a restart even at the same seed. AUTO consumes the same random draws as before; its regression reference (Rainy/Synth, seed `0x0123456789abcdef`, texture 23, 14 seconds) remains BPM 74, 84 note starts, score hash `e2ebee7135b74f78` and PCM16 hash `4ee8918a12e09f44`.

At manual tempos above the session's AUTO tempo, keys, bass and lead release tails shorten proportionally in seconds to retain their approximate length in beats. This keeps the existing twelve-voice pool usable at faster tempos. The 180 BPM Cozy/Synth regression improved from five dropped incoming notes to zero in 60 seconds. The final [tempo and gain sweep](evidence/tempo-gain-check.json) covers 40, 120 and 180 BPM, all three moods and both engines at 300%: 18 × 60 seconds, 34,560,000 frames, zero clipped samples, zero dropped note events and matching A/B score hashes/counts. The largest output peak was 31,050; AUTO audio retains its previous regression hash. These finite host checks do not establish all-seed behavior or device audio deadlines.

The output-stage tests cover every PCM16 input at 300%, volume values spanning 255/256, soft-limiter bounds, monotonic gain, block-size independence, mute and ramp retargeting. Review against pinned M5Unified caught its additional mono gain after synthesis. The corrected adapter uses magnification 8 and moves the previous 2× pre-gain ahead of the limiter; its remaining digital gain is about 0.98447, so the 32,700 limiter ceiling stays below 32,193 at the final library conversion. Below 100%, the prior level curve is preserved apart from rounding and limited peaks. This proves a digital bound, not speaker distortion or perceived loudness on hardware.

The native player uses the same output stage. WAV exports remain raw engine comparisons unless `--volume` is explicitly supplied; metadata records whether the stage was applied. `--bpm auto|40..180` controls native playback and exports. The updated 18-screen contact sheet was visually reviewed, including 300%, manual BPM and AUTO BPM. Seven native suites pass in normal and UBSAN builds; a dummy SDL audio/video run at 180 BPM/300% and the screenshot exporter also pass under UBSAN.

## LCD correction · 2026-09-13

The user's first device photo showed recognizable scene geometry with scrambled colors and unreadable text. The native exporter bypasses M5GFX, so its earlier screenshots did not validate the physical transfer. The renderer emits host-order RGB565 words; M5GFX 0.2.22 defaults to treating `uint16_t` image data as already byte-swapped for the LCD. Explicit `Display.setSwapBytes(true)` makes the pinned library convert those words to the LCD byte order. A probe against the actual library reproduced the red-pixel difference: the old path sends `00 F8`, and the corrected path sends `F8 00`. The checked-in `display_rgb565` regression exercises the pinned pixel conversion for all 64 palette entries: the corrected conversion passes all 64, and the old raw path fails 63. Enable this optional CTest target by configuring `LOFI_M5GFX_SOURCE_DIR` with the path to an M5GFX 0.2.22 checkout; it checks library conversion, not physical SPI or the LCD.

Status labels, control hints, clean-view status and help now use 5 × 7 glyphs instead of 3 × 5. The radio footer has two separated rows on a solid dark background and fewer simultaneous shortcuts; help lists the remaining controls, including engine selection and the distinct movement/adjustment keys. The 15 updated native screenshots were visually reviewed. This remains host evidence until the corrected build is checked on the same device.

The 0.1.3-dev LCD update left the composer, samples, schema and save format unchanged. Its six 180-second demos retained their previous score hashes and zero clipped samples. The older score-audit and two-hour soak records below retain their original version metadata and establish the AUTO-mode baseline.

## Previous schema-3 comparison · 2026-09-13

These historical examples use generation schema 3, seed `0xCA7CAFE`, **180 seconds**, volume 78 in the core, texture 18 and the same score within each mood. Values are PCM16 units before the preview/device master volume. Every render has zero clipped samples, zero dropped note events and at most twelve active voices.

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

## Previous firmware artifact · 0.1.4-dev

Application BIN: 681,168 bytes; 629,552 bytes below the `0x140000` compact limit. Linker static RAM: 67,208 bytes of the 327,680-byte linker budget; dynamic heap/DMA/task overhead still needs device measurements. The project/version marker and ESP application descriptor, image checksum and appended hash are validated. The package contains only the application, install guide, dependency notes, manifest and checksums.

BIN SHA-256: `c5467348447f9f29bbffabfbfefc642fdb1309e793b700c6eff6fe4559f20400`.

The 0.1.4-dev update passes seven native CTest suites (including output gain and the optional M5GFX conversion check), the same seven under UBSAN, and 36 Python tests. Its application image, release archive and 18 native-renderer screenshots are validated. The preceding harmony update's evidence includes 18 score exports, six three-minute A/B demos and two two-hour offline renders; those AUTO-mode baselines remain distinct from the new tempo/gain sweep above.
