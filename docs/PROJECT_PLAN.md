# Project plan

> Accepted design and milestone criteria. See [current implementation and verification](VERIFICATION.md) for what is built and measured; remaining targets below are not completion claims.

## Product promise

Launch an app in M5Apps and settle into an endless stream of original, locally generated lofi music. An original pixel-art cat is the main character in a cozy pixel-art room that moves quietly while the music develops. A few keys change mood, start a new session, adjust volume or recall a favorite seed.

This is procedural composition: a small musical system chooses harmony, rhythm, motifs and arrangement, then renders the notes locally. A neural audio model is not part of the proposed ESP32 design. A sample-assisted candidate still creates the composition on the device; it uses individual instrument sounds rather than finished tracks.

The sound goal is relaxed listening for at least 30 minutes: warm harmonic texture, audible but restrained groove, controlled treble, silence between melodic phrases and meaningful variation over minutes. Recognizable musical coherence matters more than the number of random combinations.

## First release

| Area | Planned behavior |
| --- | --- |
| Listening | Continuous generated sessions, roughly 3–6 minutes each, connected by musical transitions |
| Music | Chords, bass, drums, a sparse lead and optional low-level texture |
| Moods | Three curated parameter sets: Rainy Desk, Warm Evening and Night Train; names remain working labels |
| Controls | Play/pause, volume, next session, mood, favorite seed, scene view and a small settings/help screen |
| Visuals | One original pixel-art cat and cozy location, with small animated layers; additional scenes depend on measured resources |
| Offline operation | No network connection needed for boot, generation or playback |
| Sound engine | Selected after a matched comparison of pure synthesis and synthesis plus small instrument samples |
| Installation | Application-only M5Apps BIN, explicit compatibility information and optional resource package |
| Development | Shared C++ music core, real-code SDL screen preview, deterministic WAV and screenshot exports |
| Project page | Generated static page with real preview captures, listenable music examples and accurate installation instructions |

The built-in baseline must work without SD. Optional SD content adds scenes/sound banks and persistent favorites/settings. The visual direction is confirmed: a new pixel-art cat and location, with the cat as the room's main character. Lofi Cat serves as a reference for the experience, as recorded in the [decision log](DECISIONS.md).

The local [Lofi Cat project](../../lofi_cat/README.md) demonstrates cozy-room composition, a prominent cat and mood selection. Adapt its character/room asset separation into a small sprite system with newly authored pixel art. Its inspected audio implementation loads track files, so the composer and sound renderer described here are new work. Do not assume its React/HTML audio code or CSS float effect is a ready-made ESP32 engine.

Later candidates: more sound banks/scenes, a sleep timer, WAV recording to SD and an interactive browser music demo. Recording adds concurrent storage work and is deliberately outside the initial acceptance gate. A groovebox editor, live keyboard instrument, streaming radio service, Bluetooth headphone transport and cloud composition are outside the first release.

## What “good” means

These are targets to validate, not promises about unbuilt firmware:

- First audible music within five seconds of app launch using built-in resources, with a gentle fade-in and conservative initial volume.
- The default arrangement sounds calm and coherent in a 30-minute listening session. It must not become a harsh random-note demonstration or an unchanged four-bar loop.
- Two hours of physical playback with animation and repeated input produce no buffer underruns, resets, sustained heap loss or stuck notes.
- Ordinary key actions visibly respond within 100 ms. A musical change may wait for the displayed next bar or phrase boundary.
- Animation aims for 12 frames per second and can fall back to 6 without changing musical timing.
- The selected release fits the supported installation profile. A provisional compact goal is below `0x140000` bytes (1,310,720 bytes); the actual device layout must still be checked.
- A favorite reproduces a session from its beginning for the same generation version, engine, parameter set and content pack. It is not a promise of identical sound across future engine versions.
- Public screenshots, audio examples, firmware downloads and reported version identify the same source/build/content set.

## Main risks and how to resolve them

| Risk | Consequence | Earliest useful evidence |
| --- | --- | --- |
| Synthetic instruments sound thin | Technically correct firmware fails the listening goal | Matched keys/drums demos and full mixes before final art |
| Sample bank overwhelms RAM or app size | Hybrid engine becomes incompatible with the compact install | Packed bank size, link map and device heap measurements |
| Visual work interrupts audio | Glitches during animation or controls | Moving-screen stress test on the ADV in milestone 1 |
| Procedural music repeats too obviously | Listener stops after a few minutes | Multi-seed 10-minute renders and a 30-minute subjective review |
| M5Apps layout assumptions are wrong | Installer incompatibility or damage during direct updates | Read actual installed layout; prefer the launcher's installer |
| Nice host demo hides hardware problems | Site overstates sound quality or stability | Separate host render, line-output recording and device test records |
| Expanding extras delays the core product | Many controls, no satisfying radio | Ship one scene and three curated moods after the audio gate |

The critical path is audio character → device timing/memory → musical variety → animation polish → release evidence. The desktop renderer and site pipeline should be designed early so every later milestone can produce reviewable evidence.

## Work package

The first implementation task should prepare the compact audition bank on the Mac, then deliver two comparable engines behind one interface, native audio exports and a minimal Cardputer app playing continuously under visual load. [Sample production](SAMPLE_PRODUCTION.md) covers local synthesis and optional ElevenLabs effects/music sources. It should stop short of choosing the final engine until listening results, firmware size, memory and stability are available for review. Milestone exit criteria are in [Delivery](DELIVERY.md).
