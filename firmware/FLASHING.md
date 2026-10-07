# Flashing your own firmware

The hub is yours. You may build the firmware from this repository – changed
or unchanged – and flash it onto your hub. This page says how, and what the
project promises about it.

## The rule: the hub never locks out your firmware

Product requirement (PD-022, SD-010):

- **No Secure Boot against the owner.** The hub never runs only firmware
  signed by the project. Firmware the owner builds from this source code
  can always be installed.
- **Signed updates stay possible.** Over-the-air updates from the project
  are signed so that nobody else can push firmware onto your hub over the
  network. This protects against attackers, not against the owner: flashing
  over USB always works, and an owner mode for your own signing key is
  planned before the first device ships (`docs/RELEASE.md`).
- **No eFuse that takes the hub away from you.** The project never burns
  eFuses that permanently disable USB download mode, enable flash
  encryption in release mode, or enable Secure Boot with project keys only.
  If Secure Boot or flash encryption is ever used, it must leave the owner
  a documented way to install their own firmware (for example an owner key
  slot); that decision is made before the first device ships
  (`docs/SECURITY_MODEL.md`, checklist item 2).
- **The source code of every release is linked** in the web app (Settings ›
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

3. After the first start the hub sets up as usual (first password in the
   web app). Configuration, calibration values and history stay on the
   `storage` and `history` partitions as long as you flash only app,
   bootloader and partition table with an unchanged `partitions.csv`.

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
