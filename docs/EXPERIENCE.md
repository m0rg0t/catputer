# Radio experience and screen design

> Accepted design and milestone criteria. See [current implementation and verification](VERIFICATION.md) for what is built and measured; remaining targets below are not completion claims.

## Listening flow

Launch from M5Apps. Initialize the built-in sound source and scene, choose the initial session, and fade into music at a conservative volume. Show a brief key hint, then let the scenery take most of the screen. No connectivity setup is part of listening.

A scene can change independently from the musical mood. A new mood affects the next phrase/session according to the transition policy; display the pending change so an intentional delay feels responsive. Next session requests coalesce instead of queuing a long list of skips.

Play/pause applies a short fade and preserves the current transport for resumption. New session deliberately changes the seed. Recalling a favorite restarts that session from the beginning. The current eight-hex-digit seed is accessible in the information overlay.

## Screen composition

The following are layout proposals, not exported firmware screenshots. An original pixel-art cat is the confirmed main character in a newly designed cozy pixel-art room.

| Region | Coordinates | Purpose |
| --- | --- | --- |
| Compact status | x 0–239, y 0–13 | Mood/session label, play state and battery |
| Main animated scene | x 0–239, y 14–117 | Original room/scenery with a few moving layers |
| Temporary key/status strip | x 0–239, y 118–134 | Short controls, volume or pending musical change |

After a short idle period, overlays can fade away to reveal the full scene. Pressing a control should still perform its advertised action; do not make play/pause or volume require a hidden extra wake key. Help and settings are explicit overlays/screens.

Use a restricted warm palette, clear silhouettes and readable pixel fonts. Measure strings in pixels instead of truncating by character count. The label, battery and status indicators need reserved bounds. Avoid dense sequencer grids on this display.

## Proposed keyboard map

| Key | Radio behavior |
| --- | --- |
| Space | Fade to pause/resume |
| `-` / `=` | Lower/raise volume with key repeat |
| `N` | Request next session; show the scheduled transition |
| `M` | Open mood selection |
| `F` | Favorite/unfavorite current session seed |
| `L` | Open favorites |
| `V` | Toggle clean scene / information overlay |
| `S` | Open settings |
| `H` | Help, including the ADV's actual key combinations |
| Enter | Confirm a menu choice |
| Esc | Close overlay/back |

Confirm these characters on the real ADV keyboard using M5Cardputer's reported key state. Make essential actions accessible without obscure combinations. Reserve the physical Home behavior for the launcher's normal return path; do not assume that reboot or a library button shortcut returns correctly without a device test.

Initial settings should stay small: volume, brightness, texture amount, animation level and reset-to-defaults. More detailed sound-design controls are out of the initial radio scope. Favorites can live in RAM during a no-SD session; show that persistent saving needs SD. Do not show a durable-save success message when nothing was written.

## Animation proposal

Confirmed direction: an original pixel-art cat and location, with the cat as the main character in a cozy room. Proposed first location: a small room at night with a rainy window, a warm lamp, a few books/plants and a clear place for the cat to relax or study. The cat supplies the character acting and music-linked movement. Its silhouette, colors and accessories will be developed as new artwork.

Use Lofi Cat as a reference for calm pacing, a prominent cat and separate room/character layers. Author the new composition on a 240 × 135 pixel grid, starting with a shared 16-color palette, deliberate pixel clusters and a readable silhouette. The cat should occupy approximately 48–64 pixels in height; test the exact placement against the HUD. Preserve an explicit cat anchor/bounds in each scene asset instead of relying on automatic centering.

The environment is pixel art too: furniture, window, lighting and weather must share the cat's pixel scale and palette. Keep the cat and its immediate surroundings visually clear, with quieter contrast in distant details. The first art review should show the complete room and cat together at native size, followed by a pose sheet and short animation loop. These will be concept assets until they are exported from the implemented renderer.

Use one static background and a few small sprite regions:

- Two or three cat poses for breathing, reading, listening or blinking on a slow loop.
- An ear/tail movement separated from the main cat cycle.
- Bounded rain particles and occasional window highlights.
- A small steam or lamp animation.
- Subtle beat-linked motion, such as a head nod or light response; avoid a constant full-screen pulse.

Use several different animation periods to reduce the appearance of one obvious short repeating loop. Cap the particle count and active sprite bytes. A scene should remain appealing as a still image and at 6 FPS. Low-motion mode can reduce particles and cat-pose changes without affecting the song.

Lofi Cat's `useCatAnimation` already describes mood-to-state mappings such as studying, sleeping and window-watching. Adapt that vocabulary into a deterministic device state machine. Its current `CatAnimation` ignores the state prop and floats a single image with CSS, so real poses must be created and verified for the ADV. Background video assets may guide atmosphere; the device renderer uses bounded sprites rather than replaying those videos.

Artwork masters remain editable; the asset tool exports the exact device palette, dimensions and sprite layout with a manifest. Review at native 240 × 135 and at 3× nearest-neighbor scale. Large source art is not proof of legibility on the display.

## Preview and screenshot scenarios

The desktop preview compiles the actual scene and UI drawing code. Inject a clock and realistic state; freeze them for captures. Keep the music seed separate from the visual seed. Native audio output is useful for iteration, but the preview is not a full ESP32 emulator.

Minimum scripted scenarios:

| Scenario | What it should catch |
| --- | --- |
| Startup / first sound | Clear state during initialization, no indefinite waiting |
| Playing with hints / clean scene | Legibility and visual balance in both modes |
| Paused | Unambiguous transport state without a harsh visual change |
| Volume 0 / volume maximum | Accurate mute indication and bounded text |
| Mood menu / pending mood | Visible selection and delayed musical change |
| Next session pending | Immediate input feedback while transition is scheduled |
| Favorites empty / full / long name | Empty-state guidance, clipping and limits |
| No SD / invalid optional pack | Working built-in playback and a useful explanation |
| Save failed / SD removed | No false claim that state was saved |
| Help / settings | Discoverable actual keys; no tiny dense text |
| Low battery / dimmed display | Visibility and continued playback |
| Reduced animation / static mode | Attractive scene under constrained rendering |

Export original 240 × 135 PNGs, 720 × 405 integer-scaled PNGs, a labeled contact sheet and a short animation capture. The animation capture must come from the running shared renderer. Record scenario, visual seed, time, firmware source revision and asset hashes in the media manifest.

All README/site illustrations of product UI should come from these captures. Concept art must be explicitly labeled and must not replace implementation evidence. Physical photos/recordings remain separate evidence for LCD brightness, audio quality, keys and performance.
