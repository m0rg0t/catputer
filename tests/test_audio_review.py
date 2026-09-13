import base64
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request
import wave

from tools.review_audio import extract_clip, main, payload, validate_review, MODELS, NoRedirect


def review_fixture():
    return {"audio_access": True, "summary": "A short excerpt", "issues": [],
            "strengths": [], "uncertainties": [],
            "pulse": {"bpm_guess": None, "meter_guess": None, "confidence": "low"}}


class AudioReviewTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.wav = Path(self.folder.name) / "private-source-name.wav"
        self.pcm = bytes(range(256)) * 500
        with wave.open(str(self.wav), "wb") as output:
            output.setparams((1, 2, 32000, 0, "NONE", "not compressed"))
            output.writeframes(self.pcm)

    def test_excerpt_preserves_exact_pcm_and_format(self):
        clip = extract_clip(self.wav, .5, 1)
        with wave.open(io.BytesIO(clip)) as stream:
            self.assertEqual((stream.getnchannels(), stream.getsampwidth(), stream.getframerate()), (1, 2, 32000))
            self.assertEqual(stream.readframes(32000), self.pcm[32000:96000])

    def test_invalid_excerpt_rejected(self):
        for start, duration in ((-1, 1), (0, 91), (2, 1), (float("nan"), 1), (0, float("inf"))):
            with self.subTest(start=start, duration=duration), self.assertRaises(ValueError):
                extract_clip(self.wav, start, duration)

    def test_payload_has_real_audio_without_local_filename(self):
        clip = extract_clip(self.wav, 0, 1)
        request = payload(MODELS[0], clip)
        content = request["messages"][0]["content"]
        self.assertEqual(base64.b64decode(content[1]["input_audio"]["data"]), clip)
        self.assertNotIn(self.wav.name, json.dumps(request))
        self.assertNotIn("Authorization", request)

    def test_findings_must_fit_excerpt(self):
        issue = {"start_seconds": 0, "end_seconds": 1, "confidence": "low", "kind": "preference",
                 "category": "timbre", "heard": "Bright attack", "suggestion": "Audition a softer attack"}
        review = review_fixture()
        review["issues"] = [issue]
        self.assertEqual(validate_review(review, 1), review)
        for end in (2, -1, float("nan"), True):
            issue["end_seconds"] = end
            with self.assertRaises(ValueError):
                validate_review(review, 1)

    def test_no_claimed_findings_without_audio_access(self):
        for field, value in (("issues", [{}]), ("strengths", ["Strong harmony"]),
                             ("pulse", {"bpm_guess": 92, "meter_guess": "4/4", "confidence": "high"})):
            review = review_fixture()
            review["audio_access"] = False
            review[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_review(review, 1)

    def test_redirects_never_forward_authorization(self):
        request = urllib.request.Request("https://openrouter.ai/api/v1/chat/completions", b"{}",
                                         {"Authorization": "Bearer test-token"})
        for code in (301, 302, 303, 307, 308):
            with self.subTest(code=code), self.assertRaises(urllib.error.HTTPError) as failure:
                NoRedirect().redirect_request(request, None, code, "Redirect", {}, "https://another.example/")
            failure.exception.close()

    def test_malformed_api_results_leave_failed_report(self):
        responses = [[], {"choices": []}, {"choices": [None]},
                     {"choices": [{"message": []}]},
                     {"choices": [{"message": {"content": []}}]},
                     {"choices": [{"message": {"content": "{}"}}], "usage": []}]
        for index, response in enumerate(responses):
            output = Path(self.folder.name) / f"failed-{index}.json"
            with self.subTest(index=index), patch("sys.argv", ["review_audio.py", str(self.wav), "--seconds", "1", "--out", str(output), "--send"]), \
                    patch.dict("os.environ", {"OPENROUTER_API_KEY": "test-token"}), \
                    patch("tools.review_audio.submit", return_value=response), redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit) as failure:
                main()
            self.assertEqual(failure.exception.code, 1)
            self.assertEqual(json.loads(output.read_text())["status"], "failed")
            self.assertNotIn("test-token", output.read_text())

    def test_default_is_local_only_and_contains_no_audio_payload(self):
        output = Path(self.folder.name) / "prepared.json"
        with patch("sys.argv", ["review_audio.py", str(self.wav), "--seconds", "1", "--out", str(output)]), \
                patch("tools.review_audio.submit", side_effect=AssertionError("Unexpected API call")), redirect_stdout(io.StringIO()):
            main()
        report = json.loads(output.read_text())
        self.assertEqual(report["status"], "prepared")
        self.assertNotIn("input_audio", report)
        self.assertNotIn("Authorization", output.read_text())


if __name__ == "__main__":
    unittest.main()
