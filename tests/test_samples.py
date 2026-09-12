from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import wave

from tools import generate_samples
from tools import prepare_samples


def write_wav(path: Path, frames: int, *, channels: int = 1, width: int = 2, rate: int = 16_000) -> None:
    sample = b"\x00" * width * channels
    with wave.open(str(path), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(width)
        output.setframerate(rate)
        output.writeframes(sample * frames)


class SampleBankTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.selected = self.root / "selected"
        self.manifest_path = self.root / "manifest.json"
        self.manifest = generate_samples.generate_local(
            generate_samples.DEFAULT_BRIEF,
            self.selected,
            self.manifest_path,
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def save_manifest(self) -> None:
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def refresh_entry(self, index: int) -> None:
        entry = self.manifest["samples"][index]
        path = Path(entry["path"])
        pcm, frames = prepare_samples.read_pcm16_wav(path)
        entry.update({
            "sha256": prepare_samples.sha256_file(path),
            "pcm_sha256": prepare_samples.sha256_bytes(pcm),
            "frames": frames,
            "pcm_bytes": len(pcm),
        })

    def refresh_totals(self) -> None:
        combined = hashlib.sha256()
        total = 0
        for entry in self.manifest["samples"]:
            pcm, _ = prepare_samples.read_pcm16_wav(Path(entry["path"]))
            combined.update(pcm)
            total += len(pcm)
        self.manifest["total_pcm_bytes"] = total
        self.manifest["combined_pcm_sha256"] = combined.hexdigest()

    def assert_invalid(self, text: str) -> None:
        self.save_manifest()
        with self.assertRaisesRegex(prepare_samples.ValidationError, text):
            prepare_samples.validate_bank(self.manifest_path, generate_samples.ROOT)

    def test_deterministic_bank_has_exact_layout_and_size(self) -> None:
        manifest, loaded = prepare_samples.validate_bank(self.manifest_path, generate_samples.ROOT)
        self.assertEqual(tuple(manifest["sample_order"]), prepare_samples.ORDER)
        self.assertEqual([entry["root_midi"] for entry in loaded[:3]], [48, 60, 72])
        self.assertEqual(manifest["total_pcm_bytes"], 58_240)
        self.assertEqual(sum(entry["frames"] for entry in loaded), 29_120)

        second_manifest = self.root / "second.json"
        regenerated = generate_samples.generate_local(
            generate_samples.DEFAULT_BRIEF,
            self.root / "second",
            second_manifest,
        )
        self.assertEqual(regenerated["combined_pcm_sha256"], manifest["combined_pcm_sha256"])

    def test_rejects_stereo(self) -> None:
        path = Path(self.manifest["samples"][0]["path"])
        write_wav(path, 5600, channels=2)
        self.manifest["samples"][0]["sha256"] = prepare_samples.sha256_file(path)
        self.assert_invalid("expected mono")

    def test_rejects_wrong_bit_depth(self) -> None:
        path = Path(self.manifest["samples"][0]["path"])
        write_wav(path, 5600, width=1)
        self.manifest["samples"][0]["sha256"] = prepare_samples.sha256_file(path)
        self.assert_invalid("expected 16-bit")

    def test_rejects_wrong_sample_rate(self) -> None:
        path = Path(self.manifest["samples"][0]["path"])
        write_wav(path, 5600, rate=22_050)
        self.manifest["samples"][0]["sha256"] = prepare_samples.sha256_file(path)
        self.assert_invalid("expected 16000 Hz")

    def test_rejects_bad_loop_bounds(self) -> None:
        self.manifest["samples"][0]["loop"] = {"enabled": True, "start_frame": 5000, "end_frame": 6000}
        self.assert_invalid("loop bounds outside")

    def test_rejects_nonzero_disabled_loop(self) -> None:
        self.manifest["samples"][0]["loop"] = {"enabled": False, "start_frame": 1, "end_frame": 0}
        self.assert_invalid("disabled loop must use zero bounds")

    def test_rejects_wrong_count_and_order(self) -> None:
        wrong_count = copy.deepcopy(self.manifest)
        wrong_count["samples"].pop()
        self.manifest = wrong_count
        self.assert_invalid("exactly seven")

        self.manifest = json.loads((self.root / "second.json").read_text()) if (self.root / "second.json").exists() else generate_samples.generate_local(
            generate_samples.DEFAULT_BRIEF, self.root / "restored", self.root / "second.json"
        )
        self.manifest_path = self.root / "second.json"
        self.manifest["sample_order"][0], self.manifest["sample_order"][1] = self.manifest["sample_order"][1], self.manifest["sample_order"][0]
        self.assert_invalid("sample order")

    def test_rejects_hash_mismatch(self) -> None:
        self.manifest["samples"][0]["sha256"] = "0" * 64
        self.assert_invalid("WAV hash mismatch")

    def test_rejects_oversize_bank(self) -> None:
        path = Path(self.manifest["samples"][0]["path"])
        write_wav(path, 9600)
        self.refresh_entry(0)
        self.refresh_totals()
        self.assert_invalid("exceeding 65536")

    def test_generated_include_contains_all_arrays(self) -> None:
        manifest, loaded = prepare_samples.validate_bank(self.manifest_path, generate_samples.ROOT)
        include = prepare_samples.render_include(manifest, loaded)
        for sound_id in prepare_samples.ORDER:
            self.assertIn(prepare_samples._cpp_name(sound_id), include)
        self.assertNotIn("RIFF", include)

    def test_elevenlabs_plan_is_exactly_bounded(self) -> None:
        brief = generate_samples.load_brief(generate_samples.DEFAULT_BRIEF)
        plan = generate_samples._request_plan(brief, "pcm_16000")
        self.assertEqual(len(plan), 8)
        self.assertEqual({entry["variant_id"] for entry in plan}, {"v1", "v2"})
        self.assertEqual({entry["payload"]["duration_seconds"] for entry in plan}, {1.0})
        self.assertEqual({entry["payload"]["prompt_influence"] for entry in plan}, {0.7})
        self.assertEqual({entry["payload"]["loop"] for entry in plan}, {False})
        self.assertEqual({entry["payload"]["model_id"] for entry in plan}, {generate_samples.ELEVEN_MODEL})

    def test_pcm_response_validation_rejects_encoded_or_partial_data(self) -> None:
        with self.assertRaisesRegex(ValueError, "signature"):
            generate_samples._validate_pcm_response(b"RIFF" + b"\0" * 40, "audio/wav", "pcm_16000", 1.0)
        with self.assertRaisesRegex(ValueError, "complete PCM16"):
            generate_samples._validate_pcm_response(b"\0", "audio/pcm", "pcm_16000", 1.0)
        mono = generate_samples._validate_pcm_response(b"\0" * 32_000, "audio/pcm", "pcm_16000", 1.0)
        stereo = generate_samples._validate_pcm_response(b"\0" * 64_000, "audio/pcm", "pcm_16000", 1.0)
        self.assertEqual((mono["frames"], mono["channels_inferred"]), (16_000, 1))
        self.assertEqual((stereo["frames"], stereo["channels_inferred"]), (16_000, 2))
        self.assertIn("no container header", stereo["channel_inference"])


if __name__ == "__main__":
    unittest.main()
