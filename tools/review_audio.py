#!/usr/bin/env python3
"""Audition a bounded PCM clip with an audio-input model; local preparation by default."""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import io
import json
import math
import os
from pathlib import Path
import urllib.error
import urllib.request
import wave

ENDPOINT = "https://openrouter.ai/api/v1/chat/completions"
MODELS = ("google/gemini-3.8-flash", "google/gemini-2.5-pro")
MAX_TOKENS = 4096
PROMPT_VERSION = 1
CONFIDENCE = ("low", "medium", "high")
CATEGORIES = ("melody", "harmony", "rhythm", "balance", "variety", "repetition", "timbre", "artifact")
PROMPT = """Listen to the attached instrumental music excerpt as an independent music
producer. The intended experience is calm, melodic, endlessly generated lofi radio.
Judge only what is audible. No score, tempo, meter, instrument labels or previous
reviews are provided. Do not assume there are defects. Do not invent vocals,
instruments, exact notes/chords, measured dB/frequency values or exact timestamps.
Separate musical preference from an audible technical defect. Swing, syncopation,
repetition and unusual meter are not automatically errors. A clip boundary is an
excerpt cut, not proof of a broken musical phrase. If you cannot hear/access the
audio, say so and give no musical findings. Give at most four prioritized issues,
each with an approximate interval in SECONDS RELATIVE TO THIS EXCERPT, confidence,
what you actually hear and one small practical change to audition. Consider
melodic direction, harmonic fit, rhythm, balance, repetition and harsh timbre.
This runs on a 12-voice mono synthesizer: suggestions should fit that constraint.
Do not prescribe a large sample library or a pretrained generator on the device.
Return one JSON object, no markdown:
{"audio_access": true, "summary": "...", "strengths": ["..."],
 "pulse": {"bpm_guess": null, "meter_guess": null, "confidence": "low"},
 "issues": [{"category": "melody|harmony|rhythm|balance|variety|timbre|artifact",
 "start_seconds": 0, "end_seconds": 10, "confidence": "low|medium|high",
 "heard": "...", "suggestion": "...", "kind": "preference|suspected_defect"}],
 "uncertainties": ["..."]}
Use null for uncertain BPM/meter. Write prose in English. Do not treat your
guesses as measurements. Empty issues are acceptable.
"""


def sha(data):
    return hashlib.sha256(data).hexdigest()


def extract_clip(path, start, seconds):
    if not math.isfinite(start) or start < 0 or not math.isfinite(seconds) or not 1 <= seconds <= 90:
        raise ValueError("Clip start must be nonnegative; duration must be 1..90 seconds")
    with wave.open(path if hasattr(path, "read") else str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getcomptype()) != (1, 2, 32000, "NONE"):
            raise ValueError("Expected the native renderer's 32 kHz mono PCM16 WAV")
        first, count = round(start * 32000), round(seconds * 32000)
        if first + count > source.getnframes():
            raise ValueError("Requested excerpt extends beyond the WAV")
        source.setpos(first)
        pcm = source.readframes(count)
        if len(pcm) != count * 2:
            raise ValueError("Truncated PCM data")
    output = io.BytesIO()
    with wave.open(output, "wb") as target:
        target.setparams((1, 2, 32000, 0, "NONE", "not compressed"))
        target.writeframes(pcm)
    return output.getvalue()


def payload(model, clip):
    if model not in MODELS:
        raise ValueError("Choose one of the documented audio-input models")
    return {
        "model": model,
        "messages": [{"role": "user", "content": [
            {"type": "text", "text": PROMPT},
            {"type": "input_audio", "input_audio": {
                "data": base64.b64encode(clip).decode("ascii"), "format": "wav"}},
        ]}],
        "max_tokens": MAX_TOKENS,
        "reasoning": {"effort": "low", "exclude": True},
        "response_format": {"type": "json_object"},
    }


def validate_review(review, seconds):
    if not isinstance(review, dict) or type(review.get("audio_access")) is not bool:
        raise ValueError("Response lacks an explicit audio-access result")
    if not isinstance(review.get("summary"), str) or not review["summary"].strip() or not isinstance(review.get("issues"), list):
        raise ValueError("Response lacks a summary or issue list")
    for key in ("strengths", "uncertainties"):
        if not isinstance(review.get(key), list) or any(not isinstance(item, str) or not item.strip() for item in review[key]):
            raise ValueError("Response has malformed strengths/uncertainties")
    pulse = review.get("pulse")
    if not isinstance(pulse, dict) or pulse.get("confidence") not in CONFIDENCE:
        raise ValueError("Response has malformed pulse confidence")
    if "bpm_guess" not in pulse or "meter_guess" not in pulse:
        raise ValueError("Response lacks nullable pulse guesses")
    bpm, meter = pulse["bpm_guess"], pulse["meter_guess"]
    if bpm is not None and (type(bpm) not in (int, float) or not math.isfinite(bpm) or bpm <= 0):
        raise ValueError("Response has an invalid tempo guess")
    if meter is not None and (not isinstance(meter, str) or not meter.strip()):
        raise ValueError("Response has an invalid meter guess")
    if not review["audio_access"] and (review["strengths"] or bpm is not None or meter is not None):
        raise ValueError("Response claims musical findings without audio access")
    if len(review["issues"]) > 4 or (not review["audio_access"] and review["issues"]):
        raise ValueError("Response has unsupported findings")
    for issue in review["issues"]:
        if not isinstance(issue, dict):
            raise ValueError("Malformed finding")
        first, last = issue.get("start_seconds"), issue.get("end_seconds")
        if any(type(value) not in (int, float) or not math.isfinite(value) for value in (first, last)):
            raise ValueError("Finding has nonnumeric timestamps")
        if not 0 <= first <= last <= seconds:
            raise ValueError("Finding is outside the submitted excerpt")
        if issue.get("confidence") not in CONFIDENCE or issue.get("kind") not in ("preference", "suspected_defect"):
            raise ValueError("Finding does not distinguish uncertainty and preference")
        if issue.get("category") not in CATEGORIES:
            raise ValueError("Unknown finding category")
        if any(not isinstance(issue.get(key), str) or not issue[key].strip() for key in ("heard", "suggestion", "category")):
            raise ValueError("Finding lacks audible evidence or an actionable suggestion")
    return review


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file, code, message, headers, new_url):
        raise urllib.error.HTTPError(request.full_url, code, "Redirect rejected", headers, file)


