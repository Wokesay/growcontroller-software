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
- Node.js 22 to build the web app that is embedded in the image.
- A USB cable to the hub's USB port (ESP32-S3 USB-Serial-JTAG).

## Build

From the repository root:

```bash
(cd web && npm ci --ignore-scripts && npm run build)
node tools/embed_web.mjs
cd firmware
idf.py set-target esp32s3
idf.py build
```

The image is `firmware/build/growcontroller_hub.bin`; bootloader and
partition table are next to it (`build/bootloader/`,
`build/partition_table/`).

## Flash

1. Connect the hub over USB. If it is not detected and your board has
   BOOT and RESET buttons (development boards do), hold BOOT, press and
   release RESET, then release BOOT (download mode).
2. Flash and watch the log:

   ```bash
   idf.py -p /dev/ttyACM0 flash monitor     # Windows: -p COM5, macOS: -p /dev/cu.usbmodem*
   ```

   Without the ESP-IDF tooling, esptool works with the offsets ESP-IDF
   wrote to `build/flash_args`:

   ```bash
   cd firmware/build
   python -m esptool --chip esp32s3 -b 460800 write_flash @flash_args
   ```

3. Configuration, password, calibration values and history stay on the
   `storage` and `history` partitions as long as you flash only app,
   bootloader and partition table with an unchanged `partitions.csv`. On a
   new or erased hub, setup starts as usual (first password in the web
   app).

Once secrets are stored in encrypted NVS (SD-024), your own firmware needs
the same NVS encryption setting and HMAC key ID to read them; otherwise it
starts without them (as after a factory reset).

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
