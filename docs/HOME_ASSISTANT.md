# Home Assistant (read-only spike)

A trial of running the growcontroller software next to Home Assistant
instead of on its own hardware: Home Assistant stays the device layer
(ESPHome nodes, sensors, smart plugs), and the hub's core reads the
sensors from it. Whether the product goes this way is a product question
for the product repository; this spike only shows whether it works.

**What it does:** it reads Home Assistant's states through its REST API
every few seconds and finds the sensors it can use: pH, EC, temperature,
humidity, CO2 and level, recognised by Home Assistant's own device class
and unit. In the web app (Devices › Assignment) you choose one sensor per
measurement; one tap picks it, adds it as a device and assigns it. The
values go through the usual sensor truth (stale, implausible, jump lock; a
missing value is never 0). Nobody has to write a list of entity IDs.

**What it does not do:**

- It switches nothing. Every run of a pump and every switch command is
  refused, so dosing, refill, circulation and the climate outputs stay off.
  It sends Home Assistant nothing but `GET /api/states` (below the path in
  the address, if it has one; tested).
- pH, EC and level are shown but are **no values for control**: they were
  calibrated outside the hub, and the hub has not checked that calibration
  (RAT-025). Their tiles say "nur Anzeige" (display only) and "Außerhalb
  des Hubs kalibriert – vom Hub nicht geprüft" (calibrated outside the hub,
  not checked by the hub); stale, implausible and jumping values are still
  marked as such. They get no standstill ("frozen") check: what arrives is
  a rounded, calibrated value, not a raw signal, and a steady tank is no
  fault (RAT-059); a sensor that falls silent goes stale. Temperature,
  humidity and CO2 need no calibration and are valid readings.

## As a Home Assistant app

On Home Assistant OS or Supervised (a Raspberry Pi 4 or 5 with a 64-bit
system, or a PC) it installs from Home Assistant's app store, formerly
the add-on store (SD-034):

1. **Settings → Apps → store → ⋮ → Repositories**, add
   `https://github.com/Wokesay/growcontroller-software`.
2. Install **growcontroller**, start it, **Open web UI** (port 8099).
3. Set a password, then choose the sensors in Devices › Assignment (step
   6 below).

Nothing is configured and no token is made: `gc_ha_server --app` reaches
Home Assistant at `http://supervisor/core` with the Supervisor's token
(`SUPERVISOR_TOKEN`, granted by `homeassistant_api` in
`ha/app/config.yaml`), keeps its data in the app's folder (`/data/hub`,
part of Home Assistant's backups) and serves the web app on port 8099 of
the Home Assistant host. Home Assistant downloads a ready-made image of
about 3 MB (`ghcr.io/wokesay/growcontroller-ha`, built and published by
the release workflow for the version in `ha/app/config.yaml`,
`docs/RELEASE.md`). The image is `FROM scratch`: the static server, the
web app and the license texts, no shell. The app's own documentation in
the store is `ha/app/DOCS.md`.

## Run it on a computer of your own

1. Build the software (`README.md`, "Build it yourself") and the web app.
2. In Home Assistant create a user for this trial **without admin rights**,
   and for that user a long-lived access token (profile, at the bottom).
   The token gives that user's rights for ten years and Home Assistant has
   no read-only tokens; revoke it when the trial ends.
3. Save the token in a file only you can read (`chmod 600 ha-token`; on
   Windows, a file in your own user folder). Never paste it into chats,
   issues or repositories.
4. Write a file with Home Assistant's address:

   ```json
   {"url": "http://192.168.1.20:8123"}
   ```

   Use the IP address of Home Assistant, not `homeassistant.local`: the
   token goes to whatever answers for that name, and `.local` names can be
   answered by any device in the network. The address may carry a plain
   path in front of Home Assistant's API (letters, digits, `-`, `_`), as an
   add-on reaches it: `{"url": "http://supervisor/core"}` reads
   `http://supervisor/core/api/states`. The name `supervisor` is meant only
   inside an add-on, where the Supervisor's own network answers it and its
   token is used; elsewhere use the IP. Leave out `/api` (the hub adds
   `/api/states`). The host must be a plain name, an IPv4 address or an
   IPv6 address in `[ ]`: no `user@`, `?` or `#`, so the token goes only to
   the host you read in the address. If you prefer, the file can also
   list the entities (`"entities": [{"entity": "sensor.grow_ph",
   "measures": "ph"}]`, `measures` one of `ph`, `ec`, `water_temp`,
   `level`, `air_temp`, `humidity`, `co2`); they are then used from the
   start.
