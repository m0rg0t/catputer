# Delivery and verification

> Accepted design and milestone criteria. See [current implementation and verification](VERIFICATION.md) for what is built and measured; remaining targets below are not completion claims.

## Milestones

| Milestone | Work | Reviewable result and exit condition |
| --- | --- | --- |
| 0. Baseline | Pin the stack, create shared-core/native/device targets, log hardware resources, establish install profile | Empty ADV app and host runner compile; actual memory/layout assumptions recorded; no automatic flashing |
| 1. Audio feasibility | Produce/audition the compact sample bank, implement one composer and both instrument candidates, bounded output buffers, a minimal moving screen | Sample manifest and auditions, matched WAVs, device output examples and a timing/heap/size table; continuous audio with visual load |
| 2. Choose the sound | Tune instruments, inspect multi-seed longer renders, present A/B results | User selects an engine using listening and measured limits; rejected candidate remains documented |
| 3. Radio behavior | Arrangement, smooth transitions, three moods, transport, seeds, favorites and optional SD loading | 30-minute listening review; deterministic event replay; missing-SD and invalid-pack behavior works |
| 4. Scene and UI | Finish one scene, controls, help/settings, render scenarios | Real-code contact sheet and animation capture reviewed at native size; no audio regression under visual load |
| 5. Soak and package | Physical tests, app BIN checks, content manifest, checksums and installation guide | Two-hour device soak passes; installation and return to M5Apps checked; release candidate has evidence |
| 6. Site and release materials | Build a static product page from matching captures/audio/version/artifacts | Site preview and validated distribution archive ready; publishing is a subsequent requested action |

Dependencies: milestone 1 should include a small animated stress load even before final art. Milestone 4 builds on that harness. Site templates may be prepared early, but screenshots and sound examples are exported only from the matching implementation. Avoid scheduling estimates until milestone 1 establishes the sound-engine cost.

Sample sourcing and preparation happen on the Mac according to [Sample production](SAMPLE_PRODUCTION.md). ElevenLabs requests are development-time source generation; firmware playback and composition remain offline.

## Native audio/core checks

Tests should protect musical timing and memory behavior, not restate implementation details:

- Same seed/version/config yields the same event sequence; changing rendering block size or visual frame rate does not change it.
- Long simulated playback carries fractional timing without tempo drift and survives positions beyond 32-bit sample counters.
- Event queues and voice counts remain bounded for every preset; saturated input commands do not allocate or block indefinitely.
- Notes/envelopes release correctly on pause, stop, skip and voice stealing; no signed overflow, invalid samples or sustained DC.
- Transitions preserve the musical boundary policy and outgoing tails within the voice cap.
- Missing, truncated, oversized or incompatible content packs fall back to built-in content without reading outside bounds.
- Favorites persist and recover from interrupted SD updates; loading a different engine/content version does not silently promise identical replay.

Use sanitizer-enabled host builds where supported for the core and pack parsers. Export an event manifest alongside WAVs to distinguish musical changes from synthesis changes. A peak/level check can catch silence or clipping, but cannot establish whether the music is enjoyable.

## Physical evidence

Record the device variant, library versions, source revision, content hashes, install profile, volume/brightness, power source and test duration. Mark unavailable measurements as unmeasured.

| Test | Required observation |
| --- | --- |
| Cold start without SD or Wi-Fi | Built-in scene and music start, proposed ≤5-second target measured |
| Speaker and jack | Clean mono playback, no unexplained hum, correct hardware speaker disable when jack inserted |
| Pause/resume/volume/skip | No audible clicks, stuck notes or uncontrolled output level |
| UI stress | Repeated keys, menus and visual changes maintain zero underruns |
| Full playback soak | Two hours, maximum supported voices/effects/animation, no reset or continued heap decline |
| Resource logging | Minimum heap, largest free block, queue minimum, underrun count, worst render block time and frame rate |
| Storage faults | Missing/bad/removed SD does not interrupt built-in playback or falsely report a save |
| Power interruption | Recoverable settings/favorites; no corruption of required built-in content |
| Battery operation | Stable audio on battery; measure runtime separately under stated conditions |
| Install and return | Launches through M5Apps, returns correctly, other installed apps and launcher state remain usable |

Battery runtime is intentionally unpromised. The nominal battery capacity and a vendor current figure are insufficient to claim runtime for active synthesis, display and speaker use.

## Firmware installation and packaging

Normal install route: copy the application BIN to an SD card and select it in **M5Apps → Installer → SD**. The SD card is an installation transport; the accepted baseline must keep playing after it is removed. Document what a fresh device without M5Apps needs separately from an application update.

Generate a release manifest containing product/variant, app version, engine/schema version, source revision/dirty status, library pins, app image size, image SHA-256, expected chip and the supported compatibility profile. Pin the build-time profile rather than fetching an upstream maximum during every release build.

