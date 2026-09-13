#!/usr/bin/env python3
"""Pack the authored scene PNGs into a deterministic LCD-friendly include.

This tool deliberately does only mechanical image preparation.  The room and
cat are authored outside the repository (the source PNGs are the reviewable
art boundary); this script resizes with nearest-neighbour sampling, quantizes
the opaque pixels once as a combined image, and emits indexed data for the
allocation-free firmware renderer.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any

try:
    from PIL import Image
except ImportError:  # pragma: no cover - exercised by the CLI environment
    Image = None  # type: ignore[assignment]


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_VERSION = "imagegen-v1"
DEFAULT_BACKGROUND = ROOT / "assets/source/imagegen-v1/background.png"
DEFAULT_CAT_SHEET = ROOT / "assets/source/imagegen-v1/cat-sheet.png"
DEFAULT_RUNTIME = ROOT / "assets/runtime/imagegen-v1"
DEFAULT_INCLUDE = ROOT / "firmware/src/ui/generated_scene_data.inc"
DEFAULT_SYMBOL_PREFIX = "kGeneratedScene"

SCENE_WIDTH = 240
SCENE_HEIGHT = 135
CAT_FRAME_WIDTH = 64
CAT_FRAME_HEIGHT = 72
CAT_COLUMNS = 3
CAT_ROWS = 2
CAT_FRAME_COUNT = CAT_COLUMNS * CAT_ROWS
TRANSPARENT_INDEX = 255
# The source sheet is square-cell art.  Frames are contained in 64x72 by the
# packer, so this origin places the authored paws near the bench surface in
# the supplied 240x135 room; the renderer can tune it without repacking.
CAT_ORIGIN_X = 154
CAT_ORIGIN_Y = 20
CHROMA_KEY_RGB = (255, 0, 255)
CHROMA_KEY_MIN_RED = 180
CHROMA_KEY_MAX_GREEN = 90
CHROMA_KEY_MIN_BLUE = 180

# These are the first sixteen entries in firmware/src/ui/ui.cpp.  Their order
# is part of the public renderer contract and must not be quantized or moved.
UI_PALETTE_RGB: tuple[tuple[int, int, int], ...] = (
    (8, 15, 28),
    (14, 28, 49),
    (24, 43, 64),
    (42, 60, 82),
    (89, 113, 135),
    (183, 206, 196),
    (246, 223, 178),
    (102, 183, 187),
    (152, 73, 65),
    (102, 60, 49),
    (208, 126, 65),
    (242, 184, 87),
    (174, 105, 76),
    (230, 158, 116),
    (112, 161, 116),
    (255, 221, 157),
)
GENERATED_PALETTE_SIZE = 48
FULL_PALETTE_SIZE = len(UI_PALETTE_RGB) + GENERATED_PALETTE_SIZE


class SceneError(ValueError):
    """The supplied authored scene cannot be packed safely."""


def validate_symbol_prefix(value: str) -> str:
    """Return one bounded C++ identifier, rejecting source-code injection."""

    if len(value) > 64 or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) is None:
        raise SceneError(
            "symbol prefix must be a C++ identifier using at most 64 ASCII letters, digits, or underscores"
        )
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(128 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _require_pillow() -> Any:
    if Image is None:
        raise SceneError("Pillow is required; use the bundled project Python runtime")
    return Image


def _nearest() -> Any:
    pillow = _require_pillow()
    return getattr(pillow, "Resampling", pillow).NEAREST


def _load_image(path: Path, label: str) -> Any:
    pillow = _require_pillow()
    if not path.is_file():
        raise SceneError(f"missing {label} PNG: {path}")
    try:
        with pillow.open(path) as source:
            source.load()
            return source.copy()
    except (OSError, SyntaxError) as error:
        raise SceneError(f"cannot read {label} PNG {path}: {error}") from error


def _image_data(image: Any) -> list[Any]:
    """Read pixels across Pillow versions without the getdata deprecation."""

    flattened = getattr(image, "get_flattened_data", None)
    if callable(flattened):
        return list(flattened())
    return list(image.getdata())


def _is_chroma_key(red: int, green: int, blue: int) -> bool:
    return (
        red >= CHROMA_KEY_MIN_RED
        and green <= CHROMA_KEY_MAX_GREEN
        and blue >= CHROMA_KEY_MIN_BLUE
        and red - green >= 100
        and blue - green >= 100
    )


def _check_source_ratio(width: int, height: int, target_width: int, target_height: int, label: str) -> None:
    if width <= 0 or height <= 0:
        raise SceneError(f"{label} source dimensions must be positive")
    source_ratio = width / height
    target_ratio = target_width / target_height
    # Nearest-neighbour rescaling is intentional, but a wrong aspect ratio
    # would stretch a scene silently and make the result hard to review.
    if abs(source_ratio - target_ratio) / target_ratio > 0.01:
        raise SceneError(
            f"{label} source aspect ratio {width}x{height} does not match "
            f"{target_width}x{target_height}"
        )


def prepare_background(source: Any) -> Any:
    """Return an opaque RGB image at exactly 240x135."""

    pillow = _require_pillow()
    _check_source_ratio(source.width, source.height, SCENE_WIDTH, SCENE_HEIGHT, "background")
    if "A" in source.getbands():
        alpha_min, alpha_max = source.convert("RGBA").getchannel("A").getextrema()
        if alpha_min != 255 or alpha_max != 255:
            raise SceneError("background must be opaque; use alpha only for the cat sheet")
    return source.convert("RGB").resize((SCENE_WIDTH, SCENE_HEIGHT), _nearest())


def prepare_cat_frames(source: Any) -> list[Any]:
    """Split a 3x2 sheet into six 64x72 nearest-resized RGBA frames.

    Authored sheets may carry transparency in an alpha channel or use the
    explicit #FF00FF chroma key.  The latter keeps an imagegen RGB export
    reviewable without making the firmware depend on a keyed bitmap format.
    """

    if "A" not in source.getbands():
        rgb_source = source.convert("RGB")
        has_chroma_key = any(_is_chroma_key(*pixel) for pixel in _image_data(rgb_source))
        if not has_chroma_key:
            raise SceneError("cat-sheet must contain an alpha channel or #FF00FF chroma key")
    if source.width % CAT_COLUMNS != 0 or source.height % CAT_ROWS != 0:
        raise SceneError("cat-sheet dimensions must divide evenly into a 3x2 grid")
    source_frame_width = source.width // CAT_COLUMNS
    source_frame_height = source.height // CAT_ROWS
    if source_frame_width <= 0 or source_frame_height <= 0:
        raise SceneError("cat-sheet frame dimensions must be positive")
    frames: list[Any] = []
    pillow = _require_pillow()
    for row in range(CAT_ROWS):
        for column in range(CAT_COLUMNS):
            box = (
                column * source_frame_width,
                row * source_frame_height,
                (column + 1) * source_frame_width,
                (row + 1) * source_frame_height,
            )
            cropped = source.convert("RGBA").crop(box)
            scale = min(CAT_FRAME_WIDTH / cropped.width, CAT_FRAME_HEIGHT / cropped.height)
            resized_width = max(1, round(cropped.width * scale))
            resized_height = max(1, round(cropped.height * scale))
            resized = cropped.resize((resized_width, resized_height), _nearest())
            frame = pillow.new("RGBA", (CAT_FRAME_WIDTH, CAT_FRAME_HEIGHT), (0, 0, 0, 0))
            frame.alpha_composite(
                resized,
                ((CAT_FRAME_WIDTH - resized_width) // 2, (CAT_FRAME_HEIGHT - resized_height) // 2),
            )
            frames.append(frame)
    return frames


def _rgba_pixels(frame: Any) -> tuple[list[tuple[int, int, int]], list[bool]]:
    opaque: list[tuple[int, int, int]] = []
    visible: list[bool] = []
    for red, green, blue, alpha in _image_data(frame):
        is_visible = alpha >= 128 and not _is_chroma_key(red, green, blue)
        visible.append(is_visible)
        opaque.append((red, green, blue) if is_visible else (0, 0, 0))
    return opaque, visible


def _quantize_combined(background: Any, cat_frames: list[Any]) -> tuple[list[int], list[list[int]], list[tuple[int, int, int]]]:
    pillow = _require_pillow()
    background_pixels = _image_data(background)
    cat_rgb: list[list[tuple[int, int, int]]] = []
    cat_visible: list[list[bool]] = []
    combined_pixels = list(background_pixels)
    for frame in cat_frames:
        rgb, visible = _rgba_pixels(frame)
        cat_rgb.append(rgb)
        cat_visible.append(visible)
        combined_pixels.extend(pixel for pixel, keep in zip(rgb, visible) if keep)
    if not combined_pixels:
        raise SceneError("scene has no opaque pixels to quantize")

    combined = pillow.new("RGB", (len(combined_pixels), 1))
    combined.putdata(combined_pixels)
    quantize_method = getattr(pillow, "Quantize", pillow).MEDIANCUT
    dither_none = getattr(pillow, "Dither", pillow).NONE
    quantized = combined.quantize(colors=GENERATED_PALETTE_SIZE, method=quantize_method, dither=dither_none)
    raw_palette = quantized.getpalette() or []
    generated_palette = [
        tuple(int(channel) for channel in raw_palette[offset : offset + 3])
        for offset in range(0, GENERATED_PALETTE_SIZE * 3, 3)
    ]
    generated_palette = [colour for colour in generated_palette if len(colour) == 3]
    if not generated_palette:
        raise SceneError("Pillow returned an empty generated palette")
    while len(generated_palette) < GENERATED_PALETTE_SIZE:
        generated_palette.append(generated_palette[-1])

    quantized_indices = _image_data(quantized)
    background_count = len(background_pixels)
    background_indices = [16 + int(value) for value in quantized_indices[:background_count]]
    cat_indices: list[list[int]] = []
    offset = background_count
    for rgb, visible in zip(cat_rgb, cat_visible):
        result: list[int] = []
        for pixel, keep in zip(rgb, visible):
            if not keep:
                result.append(TRANSPARENT_INDEX)
            else:
                result.append(16 + int(quantized_indices[offset]))
                offset += 1
        cat_indices.append(result)
    return background_indices, cat_indices, list(UI_PALETTE_RGB) + generated_palette


def rgb565(colour: tuple[int, int, int]) -> int:
    red, green, blue = colour
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def rgb565_to_rgb(value: int) -> tuple[int, int, int]:
    """Expand RGB565 with the same integer rule as the native PPM exporter."""

    red = (value >> 11) & 0x1F
    green = (value >> 5) & 0x3F
    blue = value & 0x1F
    return (
        red * 255 // 31,
        green * 255 // 63,
        blue * 255 // 31,
    )


def _palette_flat(palette: list[tuple[int, int, int]]) -> list[int]:
    flat: list[int] = []
    for colour in palette:
        flat.extend(colour)
    return flat + [0] * (768 - len(flat))


def _indexed_image(data: list[int], width: int, height: int, palette: list[tuple[int, int, int]], transparent: int | None = None) -> Any:
    pillow = _require_pillow()
    image = pillow.new("P", (width, height))
    image.putdata(data)
    image.putpalette(_palette_flat(palette))
    if transparent is not None:
        image.info["transparency"] = transparent
    return image


def _interleaved_cat_sheet(cat_indices: list[list[int]]) -> list[int]:
    """Lay six frame-major arrays out as a normal 3-column by 2-row sheet."""

    sheet: list[int] = []
    for row in range(CAT_ROWS):
        for y in range(CAT_FRAME_HEIGHT):
            for column in range(CAT_COLUMNS):
                frame = cat_indices[row * CAT_COLUMNS + column]
                start = y * CAT_FRAME_WIDTH
                sheet.extend(frame[start : start + CAT_FRAME_WIDTH])
    return sheet


def _format_values(values: list[int], per_line: int = 24) -> list[str]:
    return [
        "    " + ", ".join(str(value) for value in values[offset : offset + per_line]) + ","
        for offset in range(0, len(values), per_line)
    ]


def render_include(
    data: dict[str, Any], symbol_prefix: str = DEFAULT_SYMBOL_PREFIX
) -> str:
    """Render a self-contained C++17 include for firmware/src/ui/ui.cpp."""

    prefix = validate_symbol_prefix(symbol_prefix)
    palette565 = [rgb565(colour) for colour in data["palette_rgb"]]
    lines = [
        "// Generated by tools/prepare_scene.py; do not edit by hand.",
        f"// Scene asset version: {data['version']}",
        "",
        f"static constexpr std::uint8_t {prefix}Background[240 * 135] = {{",
    ]
    lines.extend(_format_values(data["background_indices"]))
    lines.extend([
        "};",
        "",
        f"static constexpr std::uint8_t {prefix}Cat[6][64 * 72] = {{",
    ])
    for frame in data["cat_indices"]:
        lines.append("{")
        lines.extend(_format_values(frame))
        lines.append("},")
    lines.extend([
        "};",
        "",
        f"static constexpr std::uint16_t {prefix}Palette[64] = {{",
    ])
    lines.extend(_format_values(palette565, 12))
    lines.extend([
        "};",
        "",
        f"static constexpr std::uint32_t {prefix}PaletteRgb[64][3] = {{",
    ])
    for colour in data["palette_rgb"]:
        lines.append(f"    {{{colour[0]}, {colour[1]}, {colour[2]}}},")
    lines.extend([
        "};",
        "",
        f"static constexpr std::uint8_t {prefix}Transparent = 255;",
        f"static constexpr int {prefix}OriginX = {CAT_ORIGIN_X};",
        f"static constexpr int {prefix}OriginY = {CAT_ORIGIN_Y};",
        "",
    ])
    return "\n".join(lines)


def build_scene(background_path: Path, cat_sheet_path: Path, version: str = DEFAULT_VERSION) -> dict[str, Any]:
    """Load and pack a scene without writing any output files."""

    background_source = _load_image(background_path, "background")
    cat_source = _load_image(cat_sheet_path, "cat-sheet")
    background = prepare_background(background_source)
    cat_frames = prepare_cat_frames(cat_source)
    background_indices, cat_indices, palette_rgb = _quantize_combined(background, cat_frames)
    return {
        "version": version,
        "background_indices": background_indices,
        "cat_indices": cat_indices,
        "palette_rgb": palette_rgb,
        "background": background,
        "cat_frames": cat_frames,
        "source": {
            "background": {
                "path": str(background_path),
                "sha256": sha256_file(background_path),
                "size": [background_source.width, background_source.height],
                "mode": background_source.mode,
            },
            "cat_sheet": {
                "path": str(cat_sheet_path),
                "sha256": sha256_file(cat_sheet_path),
                "size": [cat_source.width, cat_source.height],
                "mode": cat_source.mode,
            },
        },
    }


def _write_preview(data: dict[str, Any], destination: Path) -> None:
    pillow = _require_pillow()
    device_palette = [rgb565_to_rgb(rgb565(colour)) for colour in data["palette_rgb"]]
    indexed_background = _indexed_image(
        data["background_indices"], SCENE_WIDTH, SCENE_HEIGHT, device_palette
    )
    preview = indexed_background.convert("RGBA")
    palette = device_palette
    frame = data["cat_indices"][0]
    overlay = pillow.new("RGBA", (CAT_FRAME_WIDTH, CAT_FRAME_HEIGHT))
    overlay.putdata(
        [
            (0, 0, 0, 0) if value == TRANSPARENT_INDEX else (*palette[value], 255)
            for value in frame
        ]
    )
    preview.alpha_composite(overlay, (CAT_ORIGIN_X, CAT_ORIGIN_Y))
    preview.convert("RGB").save(destination, format="PNG")


def write_outputs(
    data: dict[str, Any],
    include_path: Path,
    runtime_dir: Path,
    project_root: Path = ROOT,
    symbol_prefix: str = DEFAULT_SYMBOL_PREFIX,
) -> dict[str, Any]:
    """Write firmware include, indexed previews, and a provenance manifest."""

    rendered_include = render_include(data, symbol_prefix)
    pillow = _require_pillow()
    include_path.parent.mkdir(parents=True, exist_ok=True)
    runtime_dir.mkdir(parents=True, exist_ok=True)
    if not include_path.is_file() or include_path.read_text(encoding="utf-8") != rendered_include:
        include_path.write_text(rendered_include, encoding="utf-8")

    device_palette = [rgb565_to_rgb(rgb565(colour)) for colour in data["palette_rgb"]]
    background_image = _indexed_image(
        data["background_indices"], SCENE_WIDTH, SCENE_HEIGHT, device_palette
    )
    background_image.save(runtime_dir / "background-indexed.png", format="PNG", optimize=False)
    cat_sheet_indices = _interleaved_cat_sheet(data["cat_indices"])
    cat_image = _indexed_image(
        cat_sheet_indices,
        CAT_COLUMNS * CAT_FRAME_WIDTH,
        CAT_ROWS * CAT_FRAME_HEIGHT,
        device_palette,
        TRANSPARENT_INDEX,
    )
    cat_image.save(runtime_dir / "cat-sheet-indexed.png", format="PNG", optimize=False)

    palette_image = pillow.new("RGB", (8 * 24, 8 * 18))
    # Expand swatches row-by-row without relying on an image drawing helper
    # that changes between Pillow versions.
    swatches: list[tuple[int, int, int]] = []
    for row in range(8):
        for y in range(18):
            for column in range(8):
                swatches.extend([device_palette[row * 8 + column]] * 24)
    palette_image.putdata(swatches)
    palette_image.save(runtime_dir / "palette.png", format="PNG", optimize=False)
    _write_preview(data, runtime_dir / "scene-preview.png")

    try:
        background_rel = background_path_relative = data["source"]["background"]["path"]
        cat_rel = data["source"]["cat_sheet"]["path"]
        background_rel = str(Path(background_rel).resolve().relative_to(project_root.resolve()))
        cat_rel = str(Path(cat_rel).resolve().relative_to(project_root.resolve()))
    except ValueError:
        background_rel = data["source"]["background"]["path"]
        cat_rel = data["source"]["cat_sheet"]["path"]

    manifest = {
        "schema_version": 1,
        "asset_version": data["version"],
        "target": {
            "background": [SCENE_WIDTH, SCENE_HEIGHT],
            "cat_sheet": [CAT_COLUMNS * CAT_FRAME_WIDTH, CAT_ROWS * CAT_FRAME_HEIGHT],
            "cat_grid": [CAT_COLUMNS, CAT_ROWS],
            "cat_frame": [CAT_FRAME_WIDTH, CAT_FRAME_HEIGHT],
            "cat_frame_count": CAT_FRAME_COUNT,
            "cat_origin": [CAT_ORIGIN_X, CAT_ORIGIN_Y],
            "cat_fit": "contain-nearest-centered",
        },
        "index_format": {
            "bits_per_pixel": 8,
            "transparent_cat_index": TRANSPARENT_INDEX,
            "transparent_sources": [
                "alpha < 128",
                "chroma red>=180 blue>=180 green<=90 and both red/blue-green>=100",
            ],
            "palette_size": FULL_PALETTE_SIZE,
            "ui_palette_entries": len(UI_PALETTE_RGB),
            "generated_palette_entries": GENERATED_PALETTE_SIZE,
        },
        "sources": {
            "background": {**data["source"]["background"], "path": background_rel},
            "cat_sheet": {**data["source"]["cat_sheet"], "path": cat_rel},
        },
        "outputs": {
            "symbol_prefix": symbol_prefix,
            "include": str(include_path.resolve().relative_to(project_root.resolve()))
            if include_path.resolve().is_relative_to(project_root.resolve())
            else str(include_path),
            "runtime_dir": str(runtime_dir.resolve().relative_to(project_root.resolve()))
            if runtime_dir.resolve().is_relative_to(project_root.resolve())
            else str(runtime_dir),
            "files": [
                "background-indexed.png",
                "cat-sheet-indexed.png",
                "palette.png",
                "scene-preview.png",
            ],
        },
        "palette_rgb": [list(colour) for colour in data["palette_rgb"]],
        "palette_rgb565": [rgb565(colour) for colour in data["palette_rgb"]],
        "palette_rgb565_decoded": [list(rgb565_to_rgb(rgb565(colour))) for colour in data["palette_rgb"]],
    }
    (runtime_dir / "scene-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--background", type=Path, default=DEFAULT_BACKGROUND)
    result.add_argument("--cat-sheet", type=Path, default=DEFAULT_CAT_SHEET)
    result.add_argument("--version", default=DEFAULT_VERSION)
    result.add_argument("--output-include", type=Path, default=DEFAULT_INCLUDE)
    result.add_argument("--runtime-dir", type=Path, default=DEFAULT_RUNTIME)
    result.add_argument("--project-root", type=Path, default=ROOT)
    result.add_argument("--symbol-prefix", default=DEFAULT_SYMBOL_PREFIX)
    result.add_argument("--validate-only", action="store_true")
    return result


def main() -> int:
    args = parser().parse_args()
    try:
        validate_symbol_prefix(args.symbol_prefix)
        data = build_scene(args.background, args.cat_sheet, args.version)
        if not args.validate_only:
            write_outputs(
                data,
                args.output_include,
                args.runtime_dir,
                args.project_root,
                args.symbol_prefix,
            )
        print(
            f"Prepared {SCENE_WIDTH}x{SCENE_HEIGHT} background, "
            f"{CAT_FRAME_COUNT} cat frames, {FULL_PALETTE_SIZE}-colour palette ({args.version})"
        )
        return 0
    except (OSError, SceneError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
