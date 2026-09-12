# Music engine and comparison

> Accepted design and milestone criteria. See [current implementation and verification](VERIFICATION.md) for what is built and measured; remaining targets below are not completion claims.

## One composer, two sound engines

Both candidates receive the same sample-timed note/control events. Composition, swing, chord selection and arrangement must be identical for an A/B example. Only the instruments differ; first compare with identical effects, then allow each candidate a separately labeled best-tuned mix.

| Dimension | A: pure synthesis | B: synthesis plus samples |
| --- | --- | --- |
| Warm keys | Lightweight FM/additive electric piano with shaped attack and decay | Short root-note attacks/body samples with interpolation and looped or synthesized sustain |
| Bass | Sine/triangle or filtered oscillator | Same synthesized bass initially |
| Drums | Pitch-envelope kick; filtered-noise snare, hats and percussion | Small original or redistributable one-shot bank |
| Texture | Filtered noise and sparse synthetic crackle | Same procedural texture initially |
| Expected strength | Small assets, flexible timbre, self-contained operation | More convincing attacks and drum character with less sound-design work |
| Expected weakness | May sound like a soft chiptune until carefully tuned | Memory/flash pressure; short samples can sound repetitive or reveal loop seams |
| Main CPU work | Oscillators, envelopes, filtering and mixing | Interpolation, envelopes, filtering and mixing |
| Storage | Oscillator tables and presets | Tables/presets plus explicitly bounded instrument data |
| Dependency | Sound-design quality | Sample provenance and pack tooling as well as sound design |
| Selection status | Open | Open |

These are engineering expectations, not measured advantages. A small hybrid bank does not inherently require an SD card; it could fit in a BIN. A larger bank may require SD and preloading, which must be evaluated against the chosen installation experience.

## Comparison experiment

1. Write one deterministic composer and one bounded voice mixer with interchangeable instrument providers.
2. Export a 45-second isolated-instrument demonstration and a 90-second full mix per candidate. Use three fixed seeds spanning sparse, chord-heavy and percussion-heavy arrangements. Record every seed and parameter in a manifest.
3. Produce the same program through the physical speaker and 3.5 mm output. A digital host WAV alone cannot establish hardware sound quality. Keep one raw hardware recording alongside any listening-normalized copy.
4. Match the listening level and effects. Mark files with neutral A/B labels for comparison, and keep the mapping in the result record.
5. Measure app BIN bytes, static RAM, minimum free internal heap, largest free block, peak render time, buffer starvation and achieved scene rate.
6. Render three 10-minute examples per candidate to assess repetition and arrangement, then perform a 30-minute listening session for the preferred candidate.
7. Present examples and measurements. The user chooses the production engine; document the reason rather than silently treating the earlier hybrid recommendation as approval.

Hard gates: offline generation; no playback glitches during the device stress test; acceptable memory headroom; fit within the selected compatibility profile; redistributable assets. A candidate failing a hard gate is not rescued by a subjective score.

For candidates passing those gates, score warmth/timbre (35%), musical coherence and variation (30%), listening comfort (20%), and implementation/content maintenance (15%). The weighting is a proposed review aid. Do not publish fictitious scores before listening.

## Composition grammar

Proposed starting palette:

- 4/4 time at 68–88 BPM; default around 76 BPM. A mood narrows the range instead of choosing a radically different tempo every phrase.
- Curated four- or eight-bar progressions using major/minor sevenths and occasional ninths. Choose one key and tonal palette per session.
- Voice-led chord inversions: minimize movement and keep the melody and bass in distinct registers. Avoid uncontrolled extensions and large jumps on every chord.
- Bass emphasizes roots and selected fifths, with occasional prepared approaches. Preserve space around the kick.
- Snare/backbeat placement stays stable. Change hats, ghost notes and occasional fills within a small groove family.
- Melodies reuse a two- to four-bar motif, vary its ending and include rests. Strong beats favor chord tones; passing notes must resolve.
- Eighth-note swing starts around a 54–60% first-note share of each pair. Preserve the pair's total duration. Humanize velocity and selected note timing by small bounded amounts.
- Changes operate at multiple scales: articulation each hit, small motif variation after four/eight bars, layer changes after 16 bars, and a fresh session after roughly 64–112 bars.

