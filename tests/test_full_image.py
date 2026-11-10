import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_full_image import (  # noqa: E402
    PARTITION_TABLE_BYTES, merge_image, parse_partition_table, validate_bootloader,
)
from test_release import make_image  # noqa: E402

DEFAULT_8MB = (
    ("nvs", 1, 2, 0x9000, 0x5000),
    ("otadata", 1, 0, 0xE000, 0x2000),
    ("app0", 0, 0x10, 0x10000, 0x330000),
    ("app1", 0, 0x11, 0x340000, 0x330000),
    ("spiffs", 1, 0x82, 0x670000, 0x180000),
    ("coredump", 1, 3, 0x7F0000, 0x10000),
)


def make_table(rows=DEFAULT_8MB):
    body = b"".join(
        b"\xAA\x50" + struct.pack("<BBII", kind, subtype, offset, size) +
        name.encode().ljust(16, b"\0") + b"\0\0\0\0"
        for name, kind, subtype, offset, size in rows)
    data = body + b"\xEB\xEB" + b"\xFF" * 14 + hashlib.md5(body).digest()
    return data + b"\xFF" * (PARTITION_TABLE_BYTES - len(data))


def make_bootloader(size_nibble=3, chip=9):
    data = bytearray(64)
    data[0], data[1], data[2], data[3] = 0xE9, 3, 2, (size_nibble << 4) | 0xF
    struct.pack_into("<H", data, 12, chip)
    return bytes(data)


class FullImage(unittest.TestCase):
    def test_layout(self):
        application = bytes(make_image())
        image, slot, entries = merge_image(make_bootloader(), make_table(), application)
        self.assertEqual(slot["offset"], 0x10000)
        self.assertEqual(len(entries), 6)
        self.assertEqual(len(image), 0x10000 + len(application))
        self.assertEqual(image[:64], make_bootloader())
        self.assertEqual(image[0x8000:0x8000 + PARTITION_TABLE_BYTES], make_table())
        self.assertEqual(image[0x10000:], application)
        # Gaps, NVS and OTA data are erased flash, so the bootloader starts app0.
        self.assertEqual(set(image[64:0x8000]), {0xFF})
        self.assertEqual(set(image[0x8000 + PARTITION_TABLE_BYTES:0x10000]), {0xFF})

    def test_real_build_outputs_when_present(self):
        build = Path(__file__).resolve().parents[1] / "firmware/.pio/build/lofi-adv"
        names = ("bootloader.bin", "partitions.bin", "firmware.bin")
        if not all((build / name).is_file() for name in names):
            self.skipTest("no PlatformIO build output")
        bootloader, table, application = ((build / name).read_bytes() for name in names)
        image, slot, _ = merge_image(bootloader, table, application)
        self.assertEqual(slot["name"], "app0")
        self.assertEqual(image[slot["offset"]:], application)

    def test_rejects_wrong_bootloader(self):
        for bootloader in (make_bootloader(size_nibble=2), make_bootloader(chip=5),
                           b"\0" * 64, make_bootloader() + b"\0" * 0x8000):
            with self.subTest(length=len(bootloader), head=bootloader[:4].hex()):
                with self.assertRaises(ValueError):
                    validate_bootloader(bootloader)

    def test_rejects_bad_tables(self):
        corrupt = bytearray(make_table())
        corrupt[4] ^= 1
        factory = (("factory", 0, 0, 0x10000, 0x100000),) + DEFAULT_8MB[:2]
        no_otadata = (DEFAULT_8MB[0],) + DEFAULT_8MB[2:]
        overlap = DEFAULT_8MB[:3] + (("app1", 0, 0x11, 0x300000, 0x330000),)
        too_big = DEFAULT_8MB[:5] + (("coredump", 1, 3, 0x7F0000, 0x20000),)
        for label, table in (("md5", bytes(corrupt)), ("short", make_table()[:-32]),
                             ("empty", b"\xFF" * PARTITION_TABLE_BYTES)):
            with self.subTest(label=label):
                with self.assertRaises(ValueError):
                    parse_partition_table(table)
        for label, rows in (("factory", factory), ("no otadata", no_otadata),
                            ("overlap", overlap), ("beyond flash", too_big)):
            with self.subTest(label=label):
                with self.assertRaises(ValueError):
                    merge_image(make_bootloader(), make_table(rows), bytes(make_image()))

    def test_rejects_application_larger_than_its_slot(self):
        small = DEFAULT_8MB[:2] + (("app0", 0, 0x10, 0x10000, 0x100),)
        with self.assertRaises(ValueError):
            merge_image(make_bootloader(), make_table(small), bytes(make_image()))


if __name__ == "__main__":
    unittest.main()