5. Start it on a computer in your network, as a normal user (not root),
   with a new, empty data folder:

   ```bash
   build/gc_ha_server --config mapping.json --token-file ha-token --data ha-data --web web/dist
   # → http://127.0.0.1:8090
   ```

   Other options: `--port` (8090), `--host` (127.0.0.1), `--every S`
   (seconds between two reads, 5). Problems with Home Assistant (not
   reachable, token refused, a unit it cannot use) are printed in the
   terminal. When Home Assistant refuses the token, reading stops until
   you restart the server: Home Assistant counts every failed login and
   bans the computer after a few, however slowly they come.
6. Open the address and set a password. The setup for the hub's own
   dosing hardware is not offered on a read-only hub; the overview points
   to Devices › Assignment. There, choose a sensor for each measurement:
   the list shows each sensor's Home Assistant name, its entity ID and its
   current value. Can't tell two sensors apart? Warm one in your hand and
   watch its value rise. Name and time zone are under Settings.

## What the sensors in Home Assistant need

- **Home Assistant 2024.4 or later**, so a state carries `last_reported`,
  which every report updates even when the value stays the same [1].
  The hub dates each value by its last report; a value that is not
  reported again goes stale after the capability's limit (60 s for pH, EC
  and level, 120 s for temperature, humidity and CO2), even though Home
  Assistant still answers. On an older version a steady value goes stale,
  which is safe but useless.
- **A report interval of 10 s or less for pH, EC and level.** ESPHome's
  EZO sensors read every 60 s by default [2], the same as the freshness
  limit, so readings would flip between valid and stale.
- **`force_update: true` only if needed:** if a steady value goes stale
  although the sensor reports, set it on the ESPHome or MQTT sensor
  (default off [2]). It makes Home Assistant record every reading, so its
  database grows.
- **Map the sensor itself, not a rounding template:** a `round()` template
  sensor only writes when its source changes, so a steady value would go
  stale (assumption from how templates work, not tested here).
- **Availability instead of fallback numbers:** template sensors written
  as `| float(0)` turn "unavailable" into 0, which the hub cannot tell from
  a real 0 (RAT-006). Give them an `availability` template instead.
- **Units:** see below. Without a unit only pH is taken.

Two things to know while it runs:

- **After a Home Assistant restart** restored states carry the restart
  time, so an old value looks fresh until the freshness limit runs out.
- **Calibrating in Home Assistant** is not announced to the hub: buffer
  tests show up as jumps. Calibrate only while the hub is in maintenance
  mode.

Sources (retrieved 2026-10-09):
[1] Home Assistant developer blog, "New state timestamp State.last_reported",
2024-03-20,
https://developers.home-assistant.io/blog/2024/03/20/state_reported_timestamp;
the version 2024.4 is inferred from that date (assumption, not confirmed
from release notes).
[2] ESPHome source: `esphome/components/ezo/sensor.py` (polling interval
"60s") and `esphome/components/sensor/__init__.py` (`force_update`
default false), https://github.com/esphome/esphome (branch `dev`).

## Before any of this may feed control

What the hub would need before pH, EC or level from Home Assistant may
steer anything. Each point is open; none is built in this spike.

1. **The hub's own check, with a date.** Not a calibration: the hub
   compares the values with references. pH with two buffers, the slope
   inside the band the hub accepts for its own calibration (RAT-025); EC
   with one reference solution; level with two known volumes.
2. **The calibration state from the node where it has one.** An EZO pH
   circuit reports how many points it was calibrated with (`Cal,?`; at
   least two, RAT-025) and its slope, which can be mapped as an entity and
   checked against the same band.
3. **A calibration change in Home Assistant voids the check,** seen as a
   change of that entity or as a jump. The reading goes back to "not
   checked".
4. **EC temperature compensation:** an EZO-EC circuit assumes 25 °C unless
   it is sent the water temperature; uncompensated EC is off by roughly
   2 % per degree (a typical value, not measured here). Who compensates,
   and how, is a product question.
