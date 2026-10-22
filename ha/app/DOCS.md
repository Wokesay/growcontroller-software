<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Doseph

Doseph's own web app, running next to Home Assistant. Home
Assistant stays the device layer (ESPHome nodes, sensors); the app reads
the sensors from it and shows them with Doseph's checks.

## What it does and does not do

- It reads Home Assistant's states every 5 seconds and finds the sensors
  it can use: pH, EC, temperature, humidity, CO2 and level, recognised by
  their device class and unit.
- It **switches nothing**: no pump, no plug, no light. It asks Home
  Assistant only for the list of states. That is why there is no
  Stop button: the top bar shows **Display only** (German: **Nur
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
  keep the same value count only from Home Assistant 2024.4 on. Better: a
  sign of life (Devices › Assignment, "Lebenszeichen"), which tells the
  app when the sensor last measured; then a value is outdated 60 s after
  its last measurement. An ESPHome example is in the section "Sign of
  life" of
  <https://github.com/Wokesay/doseph/blob/main/docs/HOME_ASSISTANT.md>.
- A sensor that Home Assistant marks as unavailable shows as **Not
  responding** at once.
- pH, EC and level were calibrated in Home Assistant, which Doseph
  has not checked; their tiles say so. They are recorded in the history,
  but never used for control.
- It is experimental: a trial of the Doseph software on Home
  Assistant, not the finished product.

## Install

1. Experimental apps show in the store only in advanced mode: in Home
   Assistant open your **profile** and switch on **Advanced mode**.
2. Open **Settings → Apps** (older versions: Add-ons), then the store, the
   menu at the top right, **Repositories**, and add
   `https://github.com/Wokesay/doseph`.
3. Open **Doseph** in the store and install it. Home Assistant
   downloads a ready-made image of a few MB for your Raspberry Pi (64-bit)
   or PC.
4. Start it and choose **Open web UI**, or open
   `http://<address of Home Assistant>:8099` in your browser. Use Home
   Assistant's IP address or `homeassistant.local`; other names (your own
   domain, Home Assistant Cloud) are refused, which protects against
   websites that try to reach devices in your network.
5. **Set a password for Doseph.** It is Doseph's own
   login, separate from Home Assistant's. The page first asks for a
   **setup code**: open Settings → Apps → Doseph → **Log** and take
   the newest line "Setup code for the first password: …" (upper or lower
   case, dashes optional). Only Home Assistant's administrators, and apps
   they gave that right, can read the log, so nobody else in your network
   can set the password first. Do not share the code. It works once; every
   restart of the app makes a new one.
6. A short introduction opens: language, name and colour scheme, one
   sensor per measurement, and per area whether the app is to do it
   automatically one day or it stays outside the control (in Home
   Assistant or manually). The app switches nothing yet, whatever you
   choose. You can skip it and open it again under **Settings**. Sensors
   are also under **Devices › Assignment**. A pH probe that reports its
   voltage (mV or V) is calibrated there in the app with pH 7 and pH 4
   buffers (**Calibrate**); set `force_update: true` on it and no
   calibration filter on the node. A sensor the app does not
   suggest is under **Show other sensors**, and its unit must fit the
   measurement.

There is nothing to configure: the app reaches Home Assistant through the
Supervisor and gets its access from it. No token is needed.

## Coming from growcontroller

Up to 0.1.0-proto.4 this app was called growcontroller. Home Assistant
treats Doseph as a new app: the old one gets no more updates, also no
security fixes, your settings do not move over by themselves, and the two
should not run at the same time (both use port 8099). Moving takes a few
minutes:

1. Open the old app: Settings → Apps → growcontroller → **Open web UI**.
   In its web UI, open **Settings › Data › Back up settings** (German:
   Einstellungen › Daten › Einstellungen sichern). Your browser saves the
   file. It holds devices, sensor assignments, recipes, bottles and
   calibrations, not the password. To keep readings, also save **History
   (CSV, 7 days)** (German: Verlauf (CSV, 7 Tage)); older history and the
   events stay behind.
2. Stop the old app: Settings → Apps → growcontroller → **Stop**, and
   switch off **Start on boot**.
3. Install **Doseph** from the store. It is in the same repository, so you
   add nothing. Start it and set its password (Install, steps 4 and 5).
4. When the introduction opens, choose **Skip** (German: Überspringen).
   Then open **Settings › Data › Restore settings** (German: Einstellungen
   › Daten › Einstellungen laden) and choose the file.
5. Under **Devices › Assignment** (German: Geräte › Zuordnung), check that
   each measurement shows its sensor with a current value.
6. Uninstall the old app. This deletes its data, its history included.

Home Assistant reports the old app as removed from its repository and
offers to uninstall it. Accept that only after step 1: uninstalling
deletes the old app's data. Until you uninstall it, you can go back by
stopping Doseph and starting the old app again.

## Your data

- Doseph keeps its data (configuration, history, events, the hash
  of its password) in the app's own folder. Home Assistant's backups
  include it.
- Uninstalling the app deletes that folder; keep a backup if you want it
  later.
- Before you update the app, make a backup of it. Home Assistant offers
  this in the update dialog; check that it is switched on. The app is
  experimental, and a new version can have faults.
- If a new version does not work, restore that backup under **Settings →
  System → Backups**. What changed since the backup is lost. Installing
  the app again does not help: it installs the same new version. Keep
  automatic updates for the app off (they are off unless you switched
  them on); otherwise Home Assistant can install the faulty version again
  after the restore.

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
  it is the access of Home Assistant's own administrator. Doseph
  only reads the states with it, but a flaw in Doseph's web server
  could let an attacker in your network control Home Assistant. That is
  why the port belongs in your own network only.
- The image holds nothing but the Doseph server, the web app and
  the license texts: no shell, no package manager.
- The server starts with administrator rights (root) only to hand its
  data folder to an account of its own. Before it opens its port it
  switches to that account and cannot switch back: no root, no other
  special right. Home Assistant also confines it with the app's AppArmor
  profile (on hosts with AppArmor, such as Home Assistant OS): it reads
  its web app, writes only its data folder and makes only TCP
  connections.
- If the server cannot give up root, it does not start: the web app does
  not open, and the newest lines of the app's **Log** say why. Your data
  is kept. Start the app again. If it still does not start, restore the
  backup from before the update (see **Your data**) and report the log
  lines at <https://github.com/Wokesay/doseph/issues>.
  Do not uninstall the app to fix it: that deletes its data.

## Source code and license

AGPL-3.0-or-later. The source code of exactly this version is linked on
the web app's sign-in page and under Settings, and the license texts are
in the image under `/licenses`. Problems and ideas:
<https://github.com/Wokesay/doseph/issues>.