Provisional compact build goal: `0x140000` (1,310,720) bytes, following Hermes/Oracle. This is a useful target, not a universal M5Apps maximum. The actual supported profile and target-device layout decide compatibility. A larger observed upstream launcher image does not authorize choosing that size for an arbitrary installed application.

Validate the ESP32-S3 application image using the pinned image tool, including segments/metadata where available. A magic byte alone is not a complete validation. Package only the application image, optional public `/LOFI` content, a readable installation guide, content/firmware manifest and checksums. Confirm archived bytes match the canonical build output.

Do not label a merged full-flash image as an app update. Do not copy Hermes's `0x180000` target address into a new updater. Its address belongs to a particular local installation.

If direct USB updates are later needed, implement a separate guarded workflow:

1. Identify device/flash capacity and read the existing partition layout.
2. Identify the intended installed app using image metadata/hash and explicit local mapping. A compatible-sized partition is not enough to identify ownership.
3. Verify image type, compatibility, target bounds and non-overlap. Stop when target identity is ambiguous.
4. Keep a local recovery backup and region hashes. Compute any erase range within the verified app partition; never round it into neighbors.
5. Write only that verified application region, read it back and compare. Recheck protected neighboring regions where feasible.
6. Physically verify the launcher, this app and other installed apps.

Do not infer user authorization to overwrite another app from an app name, a sample script or a nominal “free” address. For the present task, no device writes are being performed.

## Simulator media pipeline

Plan one documented command to build the native preview, render all selected scenarios and export the contact sheet plus manifest. Use a headless SDL mode where available. Keep native captures at exact display size and use nearest-neighbor scaling for larger documentation assets.

The host environment should test the real renderer. Hardware/SD/codec adapters may be stubbed; do not claim those stubs validate device behavior. A headless screenshot CI job is useful if SDL can be provisioned reliably; unlike the current Hermes/Agent workflow, exclusion from CI is not a requirement. Separate the optional media job from mandatory core/image validation.

## Project site generator

Follow Oracle's small static-build model: `site/` templates and public copy, a Python standard-library builder, and an explicit asset allowlist into `build/site/`. Pillow is an image-export dependency, not a requirement for basic site templating.

Page contents:

- A short product explanation and clear “generated on this device” description.
- Real preview captures and one short renderer-derived animation.
- Three user-initiated audio examples from recorded seeds; label host renders versus physical line-output recordings.
- Actual controls and supported hardware.
- Install steps, optional SD folder layout, current BIN/ZIP links and checksums once a release exists.
- Build/source links, asset credits, measured limitations and the evidence date.

Read version, screenshots, audio metadata and release links from canonical manifests. Fail the site build on missing/stale assets, hash mismatches, broken local links or unresolved placeholders. Audio players should not autoplay. Before a tested firmware release exists, show development status rather than a fictitious download button or “verified” badge.

Use relative paths compatible with a GitHub Pages repository prefix. Start with English and keep copy extractable for later locales; localization is not yet an agreed requirement. A Sites mirror can use the same public output when requested, following the Sites skill at that stage.

Public packaging must exclude private favorites/settings, serial logs, device dumps, backups, local hosting metadata and unrelated SD content. Preserve all required asset/license attributions. Never point a site builder at the entire workspace or blindly copy an SD card.

### GitHub Pages deployment

The project site is published at <https://m0rg0t.github.io/catputer/>. The repository's Pages source is **GitHub Actions**. The workflow in [pages.yml](../.github/workflows/pages.yml) runs on pushes to `main` and supports manual dispatch.

Each run builds the pinned PlatformIO firmware and the native renderer, runs the native and Python checks, exports fresh PNG/GIF captures and six matched 180-second MP3 demos, and creates the validated application-only package. The site builder checks media, audio and package hashes before the workflow uploads only `build/site`. The build needs no OpenRouter, ElevenLabs or other private API keys.

The deployment job runs only after a successful build, with Pages write and OpenID Connect permissions scoped to that job and the `github-pages` environment. A failed build leaves the previous site online. The website serves a development candidate; deployment does not establish hardware verification or create a tagged firmware release.

## Future command interface

Current local command interface (see README for dependencies):

```text
pio run -d firmware -e lofi-adv
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake -j 4
ctest --test-dir build/cmake --output-on-failure
build/cmake/lofi_native --engine synth --seed 0xCA7CAFE --wav build/audio/synth.wav
build/cmake/lofi_native --shots build/screens --animation build/animation
python3 tools/export_media.py
python3 tools/build_release.py
python3 tools/build_site.py
```

Keep build/render/package commands local and reversible. Flash and publish commands must be distinct from build commands, with their targets visible and appropriate to the later user request.
