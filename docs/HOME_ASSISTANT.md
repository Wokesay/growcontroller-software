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

**What it does not do:** it switches nothing. Every run of a pump and every
switch command is refused, so dosing, refill, circulation and the climate
outputs stay off. It does not write to Home Assistant.

## Run it

1. Build the software (`README.md`, "Build it yourself") and the web app.
2. In Home Assistant create a long-lived access token (your profile, at
   the bottom: "Long-lived access tokens"). Save it in a file that only you
   can read, for example `chmod 600 ha-token`. The token gives the same
   rights as your user; keep it off chats and repositories.
3. Write a mapping file:

   ```json
   {
     "url": "http://homeassistant.local:8123",
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
   `humidity`, `co2`.
4. Start it on a computer in your network:

   ```bash
   build/gc_ha_server --config mapping.json --token-file ha-token --web web/dist
   # → http://127.0.0.1:8090
   ```

   `--data DIR` sets where the hub keeps its files
   (`growcontroller-ha-data`), `--port`, `--host` and `--every S` (seconds
   between two reads, 5) work as for the simulator.
5. Open the address, set a password, accept the devices under Devices.

## Units

| Measure | Taken as it is | Converted |
|---|---|---|
| pH | no unit, `pH` | |
| EC | `mS/cm` | `µS/cm` ÷ 1000 |
| Water and air temperature | `°C` | `°F` |
| Level | `L` | `%` is not supported yet (needs the tank's shape) |
| Humidity | `%` | |
| CO2 | `ppm` | |

An entity with another unit shows as a device with a fault and gives no
value. A state of `unknown` is no value; `unavailable` takes the device
offline.

## How it fits the code

- `ha/ha_bus.*`: the hub's device interface (`IBus`) over Home Assistant
  states; one device per entity, device classes `ha_ph`, `ha_ec`, … added
  to the built-in catalog at start-up (the product catalog stays as it is).
- The device classes are marked `externalCalibration`: Home Assistant (for
  example an Atlas EZO circuit in ESPHome) calibrates the sensor, so the
  sensor truth takes the value as it is instead of asking for its own
  calibration. All other checks still apply.
- `ha/ha_client.*`: reads `GET /api/states/<entity>` with the token in its
  own thread, so the hub's tick never waits for the network.
- `ha/main.cpp`: the server (`gc_ha_server`), following the simulator's.
- A sample's time is the entity's `last_reported` (newer Home Assistant) or
  `last_updated`, whichever is later; the sensor truth uses it for "stale".

## Limits and open points

- Plain `http://` only (the build has no TLS); use it inside your own
  network. The token travels in the clear there.
- Time is the computer's clock and counts as secured (assumption: its
  system keeps the time synced).
- No switching yet. Dosing through Home Assistant needs pumps that stop on
  their own (a run time on the device, for example ESPHome `ezo_pmp` or a
  script with a maximum on-time), because a lost "off" over Wi-Fi must
  never keep a pump running. That is the next design step, after a product
  decision.
- Packaging as a Home Assistant add-on comes later, if the product goes
  this way.
