# Home Assistant (read-only spike)

A trial of running the growcontroller software next to Home Assistant
instead of on its own hardware: Home Assistant stays the device layer
(ESPHome nodes, sensors, smart plugs), and the hub's core reads the
sensors from it. Whether the product goes this way is a product question
for the product repository; this spike only shows whether it works.

**What it does:** it reads mapped sensor entities from Home Assistant's
REST API every few seconds, turns them into readings with the usual sensor
truth (stale, frozen, implausible, jump lock; a missing value is never 0)
and serves the web app. The setup finds the entities as devices; measuring
roles assign themselves.

**What it does not do:**

- It switches nothing. Every run of a pump and every switch command is
  refused, so dosing, refill, circulation and the climate outputs stay off.
  It sends Home Assistant nothing but `GET /api/states/<entity>` (tested).
- pH, EC and level are shown but are **no values for control**: they were
  calibrated outside the hub, and the hub has not checked that calibration
  (RAT-025). They read "Außerhalb des Hubs kalibriert – vom Hub nicht
  geprüft"; stale, frozen, implausible and jumping values are still marked
  as such. Temperature, humidity and CO2 need no calibration and are valid
  readings.

## Run it

1. Build the software (`README.md`, "Build it yourself") and the web app.
2. In Home Assistant create a user for this trial **without admin rights**,
   and for that user a long-lived access token (profile, at the bottom).
   The token gives that user's rights for ten years and Home Assistant has
   no read-only tokens; revoke it when the trial ends.
3. Save the token in a file only you can read (`chmod 600 ha-token`; on
   Windows, a file in your own user folder). Never paste it into chats,
   issues or repositories.
4. Write a mapping file:

   ```json
   {
     "url": "http://192.168.1.20:8123",
     "entities": [
       {"entity": "sensor.grow_ph", "measures": "ph"},
       {"entity": "sensor.grow_ec", "measures": "ec"},
       {"entity": "sensor.grow_water_temp", "measures": "water_temp"},
       {"entity": "sensor.tent_temperature", "measures": "air_temp"},
       {"entity": "sensor.tent_humidity", "measures": "humidity"}
     ]
   }
   ```

   `measures` is one of `ph`, `ec`, `water_temp`, `level`, `air_temp`,
   `humidity`, `co2`. Use the IP address of Home Assistant, not
   `homeassistant.local`: the token goes to whatever answers for that name,
   and `.local` names can be answered by any device in the network.
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
6. Open the address, set a password, accept the devices under Devices.

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
  although the sensor reports, set it on the ESPHome sensor (default off
  [2]). It makes Home Assistant record every reading, so its database
  grows.
- **Availability instead of fallback numbers:** template sensors written
  as `| float(0)` turn "unavailable" into 0, which the hub cannot tell from
  a real 0 (RAT-006). Give them an `availability` template instead.
- **Units:** see below. Without a unit only pH is taken.

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
- `ha/ha_client.*`: reads `GET /api/states/<entity>` with the token in its
  own thread, so the hub's tick never waits for the network. Each answer is
  at most 64 KB and 10 s; redirects are not followed, so the token never
  goes to another host. A refused token (401/403) stops reading until
  restart. Text from Home Assistant in a fault (a unit) is cut to 32
  printable bytes.
- A report's age is measured in Home Assistant's own time (its `Date`
  header minus `last_reported`), so the two computers' clocks need not
  agree; the same report keeps the time it got when first seen.
- `ha/main.cpp`: the server (`gc_ha_server`), following the simulator's.

## Limits and open points

- Plain `http://` only (the build has no TLS); use it inside your own
  network, ideally on the Home Assistant host itself or over a wire. The
  token travels in the clear there.
- Time is the computer's clock and counts as secured (assumption: its
  system keeps the time synced).
- The data folder holds the hub's password hash; keep it private
  (`chmod 700`).
- No switching yet. Dosing through Home Assistant needs pumps that stop on
  their own (a run time on the device, for example ESPHome `ezo_pmp` or a
  script with a maximum on-time), because a lost "off" over Wi-Fi must
  never keep a pump running; and the hub must be the only one switching
  them. That is the next design step, after a product decision.
- The HTTP code of `ha/main.cpp` follows `sim/main.cpp`; a shared module
  is a follow-up.
- Packaging as a Home Assistant add-on comes later, if the product goes
  this way.
