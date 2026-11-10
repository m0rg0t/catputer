#!/usr/bin/env python3
"""Compile the shared music engine to WebAssembly for the site's live player.

Needs Emscripten's ``em++`` on PATH (or ``--emxx``). Writes
``build/web/catputer.wasm`` and copies the two player scripts next to it.
``tools/build_site.py`` publishes that directory when it is complete. The
browser build is a listening preview: it is the same composer and mixer, not
evidence about audio on the Cardputer ADV.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/web"
SOURCES = (
    "web/catputer_web.cpp",
    "firmware/src/audio/music.cpp",
    "firmware/src/audio/plate_reverb.cpp",
    "firmware/src/audio/sample_bank.cpp",
    "firmware/src/audio/timbre.cpp",
)
EXPORTS = (
    "init", "sample_rate", "buffer_frames", "buffer", "render", "set_mood", "next",
    "pause", "bpm", "bar", "mood", "meter", "seed_high", "seed_low", "levels",
)
SCRIPTS = ("worklet.js", "player.js")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emxx", default="em++", help="Emscripten C++ compiler")
    args = parser.parse_args()
    compiler = shutil.which(args.emxx)
    if compiler is None:
        print(f"build_web.py: {args.emxx} not found; install Emscripten first", file=sys.stderr)
        return 1
    OUTPUT.mkdir(parents=True, exist_ok=True)
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    exported = ",".join(f"_catputer_{name}" for name in EXPORTS)
    command = [
        compiler, "-std=c++17", "-O2", "-ffp-contract=off", "-fno-exceptions", "-fno-rtti",
        f"-DLOFI_VERSION=\"{version}\"", f"-I{ROOT / 'firmware/include'}",
        *(str(ROOT / source) for source in SOURCES),
        "-sSTANDALONE_WASM=1", "--no-entry", "-sALLOW_MEMORY_GROWTH=0",
        f"-sEXPORTED_FUNCTIONS={exported}",
        "-o", str(OUTPUT / "catputer.wasm"),
    ]
    subprocess.run(command, check=True)
    for script in SCRIPTS:
        shutil.copy2(ROOT / "web" / script, OUTPUT / script)
    size = (OUTPUT / "catputer.wasm").stat().st_size
    print(f"built {OUTPUT / 'catputer.wasm'} ({size:,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
