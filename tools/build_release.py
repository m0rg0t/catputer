#!/usr/bin/env python3
"""Create an app-only, allowlisted development package. Never flashes hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MAXIMUM = 0x140000
APP_DESC_MAGIC = 0xABCD5432
IDENTITY_PREFIX = "cardputer-adv-lofi/application/"
SAFE_VALUE = re.compile(r"^[0-9a-z.-]+$")


def read_version(root=ROOT):
    version = (root / "VERSION").read_text().strip()
    if not version or not SAFE_VALUE.fullmatch(version):
        raise ValueError("Unsafe version")
    return version


def build_identity(version):
    return IDENTITY_PREFIX + version


def _descriptor_text(data, offset, size, label):
    raw = data[offset:offset + size]
    value = raw.split(b"\0", 1)[0]
    try:
        return value.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError(f"Invalid UTF-8 in application {label}") from error


def validate_image(data, expected_identity=None):
    if len(data) < 48 or len(data) > MAXIMUM:
        raise ValueError("Image outside compact app size bounds")
    if data[0] != 0xE9 or not 1 <= data[1] <= 16 or struct.unpack_from("<H", data, 12)[0] != 9:
        raise ValueError("Expected an ESP32-S3 image")

    cursor, checksum = 24, 0xEF
    first_payload = None
    first_size = 0
    for segment in range(data[1]):
        if cursor + 8 > len(data):
            raise ValueError("Truncated segment header")
        _, size = struct.unpack_from("<II", data, cursor)
        cursor += 8
        if size > len(data) - cursor:
            raise ValueError("Truncated image segment")
        if segment == 0:
            first_payload, first_size = cursor, size
        for byte in data[cursor:cursor + size]:
            checksum ^= byte
        cursor += size

    checksum_at = (cursor // 16 + 1) * 16 - 1
    if checksum_at >= len(data) or data[checksum_at] != checksum:
        raise ValueError("Image checksum mismatch")
    image_end = checksum_at + 1
    if data[23] != 1 or len(data) != image_end + 32:
        raise ValueError("Expected an appended SHA-256 and no unknown trailing content")
    if hashlib.sha256(data[:image_end]).digest() != data[image_end:]:
        raise ValueError("Image SHA-256 mismatch")

    # ESP-IDF places esp_app_desc_t at the start of the first application
    # segment. Bootloaders are valid ESP images too, but do not have this.
    if first_payload is None or first_size < 256 or struct.unpack_from("<I", data, first_payload)[0] != APP_DESC_MAGIC:
        raise ValueError("Expected an ESP32-S3 application descriptor")
    metadata = {
        "app_version": _descriptor_text(data, first_payload + 16, 32, "version"),
        "project_name": _descriptor_text(data, first_payload + 48, 32, "project name"),
    }
    if expected_identity is not None:
        marker = expected_identity.encode("ascii") + b"\0"
        if marker not in data:
            raise ValueError("Project identity marker missing from application image")
    return metadata


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_inputs(root=ROOT):
    paths = [
        root / "VERSION",
        root / "firmware/platformio.ini",
        root / "assets/audio/manifests/local-v1.json",
        root / "tools/configure_firmware.py",
    ]
    paths += sorted(
        path
        for folder in ("firmware/include", "firmware/src")
        for path in (root / folder).rglob("*")
        if path.is_file()
    )
    missing = [path for path in paths if not path.is_file()]
    if missing:
        raise ValueError("Missing build input: " + missing[0].relative_to(root).as_posix())
    return paths


def source_hash(root=ROOT):
    digest = hashlib.sha256()
    for path in build_inputs(root):
        digest.update(path.relative_to(root).as_posix().encode() + b"\0" + path.read_bytes())
    return digest.hexdigest()


def sample_bank_id(root=ROOT):
    manifest_path = root / "assets/audio/manifests/local-v1.json"
    try:
        manifest = json.loads(manifest_path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("Invalid local sample-bank manifest") from error
    bank_id = manifest.get("bank_id")
    if not isinstance(bank_id, str) or not bank_id or not SAFE_VALUE.fullmatch(bank_id):
        raise ValueError("Unsafe sample-bank ID")
    generated = (root / "firmware/src/audio/sample_bank_data.inc").read_text()
    declaration = f'static constexpr char kBuiltinSampleBankId[] = "{bank_id}";'
    if declaration not in generated:
        raise ValueError("Sample-bank manifest and generated firmware data disagree")
    return bank_id


def ensure_fresh(root, binary):
    newest = max(path.stat().st_mtime_ns for path in build_inputs(root))
    if newest > binary.stat().st_mtime_ns:
        raise ValueError("Firmware build inputs changed after the BIN; rebuild before packaging")


def _refuse_unknown_entries(destination, allowed):
    if not destination.exists():
        return
    unknown = sorted(path.name for path in destination.iterdir() if path.name not in allowed)
    if unknown:
        raise ValueError("Release directory contains unexpected entry: " + unknown[0])


def build_release(root=ROOT, bin_path=None):
    root = Path(root).resolve()
    canonical_bin = (root / "firmware/.pio/build/lofi-adv/firmware.bin").resolve()
    selected_bin = canonical_bin if bin_path is None else Path(bin_path).resolve()
    if selected_bin != canonical_bin:
        raise ValueError("Only the canonical lofi-adv application artifact may be packaged")

    version = read_version(root)
    data = selected_bin.read_bytes()
    image_metadata = validate_image(data, build_identity(version))
    ensure_fresh(root, selected_bin)
    bank_id = sample_bank_id(root)

    name = "cardputer-lofi-" + version
    destination = root / "dist" / name
    allowed = {name + ".bin", "INSTALL.md", "THIRD_PARTY.md", "manifest.json", "SHA256SUMS"}
    _refuse_unknown_entries(destination, allowed)
    destination.mkdir(parents=True, exist_ok=True)
    files = {
        name + ".bin": data,
        "INSTALL.md": (root / "docs/INSTALL.md").read_bytes(),
        "THIRD_PARTY.md": (root / "docs/THIRD_PARTY.md").read_bytes(),
    }
    manifest = {
        "version": version,
        "status": "development_candidate",
        "target": "ESP32-S3 / Cardputer ADV",
        "hardware_verified": False,
        "installation_tested": False,
        "app_only": True,
        "image_project_name": image_metadata["project_name"],
        "image_app_version": image_metadata["app_version"],
        "build_identity": build_identity(version),
        "max_app_bytes": MAXIMUM,
        "app_bytes": len(data),
        "headroom_bytes": MAXIMUM - len(data),
        "source_sha256": source_hash(root),
        "sample_bank": bank_id,
        "engines": ["synth", "hybrid"],
        "files": {filename: {"bytes": len(content), "sha256": sha(content)} for filename, content in files.items()},
        "note": "Capacity target is not proof of compatibility with a particular installed launcher layout.",
    }
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    files["SHA256SUMS"] = "".join(
        f"{sha(content)}  {filename}\n" for filename, content in files.items()
    ).encode()
    for filename, content in files.items():
        (destination / filename).write_bytes(content)

    archive = root / "dist" / (name + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
        for filename, content in files.items():
            package.writestr(filename, content)
    with zipfile.ZipFile(archive) as package:
        if sorted(package.namelist()) != sorted(files):
            raise ValueError("Release archive member allowlist mismatch")
        for filename, content in files.items():
            if package.read(filename) != content:
                raise ValueError("Release archive content mismatch: " + filename)

    latest = {
        **manifest,
        "directory": name,
        "archive": archive.name,
        "archive_sha256": sha(archive.read_bytes()),
    }
    (root / "dist/latest.json").write_text(json.dumps(latest, indent=2) + "\n")
    return archive, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, default=ROOT / "firmware/.pio/build/lofi-adv/firmware.bin")
    args = parser.parse_args()
    try:
        archive, manifest = build_release(ROOT, args.bin)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
    print(
        f"{archive}: {manifest['app_bytes']:,}/{MAXIMUM:,} app bytes; "
        f"SHA-256 {manifest['files'][archive.stem + '.bin']['sha256']}"
    )


if __name__ == "__main__":
    main()
