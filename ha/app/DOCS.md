<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# growcontroller

growcontroller's own web app, running next to Home Assistant. Home
Assistant stays the device layer (ESPHome nodes, sensors); the app reads
the sensors from it and shows them with growcontroller's checks.

## What it does and does not do

- It reads Home Assistant's states every 5 seconds and finds the sensors
  it can use: pH, EC, temperature, humidity, CO2 and level, recognised by
  their device class and unit.
- It **switches nothing**: no pump, no plug, no light. It asks Home
  Assistant only for the list of states.
- pH, EC and level are shown for display only: they were calibrated
  outside growcontroller, which has not checked that calibration.
- It is experimental: a trial of the growcontroller software on Home
  Assistant, not the finished product.

## Install

1. In Home Assistant open **Settings → Apps** (older versions: Add-ons),
   then the store, the menu at the top right, **Repositories**, and add
   `https://github.com/Wokesay/growcontroller-software`.
2. Open **growcontroller** in the store and install it. Home Assistant
   downloads a ready-made image (about 3 MB) for your Raspberry Pi
   (64-bit) or PC.
3. Start it and choose **Open web UI**, or open
   `http://<address of Home Assistant>:8099` in your browser.
4. Set a password for growcontroller. It is growcontroller's own login,
   separate from Home Assistant's.
5. Open **Devices › Assignment** and choose one sensor per measurement.

There is nothing to configure: the app reaches Home Assistant through the
Supervisor and gets its access from it. No token is needed.

## Your data

- growcontroller keeps its data (configuration, history, events, the hash
  of its password) in the app's own folder. Home Assistant's backups
  include it.
- Uninstalling the app may delete that folder; keep a backup if you want
  it later.

## Security

- The web app is plain HTTP on port 8099. Use it inside your own network
  only; never forward the port to the internet. To close it, clear the
  port under the app's **Network** settings (the web app is then not
  reachable at all).
- The app's access to Home Assistant comes from the Supervisor and is not
  limited to reading; growcontroller uses it only to read the states.
- The image holds nothing but the growcontroller server, the web app and
  the license texts: no shell, no package manager.

## Source code and license

AGPL-3.0-or-later. The source code of exactly this version is linked on
the web app's sign-in page and under Settings, and the license texts are
in the image under `/licenses`. Problems and ideas:
<https://github.com/Wokesay/growcontroller-software/issues>.
