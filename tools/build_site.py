#!/usr/bin/env python3
"""Build the offline Cardputer ADV Lofi project showcase.

The builder copies a small, explicit public allowlist into ``build/site`` and
writes one self-contained HTML page.  It never crawls the workspace, embeds
remote resources, or turns missing renders/audio into placeholder downloads.
Screenshots and the scene animation come from ``tools/export_media.py`` and
remain labeled as desktop evidence until the physical ADV has been tested.
"""

from __future__ import annotations

import argparse
import datetime as datetime_module
import hashlib
import html
import json
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Any, Iterable
import zipfile


ROOT = Path(__file__).resolve().parents[1]
MEDIA_ROOT = ROOT / "docs/media"
OUTPUT_ROOT = ROOT / "build/site"

PUBLIC_DOCS = (
    "README.md",
    "docs/INSTALL.md",
    "docs/VERIFICATION.md",
    "docs/THIRD_PARTY.md",
    "docs/EXPERIENCE.md",
    "docs/DELIVERY.md",
    "docs/ARCHITECTURE.md",
)
AUDIO_SPECS = (
    ("cozy", "Cozy"),
    ("rainy", "Rainy"),
    ("night", "Night"),
)
ENGINES = (
    ("synth", "Synth"),
    ("hybrid", "Hybrid"),
)
# A release is copied only when it uses one of these explicitly reviewed names.
# No arbitrary file under dist can enter the public output.
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
    return result.stdout.strip() or "uncommitted"


def ensure_inside(path: Path, parent: Path) -> Path:
    path = path.resolve()
    parent = parent.resolve()
    try:
        path.relative_to(parent)
    except ValueError as error:
        raise ValueError(f"path escapes allowed root: {path}") from error
    return path