5. **Galvanic isolation** of pH and EC probes (the hub's heads have it;
   many DIY builds do not). Interference gives wrong but plausible values;
   an acceptance test compares readings with the circulation pump on and
   off (RAT-044, "Deviation").
6. **Probe location and smoothing:** a probe in a sump or return line, or
   smoothing in Home Assistant, adds delay. The waits after a dose must
   cover it: at least twice the smoothing window, or the time to tolerance
   measured on the tank (RAT-052, RAT-058).
7. **The hub is the only one switching** pumps that dose or move water;
   another automation in Home Assistant would be a second controller next
   to it. Each pump stops on its own (see below).
8. **How old a calibration may be,** and when to recalibrate, has no rule
   yet; that is a product question.
9. **Fresh and stuck values:** after a Home Assistant restart a restored
   value needs a change or a second report before it counts; and a probe
   stuck on one value while still reporting needs a check of its own,
   since the hub's standstill check judges a raw signal (RAT-023, RAT-059)
   and gets none from Home Assistant.

## Units

| Measure | Taken as it is | Converted |
|---|---|---|
| pH | no unit, `pH` | |
| EC | `mS/cm` | `µS/cm` ÷ 1000 |
| Water and air temperature | `°C` | `°F` |
| Level | `L` | `%` is not supported yet (needs the tank's shape) |
| Humidity | `%` | |
| CO2 | `ppm` | |

A missing unit (except for pH) or another unit gives no value; the
terminal names the entity and the reason. A state of `unknown` is no
value; `unavailable` takes the device offline. A state without a readable
time is no value.

## How it fits the code

- `ha/ha_bus.*`: the hub's device interface (`IBus`) over Home Assistant
  states; one device per entity, device classes `ha_ph`, `ha_ec`, … added
  to the built-in catalog at start-up (the product catalog stays as it is).
- The device classes are marked `externalCalibration` in code, never from
  catalog data, so the catalog cannot loosen a check (R7). The sensor truth
  shows such pH, EC and level values but keeps them unusable for control;
  the resolver says so instead of asking for a calibration, and the hub
  refuses its own calibration for such devices.
- `ha/ha_client.*`: reads all states in one request (`GET <path>/api/states`)
  with the token in its own thread, so the hub's tick never waits for the
  network. An answer is at most 16 MB and 10 s; redirects are not
  followed, so the token never goes to another host. A refused token
  (401/403) stops reading until restart.
- The answer is read event by event (`parseStates`, a SAX reader): only
  sensors with a valid entity ID are built, and of them only `entity_id`,
  `state`, the report times and the attributes `device_class`,
  `unit_of_measurement`, `state_class` and `friendly_name`; people,
  locations and everything else are never kept (their few fields pass
  through one scratch object and are dropped when the state ends). Nesting
  deeper than 32 levels or an answer that is no list stops the read, and at
  most 20 000 sensors are kept, so a large or crafted answer costs time and
  memory only in proportion to its sensors. A name longer than 256 bytes is
  cut; any other kept field longer than that is dropped (the ID then makes
  the state invalid, a value or time counts as missing), because a cut could
  turn an invalid ID into a valid one or change a value.
- Of the states, the bus keeps only the candidates: sensors whose device
  class or unit says pH (`ph`, unit `pH`), EC (`conductivity`, `µS/cm`,
  `mS/cm`), temperature (`temperature`, `°C`, `°F`), humidity (`humidity`
  with `%`), CO2 (`carbon_dioxide`) or level (`volume_storage` or `volume`,
  in `L`, not adding up like a meter); at most 100 of each kind, so device
  temperatures cannot crowd out the tank's pH. `%`, `ppm` and `L` alone
  also stand for batteries, VOC or water meters, so they need the device
  class. A candidate whose unit the hub does not convert exactly (TDS in
  ppm, EC in mS/m, temperature in K) is listed without a value and says
  why; no factor is assumed (RAT-006, RAT-015). Everything else (people,
  locations, switches) is dropped at once, never kept or logged. Only
  picked sensors become devices.
- What the hub cannot tell: whether a sensor with the right unit sits in
  the right place. A soil sensor's EC or a pool's pH looks like the tank's;
  the picker says so, and the name and live value are the guard. pH, EC
  and level stay display only (RAT-025); temperature, humidity and CO2 are
  used as valid readings, which matters once anything is switched.
- `ha/ha_assign.*`: one step from the web app, `POST /api/v1/ha/assign`
  with a measuring role and an entity: select the entity, accept the device
  under its Home Assistant name, bind the role; an empty entity takes it
  away again (unbind, remove, stop reading). A new sensor is bound before
  the role's former one is dropped, so a failed change keeps the old one;
  if any part fails, nothing this call added stays behind.
  `GET /api/v1/ha/candidates` lists the candidates with their value, their
  value and unit in Home Assistant, a unit problem, the measure a picked
  one serves and the connection state (`starting`, `ok`, `unreachable`,
  `refused`), so the web app can tell "no sensors" from "Home Assistant not
  answering". Both need a signed-in session.
- The picks live in the hub's configuration (devices `ha.<entity>` of
  class `ha_<measure>`), saved at once after each pick; at start they are
  handed back to the bus. There is no second file to get out of step.
