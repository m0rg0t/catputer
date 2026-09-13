# Architecture

> Accepted design and milestone criteria. See [current implementation and verification](VERIFICATION.md) for what is built and measured; remaining targets below are not completion claims.

## Hardware basis

The stock ADV uses the ESP32-S3FN8, dual-core LX7 up to 240 MHz, 8 MB flash, a 240 × 135 ST7789V2 display, an ES8311 codec and a 3.5 mm output. Its speaker amplifier is disabled when a jack is inserted. The SoC has 512 KB SRAM; the FN8 package has no PSRAM. Budget against measured usable internal memory rather than treating all SRAM as free heap. [M5Stack hardware documentation](https://docs.m5stack.com/en/core/Cardputer-Adv), [Espressif datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf).

The first implementation should report free/minimum heap and largest allocatable block after display/audio initialization. Plan for stock hardware with no expansion modules. Verify balanced audible output at both sides of a normal headset, but promise mono music until the complete codec/jack path has been tested.

### Bluetooth audio feasibility

Checked against manufacturer documentation on 2026-09-13 following the request for a Bluetooth-output screen. ESP32-S3 supports Bluetooth LE, but neither Bluetooth Classic/A2DP nor LE Audio. Adding a BLE scan/pairing screen or an A2DP library cannot provide standard headphone/speaker audio on the stock radio. [Espressif Bluetooth audio chip comparison](https://docs.espressif.com/projects/esp-adf/en/latest/solution-center/bluetooth-audio.html), [ESP32-S3 Bluetooth capabilities](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/bt-architecture/overview.html).

The least invasive candidate is `ADV 3.5 mm output → standalone Bluetooth transmitter → headphones/speaker`. The ADV has a headphone output and disables its speaker amplifier when a jack is inserted. A transmitter must accept analog headphone audio and support transmission (TX/A2DP source); a receive-only adapter does not serve this purpose. The transmitter owns discovery/pairing/reconnection. An analog cable provides no connection-status or device-selection control channel to the Cardputer. This is an architectural option, not a tested hardware combination. [M5Stack ADV hardware](https://docs.m5stack.com/en/core/Cardputer-Adv), [manufacturer example of a headphone-jack transmitter](https://support.twelvesouth.com/en-US/articles/airfly-84725).

A functional Cardputer device-selection screen needs a specifically selected external audio module with a documented control interface and real connection events. Its audio input, pin/power requirements, framing, sample rate and buffering must be validated before integration. A Wi-Fi stream to a phone/computer that then outputs to its paired Bluetooth device is another possible architecture, requiring a receiver and new networking work. Neither route is selected or implemented. Keep the current speaker/jack output and shared composer working while a transport is chosen; no Bluetooth search/connected states are simulated in firmware.

## Modules

```mermaid
flowchart LR
  K[Keyboard] --> C[Controls and app state]
  C --> Q[Bounded command queue]
  Q --> M[Composer and arrangement]
  M --> E[Timestamped musical events]
  E --> R[Instrument renderer and mixer]
  P[Validated sound bank] --> R
  R --> B[Owned PCM buffers]
  B --> O[Audio output adapter]
  O --> S[ES8311 speaker or jack]
  M --> V[Small transport snapshot]
  C --> U[Shared UI and scene renderer]
  V --> U
  U --> D[M5GFX device display or SDL]
  F[Storage service] --> P
  C --> F
```

Suggested future layout:

```text
firmware/
  platformio.ini
  include/lofi/
  src/core/             # composer, transport, seeds, presets
  src/audio/            # voices, instruments, mixer, output adapter
  src/ui/               # real shared drawing and scene code
  src/platform/         # M5Cardputer, storage, controls, diagnostics
  src/sim/              # desktop adapters and scenario runner
  sim/stubs/
tools/                  # banks, native WAVs, screen exports, site, release checks
assets/source/          # editable pixel artwork + provenance
assets/audio/           # sample briefs, original responses/renders, selected masters, manifests
sdcard/LOFI/            # optional distributable content; no private state
site/                   # static page templates, styles and public copy
docs/media/             # selected generated captures and media manifest
tests/                  # meaningful host tests
```

The implementation now uses shared `firmware/src/audio`, `firmware/src/core`, and `firmware/src/ui`, with the ADV adapter in `firmware/src/platform` and SDL/export adapter in `native`. The outline above remains the conceptual design.

## Toolchain proposal

Start with the common local reference pins: PlatformIO `espressif32@7.0.1`, board `m5stack-stamps3`, Arduino framework, M5Unified `0.2.17`, M5GFX `0.2.22`, M5Cardputer `1.1.1`, and C++17. These are inspected reference versions, not a claim that they are latest. Confirm that they still resolve and support the required ADV paths during milestone 0.

Pin the same graphics revision for device and native preview. The Agent preview currently allows a different revision; do not copy that mismatch by default. Keep SDL/stubs out of the firmware build. Use native tests for the core and a native audio renderer; use the real screen/scene code in `native-sim`.

Avoid bringing over network, JSON, IR or Bluetooth libraries just because a reference uses them. Use the pinned M5Unified output adapter first; a direct I2S backend is a fallback if measurements establish a limitation. Do not combine incompatible legacy/new I2S APIs. The exact ESP-IDF version supplied by the pinned platform, rather than the current web manual, determines available APIs.

## Scheduling and ownership

At 32 kHz, a block of 512 mono samples lasts 16 ms and contains 1,024 bytes. Six application blocks occupy 6 KiB and span 96 ms before additional driver buffering. The driver DMA buffers and scratch buffers are extra allocations and must be measured.

One audio producer owns synthesis state and renders upcoming events. A dedicated output adapter submits buffers in sequence and returns them to the free pool only after the library can no longer read them. Start with one persistent output channel and streaming `playRaw(..., stop_current_sound=false)`; validate the pinned library's two-entry queue semantics. Do not overwrite alternating buffers based only on a guessed delay.

The high-priority audio work must block or yield correctly when its queue is full. Avoid busy waits that starve the library output task. Choose exact priorities and core placement after tracing both tasks; a reasonable experiment isolates audio from UI work, but do not assume two cores automatically eliminate contention.

No file I/O, display calls, logging, JSON parsing, allocation or mutex held by storage/UI is allowed in the render path. Commands are bounded; coalesce repeated volume inputs. Composer lookahead runs outside urgent output deadlines. Pass a small coherent beat/level/status snapshot to the UI instead of sharing mutable synthesis state. The current snapshot also includes seven bounded instrument activity levels and the resolved meter grid. The bottom display reads those levels; it does not scan PCM buffers or run an FFT. Pause and mute make it idle. Pulse latency compensation divides by the actual pulses per bar, including two dotted-quarter pulses in 6/8.

Audio timing comes from the sample timeline. For beat-linked animation, compensate for queued output latency using consumed/estimated played sample position. If an exact playback position is unavailable, publish the known estimate and test phase error physically; do not synchronize to an unrelated `millis()` beat timer.

Meter is stable within a session: AUTO resolves from a seed, or the user selects 4/4, 3/4 or 6/8. A bar contains 16/12/12 sixteenth steps respectively, grouped into 4/3/2 pulses. Chord, bass and melody gate boundaries use the actual bar end; new meter/tone configurations apply at a boundary. The six chord/lead timbres and three bass timbres use fixed oscillator/envelope profiles and require no new sample-bank storage. Timbre selection remains outside the composition RNG.

Manual tempo is a configuration override: 0 selects the seed/mood tempo, and 40–180 selects a fixed BPM. A tempo-only update keeps the current score, voices and session, takes effect at the next bar edge, and uses that edge as the new fractional-sample timing origin. It must not recompute elapsed time as `bar × new bar length`. Explicit favorite replay requests a restart even when its seed matches the current session.

The shared `OutputGain` stage runs after synthesis with no allocation or lookahead buffer. Volume spans 0–300% and ramps over 320 samples (10 ms). At 0–100%, it retains the ADV's prior squared 0–255 volume response and soft limiter exactly. Above 100%, pre-gain is `2 + 0.05 × (percent − 100)`, reaching 12× core amplitude at 300%. A peak envelope has immediate attack, a 256-frame (8 ms) hold and a 50 ms exponential release, bounded to the current sample's required gain. Its peak target is 32,000. The legacy and upper limiter outputs crossfade over the same volume ramp when crossing 100%, so selecting a different path introduces no immediate mode jump. The sleep fade remains post-limiter.

With master/channel volume fixed at 255 and speaker magnification set to 8, pinned M5Unified's mono conversion gain remains about 0.98447. Both the legacy 32,700 ceiling and upper 32,000 target remain below full scale at I2S. The ADV codec uses DAC register `0x32 = 0xBF` (unity gain); increasing codec gain or restoring driver magnification 16 would add gain after peak control and push the hardware toward clipping. The stronger upper range therefore raises average level in software. The built-in NS4150B speaker amplifier has fixed hardware gain, while the jack bypasses it. See the [official ADV schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Sch_M5CardputerAdv_v1.0_2025_06_20_17_19_58.pdf) and [pinned codec initialization](https://github.com/m5stack/M5Unified/blob/0.2.17/src/M5Unified.cpp#L788).

Proposed baseline: worst observed 512-sample render time below 8 ms under stress, zero producer starvation, and queue low-water diagnostics. Missing a visual deadline drops a frame. It must not skip audio work. Detect underruns explicitly instead of inferring success from average CPU usage.

## Initial memory ledger

Provisional allocations, in KiB, to refine from the map and device measurements:

| Application-owned area | Initial allowance |
| --- | ---: |
| 8-bit indexed 240 × 135 canvas and palette | 32 |
| RGB565 transfer/tile scratch | 4 |
| Active animation sprites/state | 16 |
| Hybrid resident bank; mostly unused for pure synthesis | 64 |
| PCM pool and render scratch | 16 |
| Voices, phrase events, queues and transport | 8 |
| Short delay/filter state | 16 |
| Settings/content indexes | 8 |
| Application task stacks | 12 |
| Unassigned application margin | 16 |
| **Application planning envelope** | **192** |

This envelope excludes framework static/IRAM use, M5GFX internals, filesystem buffers, the M5Unified task/DMA allocation and system stacks. Measure those separately; the table is not proof of fit. Provisional operating gates after all initialization: at least 48 KiB free internal heap and at least 24 KiB as the largest free block, with no continuing decline during a two-hour soak. Revisit the budget openly if the baseline fails.

A full RGB565 frame is 64,800 bytes; two use 129,600 bytes. An indexed framebuffer is a design option to validate in M5GFX, not an excuse to overlook hidden conversion allocations. If indexed output is unsuitable, use bounded tiles/dirty rectangles and account for the chosen buffer. Avoid a sequence of full-screen uncompressed animation frames resident in RAM.

## Display and animation

Use a static room background with small animated layers: rain, lamp/steam, a few cat poses and slow background movement. The cat is the main character. Target 12 FPS, with 6 FPS as a load fallback. Update controls promptly even if the scenery is slow. Use an independent fixed-step timeline for scene motion; derive music-linked motion from rendered audio snapshots. Native animation exports render the shared engine up to each frame, rather than inventing musical activity.

Compile original pixel art into a bounded indexed format at build time. Optional SD assets are validated and loaded before use; no file reads occur for each frame. Avoid JPEG/GIF/video decoding in the main listening loop. Scene switching either fits within allocated buffers or uses a short visual transition while audio keeps playing.

The two imagegen scenes share one 32,400-byte indexed framebuffer and a 480-byte RGB565 transfer row. Each scene stores a background, six 64 × 72 cat frames and a 64-entry palette as constant data in flash (120,352 bytes of combined pixel/palette data). Both palettes keep the same first 16 UI colors and sprite transparency masks. The frame selects its own palette for row conversion; there is no mutable global palette or second framebuffer. Compile-time shade tables blend the bottom overlay for each palette. Cozy/Night use night artwork, Rainy/Sunny day artwork; Sunny skips rain and the night lamp pulse. Scene selection uses the audible mood snapshot so it stays aligned with queued musical changes. The renderer copies indexed pixels and uses transparency index 255 for sprites; no image decoder or asset-loading allocation runs on the device. This replaces the initial 16-color packed-canvas proposal.

The music visualization uses a translucent ink backing over the already drawn scene. Two compile-time 64-byte palette lookups blend 55% ink with 45% scene, quantized to the active RGB565 palette; it adds no framebuffer or per-frame color search. Labels are drawn afterward at full opacity.

## Content and persistence

The current no-SD implementation embeds both day/night scenes, presets and both compact sound engines. Optional `/LOFI` resources hold additional scenes/banks plus favorites/settings. Boot should tolerate absent or invalid optional content and show a small status message while preserving built-in playback.

Own optional SD access in one storage service. The reference uses SCK 40, MISO 39, MOSI 14, CS 12 and a conservative 10 MHz clock; cross-check the library setup before copying pin initialization. Validate manifest versions, counts, dimensions, decoded sizes and paths under `/LOFI`. Only fully validated packs can become active.

Current format 5 uses 192 bytes and retains the eight favorite records, with tempo/meter/tone bytes, an auto-dim setting in byte 7, and a CRC. It permits Sunny mood 3 without changing byte positions. The bounded decoder accepts valid 160-byte formats 1/2 and 192-byte formats 3/4; formats 1–3 default auto-dim to 60 seconds, while format 4 preserves it. Mood 3 is rejected in older formats even with a valid CRC. Music schema 5 is unchanged from 0.1.8-dev, whose favorites replay unchanged; favorite schemas 1–4 remain unavailable for replay under the current composer. Sleep timer state is runtime-only. Persist settings/favorites with a temp file, flush/close, recoverable rename/backup sequence and boot recovery. FAT rename is not treated as a universal power-loss guarantee. Avoid writes on every key repeat. Never replace the whole user's `/LOFI` directory during updates.

The controller owns the sleep deadline and inactivity clock. Its Q15 sleep gain crosses to the audio task through an atomic value, where `OutputGain` smooths it over 320 frames and applies it after limiting. User volume remains independent. Expiry queues an idempotent pause; input is handled before the automatic pause request so a deliberate resume can cancel it. The display adapter changes backlight brightness only when the effective percentage changes. The native adapter uses the same controller and gain stage, with dimming represented by SDL texture modulation; screenshot exports simulate that same relative brightness.

Without SD, the conservative proposal is RAM-only session settings and favorites, with a visible seed for recall. Persistent internal storage requires an explicit, verified app-specific partition contract; do not blindly call `nvs_flash_init` or erase `apps_nvs`. The final baseline is recorded in the decision log.

Stop/pause, audio-init failure and exit need fade/drain/mute lifecycle handling. Inspect the Agent codec shutdown practices, but revalidate codec register assumptions and gain staging. An unused microphone should stay inactive. Screen dimming during playback must keep the audio engine running; deep sleep is not the mechanism for an active radio.