def copy_allowed(source: Path, destination: Path, source_root: Path) -> str:
    source = ensure_inside(source, source_root)
    if not source.is_file():
        raise FileNotFoundError(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return destination.relative_to(OUTPUT_ROOT).as_posix()


def read_manifest(media_root: Path) -> dict[str, Any]:
    path = ensure_inside(media_root / "media-manifest.json", media_root)
    if not path.is_file():
        raise FileNotFoundError(f"{path} is missing; run tools/export_media.py first")
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid media manifest {path}: {error}") from error
    if manifest.get("schema_version") != 1:
        raise ValueError(f"unsupported media manifest schema in {path}")
    screens = manifest.get("screens")
    if not isinstance(screens, list) or not screens:
        raise ValueError("media manifest has no screenshots")
    return manifest


def public_screens(media_root: Path, output_root: Path, manifest: dict[str, Any]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for raw in manifest["screens"]:
        if not raw.get("public", True):
            continue
        identifier = str(raw.get("id", ""))
        native = str(raw.get("native_png", ""))
        scaled = str(raw.get("scaled_png", ""))
        if not identifier or not native or not scaled:
            raise ValueError("public screenshot entry has missing paths")
        native_source = ensure_inside(media_root / native, media_root)
        scaled_source = ensure_inside(media_root / scaled, media_root)
        if sha256_file(native_source) != raw.get("native_sha256") or sha256_file(scaled_source) != raw.get("scaled_sha256"):
            raise ValueError(f"screenshot hash mismatch: {identifier}; export media again")
        native_output = output_root / "media" / native
        scaled_output = output_root / "media" / scaled
        copy_allowed(native_source, native_output, media_root)
        copy_allowed(scaled_source, scaled_output, media_root)
        result.append(
            {
                "id": identifier,
                "label": str(raw.get("label", identifier.title())),
                "native": native_output.relative_to(output_root).as_posix(),
                "scaled": scaled_output.relative_to(output_root).as_posix(),
                "desktop_render": bool(raw.get("desktop_render", True)),
                "hardware_verified": bool(raw.get("hardware_verified", False)),
                "source_sha256": raw.get("source_sha256"),
            }
        )
    if not result:
        raise ValueError("media manifest has no public screenshots")
    return result


def copy_common_media(media_root: Path, output_root: Path, manifest: dict[str, Any]) -> dict[str, str]:
    contact = str(manifest.get("contact_sheet", {}).get("path", "contact-sheet.png"))
    animation = str(manifest.get("animation", {}).get("gif", "scene.gif"))
    contact_source = ensure_inside(media_root / contact, media_root)
    animation_source = ensure_inside(media_root / animation, media_root)
    if sha256_file(contact_source) != manifest["contact_sheet"].get("sha256") or sha256_file(animation_source) != manifest["animation"].get("sha256"):
        raise ValueError("contact sheet or animation hash mismatch; export media again")
    copied = {
        "contact_sheet": copy_allowed(contact_source, output_root / "media" / contact, media_root),
        "animation": copy_allowed(animation_source, output_root / "media" / animation, media_root),
    }
    # Do not copy the source manifest verbatim: it includes private/scripted
    # scenarios.  A filtered manifest is written after all public assets are
    # selected.
    return copied


def find_field(value: Any, keys: tuple[str, ...]) -> str | None:
    if isinstance(value, dict):
        for key in keys:
            if key in value and value[key] not in (None, ""):
                return str(value[key])
        for child in value.values():
            found = find_field(child, keys)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = find_field(child, keys)
            if found is not None:
                return found
    return None


def parse_favorite_seed(value: Any) -> str | None:
    if isinstance(value, dict):
        favorite_code = value.get("favorite_code")
        if isinstance(favorite_code, str) and favorite_code.startswith("lofi1-"):
            parts = favorite_code.split("-")
            if len(parts) >= 2 and len(parts[1]) == 16:
                try:
                    int(parts[1], 16)
                except ValueError:
                    pass
                else:
                    return parts[1].upper()
        for child in value.values():
            found = parse_favorite_seed(child)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = parse_favorite_seed(child)
            if found is not None:
                return found
    return None


def read_audio_metadata(root: Path, mood: str, engine: str) -> dict[str, Any] | None:
    candidates = (
        root / "build/audio" / f"{mood}-{engine}.json",
        root / "build/audio" / f"{mood}-{engine}.meta.json",
        root / "build/audio" / f"{mood}.json",
        root / "build/audio/audio-manifest.json",
        root / "build/audio/manifest.json",
    )
    for candidate in candidates:
        if not candidate.is_file():
            continue
        try:
            return json.loads(candidate.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
    return None


def audio_seed(root: Path, mood: str, engine: str) -> str | None:
    metadata = read_audio_metadata(root, mood, engine)
    if metadata is None:
        return None
    favorite_seed = parse_favorite_seed(metadata)
    if favorite_seed is not None:
        return favorite_seed
    value = find_field(metadata, ("matched_seed", "music_seed", "session_seed", "seed"))
    return value.upper() if value is not None else None


def copy_audio(root: Path, output_root: Path) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for mood, mood_label in AUDIO_SPECS:
        pair: dict[str, Any] = {"mood": mood, "label": mood_label, "engines": []}
        for engine, engine_label in ENGINES:
            source = root / "build/audio" / f"{mood}-{engine}.mp3"
            entry: dict[str, Any] = {
                "engine": engine,
                "label": engine_label,
                "source": source.relative_to(root).as_posix(),
                "available": source.is_file(),
                "seed": audio_seed(root, mood, engine),
            }
            metadata = read_audio_metadata(root, mood, engine)
            entry["favorite_code"] = find_field(metadata, ("favorite_code",)) if metadata is not None else None
            entry["score_hash"] = find_field(metadata, ("score_hash",)) if metadata is not None else None
            if source.is_file():
                output = output_root / "audio" / source.name
                entry["path"] = copy_allowed(source, output, root)
                entry["sha256"] = sha256_file(source)
                entry["bytes"] = source.stat().st_size
            pair["engines"].append(entry)
        pair["both_available"] = all(entry["available"] for entry in pair["engines"])
        seeds = [entry["seed"] for entry in pair["engines"]]
        score_hashes = [entry["score_hash"] for entry in pair["engines"]]
        pair["matched_seed"] = (
            seeds[0]
            if pair["both_available"]
            and seeds[0] is not None
            and seeds[0] == seeds[1]
            and score_hashes[0] is not None
            and score_hashes[0] == score_hashes[1]
            else None
        )
        pair["matched_score_hash"] = (
            score_hashes[0]
            if pair["matched_seed"] is not None
            else None
        )
        result.append(pair)
    return result


def copy_docs(root: Path, output_root: Path) -> list[str]:
    copied: list[str] = []
    for relative in PUBLIC_DOCS:
        source = ensure_inside(root / relative, root)
        if not source.is_file():
            continue
        destination = output_root / "source" / relative
        copied.append(copy_allowed(source, destination, root))
    return copied


def release_name(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value or value.startswith("/") or "\\" in value:
        raise ValueError(f"invalid release {field}")
    path = Path(value)
    if any(part in ("", ".", "..") for part in path.parts) or path.as_posix() != value:
        raise ValueError(f"unsafe release {field}: {value!r}")
    return value


def release_file_record(source: Path, destination: Path, source_root: Path, name: str) -> dict[str, Any]:
    return {
        "name": name,
        "path": copy_allowed(source, destination, source_root),
        "sha256": sha256_file(source),
        "bytes": source.stat().st_size,
    }


def copy_release(root: Path, output_root: Path) -> list[dict[str, Any]]:
    """Copy only a validated release selected by dist/latest.json.

    A same-named BIN in ``dist`` is not enough evidence for a download.  The
    canonical latest manifest identifies the version directory and archive;
    every file it declares is checked against both the directory and the ZIP
    before any public copy is made.
    """
    dist = root / "dist"
    latest_path = dist / "latest.json"
    if not latest_path.is_file():
        return []
    try:
        latest = json.loads(latest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid dist/latest.json: {error}") from error
    directory_name = release_name(latest.get("directory"), "directory")
    archive_name = release_name(latest.get("archive"), "archive")
    files = latest.get("files")
    if not isinstance(files, dict) or not files:
        raise ValueError("dist/latest.json has no canonical files map")
    directory = ensure_inside(dist / directory_name, dist)
    archive = ensure_inside(dist / archive_name, dist)
    if not directory.is_dir() or not archive.is_file():
        raise ValueError("dist/latest.json points to a missing release directory or archive")
    expected_archive_sha = latest.get("archive_sha256")
    if not isinstance(expected_archive_sha, str) or sha256_file(archive) != expected_archive_sha:
        raise ValueError("release archive SHA-256 does not match dist/latest.json")

    declared: list[tuple[str, Path, bytes]] = []
    for raw_name, raw_info in files.items():
        name = release_name(raw_name, "file name")
        if not isinstance(raw_info, dict):
            raise ValueError(f"release file record is not an object: {name}")
        source = ensure_inside(directory / name, directory)
        if not source.is_file():
            raise ValueError(f"release manifest file is missing: {name}")
        data = source.read_bytes()
        if raw_info.get("bytes") != len(data) or raw_info.get("sha256") != hashlib.sha256(data).hexdigest():
            raise ValueError(f"release manifest hash/size mismatch: {name}")
        declared.append((name, source, data))

    try:
        with zipfile.ZipFile(archive) as package:
            expected = set(files) | {"manifest.json", "SHA256SUMS"}
            if len(package.namelist()) != len(expected) or set(package.namelist()) != expected:
                raise ValueError("release archive has unexpected or duplicate files")
            for name, _source, data in declared:
                try:
                    packed = package.read(name)
                except KeyError as error:
                    raise ValueError(f"release archive is missing {name}") from error
                if packed != data:
                    raise ValueError(f"release archive content mismatch: {name}")
    except zipfile.BadZipFile as error:
        raise ValueError(f"invalid release archive: {archive}") from error

    result = [
        release_file_record(latest_path, output_root / "downloads" / "latest.json", dist, "latest.json"),
        release_file_record(archive, output_root / "downloads" / archive_name, dist, archive_name),
    ]
    for name, source, _data in declared:
        if name.lower().endswith(".bin"):
            result.append(
                release_file_record(
                    source,
                    output_root / "downloads" / directory_name / name,
                    dist,
                    f"{directory_name}/{name}",
                )
            )
    return result


def evidence_badge() -> str:
    return '<span class="badge">DESKTOP RENDER · HARDWARE UNVERIFIED</span>'


def render_gallery(screens: list[dict[str, Any]]) -> str:
    cards: list[str] = []
    for screen in screens:
        label = html.escape(screen["label"])
        native = html.escape(screen["native"])
        scaled = html.escape(screen["scaled"])
        cards.append(
            "<figure class=\"shot\">"
            f"<a href=\"{scaled}\" data-shot data-label=\"{label}\"><img loading=\"lazy\" src=\"{native}\" alt=\"{label} desktop render at 240 by 135 pixels\"></a>"
            f"<figcaption><strong>{label}</strong><span>240×135 · desktop render · hardware unverified</span></figcaption>"
            "</figure>"
        )
    return "\n".join(cards)


def render_audio(audio_pairs: list[dict[str, Any]]) -> str:
    sections: list[str] = []
    for pair in audio_pairs:
        cards: list[str] = []
        if pair["matched_seed"] is not None and pair.get("matched_score_hash") is not None:
            seed_note = (
                f"Matched seed {html.escape(str(pair['matched_seed']))} · "
                f"event hash {html.escape(str(pair['matched_score_hash']))}"
            )
        elif pair["both_available"]:
            seed_note = "A/B pair · seed metadata unavailable"
        else:
            seed_note = "Matched-seed A/B slots · optional host renders"
        for engine in pair["engines"]:
            label = html.escape(engine["label"])
            if engine["available"]:
                path = html.escape(engine["path"])
                control = f'<audio class="audio-hidden" preload="none" src="{path}"></audio><button class="button play-audio" type="button" aria-label="Play {html.escape(pair["label"])} {label}">Play</button> <span class="audio-time" aria-live="off">0:00 / 1:30</span><p><a href="{path}" download>Download MP3</a> · host render</p>'
            else:
                control = '<span class="missing">Optional MP3 not present in this checkout</span>'
            cards.append(
                f'<div class="audio-card"><div class="audio-label">{label}</div>{control}</div>'
            )
        sections.append(
            f'<article class="audio-pair"><div class="pair-head"><h3>{html.escape(pair["label"])} mood</h3><span>{seed_note}</span></div><div class="audio-grid">{"".join(cards)}</div></article>'
        )
    return "\n".join(sections)


def render_releases(releases: list[dict[str, Any]]) -> str:
    if not releases:
        return '<p class="muted">No release files are present. Build and verify a device package before adding a download.</p>'
    items: list[str] = []
    for release in releases:
        name = html.escape(release["name"])
        path = html.escape(release["path"])
        items.append(f'<a class="download" href="{path}"><strong>{name}</strong><span>{release["bytes"]:,} bytes · SHA-256 {html.escape(release["sha256"][:12])}…</span></a>')
    return "".join(items)


STYLE = r"""
:root { color-scheme: dark; --ink:#08101c; --panel:#101d2c; --panel2:#17283b; --line:#2e4961; --cream:#f6dfb2; --muted:#9bb0c2; --cyan:#70c0c1; --gold:#f2b857; --orange:#d77e4b; --leaf:#84b790; }
* { box-sizing:border-box; }
html { scroll-behavior:smooth; }
body { margin:0; background:radial-gradient(circle at 80% -10%, #213b4e 0, transparent 34rem), var(--ink); color:var(--cream); font:16px/1.55 ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; }
a { color:var(--cyan); text-decoration:none; }
a:hover { color:#b0e4da; }
.wrap { max-width:1160px; margin:auto; padding:0 24px; }
header { position:sticky; top:0; z-index:4; background:rgba(8,16,28,.9); border-bottom:1px solid rgba(112,192,193,.22); backdrop-filter:blur(14px); }
nav { min-height:60px; display:flex; align-items:center; justify-content:space-between; gap:20px; }
.wordmark { letter-spacing:.15em; text-transform:uppercase; font-size:.82rem; color:var(--gold); }
nav ul { display:flex; gap:18px; list-style:none; padding:0; margin:0; font-size:.82rem; }
main { overflow:hidden; }
.hero { display:grid; grid-template-columns:1.05fr .95fr; gap:34px; align-items:center; padding:74px 0 52px; }
.eyebrow { color:var(--cyan); text-transform:uppercase; letter-spacing:.17em; font-size:.72rem; font-weight:700; }
h1 { font-size:clamp(2.5rem,7vw,5.4rem); line-height:.96; letter-spacing:-.07em; margin:14px 0 20px; max-width:8ch; }
h2 { font-size:clamp(1.8rem,4vw,3rem); line-height:1.06; letter-spacing:-.04em; margin:0 0 14px; }
h3 { margin:0; font-size:1rem; }
.lead { color:#c2d0d6; font-size:1.1rem; max-width:38rem; }
.badge { display:inline-flex; border:1px solid rgba(242,184,87,.55); background:rgba(242,184,87,.09); color:var(--gold); padding:6px 10px; border-radius:999px; font-size:.68rem; letter-spacing:.1em; font-weight:700; }
.hero-actions { display:flex; gap:10px; flex-wrap:wrap; margin-top:24px; }
.button { display:inline-flex; align-items:center; justify-content:center; border:1px solid var(--line); border-radius:8px; padding:10px 15px; color:var(--cream); background:var(--panel); font-weight:700; font-size:.88rem; }
.button.primary { background:var(--orange); border-color:var(--orange); color:#1a1010; }
.hero-frame { border:1px solid var(--line); padding:10px; background:#0b1624; box-shadow:12px 12px 0 rgba(17,34,50,.72); }
.hero-frame img { display:block; width:100%; image-rendering:pixelated; }
.caption { color:var(--muted); font-size:.75rem; margin:10px 2px 0; }
section { padding:72px 0; border-top:1px solid rgba(112,192,193,.15); }
.section-intro { display:flex; align-items:end; justify-content:space-between; gap:20px; margin-bottom:22px; }
.section-intro p { color:var(--muted); max-width:42rem; margin:0; }
.media-strip { display:grid; grid-template-columns:1.25fr .75fr; gap:18px; }
.media-card { border:1px solid var(--line); background:rgba(16,29,44,.84); padding:12px; }
.media-card img { width:100%; display:block; image-rendering:pixelated; }
.media-card .caption { margin-bottom:0; }
.gallery { display:grid; grid-template-columns:repeat(3,1fr); gap:14px; }
.shot { margin:0; background:var(--panel); border:1px solid var(--line); padding:8px; }
.shot a { display:block; background:#08101c; }
.shot img { width:100%; aspect-ratio:16/9; display:block; image-rendering:pixelated; }
.shot figcaption { display:flex; justify-content:space-between; gap:8px; padding:8px 2px 2px; font-size:.78rem; }
.shot figcaption span { color:var(--muted); text-align:right; font-size:.68rem; }
.audio-pair { border:1px solid var(--line); background:rgba(16,29,44,.74); padding:16px; margin:12px 0; }
.pair-head { display:flex; justify-content:space-between; align-items:baseline; gap:12px; margin-bottom:12px; }
.pair-head span, .audio-card small { color:var(--muted); font-size:.73rem; }
.audio-grid { display:grid; grid-template-columns:1fr 1fr; gap:12px; }
.audio-card { background:var(--panel2); padding:12px; min-height:84px; }
.audio-label { color:var(--gold); font-weight:700; margin-bottom:8px; }
audio.audio-hidden { display:none; }
.audio-time {font-variant-numeric:tabular-nums;color:var(--muted);font-size:.8rem;}
.audio-card p {margin:8px 0 0;font-size:.8rem;}
.missing { display:block; color:var(--muted); font-size:.78rem; padding:8px 0; }
.controls { display:grid; grid-template-columns:repeat(2,1fr); gap:12px; }
.control { display:grid; grid-template-columns:58px 1fr; gap:14px; align-items:center; background:var(--panel); border:1px solid var(--line); padding:13px; }
kbd { display:inline-flex; justify-content:center; min-width:42px; padding:7px 5px; border:1px solid #527089; background:#0b1726; color:var(--gold); font-weight:800; border-radius:5px; font-size:.8rem; }
.control span { color:#c6d2d5; font-size:.9rem; }
.columns { display:grid; grid-template-columns:1fr 1fr; gap:18px; }
.panel { border:1px solid var(--line); background:rgba(16,29,44,.76); padding:20px; }
.panel p { color:var(--muted); }
.panel ol { margin:14px 0 0; padding-left:20px; color:#c9d2d5; }
.panel li + li { margin-top:8px; }
.downloads { display:grid; gap:8px; margin-top:12px; }
.download { display:flex; justify-content:space-between; gap:18px; border:1px solid var(--line); background:var(--panel2); padding:11px 13px; color:var(--cream); }
.download span { color:var(--muted); font-size:.72rem; }
.muted { color:var(--muted); }
footer { border-top:1px solid rgba(112,192,193,.18); color:var(--muted); padding:32px 0 48px; font-size:.8rem; }
dialog { border:1px solid var(--line); background:var(--ink); color:var(--cream); padding:10px; max-width:min(94vw,760px); }
dialog::backdrop { background:rgba(0,0,0,.76); }
dialog img { display:block; width:100%; image-rendering:pixelated; }
dialog button { margin-top:8px; background:var(--panel2); color:var(--cream); border:1px solid var(--line); border-radius:5px; padding:7px 10px; }
@media (max-width:820px) { .hero,.media-strip,.columns { grid-template-columns:1fr; } .gallery { grid-template-columns:repeat(2,1fr); } }
@media (max-width:540px) { .wrap { padding:0 15px; } nav ul { gap:9px; font-size:.7rem; } .hero { padding-top:48px; } .gallery,.controls,.audio-grid { grid-template-columns:1fr; } .shot figcaption { display:block; } .shot figcaption span { display:block; text-align:left; margin-top:3px; } .pair-head { display:block; } }
"""


def render_index(
    output_root: Path,
    manifest: dict[str, Any],
    screens: list[dict[str, Any]],
    copied_media: dict[str, str],
    audio_pairs: list[dict[str, Any]],
    releases: list[dict[str, Any]],
    copied_docs: list[str],
) -> str:
    hero = screens[0]
    hero_image = html.escape(hero["native"])
    contact = html.escape(copied_media["contact_sheet"])
    animation = html.escape(copied_media["animation"])
    revision = html.escape(str(manifest.get("source_revision", "uncommitted")))
    generated = html.escape(str(manifest.get("generated_utc", "unknown")))
    public_manifest = {
        "schema_version": 1,
        "site_source_revision": revision,
        "media_generated_utc": generated,
        "hardware_verified": False,
        "screens": screens,
        "contact_sheet": copied_media["contact_sheet"],
        "animation": copied_media["animation"],
        "audio": audio_pairs,
        "release_files": releases,
    }
    (output_root / "media" / "media-manifest.json").write_text(
        json.dumps(public_manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    gallery = render_gallery(screens)
    audio_html = render_audio(audio_pairs)
    release_html = render_releases(releases)
    docs_html = " · ".join(
        f'<a href="{html.escape(path)}">{html.escape(Path(path).name)}</a>' for path in copied_docs
    )
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Cardputer ADV Lofi · Pocket radio</title>
  <meta name="description" content="An offline lofi radio and original pixel-art room for the M5Stack Cardputer ADV.">
  <style>{STYLE}</style>
</head>
<body>
  <header><div class="wrap"><nav><a class="wordmark" href="#top">Pocket Lofi</a><ul><li><a href="#scene">Scene</a></li><li><a href="#audio">Audio</a></li><li><a href="#controls">Controls</a></li><li><a href="#install">Build</a></li></ul></nav></div></header>
  <main id="top">
    <div class="wrap"><section class="hero" style="border-top:0">
      <div><div class="eyebrow">Cardputer ADV · offline radio</div><h1>A small room for long nights.</h1><p class="lead">Pocket Lofi composes a quiet stream on the device while an original pixel-art cat reads beside a rainy window. The built-in scene and music are designed for a 240 × 135 display and no network connection.</p>{evidence_badge()}<div class="hero-actions"><a class="button primary" href="#scene">See the room</a><a class="button" href="#controls">Learn the keys</a></div></div>
      <div class="hero-frame"><img src="{hero_image}" alt="{html.escape(hero['label'])} desktop render at 240 by 135 pixels"><div class="caption">{html.escape(hero['label'])} · native renderer capture · 240×135 · physical display unverified</div></div>
    </section></div>
    <section id="scene"><div class="wrap"><div class="section-intro"><div><div class="eyebrow">Shared renderer evidence</div><h2>Rain, light, and a cat with a pulse.</h2></div><p>The screenshots use the same drawing code compiled for the native preview. They are desktop renders, shown at the device’s native size and an integer nearest-neighbour scale.</p></div>
      <div class="media-strip"><article class="media-card"><img src="{contact}" alt="Labeled contact sheet of public native renderer scenarios"><p class="caption">Public scenario contact sheet · scripted states from the native renderer · hardware unverified.</p></article><article class="media-card"><img src="{animation}" alt="Animated pixel-art room GIF from the shared renderer"><p class="caption">Scene loop · 12 FPS GIF from native renderer frames · no prerecorded video.</p></article></div>
      <details style="margin-top:18px"><summary>Explore all 15 screen states</summary><div class="gallery">{gallery}</div></details>
    </div></section>
    <section id="audio"><div class="wrap"><div class="section-intro"><div><div class="eyebrow">Matched listening tests</div><h2>Two engines, one mood.</h2></div><p>Hear the same composition through two instrument engines. Choose a mood, play a sample, then compare its warmth and rhythm with the other version.</p></div>{audio_html}</div></section>
    <section id="controls"><div class="wrap"><div class="section-intro"><div><div class="eyebrow">Keyboard map</div><h2>Quiet controls stay close.</h2></div><p>Essential actions are one key away, with menus using Enter/Esc and comma, period, slash navigation.</p></div><div class="controls">
      <div class="control"><kbd>SPACE</kbd><span>Play or pause with a short fade.</span></div><div class="control"><kbd>- / =</kbd><span>Lower or raise volume.</span></div><div class="control"><kbd>N</kbd><span>Request the next session at the next musical boundary.</span></div><div class="control"><kbd>M</kbd><span>Choose a mood.</span></div><div class="control"><kbd>F</kbd><span>Toggle the current session favorite.</span></div><div class="control"><kbd>L</kbd><span>Open favorite sessions.</span></div><div class="control"><kbd>V</kbd><span>Toggle the clean scene view.</span></div><div class="control"><kbd>S</kbd><span>Open settings.</span></div><div class="control"><kbd>E</kbd><span>Compare synth and hybrid instruments.</span></div><div class="control"><kbd>H</kbd><span>Open the on-device key guide.</span></div><div class="control"><kbd>ENTER / ESC</kbd><span>Confirm a menu choice or go back.</span></div>
    </div></div></section>
    <section id="install"><div class="wrap"><div class="section-intro"><div><div class="eyebrow">Build and install</div><h2>Build a little radio.</h2></div><p>The preview can be inspected on a desktop today. Physical audio, LCD, battery, key and install behavior still need an ADV run.</p></div><div class="columns"><article class="panel"><h3>Build the preview</h3><ol><li>Build the C++17 native target using the README commands.</li><li>Run the native renderer to create <code>build/screens/*.ppm</code> and <code>build/animation/*.ppm</code>.</li><li>Run <code>python tools/export_media.py</code> with Pillow to create the public PNG/GIF evidence.</li><li>Run <code>python tools/build_site.py</code> to regenerate this offline page.</li></ol><p>Source revision: <code>{revision}</code><br>Media evidence generated: <code>{generated}</code></p></article><article class="panel"><h3>Install path</h3><p>The planned device route is M5Apps → Installer → SD. The baseline application is intended to keep playing after the installation card is removed; this behavior is a physical verification item.</p><p>Optional audio MP3s are host listening evidence only. The development download below includes installation instructions and checksums. Physical verification is still pending.</p><div class="downloads">{release_html}</div></article></div></div></section>
    <section><div class="wrap"><div class="section-intro"><div><div class="eyebrow">Source notes</div><h2>Made to be explored.</h2></div><p>Original room and cat artwork was created with imagegen, then adapted to the LCD palette. Every scene shown here is captured from the shared renderer. This showcase uses local assets without external fonts or tracking scripts.</p></div><div class="panel"><p>Selected project documents: {docs_html or '<span class="muted">none copied</span>'}</p><p class="muted">The native captures are not a hardware certification. Current results and remaining device checks are in VERIFICATION.md.</p></div></div></section>
  </main>
  <footer><div class="wrap">Cardputer ADV Lofi · Pocket Lofi · generated locally · no hosting or deployment performed.</div></footer>
  <dialog id="lightbox"><img alt=""><button type="button">Close</button></dialog>
  <script>
    const box = document.querySelector('#lightbox');
    const image = box.querySelector('img');
    document.querySelectorAll('[data-shot]').forEach(link => link.addEventListener('click', event => {{
      event.preventDefault(); image.src = link.href; image.alt = link.dataset.label || 'Renderer screenshot'; box.showModal();
    }}));
    const formatTime = seconds => `${{Math.floor(seconds / 60)}}:${{String(Math.floor(seconds % 60)).padStart(2, '0')}}`;
    document.querySelectorAll('.audio-card').forEach(card => {{
      const player = card.querySelector('audio'), button = card.querySelector('.play-audio');
      if (!player || !button) return;
      const playLabel = button.getAttribute('aria-label');
      button.addEventListener('click', async () => {{
        if (!player.paused) {{ player.pause(); return; }}
        try {{ await player.play(); }} catch (error) {{ button.textContent = 'Use MP3 download'; }}
      }});
      player.addEventListener('play', () => {{ document.querySelectorAll('audio').forEach(other => {{ if (other !== player) other.pause(); }}); button.textContent = 'Pause'; button.setAttribute('aria-label', playLabel.replace('Play ', 'Pause ')); }});
      player.addEventListener('pause', () => {{ button.textContent = 'Play'; button.setAttribute('aria-label', playLabel); }});
      player.addEventListener('timeupdate', () => card.querySelector('.audio-time').textContent = `${{formatTime(player.currentTime)}} / ${{formatTime(Number.isFinite(player.duration) ? player.duration : 90)}}`);
    }});
    box.querySelector('button').addEventListener('click', () => box.close());
    box.addEventListener('click', event => {{ if (event.target === box) box.close(); }});
  </script>
</body>
</html>
"""


def build_site(media_root: Path, output_root: Path) -> dict[str, Any]:
    media_root = media_root.resolve()
    output_root = output_root.resolve()
    build_root = (ROOT / "build").resolve()
    if not output_root.is_relative_to(build_root) or output_root == build_root:
        raise ValueError("site output must be a dedicated directory beneath project build/")
    if output_root.exists() and any(output_root.iterdir()) and not (output_root / "site-manifest.json").is_file():
        raise ValueError("refusing to replace an existing directory without a site manifest")
    manifest = read_manifest(media_root)
    if output_root.exists():
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True, exist_ok=True)
    screens = public_screens(media_root, output_root, manifest)
    copied_media = copy_common_media(media_root, output_root, manifest)
    audio_pairs = copy_audio(ROOT, output_root)
    copied_docs = copy_docs(ROOT, output_root)
    releases = copy_release(ROOT, output_root)
    index = render_index(output_root, manifest, screens, copied_media, audio_pairs, releases, copied_docs)
    (output_root / "index.html").write_text(index, encoding="utf-8")
    site_manifest = {
        "schema_version": 1,
        "generated_utc": datetime_module.datetime.now(datetime_module.timezone.utc).replace(microsecond=0).isoformat(),
        "source_revision": git_revision(),
        "hardware_verified": False,
        "media_source_manifest": "docs/media/media-manifest.json",
        "public_files": {
            "index": "index.html",
            "media": [copied_media["contact_sheet"], copied_media["animation"], "media/media-manifest.json"]
            + [item["native"] for item in screens]
            + [item["scaled"] for item in screens],
            "audio": [entry["path"] for pair in audio_pairs for entry in pair["engines"] if entry.get("available")],
            "source_docs": copied_docs,
            "release_files": [entry["path"] for entry in releases],
        },
    }
    (output_root / "site-manifest.json").write_text(
        json.dumps(site_manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    return site_manifest


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--media-dir", type=Path, default=MEDIA_ROOT, help="export_media.py output directory")
    parser.add_argument("--output", type=Path, default=OUTPUT_ROOT, help="ignored static output directory")
    return parser.parse_args(list(argv) if argv is not None else None)


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        manifest = build_site(args.media_dir, args.output)
    except (OSError, ValueError) as error:
        print(f"build_site.py: {error}", file=sys.stderr)
        return 1
    print(f"built {manifest['public_files']['index']} and {len(manifest['public_files']['media'])} media files in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
