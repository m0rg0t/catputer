# Standalone full image

Target: **M5Stack Cardputer ADV** (ESP32-S3FN8, 8 MB flash). The original Cardputer is not supported.

`cardputer-lofi-<version>-full.bin` is a whole-flash image for a device that should start Catputer directly, with no launcher. It holds the bootloader, the partition table and the application.

> **This replaces what is on the device.** Writing it at offset `0x0` overwrites the bootloader, the partition table and the first application slot. An M5Apps or M5Launcher installation and the apps inside it stop being reachable. To get them back, flash that launcher again with its own tool (for example M5Burner). If you use M5Apps and want to keep it, use the ordinary app package and [INSTALL.md](INSTALL.md) instead.

**Status: one physical test.** On 2026-11-11 the 0.1.12-dev image was written to one Cardputer ADV with esptool at `0x0`, esptool verified the written data, and the owner reported that startup, music, keys, animation, SD settings and a power cycle all worked. That is a hands-on report from one device; timing and long sessions from this image were not measured. It is built from the same application as the M5Apps package, and its layout is checked on the build machine.

## What is inside

| Offset | Content |
| --- | --- |
| `0x0` | ESP32-S3 bootloader (DIO, 80 MHz, 8 MB) |
| `0x8000` | Partition table: Arduino `default_8MB` (nvs, otadata, app0, app1, spiffs, coredump) |
| `0x9000`–`0xFFFF` | Erased (`0xFF`): empty NVS and OTA data, so the bootloader starts `app0` |
| `0x10000` | The Catputer application, the same bytes as the app-only BIN |

The image ends with the application. Flash beyond it is not written.

## Writing it

1. Verify the download: `shasum -a 256 -c SHA256SUMS` in the extracted folder.
2. Put the Cardputer ADV in download mode: hold **G0** while switching it on or pressing reset, with USB connected.
3. Write the image at offset `0x0` with a tool you trust. With esptool:

   ```sh
   esptool --chip esp32s3 --port <your-port> write_flash 0x0 cardputer-lofi-<version>-full.bin
   ```

   Check the port first; writing to the wrong serial device can damage another board. Browser flashers based on ESP Web Tools and M5Burner's custom-firmware option also write a single image at `0x0`.
4. Reset the device. Start at low volume.

This project ships no flashing script and its build tools never write to a device.

## Differences from the M5Apps install

- There is no launcher to return to. The app's controls are the same; see [INSTALL.md](INSTALL.md).
- Settings and favorites still live on the optional SD card, not in flash.
- Updating means writing a newer full image the same way.
