#!/usr/bin/env python3
"""Turn native renderer PPM captures into small, reviewable public media.

The native runner deliberately writes simple PPM files so it has no image
library dependency.  This script is the only conversion step: it validates
the device-sized frames, keeps the original 240 x 135 PNGs, creates a nearest
neighbour 3x review size, and packages the animation frames as a GIF.  It does
not invent or redraw any scene art.

Usage (with Pillow available)::

    python tools/export_media.py

The output is written below ``docs/media`` and is safe to copy into the site
builder's explicit allowlist.
"""

from __future__ import annotations

import argparse
import datetime as datetime_module
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import Any, Iterable

try:
    from PIL import Image, ImageDraw, ImageFont
except ModuleNotFoundError as error:  # pragma: no cover - depends on host setup
    raise SystemExit(
        "Pillow is required for media export. Install it in the host environment "
        "or run this script with a Python environment that provides PIL."
    ) from error


ROOT = Path(__file__).resolve().parents[1]
DISPLAY_SIZE = (240, 135)
SCALE = 3
PRIVATE_SCENARIOS = {
    # The numeric prefix is stripped from scenario ids; keep this evidence out
    # of the public contact sheet/site until the optional pack loader exists.
    "pack-fallback": "Scripted optional-pack fallback until the pack loader is hardware-verified.",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(128 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_revision() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return "uncommitted"
    value = result.stdout.strip()
    if not value:
        return "uncommitted"
    changed = subprocess.run(
        ["git", "status", "--porcelain", "--", "firmware", "native"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return value + ("-dirty" if changed.stdout.strip() else "")


def scenario_id(path: Path) -> str:
    stem = path.stem
    if "-" in stem and stem.split("-", 1)[0].isdigit():
        return stem.split("-", 1)[1]
    return stem


def scenario_label(path: Path) -> str:
    value = scenario_id(path).replace("-", " ")
    return value.title()


def load_frame(path: Path) -> Image.Image:
    try:
        with Image.open(path) as source:
            if source.format != "PPM":
                raise ValueError(f"{path} is {source.format or 'not an image'}, expected PPM")
            image = source.convert("RGB")
    except Exception as error:
        raise ValueError(f"cannot read renderer frame {path}: {error}") from error
    if image.size != DISPLAY_SIZE:
        raise ValueError(f"{path} is {image.size}, expected {DISPLAY_SIZE}")
    return image


def write_png(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG", optimize=True)


def font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    # Use a bundled/default Pillow font so the export remains offline and
    # portable.  Pillow 10+ accepts a size argument for the bitmap fallback.
    try:
        return ImageFont.load_default(size=size)
    except TypeError:  # pragma: no cover - older Pillow
        return ImageFont.load_default()


def make_contact_sheet(entries: list[dict[str, Any]], frames: dict[str, Image.Image], destination: Path) -> None:
    if not entries:
        raise ValueError("cannot make a contact sheet without public screenshot frames")
    columns = 4
    card_width = DISPLAY_SIZE[0]
    image_height = DISPLAY_SIZE[1]
    label_height = 22
    margin = 10
    rows = (len(entries) + columns - 1) // columns
    sheet = Image.new(
        "RGB",
        (
            columns * card_width + (columns + 1) * margin,
            rows * (image_height + label_height) + (rows + 1) * margin,
        ),
        (8, 13, 23),
    )
    draw = ImageDraw.Draw(sheet)
    label_font = font(12)
    for index, entry in enumerate(entries):
        row, column = divmod(index, columns)
        x = margin + column * card_width + column * margin
        y = margin + row * (image_height + label_height) + row * margin
        image = frames[entry["id"]]
        sheet.paste(image, (x, y))
        draw.rectangle((x, y, x + card_width - 1, y + image_height - 1), outline=(88, 114, 134))
        draw.text((x + 3, y + image_height + 4), entry["label"], fill=(246, 223, 178), font=label_font)
    destination.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(destination, format="PNG", optimize=True)


def write_animation(animation_paths: list[Path], destination: Path) -> dict[str, Any]:
    if not animation_paths:
        raise ValueError("no build/animation/*.ppm frames were found")
    frames = [load_frame(path) for path in animation_paths]
    # A single adaptive palette makes the GIF stable across frames.  Disable
    # dithering so the renderer's deliberate pixels remain crisp.
    palette = frames[0].quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    indexed = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    # GIF stores delays in 10ms units. A constant 83ms silently becomes 80ms;
    # distribute 80/90ms delays to preserve the renderer's 12 FPS timeline.
    durations = [((index + 1) * 100 // 12 - index * 100 // 12) * 10 for index in range(len(indexed))]
    destination.parent.mkdir(parents=True, exist_ok=True)
    indexed[0].save(
        destination,
        format="GIF",
        save_all=True,
        append_images=indexed[1:],
        duration=durations,
        loop=0,
        disposal=2,
        optimize=False,
    )
    with Image.open(destination) as exported:
        gif_frame_count = int(getattr(exported, "n_frames", 1))
    return {
        "source_pattern": "build/animation/*.ppm",
        "frame_count": len(animation_paths),
        "gif_frame_count": gif_frame_count,
        "fps": 12,
        "duration_ms": sum(durations),
        "width": DISPLAY_SIZE[0],
        "height": DISPLAY_SIZE[1],
        "gif": destination.name,
        "sha256": sha256_file(destination),
    }


def export_media(screens_dir: Path, animation_dir: Path, media_dir: Path, day_animation_dir: Path | None = None) -> dict[str, Any]:
    screen_paths = sorted(screens_dir.glob("*.ppm"))
    if not screen_paths:
        raise ValueError(f"no screenshot PPM files found in {screens_dir}")
    animation_paths = sorted(animation_dir.glob("*.ppm"))
    if not animation_paths:
        raise ValueError(f"no animation PPM files found in {animation_dir}")
    day_paths = sorted(day_animation_dir.glob("*.ppm")) if day_animation_dir else []
    if day_animation_dir and not day_paths:
        raise ValueError(f"no daytime animation PPM files found in {day_animation_dir}")

    native_dir = media_dir / "screens" / "native240"
    scaled_dir = media_dir / "screens" / "large3x"
    native_dir.mkdir(parents=True, exist_ok=True)
    scaled_dir.mkdir(parents=True, exist_ok=True)
    # Remove only files generated by this script.  Other media in docs/media is
    # left alone so an unrelated editorial asset cannot be lost accidentally.
    for path in native_dir.glob("*.png"):
        path.unlink()
    for path in scaled_dir.glob("*.png"):
        path.unlink()

    entries: list[dict[str, Any]] = []
    frames: dict[str, Image.Image] = {}
    for source_path in screen_paths:
        identifier = scenario_id(source_path)
        if identifier in frames:
            raise ValueError(f"duplicate screenshot scenario id: {identifier}")
        image = load_frame(source_path)
        frames[identifier] = image
        native_path = native_dir / f"{source_path.stem}.png"
        scaled_path = scaled_dir / f"{source_path.stem}@3x.png"
        write_png(image, native_path)
        write_png(image.resize((DISPLAY_SIZE[0] * SCALE, DISPLAY_SIZE[1] * SCALE), Image.Resampling.NEAREST), scaled_path)
        private_reason = PRIVATE_SCENARIOS.get(identifier)
        entries.append(
            {
                "id": identifier,
                "label": scenario_label(source_path),
                "source_ppm": source_path.relative_to(ROOT).as_posix(),
                "native_png": native_path.relative_to(media_dir).as_posix(),
                "scaled_png": scaled_path.relative_to(media_dir).as_posix(),
                "width": DISPLAY_SIZE[0],
                "height": DISPLAY_SIZE[1],
                "desktop_render": True,
                "hardware_verified": False,
                "public": private_reason is None,
                "private_reason": private_reason,
                "source_sha256": sha256_file(source_path),
                "native_sha256": sha256_file(native_path),
                "scaled_sha256": sha256_file(scaled_path),
                "visual_seed": None,
                "time_ms": None,
            }
        )

    public_entries = [entry for entry in entries if entry["public"]]
    make_contact_sheet(public_entries, frames, media_dir / "contact-sheet.png")
    animation = write_animation(animation_paths, media_dir / "scene.gif")
    manifest = {
        "schema_version": 1,
        "generated_by": "tools/export_media.py",
        "generated_utc": datetime_module.datetime.now(datetime_module.timezone.utc).replace(microsecond=0).isoformat(),
        "source_revision": git_revision(),
        "renderer": {
            "kind": "native_shared_renderer",
            "source_screens": screens_dir.relative_to(ROOT).as_posix(),
            "source_animation": animation_dir.relative_to(ROOT).as_posix(),
            "width": DISPLAY_SIZE[0],
            "height": DISPLAY_SIZE[1],
            "scale": SCALE,
            "desktop_render": True,
            "hardware_verified": False,
        },
        "screens": entries,
        "contact_sheet": {
            "path": "contact-sheet.png",
            "public_screenshot_count": len(public_entries),
            "sha256": sha256_file(media_dir / "contact-sheet.png"),
        },
        "animation": animation,
    }
    if day_animation_dir:
        day_animation = write_animation(day_paths, media_dir / "scene-day.gif")
        day_animation["source_pattern"] = day_animation_dir.relative_to(ROOT).as_posix() + "/*.ppm"
        manifest["day_animation"] = day_animation
    manifest_path = media_dir / "media-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return manifest


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screens", type=Path, default=ROOT / "build/screens", help="native renderer screenshot directory")
    parser.add_argument("--animation", type=Path, default=ROOT / "build/animation", help="native renderer animation directory")
    parser.add_argument("--day-animation", type=Path, help="optional Sunny native renderer animation directory")
    parser.add_argument("--media-dir", type=Path, default=ROOT / "docs/media", help="public media output directory")
    return parser.parse_args(list(argv) if argv is not None else None)


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        manifest = export_media(
            args.screens.resolve(),
            args.animation.resolve(),
            args.media_dir.resolve(),
            args.day_animation.resolve() if args.day_animation else None,
        )
    except (OSError, ValueError) as error:
        print(f"export_media.py: {error}", file=sys.stderr)
        return 1
    print(
        f"exported {len(manifest['screens'])} screenshots and "
        f"{manifest['animation']['frame_count']} animation frames to {args.media_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
