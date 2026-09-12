# Audio source workspace

The hybrid candidate uses **seven original sounds generated locally**: three key notes, kick, snare, closed hat and rim. They total 58,240 bytes of mono PCM16 at 16 kHz. The composer triggers/resamples these one-shots into its 32 kHz output; no songs or backing loops are embedded.

- `briefs/starter-bank.json`: bounded sounds, source prompts and packing targets.
- `selected/local-v1/`: reproducible accepted WAV masters for the initial comparison.
- `manifests/local-v1.json`: exact hashes, root pitches, lengths and provenance.
- `manifests/elevenlabs-auditions.json`: sanitized records for eight private effects auditions, with listening and redistribution review still unset.
- `raw/elevenlabs/`: ignored original API responses and request metadata.
- `build/audio/auditions/` at the project root: ignored audition WAVs.

Rebuild and validate the local bank:

```sh
python3 tools/generate_samples.py local
python3 tools/prepare_samples.py
python3 tools/prepare_samples.py --validate-only
```

Commands run from the project root. `prepare_samples.py` emits `firmware/src/audio/sample_bank_data.inc` and bounded bank reports. Both tools use Python's standard library.

Optional effects generation uses `ELEVENLABS_API_KEY` from the development environment. Inspect `python3 tools/generate_samples.py elevenlabs --dry-run --max-requests 8` first. Actual generation is bounded and resumable; uncertain timeouts require explicit retry. The eight current responses are retained locally, so a normal rerun creates no additional requests. No provider credential or generated effect is shipped in the firmware.

Read [sample production](../../docs/SAMPLE_PRODUCTION.md) for the comparison workflow. Keep generated effects out of public assets until they have been listened to, prepared, and approved for redistribution.
