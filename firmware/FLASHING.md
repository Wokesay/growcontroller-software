# Flashing your own firmware

The hub is yours. You may build the firmware from this repository – changed
or unchanged – and flash it onto your hub. This page says how, and what the
project promises about it.

## The rule: devices do not lock out third-party firmware

Product requirement (PD-022, SD-010):

- **No Secure Boot against the user.** Devices do not lock out
  third-party firmware; users can install their own firmware.
- **Signed updates over the network, your firmware over USB** (PD-043,
  SD-021). Over-the-air updates will be installed only with a valid
  signature, so nobody can push firmware onto your hub over the network.
  Over USB you can always install your own firmware; after that, your own
  key applies to network updates. No eFuses are burned except the HMAC
  key that encrypts stored secrets (SD-024); your firmware can use it too.
- **The source code of every build is linked** in the web app (Settings ›
  Display and info) and on the login page (AGPL §13).

Status 2026-10-07: the firmware is a skeleton (`firmware/README.md`). It
uses no Secure Boot, no flash encryption and no signature check yet, so any
image you build runs.

## What you need

- ESP-IDF v5.4 (CI uses the container `espressif/idf:v5.4.1`):
  https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32s3/get-started/
- Node.js 22.12 or later to build the web app that is embedded in the image.
- A USB cable to the hub's USB port (ESP32-S3 USB-Serial-JTAG).

## Build

First find out which board you have; it is printed on the board and on
the module:

| Board | `GC_BOARD` |
|---|---|
| ESP32-S3-DevKitC-1-N32R16V (32 MB flash, the main board) | `n32r16v` |
| ESP32-S3-DevKitC-1-N8R8 (8 MB flash, the fallback) | `n8r8` |

From the repository root:

```bash
(cd web && npm ci --ignore-scripts && npm run build)
node tools/embed_web.mjs
cd firmware
idf.py -B build-n32r16v -D GC_BOARD=n32r16v -D IDF_TARGET=esp32s3 build
```

For the N8R8 write `n8r8` instead of `n32r16v`, here and below. The image
is `firmware/build-n32r16v/doseph_hub.bin`; bootloader and
partition table are next to it (`bootloader/`, `partition_table/`). Never
flash an image built for one board onto the other: it may start, format
the storage partition and so lose configuration and password for good;
flashing the right image afterwards does not bring them back.

For development you can take the CI artifact `firmware-<board>` of a run
on `main` instead of building: the same images, `flash_args` and their
SHA-256 checksums (`SHA256SUMS`). Runs for a pull request build code that
nobody has reviewed yet; do not flash those. The checksums only show that
the download is complete; they are no signature and do not say who built
it. Flashing from the browser (ESP Web Tools) comes with the first signed
release.

## Flash

1. Connect the hub over USB. On the ESP32-S3's own USB port (the
   carrier board's port, or the DevKit's port marked USB) no button is
   needed: esptool starts download mode by itself. If the hub is not
   detected and your board has BOOT and RESET buttons (development boards
   do), hold BOOT, press and release RESET, then release BOOT (download
   mode). As any computer on either USB port of the DevKit can flash the
   hub without a button (the UART port through its auto-program
   circuit), unplug USB while the hub is in operation (SD-076).
2. Flash and watch the log:

   ```bash
   idf.py -B build-n32r16v -p /dev/ttyACM0 flash monitor     # Windows: -p COM5, macOS: -p /dev/cu.usbmodem*
   ```

   Without the ESP-IDF tooling, esptool works with the offsets ESP-IDF
   wrote to `flash_args`:

   ```bash
   cd firmware/build-n32r16v
   python -m esptool --chip esp32s3 -b 460800 write_flash @flash_args
   ```

   From a CI artifact, check the checksums first, then flash the same
   way in its folder:

   ```bash
   sha256sum -c SHA256SUMS
   python -m esptool --chip esp32s3 -b 460800 write_flash @flash_args
   ```

3. Configuration, password, calibration values and history stay on the
   `storage` and `history` partitions as long as you flash only
   bootloader, partition table, OTA data and app, onto the same board,
   with the partitions in the same places (`firmware/boards/<board>.csv`;
   adding the factory data partition `fctry` moved nothing, SD-076). The
   OTA data makes the hub start the app you flashed. On a
   new or erased hub, setup starts as usual (first password in the web
   app).

Do not erase the whole flash (`idf.py erase-flash`, `esptool
erase_flash`, or "Erase" in a browser installer) unless you mean to: it
also erases `fctry`, which on a pre-flashed hub holds the setup network's
key printed on its label; the label then no longer applies. A factory
reset on the hub (holding BOOT for 15 s and confirming with a short
press, once the firmware has it) keeps it (SD-076, SD-080). Erasing over
USB also skips the factory reset's first step, which switches off the
smart plugs with a water or CO2 role (irrigation and circulation pump,
inlet and inlet pump, humidifier, dehumidifier, CO2, tank heater): they stay as they
are, so switch them off by hand first.

Once secrets are stored in encrypted NVS (SD-024), your own firmware needs
the same NVS encryption setting and HMAC key ID to read them; otherwise it
starts without them (as after a factory reset). With the factory reset of
SD-080 it also needs our key derivation and the small partition that holds
the reset's value; both are documented in the source, so your own builds
work.

## Going back to an official release

Flash the official image of a release the same way, or install it as an
update in the web app once signed releases exist. A hub whose app does not
start cleanly after an update returns to the previous app by itself
(A/B partitions with rollback).

## Passing on your firmware

If you pass on a hub or a firmware image built from changed source code,
the AGPL asks you to offer that source code too (`LICENSE`, README
"License"). The binary-only ESP-IDF libraries may be included without
their source code (`ADDITIONAL_PERMISSION.md`).
