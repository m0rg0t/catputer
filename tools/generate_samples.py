#!/usr/bin/env python3
"""Generate deterministic local samples and bounded ElevenLabs auditions."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import random
import socket
import struct
import sys
import urllib.error
import urllib.parse
import urllib.request
import wave


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BRIEF = ROOT / "assets/audio/briefs/starter-bank.json"
DEFAULT_SELECTED = ROOT / "assets/audio/selected/local-v1"
DEFAULT_MANIFEST = ROOT / "assets/audio/manifests/local-v1.json"
DEFAULT_RAW = ROOT / "assets/audio/raw/elevenlabs"
DEFAULT_AUDITIONS = ROOT / "build/audio/auditions/elevenlabs"
DEFAULT_AUDITION_MANIFEST = ROOT / "assets/audio/manifests/elevenlabs-auditions.json"
SAMPLE_RATE = 16_000
ORDER = ("keys_low", "keys_mid", "keys_high", "kick_soft", "snare_brush", "hat_closed", "rim_wood")
ELEVEN_ENDPOINT = "https://api.elevenlabs.io/v1/sound-generation"
ELEVEN_MODEL = "eleven_text_to_sound_v2"


def canonical_json(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(128 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def display_path(path: Path) -> str:
    return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)


def load_brief(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("unsupported starter-bank schema")
    return data


def _finish(samples: list[float], peak: float) -> list[int]:
    # Remove the tiny finite-window DC residue, apply a click-safe final fade, and
    # use a fixed relative peak so regenerating the bank is byte-identical.
    mean = sum(samples) / len(samples)
    samples = [value - mean for value in samples]
    fade = min(192, len(samples))
    for index in range(fade):
        samples[-fade + index] *= 0.5 + 0.5 * math.cos(math.pi * (index + 1) / fade)
    scale = peak * 32767.0 / max(max(abs(value) for value in samples), 1.0e-12)
    return [max(-32768, min(32767, int(round(value * scale)))) for value in samples]


def electric_piano(frames: int, frequency: float) -> list[int]:
    result: list[float] = []
    for index in range(frames):
        t = index / SAMPLE_RATE
        attack = min(1.0, t / 0.008)
        body = math.exp(-2.35 * t)
        bell = math.exp(-10.5 * t)
        tremolo = 1.0 + 0.025 * math.sin(2.0 * math.pi * 4.7 * t)
        phase = 2.0 * math.pi * frequency * t
        value = attack * (
            0.73 * math.sin(phase) * body * tremolo
            + 0.20 * math.sin(2.0 * phase + 0.09) * math.exp(-4.3 * t)
            + 0.085 * math.sin(3.997 * phase + 0.31) * bell
            + 0.035 * math.sin(7.01 * phase + 0.7) * bell
        )
        result.append(value)
    return _finish(result, 0.66)


def kick(frames: int) -> list[int]:
    result: list[float] = []
    phase = 0.0
    rng = random.Random(0x4B49434B)
    for index in range(frames):
        t = index / SAMPLE_RATE
        freq = 48.0 + 78.0 * math.exp(-31.0 * t)
        phase += 2.0 * math.pi * freq / SAMPLE_RATE
        body = math.sin(phase) * math.exp(-14.0 * t)
        felt = (rng.random() * 2.0 - 1.0) * math.exp(-95.0 * t)
        result.append((0.96 * body + 0.055 * felt) * min(1.0, t / 0.0025))
    return _finish(result, 0.72)


def snare(frames: int) -> list[int]:
    result: list[float] = []
    rng = random.Random(0x534E4152)
    filtered = 0.0
    for index in range(frames):
        t = index / SAMPLE_RATE
        noise = rng.random() * 2.0 - 1.0
        filtered = 0.58 * filtered + 0.42 * noise
        wire = noise - filtered
        body = math.sin(2.0 * math.pi * 176.0 * t) * math.exp(-24.0 * t)
        brush = (0.63 * filtered + 0.25 * wire) * math.exp(-19.0 * t)
        result.append((brush + 0.30 * body) * min(1.0, t / 0.003))
    return _finish(result, 0.53)


def hat(frames: int) -> list[int]:
    result: list[float] = []
    rng = random.Random(0x484154)
    low = 0.0
    for index in range(frames):
        t = index / SAMPLE_RATE
        noise = rng.random() * 2.0 - 1.0
        low += 0.18 * (noise - low)
        high = noise - low
        metallic = math.sin(2.0 * math.pi * 4311.0 * t) + 0.45 * math.sin(2.0 * math.pi * 6173.0 * t)
        result.append((0.55 * high + 0.13 * metallic) * math.exp(-52.0 * t) * min(1.0, t / 0.0015))
    return _finish(result, 0.34)


def rim(frames: int) -> list[int]:
    result: list[float] = []
    rng = random.Random(0x52494D)
    for index in range(frames):
        t = index / SAMPLE_RATE
        tone = 0.72 * math.sin(2.0 * math.pi * 925.0 * t) + 0.28 * math.sin(2.0 * math.pi * 1470.0 * t + 0.2)
        click = (rng.random() * 2.0 - 1.0) * math.exp(-120.0 * t)
        result.append((tone * math.exp(-42.0 * t) + 0.16 * click) * min(1.0, t / 0.001))
    return _finish(result, 0.43)


def write_wav(path: Path, pcm: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))


def generate_local(brief_path: Path, selected_dir: Path, manifest_path: Path) -> dict:
    brief = load_brief(brief_path)
    sounds = {sound["id"]: sound for sound in brief["sounds"]}
    synths = {
        "keys_low": lambda frames: electric_piano(frames, float(sounds["keys_low"]["root_hz"])),
        "keys_mid": lambda frames: electric_piano(frames, float(sounds["keys_mid"]["root_hz"])),
        "keys_high": lambda frames: electric_piano(frames, float(sounds["keys_high"]["root_hz"])),
        "kick_soft": kick,
        "snare_brush": snare,
        "hat_closed": hat,
        "rim_wood": rim,
    }
    entries = []
    pcm_hash = hashlib.sha256()
    total_bytes = 0
    for sound_id in ORDER:
        sound = sounds[sound_id]
        frames = int(sound["target_duration_ms"]) * SAMPLE_RATE // 1000
        pcm = synths[sound_id](frames)
        path = selected_dir / f"{sound_id}.wav"
        write_wav(path, pcm)
        raw_pcm = struct.pack(f"<{len(pcm)}h", *pcm)
        pcm_hash.update(raw_pcm)
        total_bytes += len(raw_pcm)
        root_midi = int(sound.get("root_midi", 60))
        entries.append({
            "id": sound_id,
            "path": display_path(path),
            "sha256": sha256_file(path),
            "pcm_sha256": sha256_bytes(raw_pcm),
            "frames": frames,
            "pcm_bytes": len(raw_pcm),
            "sample_rate_hz": SAMPLE_RATE,
            "channels": 1,
            "bits_per_sample": 16,
            "root_midi": root_midi,
            "loop": {"enabled": False, "start_frame": 0, "end_frame": 0},
            "source": {
                "kind": "deterministic_local_synthesis",
                "generator": "tools/generate_samples.py",
                "preset": "warm_electric_piano_v1" if sound_id.startswith("keys_") else f"soft_lofi_{sound_id}_v1",
            },
        })
    expected = int(brief["packed_format"]["target_pcm_bytes"])
    if total_bytes != expected:
        raise ValueError(f"generated {total_bytes} PCM bytes, expected {expected}")
    manifest = {
        "schema_version": 1,
        "status": "accepted_for_local_v1_candidate_bank",
        "bank_id": f"local-v1-{pcm_hash.hexdigest()[:12]}",
        "purpose": "Deterministic built-in sample-assisted audition candidate; engine selection remains pending.",
        "format": {"encoding": "signed_little_endian_pcm", "sample_rate_hz": SAMPLE_RATE, "channels": 1, "bits_per_sample": 16},
        "sample_order": list(ORDER),
        "sample_count": len(entries),
        "total_pcm_bytes": total_bytes,
        "resident_pcm_max_bytes": int(brief["packed_format"]["resident_pcm_max_bytes"]),
        "combined_pcm_sha256": pcm_hash.hexdigest(),
        "samples": entries,
        "provenance": {
            "created_by": "deterministic local mathematical synthesis",
            "external_assets": False,
            "note": "Keys use complete synthesized decays with click-safe edge fades; looping is intentionally disabled.",
        },
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return manifest


def _response_extension(output_format: str) -> str:
    if output_format.startswith("pcm_"):
        return ".pcm"
    if output_format.startswith("mp3_"):
        return ".mp3"
    return ".bin"


def _safe_headers(headers) -> dict:
    allowed = ("request-id", "x-request-id", "history-item-id", "x-character-count", "x-character-cost")
    return {key: headers.get(key) for key in allowed if headers.get(key)}


def _write_pcm_audition(source: Path, destination: Path, sample_rate: int, channels: int) -> None:
    payload = source.read_bytes()
    if not payload or len(payload) % 2:
        raise ValueError("PCM response is empty or has a partial 16-bit frame")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(destination), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        output.writeframes(payload)


def _validate_pcm_response(body: bytes, content_type: str, output_format: str, duration_seconds: float) -> dict:
    if not output_format.startswith("pcm_"):
        raise ValueError(f"this adapter currently validates only PCM output, got {output_format}")
    if body.startswith((b"RIFF", b"ID3")) or body[:2] in (b"\xff\xfb", b"\xff\xf3", b"\xff\xf2"):
        raise ValueError("response signature does not match requested raw PCM")
    if not body or len(body) % 2:
        raise ValueError("response is not complete PCM16 data")
    lowered = content_type.lower()
    if lowered and not any(token in lowered for token in ("audio/pcm", "audio/l16", "application/octet-stream")):
        raise ValueError(f"unexpected Content-Type for PCM response: {content_type}")
    sample_rate = int(output_format.split("_", 1)[1])
    expected_frames = int(round(sample_rate * duration_seconds))
    if expected_frames <= 0 or len(body) % (expected_frames * 2):
        raise ValueError("PCM byte count does not match the requested duration and sample rate")
    channels_inferred = len(body) // (expected_frames * 2)
    if channels_inferred not in (1, 2):
        raise ValueError(f"unsupported inferred PCM channel count: {channels_inferred}")
    return {
        "frames": expected_frames,
        "channels_inferred": channels_inferred,
        "channel_inference": "raw byte count divided by requested frames and PCM16 sample width; response has no container header",
        "sample_rate_hz": sample_rate,
        "bits_per_sample": 16,
    }


def _request_plan(brief: dict, output_format: str) -> list[dict]:
    requests = []
    for sound in brief["sounds"]:
        if "payload" not in sound:
            continue
        payload = dict(sound["payload"])
        if payload != {
            "text": payload.get("text"),
            "duration_seconds": 1.0,
            "prompt_influence": 0.7,
            "loop": False,
            "model_id": ELEVEN_MODEL,
        }:
            raise ValueError(f"{sound['id']} payload does not match the bounded audition contract")
        for variant in (1, 2):
            variant_id = f"v{variant}"
            envelope = {
                "provider": "elevenlabs",
                "model_id": ELEVEN_MODEL,
                "payload": payload,
                "output_format": output_format,
                "variant_id": variant_id,
            }
            requests.append({
                "sound_id": sound["id"],
                "variant_id": variant_id,
                "payload": payload,
                "request_hash": sha256_bytes(canonical_json(envelope)),
                "output_format": output_format,
            })
    if len(requests) != 8:
        raise ValueError(f"expected exactly eight percussion auditions, found {len(requests)}")
    return requests


def _write_metadata(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def write_audition_manifest(plan: list[dict], raw_dir: Path, destination: Path) -> None:
    candidates = []
    for item in plan:
        stem = f"{item['sound_id']}-{item['variant_id']}-{item['request_hash'][:12]}"
        metadata_path = raw_dir / f"{stem}.json"
        if not metadata_path.exists():
            continue
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        candidates.append({
            "sound_id": item["sound_id"],
            "variant_id": item["variant_id"],
            "status": metadata.get("status"),
            "request_hash": item["request_hash"],
            "model_id": metadata.get("model_id"),
            "payload": metadata.get("payload"),
            "output_format": metadata.get("output_format"),
            "content_type": metadata.get("content_type"),
            "response_frames": metadata.get("response_frames"),
            "response_channels_inferred": metadata.get("response_channels_inferred"),
            "response_channel_inference": metadata.get("response_channel_inference"),
            "response_bits_per_sample": metadata.get("response_bits_per_sample"),
            "asset_path": metadata.get("asset_path"),
            "asset_sha256": metadata.get("asset_sha256"),
            "audition_path": metadata.get("audition_path"),
            "audition_sha256": metadata.get("audition_sha256"),
            "completed_at": metadata.get("completed_at"),
            "redistribution_status": metadata.get("redistribution_status", "not_reviewed"),
        })
    manifest = {
        "schema_version": 1,
        "status": "generated_unreviewed_auditions",
        "selection_status": "listening_required",
        "provider": "elevenlabs",
        "endpoint": "/v1/sound-generation",
        "candidate_count": len(candidates),
        "requested_duration_seconds": sum(float(entry["payload"]["duration_seconds"]) for entry in candidates),
        "channel_layout_note": "Channel count is inferred from raw byte count, requested duration and PCM16 sample rate because the provider response has no container header.",
        "candidates": candidates,
        "note": "These generated files are audition sources only. They are not approved for the built-in bank or for redistribution.",
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def generate_elevenlabs(args: argparse.Namespace) -> int:
    if args.max_requests < 0 or args.max_requests > 8:
        raise ValueError("--max-requests must be between 0 and 8")
    brief = load_brief(args.brief)
    plan = _request_plan(brief, args.output_format)
    pending = []
    for item in plan:
        stem = f"{item['sound_id']}-{item['variant_id']}-{item['request_hash'][:12]}"
        meta_path = args.raw_dir / f"{stem}.json"
        raw_path = args.raw_dir / f"{stem}{_response_extension(args.output_format)}"
        if meta_path.exists():
            metadata = json.loads(meta_path.read_text(encoding="utf-8"))
            status = metadata.get("status")
            if status == "complete" and raw_path.exists() and metadata.get("asset_sha256") == sha256_file(raw_path):
                audio_format = _validate_pcm_response(
                    raw_path.read_bytes(),
                    metadata.get("content_type", ""),
                    item["output_format"],
                    float(item["payload"]["duration_seconds"]),
                )
                audition = args.auditions_dir / f"{item['sound_id']}-{item['variant_id']}.wav"
                _write_pcm_audition(raw_path, audition, audio_format["sample_rate_hz"], audio_format["channels_inferred"])
                metadata.pop("response_channels", None)
                metadata.update({
                    "response_frames": audio_format["frames"],
                    "response_channels_inferred": audio_format["channels_inferred"],
                    "response_channel_inference": audio_format["channel_inference"],
                    "response_bits_per_sample": audio_format["bits_per_sample"],
                    "audition_path": display_path(audition),
                    "audition_sha256": sha256_file(audition),
                })
                _write_metadata(meta_path, metadata)
                item["resume_status"] = "cached"
                continue
            if status == "uncertain" and not args.retry_uncertain:
                item["resume_status"] = "uncertain_skipped"
                continue
        item.update({"metadata_path": meta_path, "raw_path": raw_path, "resume_status": "pending"})
        pending.append(item)

    print(json.dumps({
        "mode": "dry-run" if args.dry_run else "generate",
        "endpoint": ELEVEN_ENDPOINT,
        "output_format": args.output_format,
        "total_variants": len(plan),
        "pending_requests": len(pending),
        "request_cap": args.max_requests,
        "pending_duration_seconds": sum(item["payload"]["duration_seconds"] for item in pending[:args.max_requests]),
        "requests": [{key: item[key] for key in ("sound_id", "variant_id", "request_hash", "resume_status")} for item in plan],
    }, indent=2))
    if args.dry_run:
        return 0
    api_key = os.environ.get("ELEVENLABS_API_KEY")
    if not api_key:
        raise RuntimeError("ELEVENLABS_API_KEY is not set; local samples are unaffected")

    completed = 0
    for item in pending[:args.max_requests]:
        meta_path = item["metadata_path"]
        raw_path = item["raw_path"]
        metadata = {
            "schema_version": 1,
            "status": "in_progress",
            "provider": "elevenlabs",
            "endpoint": "/v1/sound-generation",
            "model_id": ELEVEN_MODEL,
            "sound_id": item["sound_id"],
            "variant_id": item["variant_id"],
            "request_hash": item["request_hash"],
            "payload": item["payload"],
            "output_format": item["output_format"],
            "started_at": utc_now(),
            "redistribution_status": "not_reviewed",
        }
        _write_metadata(meta_path, metadata)
        query = urllib.parse.urlencode({"output_format": args.output_format})
        request = urllib.request.Request(
            f"{ELEVEN_ENDPOINT}?{query}",
            data=canonical_json(item["payload"]),
            headers={"Content-Type": "application/json", "Accept": "audio/pcm", "xi-api-key": api_key},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=args.timeout_seconds) as response:
                body = response.read()
                content_type = response.headers.get("Content-Type", "")
                audio_format = _validate_pcm_response(
                    body,
                    content_type,
                    args.output_format,
                    float(item["payload"]["duration_seconds"]),
                )
                raw_path.parent.mkdir(parents=True, exist_ok=True)
                raw_path.write_bytes(body)
                audition = args.auditions_dir / f"{item['sound_id']}-{item['variant_id']}.wav"
                _write_pcm_audition(raw_path, audition, audio_format["sample_rate_hz"], audio_format["channels_inferred"])
                metadata.update({
                    "status": "complete",
                    "completed_at": utc_now(),
                    "content_type": content_type,
                    "response_frames": audio_format["frames"],
                    "response_channels_inferred": audio_format["channels_inferred"],
                    "response_channel_inference": audio_format["channel_inference"],
                    "response_bits_per_sample": audio_format["bits_per_sample"],
                    "asset_path": display_path(raw_path),
                    "asset_sha256": sha256_file(raw_path),
                    "audition_path": display_path(audition),
                    "audition_sha256": sha256_file(audition),
                    "response_ids": _safe_headers(response.headers),
                })
                completed += 1
        except urllib.error.HTTPError as error:
            detail = error.read(4096).decode("utf-8", errors="replace")
            metadata.update({"status": "failed", "ended_at": utc_now(), "http_status": error.code, "error": detail[:1000]})
        except (socket.timeout, TimeoutError) as error:
            metadata.update({"status": "uncertain", "ended_at": utc_now(), "error": type(error).__name__, "retry_requires_explicit_flag": True})
        except urllib.error.URLError as error:
            if isinstance(error.reason, (socket.timeout, TimeoutError)):
                metadata.update({"status": "uncertain", "ended_at": utc_now(), "error": "network_timeout", "retry_requires_explicit_flag": True})
            else:
                metadata.update({"status": "failed", "ended_at": utc_now(), "error": str(error.reason)[:300]})
        except ValueError as error:
            metadata.update({"status": "rejected_format", "ended_at": utc_now(), "error": str(error)})
        finally:
            _write_metadata(meta_path, metadata)
        print(f"{item['sound_id']} {item['variant_id']}: {metadata['status']}")
    print(f"Completed {completed} new request(s); attempted {min(len(pending), args.max_requests)}.")
    write_audition_manifest(plan, args.raw_dir, args.audition_manifest)
    return 0 if completed == min(len(pending), args.max_requests) else 1


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    subparsers = root.add_subparsers(dest="command", required=True)
    local = subparsers.add_parser("local", help="render the deterministic seven-sample local candidate")
    local.add_argument("--brief", type=Path, default=DEFAULT_BRIEF)
    local.add_argument("--selected-dir", type=Path, default=DEFAULT_SELECTED)
    local.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    eleven = subparsers.add_parser("elevenlabs", help="create bounded percussion auditions")
    eleven.add_argument("--brief", type=Path, default=DEFAULT_BRIEF)
    eleven.add_argument("--raw-dir", type=Path, default=DEFAULT_RAW)
    eleven.add_argument("--auditions-dir", type=Path, default=DEFAULT_AUDITIONS)
    eleven.add_argument("--audition-manifest", type=Path, default=DEFAULT_AUDITION_MANIFEST)
    eleven.add_argument("--output-format", default="pcm_16000", choices=("pcm_16000",))
    eleven.add_argument("--max-requests", type=int, default=8)
    eleven.add_argument("--timeout-seconds", type=float, default=45.0)
    eleven.add_argument("--retry-uncertain", action="store_true", help="explicitly retry records left uncertain by a timeout")
    eleven.add_argument("--dry-run", action="store_true")
    return root


def main() -> int:
    args = parser().parse_args()
    try:
        if args.command == "local":
            manifest = generate_local(args.brief, args.selected_dir, args.manifest)
            print(f"Generated {manifest['sample_count']} samples, {manifest['total_pcm_bytes']} PCM bytes, bank {manifest['bank_id']}")
            return 0
        return generate_elevenlabs(args)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
