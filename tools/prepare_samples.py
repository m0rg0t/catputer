#!/usr/bin/env python3
"""Validate selected WAV masters and pack the built-in PCM sample bank."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import wave


ROOT = Path(__file__).resolve().parents[1]
ORDER = ("keys_low", "keys_mid", "keys_high", "kick_soft", "snare_brush", "hat_closed", "rim_wood")
EXPECTED_RATE = 16_000
EXPECTED_CHANNELS = 1
EXPECTED_BITS = 16
MAX_PCM_BYTES = 65_536
TARGET_PCM_BYTES = 58_240
DEFAULT_MANIFEST = ROOT / "assets/audio/manifests/local-v1.json"
DEFAULT_INCLUDE = ROOT / "firmware/src/audio/sample_bank_data.inc"
DEFAULT_REPORT = ROOT / "build/audio/builtin/local-v1-report.json"
DEFAULT_COMPARISON = ROOT / "build/audio/auditions/comparison-summary.json"


class ValidationError(ValueError):
    """The selected bank cannot safely be packed."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(128 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_pcm16_wav(path: Path) -> tuple[bytes, int]:
    try:
        with wave.open(str(path), "rb") as source:
            channels = source.getnchannels()
            sample_width = source.getsampwidth()
            sample_rate = source.getframerate()
            frames = source.getnframes()
            compression = source.getcomptype()
            payload = source.readframes(frames)
    except (wave.Error, EOFError) as error:
        raise ValidationError(f"{path}: malformed WAV: {error}") from error
    if channels != EXPECTED_CHANNELS:
        raise ValidationError(f"{path}: expected mono, got {channels} channels")
    if sample_width * 8 != EXPECTED_BITS:
        raise ValidationError(f"{path}: expected 16-bit PCM, got {sample_width * 8}-bit")
    if sample_rate != EXPECTED_RATE:
        raise ValidationError(f"{path}: expected {EXPECTED_RATE} Hz, got {sample_rate} Hz")
    if compression != "NONE":
        raise ValidationError(f"{path}: expected uncompressed PCM, got {compression}")
    if len(payload) != frames * 2:
        raise ValidationError(f"{path}: truncated PCM payload")
    return payload, frames


def _resolve_asset(path_value: str, project_root: Path) -> Path:
    path = Path(path_value)
    return path if path.is_absolute() else project_root / path


def validate_bank(manifest_path: Path, project_root: Path = ROOT) -> tuple[dict, list[dict]]:
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot read manifest {manifest_path}: {error}") from error
    if manifest.get("schema_version") != 1:
        raise ValidationError("unsupported manifest schema")
    if manifest.get("status") != "accepted_for_local_v1_candidate_bank":
        raise ValidationError("manifest is not an accepted local-v1 candidate")
    if not isinstance(manifest.get("bank_id"), str) or not re.fullmatch(r"[a-z0-9-]{1,63}", manifest["bank_id"]):
        raise ValidationError("bank ID is not safe for generated C++")
    if tuple(manifest.get("sample_order", ())) != ORDER:
        raise ValidationError(f"sample order must be exactly {', '.join(ORDER)}")
    samples = manifest.get("samples")
    if not isinstance(samples, list) or len(samples) != len(ORDER) or manifest.get("sample_count") != len(ORDER):
        raise ValidationError("bank must contain exactly seven sample descriptors")
    fmt = manifest.get("format", {})
    if (fmt.get("sample_rate_hz"), fmt.get("channels"), fmt.get("bits_per_sample")) != (
        EXPECTED_RATE,
        EXPECTED_CHANNELS,
        EXPECTED_BITS,
    ):
        raise ValidationError("manifest format must be mono PCM16 at 16000 Hz")

    loaded: list[dict] = []
    combined = hashlib.sha256()
    total_bytes = 0
    for expected_id, entry in zip(ORDER, samples):
        if entry.get("id") != expected_id:
            raise ValidationError(f"descriptor order mismatch: expected {expected_id}")
        asset_path = _resolve_asset(entry.get("path", ""), project_root)
        if not asset_path.is_file():
            raise ValidationError(f"missing WAV for {expected_id}: {asset_path}")
        if entry.get("sha256") != sha256_file(asset_path):
            raise ValidationError(f"WAV hash mismatch for {expected_id}")
        pcm, frames = read_pcm16_wav(asset_path)
        if entry.get("frames") != frames or entry.get("pcm_bytes") != len(pcm):
            raise ValidationError(f"frame/byte count mismatch for {expected_id}")
        if entry.get("pcm_sha256") != sha256_bytes(pcm):
            raise ValidationError(f"PCM hash mismatch for {expected_id}")
        root_midi = entry.get("root_midi")
        if not isinstance(root_midi, int) or not 0 <= root_midi <= 127:
            raise ValidationError(f"invalid root MIDI note for {expected_id}")
        loop = entry.get("loop", {})
        enabled = loop.get("enabled")
        start = loop.get("start_frame")
        end = loop.get("end_frame")
        if not isinstance(enabled, bool) or not isinstance(start, int) or not isinstance(end, int):
            raise ValidationError(f"invalid loop metadata for {expected_id}")
        if enabled:
            if not (0 <= start < end <= frames):
                raise ValidationError(f"loop bounds outside sample for {expected_id}")
        elif start != 0 or end != 0:
            raise ValidationError(f"disabled loop must use zero bounds for {expected_id}")
        combined.update(pcm)
        total_bytes += len(pcm)
        loaded.append({**entry, "pcm": pcm, "asset_path": asset_path})

    if total_bytes > MAX_PCM_BYTES:
        raise ValidationError(f"resident PCM is {total_bytes} bytes, exceeding {MAX_PCM_BYTES}")
    if total_bytes != TARGET_PCM_BYTES:
        raise ValidationError(f"local-v1 PCM must be exactly {TARGET_PCM_BYTES} bytes, got {total_bytes}")
    if manifest.get("total_pcm_bytes") != total_bytes:
        raise ValidationError("manifest total PCM byte count mismatch")
    if manifest.get("resident_pcm_max_bytes") != MAX_PCM_BYTES:
        raise ValidationError("manifest resident PCM ceiling mismatch")
    if manifest.get("combined_pcm_sha256") != combined.hexdigest():
        raise ValidationError("combined PCM hash mismatch")
    return manifest, loaded


