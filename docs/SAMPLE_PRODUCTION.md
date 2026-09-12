# Sample production

> Accepted design and milestone criteria. See [current implementation and verification](VERIFICATION.md) for what is built and measured; remaining targets below are not completion claims.

Samples are authored during development on the Mac, stored under `assets/audio/`, and packed into the firmware or an optional SD bank. The Cardputer composes and renders music offline using the selected instruments. API access is a development tool, not a listening requirement.

The user explicitly suggested ElevenLabs for effects or musical components when useful. It is an available source for the sample-assisted experiment; the pure-synthesis versus sample-assisted engine decision remains open. Implementation now includes a deterministic local bank and eight actual ElevenLabs percussion auditions. Generated effects remain private and unapproved; the embedded candidate uses the local bank.

## Sound sources

| Material | Proposed source | Why |
| --- | --- | --- |
| Electric-piano/key notes | Local synthesis rendered to WAV, initially using the native instrument code | Known pitch, controllable envelopes, clean isolated notes and reproducible edits |
| Bass | Live on-device synthesis in both candidates | Small, adjustable and no sample bank needed |
| Kick, soft snare, closed hat, rim | Local synthesis and ElevenLabs Sound Effects candidates | Compare character and attacks through the actual device output |
| Rain, room tone, vinyl texture | Procedural noise baseline; optional ElevenLabs effects | Adds atmosphere without making the baseline depend on long recordings |
| Cat purr or short mew | Optional ElevenLabs effect | A later scene detail, off by default during passive listening |
| Instrumental musical fragments | Optional ElevenLabs Music exploration | Audition timbre or extract a usable isolated sound after inspection |

ElevenLabs documents sound-effect generation, including musical elements and looping. Its Music API can generate instrumental material from a prompt or a structured composition plan. [Sound Effects overview](https://elevenlabs.io/docs/overview/capabilities/sound-effects), [Music quickstart](https://elevenlabs.io/docs/eleven-api/guides/cookbooks/music).

The proposed division is an engineering choice: a prompt requesting a particular note is not sufficient evidence of correct tuning, isolation or a clean sustain loop. Measure pitch and audition every candidate. Full phrases can serve as sound-design references; the initial device bank uses individual sounds so harmony, melodies and rhythm remain under the local composer's control.

## Project paths

```text
assets/audio/
  README.md
  briefs/starter-bank.json     # source recipes, prompts and target durations
  raw/elevenlabs/              # original responses + sanitized request metadata
  raw/local-synth/             # full-quality local renders + synthesis settings
  selected/                   # accepted, edited WAV masters
  manifests/                  # source hashes, pitch, trims, loops, provenance
build/audio/
  auditions/                  # labeled samples and matched musical examples
  builtin/                    # generated PCM bank/header + size report
  sd/                         # optional larger content packs
```

The audio README, brief, local WAV masters, bank manifest, firmware include and production tools are implemented. Eight raw ElevenLabs responses and audition WAVs are retained locally in ignored directories. Keep raw experiments out of normal Git commits; preserve accepted masters and metadata for rebuilding. Raw API output should be retained locally rather than assuming a future request recreates it exactly.

## First audition bank

Start with seven sounds: three key samples with roots at MIDI 48, 60 and 72, plus one kick, snare, closed hat and rim hit. Render local keys with the same sound-design code used by the native engine. For each percussion voice, compare its local synth with two generated candidates. That bounds the initial ElevenLabs audition at eight one-second requests, with further exploration based on what is missing.

Proposed packed durations at mono PCM16 / 16 kHz:

| Sound | Count × target duration | PCM bytes |
| --- | ---: | ---: |
| Key roots | 3 × 0.35 s | 33,600 |
| Kick | 1 × 0.30 s | 9,600 |
| Snare | 1 × 0.25 s | 8,000 |
| Closed hat | 1 × 0.10 s | 3,200 |
| Rim | 1 × 0.12 s | 3,840 |
| **Total target** | **1.82 s** | **58,240** |

This leaves 7,296 bytes below the 64 KiB resident-PCM ceiling. Sample descriptors and other runtime allocations must still be accounted for. These are editing targets: reject a bank that only fits by audibly chopping attacks/tails. Key sustains use reviewed loops or synthesized tails; playback interpolates into the proposed 32 kHz mix.

The first bank excludes recorded ambience. A two-second 16 kHz PCM16 rain loop alone occupies 64,000 bytes. Use procedural ambience in the compact baseline; larger optional packs must pass the same active-memory checks. Storage on SD does not make their decoded RAM cost disappear.

## ElevenLabs integration contract

The Sound Effects endpoint is `POST /v1/sound-generation`, with text, duration, prompt influence and model settings. The inspected API reference specifies 0.5–30 seconds and supports the `loop` option on `eleven_text_to_sound_v2`. Generate a longer master for a short hit, then trim locally. Choose an output format supported by the account and validate the returned format before conversion. [Sound Effects API](https://elevenlabs.io/docs/api-reference/text-to-sound-effects/convert).

Music exploration uses `POST /v1/music`. For a simple prompt, set `force_instrumental=true`; use either `prompt` or `composition_plan` according to the endpoint contract. Pin the selected model explicitly and save the returned source file. Composition seeds are not a guarantee that future service versions produce the same audio. [Music API](https://elevenlabs.io/docs/api-reference/music/compose).

Follow the useful parts of [Oracle's generation script](../../cardputer-adv-oracle/tools/generate_audio.py): read `ELEVENLABS_API_KEY` only from the local environment, hash the normalized request, skip already-completed variants and save progress per file. Adapt this pattern to the effects/music endpoints; its speech payload is unrelated to this bank.

Each request record contains provider/model, prompt, parameters, variant ID, date, response format, asset hash and usage/request identifiers when available. Never put authentication headers in metadata. Plan a preview mode that lists requests and estimated usage before generation, an explicit request cap, and no unlimited automatic regeneration. An uncertain timeout must be recorded so a retry does not silently duplicate work/cost.

Preserve source/provenance and the relevant account/output usage terms with accepted assets. Confirm suitability for distribution inside firmware/SD packs when preparing the release; do not infer raw-asset redistribution rights solely from a general music marketing statement.

## Editing and validation

1. Keep the original response/render unchanged. Decode a working master with desktop audio tools; record its actual channel count/rate/format.
2. Audition in isolation. Reject extra hits, voices, musical backing or unwanted reverberation in an instrument one-shot.
3. Trim leading silence, remove DC, apply a tiny edge fade and set sensible relative levels. Do not normalize every hat to the same loudness as the kick.
4. For keys, measure root pitch and check several transpositions. Edit loop points/crossfades or use a synthesized tail. Record loop coordinates in sample frames and check their bounds.
5. Downmix with phase checks, low-pass before resampling and export PCM16. Listen after conversion, not only to the full-quality source.
6. Pack the seven-sound candidate, validate total bytes/rates/offsets and export a manifest plus firmware data. A no-SD boot embeds the chosen compact bank if the hybrid engine wins.
7. Render identical score/seed examples through pure synthesis and the sample-assisted candidate. Test the device speaker and jack, memory and glitch behavior before choosing the production engine.

Proposed tools: `generate_samples.py` for bounded API/local source batches, `prepare_samples.py` for deterministic edits/packing, and the existing planned `render_audio.py` for auditions. These scripts are not implemented yet. Sample production belongs at the start of audio feasibility, before the final engine choice.
