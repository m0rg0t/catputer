# Audio model feedback for Catputer

Checked 2026-09-13. This is a development-time listening aid on the Mac. The
Cardputer still composes and renders entirely offline. Model feedback is advisory;
a valid response is not a music-quality test pass or proof that an issue exists.

## Available candidates

| Candidate | Useful role | Evidence and limits |
| --- | --- | --- |
| OpenRouter `google/gemini-3.8-flash` | Inexpensive first-pass feedback on phrasing, balance and timbre | Audio input and text output confirmed in the live catalog; successfully tested on our WAVs. [Model and current pricing](https://openrouter.ai/google/gemini-3.8-flash). |
| OpenRouter `google/gemini-2.5-pro` | Independent second opinion on a shortlisted passage | Successfully tested on the same WAVs. An older, independently studied baseline; this is not a claim that it outperforms every newer model. [Model](https://openrouter.ai/google/gemini-2.5-pro), [music-perception study](https://arxiv.org/abs/2510.22455). |
| Replicate `zsxkib/audio-flamingo-3` | Alternative audio question-answering model | Its hosted interface accepts audio and a prompt and advertises music/production analysis, up to ten minutes. Not run in this pilot. The model page flags non-commercial weight licensing. [Model, schema and pricing](https://replicate.com/zsxkib/audio-flamingo-3). |
| Replicate `sakemin/all-in-one-music-structure-analyzer` | Estimated beats, downbeats and section boundaries | Produces structural analysis, not prose criticism. Not run here. For our generated audio, the composer's exact event clock is a better starting reference. Useful later for a physical recording. [Model](https://replicate.com/sakemin/all-in-one-music-structure-analyzer), [author's implementation](https://github.com/mir-aidj/all-in-one). |

OpenRouter accepts base64 audio in `input_audio` through `/chat/completions`;
audio URLs are not supported by this interface. The pilot sends an actual PCM WAV
excerpt, not a transcript or a description of the track. [Audio API documentation](https://openrouter.ai/docs/guides/overview/multimodal/audio).

NVIDIA's specialized [Music Flamingo](https://research.nvidia.com/labs/adlr/MF/)
is also relevant: its author reports training and evaluation specifically for
music, including harmony and timbre. We did not confirm a ready-to-run Replicate
listing, so it is not treated as an available provider integration here.

## What we actually tried

Two 45-second openings, Cozy/Synth and Night/Synth, were submitted separately to
both OpenRouter models. They are version 0.1.5-dev, schema 4, seed `0xCA7CAFE`,
electric-piano chords, vibraphone lead and round bass. PCM is the existing raw
native render, with no gain adjustment, normalization, resampling or effects.
The models received neither the filenames, meter/BPM metadata, score nor each
other's answers. They did receive the intended lofi-radio experience and the
12-voice mono constraint. This is an independent critique of each excerpt, not a
loudness-matched Synth/Hybrid preference test.

All four requests returned audio-token usage and parseable advice. This confirms
the API processed an audio input; self-reported `audio_access` and confidence are
still model claims. Results and excerpt hashes are retained in the
[pilot record](evidence/audio-model-pilot.json).

| Excerpt | Known clock | Flash guess | Pro guess | Reported Flash / Pro cost |
| --- | --- | --- | --- | --- |
| Cozy, 0–45 s | 80 BPM, 4/4 | 76 BPM, 4/4, high confidence | 75 BPM, 4/4, medium confidence | $0.00257625 / $0.01538625 |
| Night, 0–45 s | 76 BPM, 3/4 | 74 BPM, 4/4, medium confidence | Uncertain; no BPM/meter guess | $0.00254625 / $0.01616625 |

Total reported inference cost: **$0.036675**. These are actual response usage
figures, not a price guarantee for later models/providers or a music-quality
benchmark. Four excerpts are insufficient to rank the models generally.

## Advice and our cross-check

- **Accompaniment and melody separation.** Flash/Cozy and Pro/Night suggested
  that accompaniment sometimes crowds the lead. Pro/Cozy instead described the
  balance as good. Audition a modest accompaniment duck while lead notes sound;
  do not treat a specific EQ frequency suggested by a model as a measured defect.
- **Memorable phrasing and breathing space.** Both models offered variations of
  shorter gaps or stronger melodic direction. The exact 45-second scores already
  contain two distinct four-bar progressions each, plus three Cozy and four Night
  melodic phrases. A preference for more audible contrast is compatible with
  these facts; a claim that the score has no variation would be incorrect.
- **Cozy around 21–24 seconds.** Pro marked a phrase transition as disconnected.
  This is a timestamped listening hypothesis, not a confirmed harmony violation.
  Compare a smoother ending against the current returning motif at the same seed.
- **Night around 22–25 seconds.** Pro heard a dissonance and suggested restricting
  notes to the current chord. The score already anchors strong/sustained notes to
  chords. At 24.671 s it schedules B-flat5 over A-flat major seventh, resolving to
  A-flat5 at 24.908 s: an intentional short passing ninth, not an out-of-key note.
  A version with fewer passing tones could still sound calmer. Score correctness
  does not establish that the timbre and note overlap sound pleasant.
- **Meter clarity.** Flash's confident 4/4 reading of Night disagrees with the
  actual 3/4 schedule; Pro abstained. This does not prove the rhythm is broken.
  It gives us a reason to compare the perceived downbeat clarity on headphones.

Both exported scores pass the existing scale, chord-anchor, passing-resolution,
gate and meter-bound checks. No firmware or musical parameters were changed on
the strength of these opinions. The next useful experiment is one small change
at a time, matched by seed, meter, tempo, backend and listening level, followed by
our numeric checks and human A/B listening. Test both speaker and headphone output
before drawing conclusions about the device sound.

This caution is supported by research separating musical reasoning from auditory
perception: models can reason correctly about symbolic notes while mishearing
audio, and reported confidence does not resolve that gap. [Core music perception](https://arxiv.org/abs/2510.22455),
[MUSE benchmark](https://arxiv.org/abs/2510.19055).

## Repeat a review

Requires Python 3.11+ and an existing native-renderer 32 kHz mono PCM16 WAV.
The script has no added package dependency. Prepare locally without a network call:

```sh
python3 tools/review_audio.py build/audio/cozy-synth.wav \
  --start 0 --seconds 45 --out build/audio-review/cozy-prepared.json
```

For one paid request, set `OPENROUTER_API_KEY` securely in the process environment
and use a new output path:

```sh
python3 tools/review_audio.py build/audio/cozy-synth.wav \
  --start 0 --seconds 45 --model google/gemini-3.8-flash \
  --out build/audio-review/cozy-flash-review.json --send
```

Use `--model google/gemini-2.5-pro` for the second opinion. Each invocation sends
one excerpt of at most 90 seconds and caps completion tokens at 4,096. This bounds
the request size, not the account's dollar spend. Provider rates can change.
There are no automatic retries; after an uncertain failure inspect the provider's
usage before repeating. Never put a key in arguments, Git, firmware or the site.

The JSON stores the source/excerpt hashes, unmodified prompt, requested/returned
model, response usage and advice. The audio payload and authorization header are
not stored. Local reports live under ignored `build/audio-review/`; only an
explicitly selected, reviewed evidence record belongs in public documentation.
Issue timestamps are approximate excerpt-relative seconds, not sample-accurate
measurements. Add `excerpt_start_seconds` to map them to the original render.

For subsequent comparisons, include an opening, a later phrase transition and a
fresh seed. Check at least one deliberately changed example to see whether a
reviewer can distinguish it; generic praise/criticism alone is weak evidence.
Keep model advice separate from the existing deterministic test suite and never
automatically patch the composer based on model scores.