def _cpp_name(sound_id: str) -> str:
    return "k" + "".join(word.capitalize() for word in sound_id.split("_")) + "Pcm"


def render_include(manifest: dict, loaded: list[dict]) -> str:
    lines = [
        "// Generated by tools/prepare_samples.py; do not edit by hand.",
        f"// Bank: {manifest['bank_id']} ({manifest['total_pcm_bytes']} bytes PCM16)",
        "",
    ]
    for entry in loaded:
        values = struct.unpack(f"<{entry['frames']}h", entry["pcm"])
        lines.append(f"alignas(4) static const std::int16_t {_cpp_name(entry['id'])}[{entry['frames']}] = {{")
        for offset in range(0, len(values), 12):
            lines.append("    " + ", ".join(str(value) for value in values[offset : offset + 12]) + ",")
        lines.append("};")
        lines.append("")
    lines.append("static const Sample kBuiltinSamples[] = {")
    for entry in loaded:
        loop = entry["loop"]
        lines.append(
            f"    {{{_cpp_name(entry['id'])}, {entry['frames']}, {entry['sample_rate_hz']}, "
            f"{entry['root_midi']}, {loop['start_frame']}, {loop['end_frame']}}},"
        )
    lines.extend([
        "};",
        "",
        f'static constexpr char kBuiltinSampleBankId[] = "{manifest["bank_id"]}";',
        "static_assert(sizeof(kBuiltinSamples) / sizeof(kBuiltinSamples[0]) == 7, \"built-in sample order/count changed\");",
        "",
    ])
    return "\n".join(lines)


def write_outputs(manifest: dict, loaded: list[dict], include_path: Path, report_path: Path) -> None:
    include_path.parent.mkdir(parents=True, exist_ok=True)
    include_path.write_text(render_include(manifest, loaded), encoding="utf-8")
    report = {
        "schema_version": 1,
        "bank_id": manifest["bank_id"],
        "sample_count": len(loaded),
        "sample_order": list(ORDER),
        "total_frames": sum(entry["frames"] for entry in loaded),
        "total_pcm_bytes": sum(len(entry["pcm"]) for entry in loaded),
        "resident_pcm_max_bytes": MAX_PCM_BYTES,
        "headroom_bytes": MAX_PCM_BYTES - sum(len(entry["pcm"]) for entry in loaded),
        "combined_pcm_sha256": manifest["combined_pcm_sha256"],
        "generated_include": include_path.relative_to(ROOT).as_posix() if include_path.is_relative_to(ROOT) else str(include_path),
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def write_comparison_summary(manifest: dict, destination: Path, raw_dir: Path) -> None:
    generated = []
    if raw_dir.is_dir():
        for metadata_path in sorted(raw_dir.glob("*.json")):
            try:
                metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            generated.append({
                "sound_id": metadata.get("sound_id"),
                "variant_id": metadata.get("variant_id"),
                "request_hash": metadata.get("request_hash"),
                "status": metadata.get("status"),
                "audition_path": metadata.get("audition_path"),
                "redistribution_status": metadata.get("redistribution_status", "not_reviewed"),
            })
    summary = {
        "schema_version": 1,
        "selection_status": "listening_required",
        "local_candidate_bank": manifest["bank_id"],
        "local_samples": [{"sound_id": entry["id"], "path": entry["path"], "sha256": entry["sha256"]} for entry in manifest["samples"]],
        "elevenlabs_candidates": generated,
        "note": "Generated effects are auditions only and never replace selected/local-v1 automatically.",
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    result.add_argument("--project-root", type=Path, default=ROOT)
    result.add_argument("--output-include", type=Path, default=DEFAULT_INCLUDE)
    result.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    result.add_argument("--comparison-summary", type=Path, default=DEFAULT_COMPARISON)
    result.add_argument("--raw-elevenlabs-dir", type=Path, default=ROOT / "assets/audio/raw/elevenlabs")
    result.add_argument("--validate-only", action="store_true")
    return result


def main() -> int:
    args = parser().parse_args()
    try:
        manifest, loaded = validate_bank(args.manifest, args.project_root)
        if not args.validate_only:
            write_outputs(manifest, loaded, args.output_include, args.report)
            write_comparison_summary(manifest, args.comparison_summary, args.raw_elevenlabs_dir)
        print(f"Validated {len(loaded)} samples, {manifest['total_pcm_bytes']} PCM bytes, bank {manifest['bank_id']}")
        return 0
    except ValidationError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
