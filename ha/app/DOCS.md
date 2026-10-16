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
  <https://github.com/Wokesay/growcontroller-software/blob/main/docs/HOME_ASSISTANT.md>.
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
4. Start it and choose **Open web UI** (German: **Weboberfläche öffnen**).
   Switch on **Show in sidebar** to find it in Home Assistant's sidebar
   from then on. growcontroller opens inside Home Assistant and uses your
   Home Assistant login: there is no password of its own to set. Only
   Home Assistant's administrators can open it (with Supervisor 2026.10.2
   or newer; an older one only hides it from the others' sidebar).
5. A short introduction opens: name, language and colour scheme, one
   sensor per measurement, and per area whether the app is to do it
   automatically one day or it stays outside the control (in Home
   Assistant or manually). The app switches nothing yet, whatever you
   choose. You can skip it and open it again under **Settings**. Sensors
   are also under **Devices › Assignment**. A pH probe that reports its
   voltage (mV or V) is calibrated there in the app with pH 7 and pH 4
   buffers (**Kalibrieren**); set `force_update: true` on it and no
   calibration filter on the node. A sensor the app does not
   suggest is under **Show other sensors**, and its unit must fit the
   measurement.
6. **Optional: open it directly,** for example from a bookmark or on a
   phone on the wall without Home Assistant's app:
   1. In growcontroller, opened from the sidebar, open **Settings ›
      Access** (German: Einstellungen › Zugang) and set a password for
      direct access. Never use your Home Assistant password: on port 8099
      it travels unencrypted.
   2. In Home Assistant open **Settings → Apps → growcontroller →
      Configuration** (German: Konfiguration). Under **Network**
      (Netzwerk) choose **⋮ → Show disabled ports** (Deaktivierte Ports
      anzeigen), enter `8099` and save.
   3. Open `http://homeassistant.local:8099` or
      `http://<IP address of Home Assistant>:8099` and sign in with that
      password. Other names (your own domain, Home Assistant Cloud) are
      refused, which protects against websites that try to reach devices
      in your network. Until a password for direct access is set, the
      port shows only a pointer to the sidebar.

There is nothing to configure: the app reaches Home Assistant through the
Supervisor and gets its access from it. No token is needed.

## Your data

- growcontroller keeps its data (configuration, history, events, the hash
  of the password for direct access if you set one) in the app's own
  folder. Home Assistant's backups
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

- Inside Home Assistant, growcontroller is protected by your Home
  Assistant login. Secure that login: use multi-factor authentication,
  and set a limit for failed sign-ins in Home Assistant's HTTP settings
  (by default Home Assistant bans no address after failed sign-ins). Use
  HTTPS for Home Assistant if you reach it from outside your home.
- Port 8099 is closed unless you open it (or saved the app's network
  settings before, which keeps it as it was). If you open it for direct
  access, it is plain HTTP: use it inside your own network only. Never
  forward the port to the internet, and check that your router does not
  open the Home Assistant host to the internet over IPv6 (an "exposed
  host" or "open all ports" setting). On a shared or open Wi-Fi, others
  could read the password on its way, so never use your Home Assistant
  password for direct access.
- To close the port again, clear 8099 under **Configuration → Network**
  and save; to end direct access altogether, remove its password in
  growcontroller under **Settings › Access**. If you came from version
  0.1.0-proto.4 or earlier, your old growcontroller password is now the
  password for direct access: if you are not sure who set it, open
  growcontroller from Home Assistant's sidebar and change or remove it
  under **Settings › Access**.
- The access the app gets from the Supervisor is not limited to reading:
  it is the access of Home Assistant's own administrator. growcontroller
  only reads the states with it, but a flaw in growcontroller's web server
  could let an attacker in your network control Home Assistant. That is
  why the direct port belongs in your own network only.
- The image holds nothing but the growcontroller server, the web app and
  the license texts: no shell, no package manager.
- The server starts with administrator rights (root) only to hand its
  data folder to an account of its own. Before it opens its ports it
  switches to that account and cannot switch back: no root, no other
  special right. Home Assistant also confines it with the app's AppArmor
  profile (on hosts with AppArmor, such as Home Assistant OS): it reads
  its web app, writes only its data folder and makes only TCP
  connections.
- If the server cannot give up root, it does not start: the web app does
  not open, and the newest lines of the app's **Log** say why. Your data
  is kept. Start the app again. If it still does not start, restore the
  backup from before the update (see **Your data**) and report the log
  lines at <https://github.com/Wokesay/growcontroller-software/issues>.
  Do not uninstall the app to fix it: that deletes its data.

## Source code and license

AGPL-3.0-or-later. The source code of exactly this version is linked
under Settings and on every page of port 8099, and the license texts are
in the image under `/licenses`. Problems and ideas:
<https://github.com/Wokesay/growcontroller-software/issues>.
