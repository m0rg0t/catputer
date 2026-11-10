#!/usr/bin/env python3
"""Create a standalone whole-flash image for a Cardputer ADV without M5Apps.

The image holds the bootloader, the partition table and the application at
the offsets the partition table declares, padded with 0xFF. Writing it at
flash offset 0 REPLACES whatever is installed, including an M5Apps launcher
and its applications. This tool only builds and checks files: it never talks
to a device. The ordinary app-only package stays ``tools/build_release.py``.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

try:
    from .build_release import (
        ROOT, build_identity, ensure_fresh, music_profile, read_version,
        sample_bank_id, sha, source_hash, validate_image, _refuse_unknown_entries,
    )
except ImportError:
    from build_release import (
        ROOT, build_identity, ensure_fresh, music_profile, read_version,
        sample_bank_id, sha, source_hash, validate_image, _refuse_unknown_entries,
    )

BOOTLOADER_OFFSET = 0x0          # ESP32-S3 second-stage bootloader.
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_BYTES = 0xC00
FLASH_BYTES = 8 * 1024 * 1024    # ESP32-S3FN8.
ESP32S3_CHIP_ID = 9
FLASH_SIZE_8MB = 3               # High nibble of bootloader header byte 3.
APP_TYPE, DATA_TYPE = 0, 1
OTA_0, OTADATA = 0x10, 0x00


def parse_partition_table(data):
    """Return the entries of a binary ESP-IDF partition table, MD5-checked."""
    if len(data) != PARTITION_TABLE_BYTES:
        raise ValueError("Unexpected partition table size")
    entries, digest = [], hashlib.md5()
    for offset in range(0, len(data), 32):
        record = data[offset:offset + 32]
        if record[:2] == b"\xAA\x50":
            kind, subtype, start, size = struct.unpack_from("<BBII", record, 2)
            name = record[12:28].split(b"\0", 1)[0].decode("ascii")
            entries.append({"name": name, "type": kind, "subtype": subtype,
                            "offset": start, "size": size})
            digest.update(record)
        elif record[:2] == b"\xEB\xEB":
            if record[16:] != digest.digest():
                raise ValueError("Partition table MD5 mismatch")
            if data[offset + 32:].strip(b"\xFF"):
                raise ValueError("Unexpected data after the partition table")
            break
        else:
            raise ValueError("Partition table has no MD5 record")
    else:
        raise ValueError("Partition table has no MD5 record")
    if not entries:
        raise ValueError("Partition table is empty")
    previous_end = PARTITION_TABLE_OFFSET + PARTITION_TABLE_BYTES
    for entry in sorted(entries, key=lambda item: item["offset"]):
        if entry["offset"] < previous_end or entry["offset"] + entry["size"] > FLASH_BYTES:
            raise ValueError("Partition overlaps or exceeds 8 MB flash: " + entry["name"])
        previous_end = entry["offset"] + entry["size"]
    return entries


def validate_bootloader(data):
    if len(data) < 24 or len(data) > PARTITION_TABLE_OFFSET - BOOTLOADER_OFFSET:
        raise ValueError("Bootloader does not fit before the partition table")
    if data[0] != 0xE9 or struct.unpack_from("<H", data, 12)[0] != ESP32S3_CHIP_ID:
        raise ValueError("Expected an ESP32-S3 bootloader image")
    if data[3] >> 4 != FLASH_SIZE_8MB:
        raise ValueError("Bootloader is not configured for 8 MB flash")


def merge_image(bootloader, partition_table, application):
    """Bootloader + partition table + application in ota_0, 0xFF elsewhere.

    The otadata region stays erased (0xFF), which makes the bootloader start
    ota_0. The image ends with the application, so later partitions on the
    device are left as they were.
    """
    validate_bootloader(bootloader)
    entries = parse_partition_table(partition_table)
    apps = [entry for entry in entries if entry["type"] == APP_TYPE]
    first = [entry for entry in apps if entry["subtype"] == OTA_0]
    if len(first) != 1 or any(entry["subtype"] < OTA_0 for entry in apps):
        raise ValueError("Expected exactly one ota_0 application partition and no factory app")
    if not any(entry["type"] == DATA_TYPE and entry["subtype"] == OTADATA for entry in entries):
        raise ValueError("Partition table has no otadata partition")
    slot = first[0]
    if len(application) > slot["size"]:
        raise ValueError("Application does not fit its partition")
    image = bytearray(b"\xFF" * (slot["offset"] + len(application)))
    image[BOOTLOADER_OFFSET:BOOTLOADER_OFFSET + len(bootloader)] = bootloader
    image[PARTITION_TABLE_OFFSET:PARTITION_TABLE_OFFSET + len(partition_table)] = partition_table
    image[slot["offset"]:] = application
    return bytes(image), slot, entries


def build_full_image(root=ROOT):
    root = Path(root).resolve()
    build = root / "firmware/.pio/build/lofi-adv"
    version = read_version(root)
    application = (build / "firmware.bin").read_bytes()
    image_metadata = validate_image(application, build_identity(version))
    ensure_fresh(root, build / "firmware.bin")
    image, slot, entries = merge_image(
        (build / "bootloader.bin").read_bytes(), (build / "partitions.bin").read_bytes(), application)

    name = "cardputer-lofi-" + version + "-full"
    destination = root / "dist" / name
    allowed = {name + ".bin", "INSTALL_FULL.md", "THIRD_PARTY.md", "manifest.json", "SHA256SUMS"}
    _refuse_unknown_entries(destination, allowed)
    destination.mkdir(parents=True, exist_ok=True)
    files = {
        name + ".bin": image,
        "INSTALL_FULL.md": (root / "docs/INSTALL_FULL.md").read_bytes(),
        "THIRD_PARTY.md": (root / "docs/THIRD_PARTY.md").read_bytes(),
    }
    manifest = {
        "version": version,
        "status": "development_candidate",
        "target": "ESP32-S3FN8 / Cardputer ADV, 8 MB flash",
        "hardware_verified": False,
        "installation_tested": False,
        "app_only": False,
        "replaces_launcher": True,
        "write_offset": "0x0",
        "image_bytes": len(image),
        "application_offset": hex(slot["offset"]),
        "application_bytes": len(application),
        "application_sha256": sha(application),
        "image_project_name": image_metadata["project_name"],
        "image_app_version": image_metadata["app_version"],
        "build_identity": build_identity(version),
        "partitions": [
            {"name": entry["name"], "offset": hex(entry["offset"]), "size": hex(entry["size"])}
            for entry in entries
        ],
        "source_sha256": source_hash(root),
        "sample_bank": sample_bank_id(root),
        **music_profile(root),
        "files": {filename: {"bytes": len(content), "sha256": sha(content)} for filename, content in files.items()},
        "note": "Writing this image replaces the installed launcher and applications.",
    }
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    files["SHA256SUMS"] = "".join(
        f"{sha(content)}  {filename}\n" for filename, content in files.items()).encode()
    for filename, content in files.items():
        (destination / filename).write_bytes(content)
    archive = root / "dist" / (name + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
        for filename, content in files.items():
            package.writestr(filename, content)
    with zipfile.ZipFile(archive) as package:
        if sorted(package.namelist()) != sorted(files):
            raise ValueError("Full-image archive member allowlist mismatch")
    return archive, manifest


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    try:
        archive, manifest = build_full_image(ROOT)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
    print(f"{archive}: {manifest['image_bytes']:,} bytes, write at {manifest['write_offset']}; "
          f"app at {manifest['application_offset']}; replaces the installed launcher")


if __name__ == "__main__":
    main()
