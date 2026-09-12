# Reference findings

Inspected 2026-09-12. Local working files were read without editing the reference projects. Commit IDs below identify their HEADs, not a claim that all inspected files were clean.

| Reference | Local HEAD | Working-copy qualification |
| --- | --- | --- |
| Hermes | `e09b876755802cc8486db62e074518f9bb61b376` | README modified; DOMAIN document untracked |
| Oracle | `9bf6a907078116d9e29c82d66382b5dc94164b8d` | Clean at inspection |
| Agent Console | `dfc1e6d6c095d8b8b56c02ad0c655581087ad291` | Includes local edits, notably audio/storage documentation; no private recovery contents inspected |
| Lofi Cat | `c5b1fcd7bde57cf01abf292813a4c041699d2464` | `REVIEW_NOTES.md` untracked; inspected source/assets are local working-copy evidence |

## Hermes: shared screens and compact firmware

Useful entry points:

- [Preview instructions](../../cardputer-adv-hermes-terminal/sim/README.md)
- [PlatformIO targets](../../cardputer-adv-hermes-terminal/platformio.ini)
- [Actual drawing code](../../cardputer-adv-hermes-terminal/src/app_draw.cpp)
- [Size guard](../../cardputer-adv-hermes-terminal/scripts/check_m5apps_size.py)
- [Flash script](../../cardputer-adv-hermes-terminal/scripts/flash_hermes_m5apps.sh)
- [Flashing documentation](../../cardputer-adv-hermes-terminal/docs/SAFE_FLASHING.md)
- [Release evidence checklist](../../cardputer-adv-hermes-terminal/docs/RELEASE_CHECKLIST.md)

Adapt: compile the real drawing unit against M5GFX SDL; use realistic scenario state; freeze time for captures; export exact 240 × 135 pixels with nearest-neighbor enlargement; distinguish desktop evidence from physical device checks.

The inspected size guard enforces `0x140000`. The flash script hardcodes offset `0x180000` and size `0x140000`. It checks the port path, image existence and byte size, asks for a target-specific confirmation, then erases/writes/verifies that region. **It does not read and validate the installed partition layout**, despite broader safeguards described in the accompanying documentation. Copy the intent of safe app-only updates, not this device-specific address or an unverified documentation claim.

The drawing code uses a full RGB565 canvas in places. That works for its design but consumes 64,800 bytes per 240 × 135 buffer; the music project needs an explicit comparison with indexed/tiled rendering because audio adds a significant working set.

## Oracle: asset packs, site generation and release checks

Useful entry points:

- [Development workflow](../../cardputer-adv-oracle/docs/DEVELOPMENT.md)
- [Shared draw unit](../../cardputer-adv-oracle/firmware/src/draw.cpp)
- [Site instructions](../../cardputer-adv-oracle/docs/PAGES.md)
- [Static site generator](../../cardputer-adv-oracle/tools/build_site.py)
- [Screen export/contact sheet](../../cardputer-adv-oracle/tools/render_screens.py)
- [Media provenance manifest](../../cardputer-adv-oracle/docs/media/site-media.json)
- [Release validator](../../cardputer-adv-oracle/tools/validate_release.py)
- [Installation instructions](../../cardputer-adv-oracle/docs/INSTALL.en.md)

Adapt: generate a site from real versioned data and allowlisted media; verify image hashes; validate local links; keep public content separate from private settings; match firmware/resources inside release archives. Its native renderer exercises core/drawing code while hardware remains separate.

Oracle also enforces `0x140000`, but its manifest/image checks are a starting point rather than complete proof of device compatibility. The new project should validate a complete image with the pinned tooling and an explicit install profile.

Its M5Apps/NVS work is relevant: a conventional `nvs` partition may not exist and library initialization can conflict with the launcher. This offline radio should avoid importing Bluetooth/Wi-Fi persistence workarounds unnecessarily. Investigate only the storage actually needed.

## Agent Console: audio ownership and public presentation

Useful entry points:

- [Audio/storage behavior](../../cardputer-adv-voice-agent/docs/audio-and-storage.md)
- [Audio service implementation](../../cardputer-adv-voice-agent/firmware/src/hardware/audio_service.cpp)
- [Simulator instructions](../../cardputer-adv-voice-agent/firmware/sim/README.md)
- [Screen documentation exporter](../../cardputer-adv-voice-agent/firmware/sim/render_docs.py)
- [Review-to-screenshot playbook](../../cardputer-adv-voice-agent/docs/prompt-playbook.md)
- [Release packaging script](../../cardputer-adv-voice-agent/firmware/scripts/package-release.sh)
- [Site build notes](../../cardputer-adv-voice-agent/site/README.md)
- [Publishing notes](../../cardputer-adv-voice-agent/docs/publishing.md)

Adapt: queued PCM data stays valid until consumed; codec startup/failure/shutdown should reach a quiet state; hardware work belongs behind services; actual screen exports feed both README and site; checksums refer to one canonical firmware artifact.

The inspected release script allows `0x170000`. Its firmware and native graphics dependency ranges differ. Resolve both deliberately for the new project rather than copying them as universal limits or automatically matching dependencies.

