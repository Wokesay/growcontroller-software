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
- pH, EC and level are shown but are **no values for control**: Home
  Assistant calibrated them, and the hub has not checked that calibration
  (RAT-025). They read "In Home Assistant kalibriert – vom Hub nicht
  geprüft". Temperature, humidity and CO2 need no calibration and are valid
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
   terminal.
6. Open the address, set a password, accept the devices under Devices.

## What the sensors in Home Assistant need

- **Home Assistant 2024.3 or later**, so a state carries `last_reported`.
  The hub dates each value by its last report; a value that is not
  reported again goes stale after the capability's limit (60 s for pH, EC
  and level, 120 s for temperature, humidity and CO2), even though Home
  Assistant still answers.
- **ESPHome sensors with `force_update: true`**, otherwise Home Assistant
  drops repeated identical values and a steady reading looks stuck.
- **A report interval of 10 s or less for pH, EC and level** (ESPHome's
  EZO default is 60 s, the same as the freshness limit, so readings would
  flip between valid and stale).
- **Availability instead of fallback numbers:** template sensors written
  as `| float(0)` turn "unavailable" into 0, which the hub cannot tell from
  a real 0 (RAT-006). Give them an `availability` template instead.
- **Units:** see below. Without a unit only pH is taken.

Points to settle before any of this may feed control later:

- **EC temperature compensation:** an EZO-EC circuit assumes 25 °C unless
  it is sent the water temperature; uncompensated EC reads several percent
  low per degree below that. Who compensates, and how, is open.
- **Galvanic isolation** of pH and EC probes (the hub's heads have it; many
  DIY builds do not). Interference gives wrong but plausible values.
- **Probe location and smoothing:** a probe in a sump or return line, or
  smoothing in Home Assistant, adds delay the hub's wait times do not know.
- **Other automations** in Home Assistant that dose or pump would be a
  second controller next to the hub.
- **Calibrating in Home Assistant** is not announced to the hub: buffer
  tests show up as jumps. Calibrate only while the hub is in maintenance
  mode.

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
  goes to another host. A refused token (401/403) waits five minutes
  before the next try, so Home Assistant does not ban the computer.
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
- Before pH, EC or level from Home Assistant may feed control, the hub
  needs its own check of them (reference buffers or volumes, with a date),
  and a calibration change in Home Assistant must void that check.
- The HTTP code of `ha/main.cpp` follows `sim/main.cpp`; a shared module
  is a follow-up.
- Packaging as a Home Assistant add-on comes later, if the product goes
  this way.
