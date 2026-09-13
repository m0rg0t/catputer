#!/usr/bin/env python3
"""Bounded host render/score matrix for meter, tone and gain regressions.

Runs the real native composer and output stage. Keeps one temporary WAV/CSV
pair, emits public numeric evidence only, and does not use any SD/user state.
Host throughput and rule checks do not establish device audio quality/timing.
"""
import argparse
import json
import hashlib
from pathlib import Path
import subprocess
import tempfile

try:
    from .analyze_score import inspect
    from .build_release import music_profile, read_version
except ImportError:
    from analyze_score import inspect
    from build_release import music_profile, read_version

ROOT = Path(__file__).resolve().parents[1]
TONES = ("epiano", "felt", "nylon", "vibes", "pad", "flute")
BASSES = ("round", "upright", "sub")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, default=ROOT / "build/cmake/lofi_native")
    parser.add_argument("--seconds", type=int, choices=range(10, 121), default=30, metavar="10..120")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/evidence/meter-tone-check.json")
    args = parser.parse_args()
    profile = music_profile(ROOT)
    seed = 0xCA7CAFE
    results = []
    failures = []
    # Every tone on chords and melody at the stressful upper tempo, every
    # meter/mood/backend; add the lower and middle tempo on the default palette.
    palettes = [(tone, TONES[(i + 3) % len(TONES)], BASSES[i % 3])
                for i, tone in enumerate(TONES)]
    cases = [(180, *palette) for palette in palettes]
    cases += [(bpm, "epiano", "vibes", "round") for bpm in (40, 120)]
    cases += [(180, "pad", "pad", "sub")]
    (ROOT / "build/tmp").mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="music-matrix-", dir=ROOT / "build/tmp") as tmp:
        tmp = Path(tmp)
        for mood in ("cozy", "rainy", "night"):
            for meter in ("4/4", "3/4", "6/8"):
                baseline_scores = {}
                for bpm, keys, lead, bass in cases:
                    pair = []
                    for engine in ("synth", "hybrid"):
                        cmd = [str(args.native), "--seed", str(seed), "--seconds", str(args.seconds),
                               "--mood", mood, "--meter", meter, "--bpm", str(bpm),
                               "--keys", keys, "--lead", lead, "--bass", bass, "--engine", engine,
                               "--volume", "300", "--wav", str(tmp / "current.wav"),
                               "--meta", str(tmp / "current.json"), "--score", str(tmp / "current.csv")]
                        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
                        data = json.loads((tmp / "current.json").read_text())
                        score = inspect(tmp / "current.csv")
                        if data["version"] != read_version(ROOT) or data["generation_schema"] != profile["generation_schema"]:
                            raise ValueError("Stale native binary")
                        result = {field: data[field] for field in (
                            "engine", "mood", "meter", "bpm", "keys_tone", "lead_tone", "bass_tone",
                            "frames", "peak", "rms", "clipped_samples", "max_voices", "voice_steals",
                            "dropped_note_events", "score_hash", "score_events", "limited_samples", "minimum_keys_gain_q15")}
                        result["score_audit"] = {key: value for key, value in score.items() if key not in ("file", "sha256")}
                        results.append(result)
                        pair.append((data["score_hash"], data["score_events"]))
                        if (data["clipped_samples"] or data["dropped_note_events"]
                                or data["max_voices"] > profile["voice_capacity"] or score["rule_violations"]
                                or not 28835 <= data["minimum_keys_gain_q15"] <= 32767):
                            failures.append({"case": len(results) - 1, "reason": "audio/score rule failure"})
                    if pair[0] != pair[1]:
                        failures.append({"case": len(results) - 2, "reason": "A/B scores differ"})
                    baseline = baseline_scores.setdefault(bpm, pair[0])
                    if baseline != pair[0]:
                        failures.append({"case": len(results) - 2, "reason": "tone selection changes score"})
                print(f"Checked {mood} {meter}: {len(cases) * 2} renders", flush=True)
    output = {"version": read_version(ROOT), "generation_schema": profile["generation_schema"],
              "source": "native_shared_engine_and_score_csv", "hardware_verified": False,
              "native_sha256": hashlib.sha256(args.native.read_bytes()).hexdigest(), "seed": f"{seed:016x}",
              "seconds_per_render": args.seconds, "render_count": len(results),
              "total_audio_frames": sum(row["frames"] for row in results),
              "scope": "Finite host matrix; every selected tone, all meters/moods/backends at 180 BPM; default tones also 40/120 BPM; 300% output. No subjective quality or device timing claim.",
              "failures": failures, "renders": results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(f"Wrote {args.output}; {len(failures)} failures", flush=True)
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
