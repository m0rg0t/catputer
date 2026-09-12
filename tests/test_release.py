import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build_release import (  # noqa: E402
    APP_DESC_MAGIC,
    MAXIMUM,
    ROOT,
    build_identity,
    build_release,
    ensure_fresh,
    sha,
    validate_image,
)


def make_image(identity="cardputer-adv-lofi/application/0.1.0-dev"):
    payload = bytearray(384)
    struct.pack_into("<I", payload, 0, APP_DESC_MAGIC)
    payload[16:16 + len(b"fixture-version")] = b"fixture-version"
    payload[48:48 + len(b"fixture-project")] = b"fixture-project"
    marker = identity.encode() + b"\0"
    payload[272:272 + len(marker)] = marker

    data = bytearray(24)
    data[0] = 0xE9
    data[1] = 1
    data[12] = 9
    data[23] = 1
    data += struct.pack("<II", 0x3C000020, len(payload)) + payload
    checksum = 0xEF
    for value in payload:
        checksum ^= value
    checksum_at = (len(data) // 16 + 1) * 16 - 1
    data.extend(b"\0" * (checksum_at + 1 - len(data)))
    data[checksum_at] = checksum
    return data + hashlib.sha256(data).digest()


class ImageValidation(unittest.TestCase):
    def test_good_application(self):
        metadata = validate_image(make_image(), build_identity("0.1.0-dev"))
        self.assertEqual(metadata["project_name"], "fixture-project")
        self.assertEqual(metadata["app_version"], "fixture-version")

    def test_corruption(self):
        for offset in (0, 1, 12, 23, 28, 32, 48, 416, 430):
            with self.subTest(offset=offset):
                data = make_image()
                data[offset] ^= 0x80
                with self.assertRaises(ValueError):
                    validate_image(data, build_identity("0.1.0-dev"))

    def test_bounds_and_identity(self):
        for data in (b"", make_image()[:-1], make_image() + b"\0", b"\0" * (MAXIMUM + 1)):
            with self.assertRaises(ValueError):
                validate_image(data)
        with self.assertRaisesRegex(ValueError, "identity"):
            validate_image(make_image("some-other-project/0.1.0"), build_identity("0.1.0-dev"))

    def test_real_bootloader_is_not_an_application(self):
        bootloader = ROOT / "firmware/.pio/build/lofi-adv/bootloader.bin"
        if not bootloader.is_file():
            self.skipTest("PlatformIO bootloader artifact is not present")
        with self.assertRaisesRegex(ValueError, "application descriptor"):
            validate_image(bootloader.read_bytes())


class ReleasePackaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        for folder in (
            "firmware/.pio/build/lofi-adv",
            "firmware/include/lofi",
            "firmware/src/audio",
            "firmware/src/platform",
            "assets/audio/manifests",
            "tools",
            "docs",
        ):
            (self.root / folder).mkdir(parents=True, exist_ok=True)
        (self.root / "VERSION").write_text("0.1.0-dev\n")
        (self.root / "firmware/platformio.ini").write_text("[env:lofi-adv]\n")
        (self.root / "tools/configure_firmware.py").write_text("# fixture\n")
        (self.root / "firmware/include/lofi/example.h").write_text("// fixture\n")
        (self.root / "firmware/include/lofi/music.h").write_text(
            "constexpr std::uint32_t kMusicSchemaVersion = 1;\n"
            "constexpr std::uint8_t kMusicVoiceCapacity = 8;\n"
        )
        (self.root / "firmware/src/platform/main.cpp").write_text("// fixture\n")
        bank_id = "local-v1-aabbccddeeff"
        (self.root / "assets/audio/manifests/local-v1.json").write_text(
            json.dumps({"bank_id": bank_id}) + "\n"
        )
        (self.root / "firmware/src/audio/sample_bank_data.inc").write_text(
            f'static constexpr char kBuiltinSampleBankId[] = "{bank_id}";\n'
        )
        (self.root / "docs/INSTALL.md").write_text("install\n")
        (self.root / "docs/THIRD_PARTY.md").write_text("credits\n")
        self.binary = self.root / "firmware/.pio/build/lofi-adv/firmware.bin"
        self.binary.write_bytes(make_image())
        newest = max(path.stat().st_mtime_ns for path in self.root.rglob("*") if path.is_file())
        os.utime(self.binary, ns=(newest + 1_000_000, newest + 1_000_000))

    def tearDown(self):
        self.temporary.cleanup()

    def test_archive_allowlist_and_hashes(self):
        archive, manifest = build_release(self.root)
        self.assertEqual(manifest["generation_schema"],1)
        self.assertEqual(manifest["voice_capacity"],8)
        expected = {
            "cardputer-lofi-0.1.0-dev.bin",
            "INSTALL.md",
            "THIRD_PARTY.md",
            "manifest.json",
            "SHA256SUMS",
        }
        with zipfile.ZipFile(archive) as package:
            self.assertEqual(set(package.namelist()), expected)
            sums = {}
            for line in package.read("SHA256SUMS").decode().splitlines():
                digest, filename = line.split("  ", 1)
                sums[filename] = digest
            self.assertEqual(set(sums), expected - {"SHA256SUMS"})
            for filename, digest in sums.items():
                self.assertEqual(sha(package.read(filename)), digest)
            archived_manifest = json.loads(package.read("manifest.json"))
        self.assertEqual(archived_manifest, manifest)
        self.assertEqual(manifest["sample_bank"], "local-v1-aabbccddeeff")
        latest = json.loads((self.root / "dist/latest.json").read_text())
        self.assertEqual(latest["archive_sha256"], sha(archive.read_bytes()))

    def test_refuses_noncanonical_binary(self):
        other = self.root / "other.bin"
        other.write_bytes(make_image())
        with self.assertRaisesRegex(ValueError, "canonical"):
            build_release(self.root, other)

    def test_refuses_unknown_release_entry_without_deleting_it(self):
        destination = self.root / "dist/cardputer-lofi-0.1.0-dev"
        destination.mkdir(parents=True)
        foreign = destination / "private-state.bin"
        foreign.write_bytes(b"keep")
        with self.assertRaisesRegex(ValueError, "unexpected entry"):
            build_release(self.root)
        self.assertEqual(foreign.read_bytes(), b"keep")

    def test_version_and_platform_config_must_not_be_newer_than_binary(self):
        for relative in ("VERSION", "firmware/platformio.ini"):
            with self.subTest(path=relative):
                path = self.root / relative
                old = path.stat().st_mtime_ns
                newer = self.binary.stat().st_mtime_ns + 1_000_000
                os.utime(path, ns=(newer, newer))
                with self.assertRaisesRegex(ValueError, "changed after"):
                    ensure_fresh(self.root, self.binary)
                os.utime(path, ns=(old, old))


if __name__ == "__main__":
    unittest.main()
