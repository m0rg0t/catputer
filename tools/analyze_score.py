#!/usr/bin/env python3
"""Inspect native --score CSV exports; musical-rule evidence, not a listening score."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
from pathlib import Path

try:
    from .build_release import music_profile
except ImportError:
    from build_release import music_profile

SAMPLE_RATE = 32000
SCALES = {0: {0, 2, 4, 5, 7, 9, 11}, 1: {0, 2, 3, 5, 7, 8, 10}}
PITCHED = {"keys", "bass", "lead"}
INSTRUMENTS = PITCHED | {"kick", "snare", "hat", "rim"}


def inspect(path):
    expected_schema = music_profile(Path(__file__).resolve().parents[1])["generation_schema"]
    with path.open(newline="") as source:
        raw = list(csv.DictReader(source))
    if not raw:
        raise ValueError(f"Empty score: {path}")
    rows = [{key: value if key == "instrument" else int(value)
             for key, value in row.items()} for row in raw]
    bars = defaultdict(list)
    phrases = defaultdict(list)
    previous = {}
    max_leap = {"bass": 0, "lead": 0}
    ranges = {name: [] for name in PITCHED}
    violations = []
    passing = 0
    truncated_passing = 0
    last_time = -1
    offsets = {}
    seen = set()
    export_frames = rows[0]["export_frames"]
    for row in rows:
        fingerprint = tuple(sorted(row.items()))
        if fingerprint in seen:
            raise ValueError("Duplicated score event")
        seen.add(fingerprint)
        if row["schema"] != expected_schema:
            raise ValueError("Score schema does not match current composer; rebuild and export again")
        if (row["instrument"] not in INSTRUMENTS or not 0 <= row["midi"] <= 127
                or not 1 <= row["velocity"] <= 127 or row["duration_samples"] <= 0
                or row["minor"] not in SCALES or not 0 <= row["key_pc"] < 12
                or row["bpm"] <= 0 or row["bar"] < 0 or row["session"] < 0
                or row["export_frames"] != export_frames
                or not 0 <= row["transport_start_sample"] < export_frames):
            raise ValueError("Invalid note or export bounds")
        offset = row["transport_start_sample"] - row["start_sample"]
        if offset < 0 or offsets.setdefault(row["session"], offset) != offset:
            raise ValueError("Inconsistent automatic-session transport offset")
        step_q32 = (SAMPLE_RATE * 60 << 32) // (row["bpm"] * 4)
        if not ((step_q32 * row["bar"] * 16) >> 32) <= row["start_sample"] < (
                (step_q32 * (row["bar"] + 1) * 16) >> 32):
            raise ValueError("Note outside declared score bar")
        if row["transport_start_sample"] < last_time:
            raise ValueError("Score notes are not in transport order")
        last_time = row["transport_start_sample"]
        identity = (row["session"], row["bar"])
        bars[identity].append(row)
        instrument, note = row["instrument"], row["midi"]
        if instrument not in PITCHED:
            continue
        ranges[instrument].append(note)
        chord = {row[f"chord_{i}"] % 12 for i in range(4)}
        scale = {(row["key_pc"] + degree) % 12 for degree in SCALES[row["minor"]]}
        if instrument in ("bass", "lead"):
            low, high = (32, 48) if instrument == "bass" else (64, 83)
            if not low <= note <= high:
                violations.append([*identity, instrument, note, "outside instrument register"])
        step_samples = SAMPLE_RATE * 60 / (row["bpm"] * 4)
        step = round(row["start_sample"] / step_samples - row["bar"] * 16)
        if note % 12 not in scale:
            violations.append([*identity, instrument, note, "outside session scale"])
        if instrument == "keys" and note % 12 not in chord:
            violations.append([*identity, instrument, note, "outside current chord"])
        if instrument == "bass" and step % 4 == 0 and note % 12 not in chord:
            violations.append([*identity, instrument, note, "strong bass outside chord"])
        if instrument == "lead":
            if note % 12 not in chord:
                passing += 1
                if step % 4 == 0 or row["duration_samples"] >= 3 * step_samples - 1:
                    violations.append([*identity, instrument, note, "strong/long nonchord note"])
            # Ignore velocity/microtiming so a new signature needs an actual
            # pitch, rhythm or articulation change, not random humanization.
            phrases[(row["session"], row["bar"] // 4)].append(
                (row["bar"] % 4, step, note - row["key_pc"],
                 round(row["duration_samples"] / step_samples)))
        if instrument in max_leap:
            key = (row["session"], instrument)
            if key in previous:
                max_leap[instrument] = max(max_leap[instrument], abs(note - previous[key]))
            previous[key] = note
    ordered_sessions = sorted(offsets)
    if (ordered_sessions != list(range(len(offsets))) or offsets[0] != 0
            or any(offsets[b] <= offsets[a] for a, b in zip(ordered_sessions, ordered_sessions[1:]))):
        raise ValueError("Missing/reordered automatic session")
    for session in ordered_sessions:
        observed = sorted(bar for s, bar in bars if s == session)
        if observed != list(range(len(observed))):
            raise ValueError("Missing or reordered score bar")
    last_bar = max(bars)
    harmony_fields = ("seed", "bpm", "key_pc", "minor", "chord_root",
                      "chord_0", "chord_1", "chord_2", "chord_3")
    for identity, events in bars.items():
        if len(events) > 48:
            raise ValueError("Scheduled bar exceeds note capacity")
        if any(any(row[key] != events[0][key] for key in harmony_fields) for row in events):
            raise ValueError("Inconsistent harmony metadata within bar")
        row = events[0]
        scale = {(row["key_pc"] + degree) % 12 for degree in SCALES[row["minor"]]}
        if any(row[f"chord_{i}"] % 12 not in scale for i in range(4)):
            violations.append([*identity, "harmony", row["chord_root"], "chord outside session scale"])
        lead = [row for row in events if row["instrument"] == "lead"]
        for i, row in enumerate(lead):
            chord = {row[f"chord_{j}"] % 12 for j in range(4)}
            if row["midi"] % 12 in chord:
                continue
            following = lead[i + 1] if i + 1 < len(lead) else None
            if following is None and identity == last_bar:
                # A timed export can end between a passing note and its target.
                # Do not classify that unobserved continuation as a failure.
                truncated_passing += 1
                continue
            if (following is None or following["midi"] % 12 not in chord
                    or abs(following["midi"] - row["midi"]) not in (1, 2)
                    or following["start_sample"] - row["start_sample"] >
                       3 * SAMPLE_RATE * 60 / (row["bpm"] * 4) + 96):
                violations.append([*identity, "lead", row["midi"], "passing note does not resolve"])
    progressions = defaultdict(list)
    for (session, bar), events in bars.items():
        row = events[0]
        progressions[(session, bar // 4)].append(
            tuple(sorted((row[f"chord_{i}"] - row["key_pc"]) % 12 for i in range(4))))
    # The explicit export horizon distinguishes four complete bars from a
    # truncated phrase whose final chord has merely begun.
    complete = set()
    for session, phrase in progressions:
        final_bar = phrase * 4 + 3
        if (session, final_bar) not in bars:
            continue
        bpm = bars[(session, final_bar)][0]["bpm"]
        step_q32 = (SAMPLE_RATE * 60 << 32) // (bpm * 4)
        end_sample = (step_q32 * (final_bar + 1) * 16) >> 32
        if offsets[session] + end_sample <= export_frames:
            complete.add((session, phrase))
    complete_progressions = {tuple(chords) for identity, chords in progressions.items()
                             if identity in complete and len(chords) == 4}
    return {
        "file": path.name,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "generation_schemas": sorted({row["schema"] for row in rows}),
        "sessions": len({row["session"] for row in rows}),
        "bars": len(bars), "scheduled_notes": len(rows),
        "max_notes_in_bar": max(map(len, bars.values())),
        "pitch_ranges": {name: [min(notes), max(notes)] if notes else None
                         for name, notes in sorted(ranges.items())},
        "max_consecutive_semitones": max_leap,
        "resolved_passing_notes": passing - truncated_passing - sum(v[-1] == "passing note does not resolve" for v in violations),
        "passing_notes_at_export_cutoff": truncated_passing,
        "distinct_four_bar_progressions": len(complete_progressions),
        "distinct_four_bar_lead_phrases": len({tuple(notes) for identity, notes in phrases.items()
                                              if identity in complete}),
        "rule_violations": violations,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scores", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    reports = [inspect(path) for path in args.scores]
    result = {"source": "native_shared_composer_csv", "hardware_verified": False,
              "scope": "Natural-scale membership, chord anchors, passing-note resolution, register and structural variety; no subjective quality rating.",
              "scores": reports}
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
        print(f"Wrote {args.output}")
    else:
        print(text, end="")
    if any(report["rule_violations"] for report in reports):
        raise SystemExit("Score rule violations found; inspect report")


if __name__ == "__main__":
    main()