def submit(request_payload, key):
    request = urllib.request.Request(ENDPOINT, json.dumps(request_payload).encode(), {
        "Authorization": "Bearer " + key, "Content-Type": "application/json",
    })
    try:
        with urllib.request.build_opener(NoRedirect()).open(request, timeout=120) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        # Upstream bodies can echo inputs; never print them or credentials.
        error.close()
        raise RuntimeError(f"OpenRouter returned HTTP {error.code}; no automatic retry") from None
    except (urllib.error.URLError, TimeoutError):
        raise RuntimeError("OpenRouter connection failed; billing may be uncertain; no automatic retry") from None


def response_fields(response):
    if not isinstance(response, dict):
        raise ValueError("API response must be an object")
    choices = response.get("choices")
    if not isinstance(choices, list) or not choices or not isinstance(choices[0], dict):
        raise ValueError("API response lacks a valid choice")
    choice = choices[0]
    message, usage = choice.get("message"), response.get("usage")
    if not isinstance(message, dict) or not isinstance(message.get("content"), str):
        raise ValueError("API response lacks text content")
    if usage is not None and not isinstance(usage, dict):
        raise ValueError("API usage must be an object when supplied")
    return {"returned_model": response.get("model"), "provider": response.get("provider"),
            "usage": usage, "response_id": response.get("id"),
            "finish_reason": choice.get("finish_reason"), "response_text": message["content"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--start", type=float, default=0)
    parser.add_argument("--seconds", type=float, default=45)
    parser.add_argument("--model", choices=MODELS, default=MODELS[0])
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--send", action="store_true", help="Send one paid audio request using OPENROUTER_API_KEY")
    args = parser.parse_args()
    try:
        if args.out.exists():
            raise ValueError("Output already exists; choose a new report path")
        key = os.environ.get("OPENROUTER_API_KEY", "")
        if args.send and not key:
            raise ValueError("OPENROUTER_API_KEY is not set; do not put the key in command arguments")
        with args.wav.open("rb") as source_file:
            before = os.fstat(source_file.fileno())
            source_digest = hashlib.file_digest(source_file, "sha256").hexdigest()
            source_file.seek(0)
            clip = extract_clip(source_file, args.start, args.seconds)
            after = os.fstat(source_file.fileno())
            if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError("Source changed while preparing the excerpt")
        report = {
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "status": "prepared", "advisory_only": True, "hardware_audio": False,
            "endpoint": ENDPOINT, "requested_model": args.model,
            "source_file": args.wav.name,
            "source_sha256": source_digest,
            "excerpt_start_seconds": args.start, "excerpt_seconds": args.seconds,
            "excerpt_sha256": sha(clip), "excerpt_bytes": len(clip),
            "audio_preprocessing": "Exact PCM excerpt; no gain, normalization, resampling or effects",
            "prompt": PROMPT, "max_tokens": MAX_TOKENS,
            "prompt_version": PROMPT_VERSION,
            "request_options": {"reasoning": {"effort": "low", "exclude": True},
                                "response_format": {"type": "json_object"}},
            "timestamp_basis": "excerpt_relative", "timestamp_precision": "model_estimate",
        }
        args.out.parent.mkdir(parents=True, exist_ok=True)
        # Reserve and write a local request record BEFORE incurring API usage.
        with args.out.open("x") as output:
            output.write(json.dumps(report, indent=2) + "\n")
        if args.send:
            report["status"] = "request_started"
            args.out.write_text(json.dumps(report, indent=2) + "\n")
            try:
                response = submit(payload(args.model, clip), key)
                report.update(response_fields(response))
                if report["finish_reason"] != "stop":
                    raise ValueError("Model response did not finish normally; report retained")
                report["review"] = validate_review(json.loads(report["response_text"]), args.seconds)
                report["model_claimed_audio_access"] = report["review"]["audio_access"]
                report["status"] = "complete" if report["review"]["audio_access"] else "audio_unavailable"
            except (RuntimeError, ValueError, KeyError, IndexError, TypeError) as error:
                report["status"] = "failed"
                report["error"] = str(error)
                raise
            finally:
                args.out.write_text(json.dumps(report, indent=2) + "\n")
        print(f"{report['status']}: {args.out}; {args.seconds:g}s; {args.model}")
        if isinstance(report.get("usage"), dict):
            print("Reported cost USD:", report["usage"].get("cost", "unavailable"))
    except (OSError, ValueError, RuntimeError, wave.Error) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
