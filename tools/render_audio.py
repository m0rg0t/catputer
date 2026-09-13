#!/usr/bin/env python3
"""Render and encode the eight matched development demos, with bound metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import wave

try:
    from .build_release import music_profile, read_version
except ImportError:
    from build_release import music_profile, read_version

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=180, choices=range(1, 601), metavar="1..600")
    parser.add_argument("--seed", type=lambda text: int(text, 0), default=0xCA7CAFE)
    parser.add_argument("--mood", choices=("cozy", "rainy", "night", "sunny"))
    parser.add_argument("--engine", choices=("synth", "hybrid"))
    args = parser.parse_args()
    if not 0 <= args.seed < 2**64:
        parser.error("Seed must fit an unsigned 64-bit integer")
    output = ROOT / "build/audio"
    output.mkdir(parents=True, exist_ok=True)
    version, profile = read_version(ROOT), music_profile(ROOT)
    for mood in ([args.mood] if args.mood else ("cozy", "rainy", "night", "sunny")):
        for engine in ([args.engine] if args.engine else ("synth", "hybrid")):
            stem = f"{mood}-{engine}"
            wav, meta, mp3 = [output / f"{stem}.rendering.{ext}" for ext in ("wav", "json", "mp3")]
            subprocess.run([str(ROOT / "build/cmake/lofi_native"), "--mood", mood, "--engine", engine,
                            "--seed", str(args.seed), "--seconds", str(args.seconds),
                            "--wav", str(wav), "--meta", str(meta)], check=True)
            data = json.loads(meta.read_text())
            if data.get("version") != version or data.get("generation_schema") != profile["generation_schema"]:
                raise ValueError("Native binary is stale; rebuild with CMake before rendering audio")
            with wave.open(str(wav), "rb") as source:
                if (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getnframes()) != (1, 2, data["sample_rate"], data["frames"]):
                    raise ValueError("WAV does not match render metadata")
            subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav),
                            "-map_metadata", "-1", "-codec:a", "libmp3lame", "-b:a", "128k", str(mp3)], check=True)
            data["wav_sha256"] = digest(wav)
            data["mp3_sha256"] = digest(mp3)
            data["encoding"] = "ffmpeg/libmp3lame 128k; mono input; no normalization"
            meta.write_text(json.dumps(data, indent=2) + "\n")
            for temporary, extension in ((wav, "wav"), (mp3, "mp3"), (meta, "json")):
                temporary.replace(output / f"{stem}.{extension}")
            print(f"Ready: {stem}; schema {data['generation_schema']}; {data['score_events']} events")


if __name__ == "__main__":
    main()