- Text from Home Assistant is cut and cleaned before it is shown, without
  control or invisible format characters: a unit in a fault to 32 bytes, a
  sensor's name to 60 in the list (40 as the device's name).
- A report's age is measured in Home Assistant's own time (its `Date`
  header minus `last_reported`), so the two computers' clocks need not
  agree; the same report keeps the time it got when first seen.
- `ha/main.cpp`: the server (`gc_ha_server`), following the simulator's.
  `--app` sets what the app needs (above) and refuses `--config` and
  `--token-file`; the Supervisor's token is read from the environment and
  removed from it at once, like `GC_HA_TOKEN`.
- `ha/app/`: the app (`config.yaml`, `Dockerfile`, store texts);
  `repository.yaml` at the root makes the repository an app repository.
  `.github/workflows/app.yml` builds the image for `amd64` and `aarch64`,
  each on its own architecture, and starts it as the Supervisor would
  (labels, answer, web app, a foreign host name refused, clean stop on
  SIGTERM, private data folder), on every code change and for a release;
  `tools/app.test.mjs` keeps `config.yaml`, the server, the Dockerfile and
  the release workflow in step.
- `web/src/ha.tsx`: the picker in Devices › Assignment and the overview's
  notice on a Home Assistant hub.

## Limits and open points

- Plain `http://` only (the build has no TLS); use it inside your own
  network, ideally on the Home Assistant host itself or over a wire. The
  token travels in the clear there.
- Time is the computer's clock and counts as secured (assumption: its
  system keeps the time synced).
- The data folder holds the hub's password hash; keep it private
  (`chmod 700`).
- **Writes to the data folder (#68):** a password, a configuration change,
  a picked sensor and a STOP are on disk when the call returns. History and
  events are appended, and written whole only at the start and once a day
  (`docs/HISTORY.md`). An idle server hands about 90 KB of data an hour to
  the disk, about 2 MB an hour at the file system, so an SD card is fine.
  A hard power cut loses up to about 30 s of history and events (appended
  without `fsync`), never a STOP or a password. The server does not start
  with a data folder it cannot write; a full card is said as an alarm in
  the event log, and the files are written again once there is space.
- No switching yet. Dosing through Home Assistant needs pumps that stop on
  their own (a run time on the device, for example ESPHome `ezo_pmp` or a
  script with a maximum on-time), because a lost "off" over Wi-Fi must
  never keep a pump running; and the hub must be the only one switching
  them. That is the next design step, after a product decision.
- The HTTP code of `ha/main.cpp` follows `sim/main.cpp`; a shared module
  is a follow-up.
- **The app's web app is plain HTTP on port 8099 of the Home Assistant
  host,** reachable by every device in your network; the growcontroller
  password travels in the clear there. Home Assistant's ingress (its own
  login and HTTPS) would need the web app to work below a path; that is a
  follow-up.
- **The Supervisor's token is not read-only:** Home Assistant has no
  read-only access for apps, so the app could call any of Home Assistant's
  API. growcontroller sends only `GET /api/states` (tested); the image has
  no shell and the app no other rights (`tools/app.test.mjs` refuses any
  other key in `config.yaml`).
- The app store reads `ha/app/config.yaml` from `main`: between merging a
  new version and the release workflow's end, Home Assistant offers a
  version whose image is not there yet, and an install fails until it is.
