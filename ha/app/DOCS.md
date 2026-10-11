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
  Assistant only for the list of states. That is why there is no
  emergency stop: the top bar shows **Display only** (German: **Nur
  Anzeige**) in its place. In an emergency, switch off in Home Assistant
  or at the device itself, or pull the plug.
- It learns how often each sensor reports. A value shows as **Outdated**
  only when its sensor has missed about three of its usual reports: never
  sooner than 5 min, and at the latest after 30 min without a report.
  Until the app has seen a sensor report a few times (after a start, or
  after you pick another sensor), the limit is 10 min.
- A sensor that reports only when its value changes shows as outdated
  once its value stays the same longer than that. Then let it report at
  least every 10 min (ESPHome or MQTT: `force_update: true`). Reports that
  keep the same value count only from Home Assistant 2024.4 on.
- A sensor that Home Assistant marks as unavailable shows as **Not
  responding** at once.
- pH, EC and level were calibrated in Home Assistant, which growcontroller
  has not checked; their tiles say so. They are recorded in the history,
  but never used for control.
- It is experimental: a trial of the growcontroller software on Home
  Assistant, not the finished product.

## Install

1. Experimental apps show in the store only in advanced mode: in Home
   Assistant open your **profile** and switch on **Advanced mode**.
2. Open **Settings → Apps** (older versions: Add-ons), then the store, the
   menu at the top right, **Repositories**, and add
   `https://github.com/Wokesay/growcontroller-software`.
3. Open **growcontroller** in the store and install it. Home Assistant
   downloads a ready-made image of a few MB for your Raspberry Pi (64-bit)
   or PC.
4. Start it and choose **Open web UI**, or open
   `http://<address of Home Assistant>:8099` in your browser. Use Home
   Assistant's IP address or `homeassistant.local`; other names (your own
   domain, Home Assistant Cloud) are refused, which protects against
   websites that try to reach devices in your network.
5. **Set a password for growcontroller right away.** It is
   growcontroller's own login, separate from Home Assistant's. Until it is
   set, anyone in your network who opens the page first could set it. If
   the page asks you to sign in although you never set a password,
   someone else did: uninstall the app and install it again.
6. A short introduction opens: name, language and colour scheme, one
   sensor per measurement, and per area whether the app is to do it
   automatically one day or it stays outside the control (in Home
   Assistant or manually). The app switches nothing yet, whatever you
   choose. You can skip it and open it again under **Settings**. Sensors
   are also under **Devices › Assignment**; a sensor the app does not
   suggest is under **Show other sensors**, and its unit must fit the
   measurement.

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
  only. Never forward the port to the internet, and check that your
  router does not open the Home Assistant host to the internet over IPv6
  (an "exposed host" or "open all ports" setting). On a shared or open
  Wi-Fi, others could read the password on its way.
- To close the port, clear it under the app's **Network** settings. The
  web app is then not reachable from your network (Home Assistant and
  other apps on the same host can still reach it).
- The access the app gets from the Supervisor is not limited to reading:
  it is the access of Home Assistant's own administrator. growcontroller
  only reads the states with it, but a flaw in growcontroller's web server
  could let an attacker in your network control Home Assistant. That is
  why the port belongs in your own network only.
- The image holds nothing but the growcontroller server, the web app and
  the license texts: no shell, no package manager.

## Source code and license

AGPL-3.0-or-later. The source code of exactly this version is linked on
the web app's sign-in page and under Settings, and the license texts are
in the image under `/licenses`. Problems and ideas:
<https://github.com/Wokesay/growcontroller-software/issues>.