The Agent site uses a heavier vinext/Sites setup with a separate static Pages build. Oracle's standard-library generator is the proposed simpler fit for this project's product page; a live web app is not required for the first radio release.

The prompt playbook contains historical prompts for reviews, parallel agents, commits and pushes. These were reference material, not instructions to execute in this planning task.

## Screens inspected visually

- [Oracle home](../../cardputer-adv-oracle/docs/media/screen-home-en.png): clear hierarchy and a compact status/header/footer pattern.
- [Agent rain screensaver](../../cardputer-adv-voice-agent/docs/images/screens/saver-rain.png): demonstrates bounded decorative motion and a visible wake affordance.
- [Hermes working scene](../../cardputer-adv-hermes-terminal/docs/images/screens/sleep-working.png): demonstrates using a fixed portrait area alongside compact status.

The lofi app should adapt the rendering discipline, then use original artwork and calmer, scene-dominant composition suited to passive listening.

## Lofi Cat: the user's existing product and artwork

Found locally at `../../lofi_cat`, with Git remote [m0rg0t/lofi_cat](https://github.com/m0rg0t/lofi_cat). Findings below are from the local checkout; the remote was not fetched or claimed to be synchronized.

Useful entry points:

- [Product README](../../lofi_cat/README.md)
- [Track playback hook](../../lofi_cat/frontend/src/hooks/useMusic.ts)
- [Mood-to-cat-state hook](../../lofi_cat/frontend/src/hooks/useCatAnimation.ts)
- [Actual cat component](../../lofi_cat/frontend/src/components/CatAnimation/CatAnimation.tsx)
- [Cat CSS](../../lofi_cat/frontend/src/components/CatAnimation/CatAnimation.module.css)
- [Room rendering](../../lofi_cat/frontend/src/panels/MainPanel/components/RoomBackground.tsx)
- [Scene positioning model](../../lofi_cat/docs/cat-positioning-system.md)
- [Room catalog](../../lofi_cat/pocketbase/pb_migrations/1757300002_import_initial_room_themes.js)
- [Cat catalog](../../lofi_cat/pocketbase/pb_migrations/1757300001_import_initial_cat_skins.js)

Verified behavior matters here. `useMusic.ts` assigns the selected track file to an HTML Audio element; its AudioContext controls playback gain. It does not compose the music. `useCatAnimation` selects semantic states, but `CatAnimation.tsx` currently ignores its animation prop and shows one PNG with a CSS float cycle. The room component supports image/video backgrounds and responsive cat placement. This provides useful product/asset structure, not an embedded generator or completed multi-pose animation set.

Visually inspected the [cozy room](../../lofi_cat/frontend/public/backgrounds/rooms/room1_big.webp), [headphone cat](../../lofi_cat/frontend/public/cats/cat3.png) and [existing app screenshot](../../lofi_cat/assets/screenshoot1.png). The warm lighting and prominent cat are useful experience references. The user chose a new pixel-art cat and location for the ADV. Author new device-scale artwork; retain lessons about scene hierarchy and separate cat/room layers.

| Existing concept | Cardputer adaptation |
| --- | --- |
| Mood tags and player controls | Three curated generator presets, play/pause/next and simple keys |
| Separate room and cat assets | Background, cat anchor and compact sprite poses |
| Room coordinate metadata | Fixed device-space anchor and bounds, validated at build time |
| Studying/sleeping/window states | Actual deterministic pose/idle transitions |
| Cached active assets | Preloaded bounded content; no network dependency |
| Favorite tracks | Versioned generated session seeds |

The new radio's scope does not require the existing VK/backend/account/shop systems. Source music files remain references for listening if appropriate; they are not the device's generated output. Record the newly authored artwork's sources and provenance in the new asset manifest. No source assets or code were copied into this planning folder.

## Upstream verification

| Source | Relevant evidence |
| --- | --- |
| [M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv) | ADV display, codec, speaker/jack behavior, pins and board identity |
| [ESP32-S3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) | 512 KB SRAM; chip comparison identifies FN8 flash and absence of in-package PSRAM |
| [M5Unified 0.2.17 speaker API](https://raw.githubusercontent.com/m5stack/M5Unified/0.2.17/src/utility/Speaker_Class.hpp) | Configurable output rate/DMA/task parameters, streaming PCM interface and queue depth semantics |
| [M5Apps README](https://github.com/d4rkmen/M5Apps) | Installation sources and launcher usage |
| [M5Apps partition CSV](https://raw.githubusercontent.com/d4rkmen/M5Apps/main/partitions.csv) | Upstream partition declaration; fetched content differed from the README table |
| [ESP-IDF I2S documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2s.html) | Driver concepts; use the actual pinned framework version for implementation |

The fetched M5Apps README showed `apps_app` size `0x180000`; the raw CSV showed `0x170000`. These reads may reflect documentation drift or different cache revisions. Neither establishes the user's installed layout or the location/size of this new application. Reconcile a pinned upstream commit if needed, then inspect the actual device before a direct update.

The M5Unified speaker header was also inspected locally in Hermes's installed `0.2.17` library: default output is 48 kHz, so passing 32 kHz PCM alone is not evidence that the hardware runs at 32 kHz. Explicitly configure and measure the output path, and report any resampling.
