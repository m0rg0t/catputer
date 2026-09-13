# Installing the development candidate

Target: **M5Stack Cardputer ADV**, ESP32-S3 with ES8311 audio. The original Cardputer is not supported by this build.

This is an **application-only development image**, built against a conservative `0x140000` byte application capacity. Physical installation, audio output, two-hour device stability and return to the launcher have not yet been tested. That capacity is a build limit, not a universal M5Apps partition size.

## Existing M5Apps installation

1. Verify `SHA256SUMS` for the package. On macOS/Linux use `shasum -a 256 -c SHA256SUMS` from the extracted folder.
2. Copy `cardputer-lofi-0.1.8-dev.bin` to an SD card.
3. On the device, open **M5Apps → Installer → SD**, select the BIN and use the installer's compatible application slot.
4. Launch the installed app. Start at low volume and test the speaker and headphone output separately.
5. The built-in scene, music engines and instrument bank work without SD. To test that baseline, power down, remove SD, and restart the app.

The package contains no launcher, bootloader, partition table or erase/flash script. Do not feed an app BIN to a whole-device recovery workflow. A stock device without M5Apps needs a separately selected launcher installation; this package does not perform it. Home/reset behavior depends on the installed launcher and remains a physical acceptance test. The Go key currently opens help.

## Optional SD state

A FAT-formatted card present at boot enables `/LOFI/state.bin`. Settings and up to eight favorite sessions are saved after keyboard activity settles, through a dedicated storage task. Writes use a validated temporary file and retain a valid backup. Startup tries the primary, backup, then temporary state. Corruption falls back to RAM defaults.

No SD: music continues, settings/favorites live in RAM and disappear at restart. A write failure keeps the RAM state, displays an error and disables further saves until restart. Insert/remove cards while powered down for this candidate; hot-plug recovery is not implemented. External sample/scene packs are a later milestone, not loaded by this version.

Arduino's generic NVS startup is intentionally skipped: this app has no NVS consumers and never formats the launcher's shared NVS for its own settings. Adding networking or flash persistence requires revisiting that guard.

## Controls

Space play/pause; `-` / `=` volume; `M` moods; `N` next session; `F` add/remove current favorite; `L` favorites; `V` clean scene; `S` settings; `I` instruments/meter; `H` help; `E` compare sound engine.

In menus use `;` / `.` up/down, `,` / `/` left/right, Enter select, backtick/Escape back. Backspace removes a selected favorite. Keyboard letters are case-insensitive. The native preview also accepts arrow keys and `Q` to quit.

In **S → BPM**, use `,` / `/` for 1 BPM steps from 40 to 180. Enter toggles AUTO/manual; starting manual mode uses the currently audible tempo. AUTO chooses tempo from the mood and seed. Manual tempo remains selected across new sessions and is retained in favorites.

Volume runs from 0 to 300% in 5% steps. Above 100% it adds software gain, with a soft limiter before PCM output; actual loudness depends on the speaker or headphones. Volume changes ramp over 10 ms. The default remains 35%.

In **S → SLEEP**, select Off / 30 / 60 / 90 minutes. It fades over the final 30 seconds, then pauses playback without powering down or changing your volume. The countdown continues while manually paused. Select Off to cancel; deliberately resuming with Space clears a fading or expired timer. Sleep starts Off on every boot.

In **S → AUTO DIM**, select Off / 30 / 60 / 120 seconds (default 60). Inactivity dims the backlight to at most 10%. The first keypress wakes only; release it and press again to use the control. This preference persists on SD. On the desktop, dimming is simulated relative to the normal preview brightness.

Reset Settings restores saved preferences and leaves the runtime sleep timer alone; use Sleep Off to cancel it.

Mood/engine/instrument/meter/session changes take effect at the next bar with a fade. A BPM-only change takes effect at the next bar without restarting the current tune. A favorite restarts a seed at its beginning, not at the point where it was saved. Replay is tied to generation schema, parameters, tempo and the matching hybrid sample bank.

Open **I** to select the chord tone, melody tone, bass tone and musical meter. Use `;` / `.` to move, `,` / `/` or Enter to change the selected value. AUTO chooses a meter once per session; manual choices are 4/4, 3/4 and 6/8. In 6/8, BPM counts dotted-quarter pulses (two per bar).

The bottom strip shows real instrument activity and musical pulses over the room, with a translucent dark backing. Pause or mute silences its activity; motion OFF keeps a static meter display. The clean scene retains the unobtrusive strip.

Version 0.1.5-dev retains the LCD RGB565 correction and larger text, and introduces generation schema 4. It migrates earlier saved settings and retains old favorites, marked `OLD`; faithful replay needs the earlier firmware. New favorites include the selected tempo, meter and tones. Older firmware cannot read the new save format, so retain a copy of old state before a downgrade.

Version 0.1.6-dev changes only the visualization backing and label contrast. Music schema 4, save format 3 and existing 0.1.5-dev favorites remain compatible.

Version 0.1.7-dev fixes random-draw ordering so timing and velocity follow the intended schema-4 sequence across compilers, and pins floating-point multiply/add rounding. Save format 3 and existing schema-4 favorites remain readable. Earlier builds affected by compiler ordering can sound slightly different when replayed; the reference desktop note sequence is preserved.

Version 0.1.8-dev introduces clearer melodic phrasing and generation schema 5. Save format 4 remains 192 bytes and adds the auto-dim preference. Valid formats 1/2/3 migrate with auto-dim set to 60 seconds; old favorites remain visible as `OLD` and removable. Replay of a schema-4 favorite requires 0.1.7-dev. Keep a backup of your earlier state before downgrading, because earlier firmware cannot read format 4. Sleep timers are never saved.