For example, a generated session may move through an eight-bar sparse intro, a 16-bar groove, a 16-bar melodic variation, an eight-bar breakdown, a 16-bar return and an eight-bar outro. This is a template family with constrained variations, not a prerecorded song.

Prepare the next phrase in advance. Generate into fixed-size event storage with a known upper bound; do not allocate an entire endless score. A 64-bit musical sample position avoids long-running clock overflow. Fractional scheduling must carry remainder so tempo does not drift from repeated integer rounding.

## Voices and signal chain

Start at 32,000 Hz, mono, signed 16-bit output. This is a proposed quality/performance point; test codec configuration and output on the pinned library. Compare 22,050 Hz only if measured constraints justify it, and record any backend resampling separately.

Baseline polyphony: eight simultaneous voices with explicit priority and stealing rules. A four-note chord, bass and lead leave two voices for percussion/tails, so the arrangement must avoid uncontrolled overlap. Evaluate 12 voices as an upgrade after measurement. Voice stealing applies a short release; the bass or a loud sustained chord must not disappear abruptly.

The application mixes instruments into one stream rather than treating M5Unified's virtual playback channels as the composition model.

```text
note events → instruments/envelopes → wide accumulator
            → gentle tone filter → short delay (optional)
            → subtle saturation / gain control → fade → int16 PCM
```

Keep wide intermediates and explicit saturation before int16 conversion. Avoid signed overflow. Use band-limited oscillator/table choices where required; “lofi” does not excuse harsh aliasing. Remove DC bias and smooth volume/filter changes. Start with a short delay; a larger reverb is optional after profiling.

Noise and crackle remain quiet, sparse and adjustable down to zero. Kick-triggered attenuation, if used, is subtle. Transitions change layers at a bar/phrase boundary, use short audio fades where necessary, and preserve outgoing tails within the voice budget. Arbitrary key changes should use a planned outro/intro instead of overlapping two incompatible harmonies.

## Samples, if selected

Author samples on the development Mac under `assets/audio/`. The [sample production plan](SAMPLE_PRODUCTION.md) defines local key-note rendering, bounded ElevenLabs percussion auditions, optional atmospheric effects and packing. A [seven-sound starter brief](../assets/audio/briefs/starter-bank.json) targets 58,240 PCM bytes. No samples have been generated yet.

Use original synthesized/recorded sources or a pack whose redistribution terms are recorded. Every asset has source, license, attribution, conversion parameters and a hash. No stream recordings or complete song loops are part of the bank.

The first packed-bank experiment targets at most 64 KiB of resident PCM, with optional flash-resident read-only data evaluated separately. At 16 kHz mono PCM16 this is only about 2.05 seconds across the entire bank, so longer decays require loops or synthesized tails. This is a constraint to test, not a claim that an acoustic piano can fit convincingly in that budget.

Prepare data offline: trim, normalize sensibly, remove DC, set root pitch, check loop boundaries and pack a versioned manifest. The device still makes every musical decision. Do not perform WAV parsing, decompression or SD reads inside the audio render function. Validate maximum counts, sizes and rates before a pack becomes active.

## Reproducibility

A saved session includes seed, generation schema version, engine ID/version, mood and effective parameters, sound-pack identity/hash, and tempo/key decisions when necessary. Favorites restart the session from its beginning; mid-session resume is a separate feature.

Use independent deterministic random streams for composition, instrument variation and visuals. Changing FPS, opening a menu or changing the scene must not change the music. The event stream should be identical across supported host/device builds; PCM comparisons use documented tolerances if floating-point backends differ. Never promise bit-identical audio from an untested cross-platform float implementation.
