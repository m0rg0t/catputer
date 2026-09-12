# Installing the development candidate

Target: **M5Stack Cardputer ADV**, ESP32-S3 with ES8311 audio. The original Cardputer is not supported by this build.

This is an **application-only development image**, built against a conservative `0x140000` byte application capacity. Physical installation, audio output, two-hour device stability and return to the launcher have not yet been tested. That capacity is a build limit, not a universal M5Apps partition size.

## Existing M5Apps installation

1. Verify `SHA256SUMS` for the package. On macOS/Linux use `shasum -a 256 -c SHA256SUMS` from the extracted folder.
2. Copy `cardputer-lofi-0.1.2-dev.bin` to an SD card.
3. On the device, open **M5Apps → Installer → SD**, select the BIN and use the installer's compatible application slot.
4. Launch the installed app. Start at low volume and test the speaker and headphone output separately.
5. The built-in scene, music engines and instrument bank work without SD. To test that baseline, power down, remove SD, and restart the app.

The package contains no launcher, bootloader, partition table or erase/flash script. Do not feed an app BIN to a whole-device recovery workflow. A stock device without M5Apps needs a separately selected launcher installation; this package does not perform it. Home/reset behavior depends on the installed launcher and remains a physical acceptance test. The Go key currently opens help.

## Optional SD state

A FAT-formatted card present at boot enables `/LOFI/state.bin`. Settings and up to eight favorite sessions are saved after keyboard activity settles, through a dedicated storage task. Writes use a validated temporary file and retain a valid backup. Startup tries the primary, backup, then temporary state. Corruption falls back to RAM defaults.

No SD: music continues, settings/favorites live in RAM and disappear at restart. A write failure keeps the RAM state, displays an error and disables further saves until restart. Insert/remove cards while powered down for this candidate; hot-plug recovery is not implemented. External sample/scene packs are a later milestone, not loaded by this version.

Arduino's generic NVS startup is intentionally skipped: this app has no NVS consumers and never formats the launcher's shared NVS for its own settings. Adding networking or flash persistence requires revisiting that guard.

## Controls

Space play/pause; `-` / `=` volume; `M` moods; `N` next session; `F` add/remove current favorite; `L` favorites; `V` clean scene; `S` settings; `H` help; `E` compare sound engine.

In menus use `;` / `.` up/down, `,` / `/` left/right, Enter select, backtick/Escape back. Backspace removes a selected favorite. Keyboard letters are case-insensitive. The native preview also accepts arrow keys and `Q` to quit.

Mood/engine/session changes take effect at the next bar with a fade. A favorite restarts a seed at its beginning, not at the point where it was saved. Replay is tied to generation schema, parameters and the matching hybrid sample bank.

Version 0.1.2-dev changes the composer to schema 3 and emits `lofi3-` codes. Existing schema-1 and schema-2 favorites stay in the save file and display `OLD`; they require the earlier firmware for faithful replay. Settings are retained, and old favorites can be removed normally. Do not reinterpret a `lofi1-` or `lofi2-` code as the same composition under schema 3.
