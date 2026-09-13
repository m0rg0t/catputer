import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from analyze_score import inspect


class ScoreAuditTests(unittest.TestCase):
    def note(self, step, midi, duration=1):
        return dict(schema=4, session=0, seed=42, bar=0, bpm=75, key_pc=0,
                    minor=0, chord_root=0, chord_0=60, chord_1=64,
                    chord_2=67, chord_3=71, instrument="lead", midi=midi,
                    velocity=50, start_sample=step * 6400,
                    transport_start_sample=step * 6400,
                    duration_samples=duration * 6400, export_frames=102400,
                    meter_numerator=4, meter_denominator=4, beats_per_bar=4,
                    steps_per_bar=16, steps_per_beat=4, bar_start_sample=0,
                    bar_end_sample=102400)

    def audit(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "score.csv"
            with path.open("w", newline="") as target:
                writer = csv.DictWriter(target, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            return inspect(path)

    def test_accepts_weak_passing_note_with_stepwise_resolution(self):
        result = self.audit([self.note(4, 76, 2), self.note(6, 77), self.note(8, 79, 3)])
        self.assertEqual(result["rule_violations"], [])
        self.assertEqual(result["resolved_passing_notes"], 1)
        self.assertEqual(result["max_consecutive_semitones"]["lead"], 2)

    def test_rejects_chromatic_sustained_and_unresolved_notes(self):
        result = self.audit([self.note(4, 73, 3), self.note(10, 77)])
        reasons = {item[-1] for item in result["rule_violations"]}
        self.assertIn("outside session scale", reasons)
        self.assertIn("strong/long nonchord note", reasons)
        self.assertIn("passing note does not resolve", reasons)

    def test_rejects_reordered_transport(self):
        with self.assertRaisesRegex(ValueError, "transport order"):
            self.audit([self.note(8, 79), self.note(4, 76)])

    def test_complete_phrase_requires_its_end_within_export(self):
        rows = []
        for bar in range(4):
            row = self.note(bar * 16, 76)
            row.update(bar=bar, export_frames=409600, bar_start_sample=bar*102400, bar_end_sample=(bar+1)*102400)
            rows.append(row)
        self.assertEqual(self.audit(rows)["distinct_four_bar_lead_phrases"], 1)
        for row in rows:
            row["export_frames"] = 320000
        self.assertEqual(self.audit(rows)["distinct_four_bar_lead_phrases"], 0)

    def test_rejects_duplicates_offsets_and_missing_bars(self):
        first, second = self.note(0, 76), self.note(8, 79)
        bad_offset = dict(second, transport_start_sample=51201)
        skipped_bar = dict(second, bar=2, bar_start_sample=204800, bar_end_sample=307200)
        for rows, message in (([first, first], "Duplicated"),
                              ([first, bad_offset], "offset"),
                              ([first, skipped_bar], "score bar")):
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    self.audit(rows)

    def test_detects_overlapping_melody_and_held_boundary(self):
        result = self.audit([self.note(0, 76, 4), self.note(2, 79, 2), self.note(15, 76, 2)])
        reasons = {item[-1] for item in result["rule_violations"]}
        self.assertIn("overlapping melody gates", reasons)
        self.assertIn("held note crosses harmony boundary", reasons)

    def test_compound_meter_uses_two_dotted_quarter_pulses(self):
        row = self.note(0, 76, 1)
        row.update(meter_numerator=6, meter_denominator=8, beats_per_bar=2,
                   steps_per_bar=12, steps_per_beat=6, bar_end_sample=51200,
                   export_frames=51200, duration_samples=4266)
        self.assertEqual(self.audit([row])["rule_violations"], [])
        row["steps_per_beat"] = 4
        with self.assertRaisesRegex(ValueError, "subdivision"):
            self.audit([row])

    def test_rejects_stale_or_mixed_schema(self):
        for rows in ([dict(self.note(4, 76), schema=2)],
                     [self.note(4, 76), dict(self.note(8, 79), schema=2)]):
            with self.assertRaisesRegex(ValueError, "schema"):
                self.audit(rows)


if __name__ == "__main__":
    unittest.main()
