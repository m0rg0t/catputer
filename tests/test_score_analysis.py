import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from analyze_score import inspect


class ScoreAuditTests(unittest.TestCase):
    def note(self, step, midi, duration=1):
        return dict(schema=3, session=0, seed=42, bar=0, bpm=75, key_pc=0,
                    minor=0, chord_root=0, chord_0=60, chord_1=64,
                    chord_2=67, chord_3=71, instrument="lead", midi=midi,
                    velocity=50, start_sample=step * 6400,
                    transport_start_sample=step * 6400,
                    duration_samples=duration * 6400, export_frames=102400)

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
            row.update(bar=bar, export_frames=409600)
            rows.append(row)
        self.assertEqual(self.audit(rows)["distinct_four_bar_lead_phrases"], 1)
        for row in rows:
            row["export_frames"] = 320000
        self.assertEqual(self.audit(rows)["distinct_four_bar_lead_phrases"], 0)

    def test_rejects_duplicates_offsets_and_missing_bars(self):
        first, second = self.note(0, 76), self.note(8, 79)
        bad_offset = dict(second, transport_start_sample=51201)
        skipped_bar = dict(second, bar=2)
        for rows, message in (([first, first], "Duplicated"),
                              ([first, bad_offset], "offset"),
                              ([first, skipped_bar], "score bar")):
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    self.audit(rows)

    def test_rejects_stale_or_mixed_schema(self):
        for rows in ([dict(self.note(4, 76), schema=2)],
                     [self.note(4, 76), dict(self.note(8, 79), schema=2)]):
            with self.assertRaisesRegex(ValueError, "schema"):
                self.audit(rows)


if __name__ == "__main__":
    unittest.main()
