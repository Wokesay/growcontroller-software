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
  the address, if it has one; tested). So the web app shows no emergency
  stop, which would stop nothing; in its place "Nur Anzeige" (display only)
  says to switch off in Home Assistant or at the device (SD-040).
- pH, EC and level are shown but are **no values for control**: they were
  calibrated outside the hub, and the hub has not checked that calibration
  (RAT-025). Their tiles say "anderswo kalibriert" (calibrated elsewhere)
  and "Außerhalb des Hubs kalibriert – vom Hub nicht geprüft" (calibrated
  outside the hub, not checked by the hub); stale, implausible and jumping values are still
  marked as such. They get no standstill ("frozen") check: what arrives is
  a rounded, calibrated value, not a raw signal, and a steady tank is no
  fault (RAT-059); a sensor that falls silent goes stale. Temperature,
  humidity and CO2 need no calibration and are valid readings.
- **A raw signal is calibrated in the app instead** (SD-038, SD-041): a pH
  probe's voltage in mV or V. The app calibrates it with two buffers and
  the hub's own check (RAT-025), as it does the hub's own probes; then it
  is a valid reading, still switching nothing, with the standstill check
  of a raw signal (RAT-023, RAT-059). See "Raw values calibrated in the
  app" below. Level and EC as raw values follow (SD-041).
- The history records pH, EC and level too: a value that passes every
  check and is only calibrated elsewhere is a measurement worth keeping;
  control still never reads it (SD-039).

## As a Home Assistant app

On Home Assistant OS or Supervised (a Raspberry Pi 4 or 5 with a 64-bit
system, or a PC) it installs from Home Assistant's app store, formerly
the add-on store (SD-034):

1. Switch on **Advanced mode** in your Home Assistant profile: the app is
   marked experimental (`stage` in `ha/app/config.yaml`), and the store
   lists such apps only in advanced mode.
2. **Settings → Apps → store → ⋮ → Repositories**, add
   `https://github.com/Wokesay/growcontroller-software`.
3. Install **growcontroller**, start it, **Open web UI** (port 8099). Open
   it by Home Assistant's IP address or `homeassistant.local`: any other
   name (a bare `homeassistant`, an own domain, the Home Assistant Cloud
   address) is refused, the protection against DNS rebinding below.
4. **Set the password:** the page first asks for the setup code, in the
   newest line "Setup code for the first password: …" of the app's **Log**
   tab (Settings → Apps → growcontroller → Log), so nobody else in the
   network can set it first (#79, SD-068). Then choose the sensors in
   Devices › Assignment (step 6 below).

Nothing is configured and no token is made: `gc_ha_server --app` reaches
Home Assistant at `http://supervisor/core` with the Supervisor's token
(`SUPERVISOR_TOKEN`, granted by `homeassistant_api` in
`ha/app/config.yaml`), keeps its data in the app's folder (`/data/hub`,
part of Home Assistant's backups) and serves the web app on port 8099 of
the Home Assistant host. Home Assistant downloads a ready-made image of a
few MB (`ghcr.io/wokesay/growcontroller-ha`, built and published by the
release workflow for the version in `ha/app/config.yaml`,
`docs/RELEASE.md`). The image is `FROM scratch`: the static server, the
web app and the license texts, no shell. The app's own documentation in
the store is `ha/app/DOCS.md`. The keys of `config.yaml` and the image
labels follow Home Assistant's developer documentation for apps
(<https://developers.home-assistant.io/docs/apps/configuration>, retrieved
2026-10-10).

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
   app reaches it: `{"url": "http://supervisor/core"}` reads
   `http://supervisor/core/api/states`. The name `supervisor` answers only
   inside a Home Assistant app, which uses `--app` instead (above);
   elsewhere use the IP. Leave out `/api` (the hub adds
   `/api/states`). The host must be a plain name, an IPv4 address or an
   IPv6 address in `[ ]`: no `user@`, `?` or `#`, so the token goes only to
   the host you read in the address. If you prefer, the file can also
   list the entities (`"entities": [{"entity": "sensor.grow_ph",
   "measures": "ph"}]`, `measures` one of `ph`, `ec`, `water_temp`,
   `level`, `air_temp`, `humidity`, `co2`; `"signal": true` for a pH
   probe's voltage, calibrated in the app); they are then used from the
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
   dosing hardware is not offered on a read-only hub; a short introduction
   opens instead (SD-037): name, language and colour scheme, a sensor per
   measurement, and per area whether the app is to do it automatically or
   it stays outside the control (for example in Home Assistant). The app
   switches nothing yet and says so; the choice is only recorded. The
   introduction can be skipped and is under Settings. Sensors are also
   under Devices › Assignment; choose a sensor for each measurement:
   the list shows each sensor's Home Assistant name, its entity ID and its
   current value. Can't tell two sensors apart? Warm one in your hand and
   watch its value rise. A sensor the hub does not suggest is under "Andere
   Sensoren anzeigen" (show other sensors), with a search (SD-036). Name
   and time zone are under Settings.

## What the sensors in Home Assistant need

- **Home Assistant 2024.4 or later**, so a state carries `last_reported`,
  which every report updates even when the value stays the same [1].
  On an older version a steady value is reported only when it changes.
- **A report at least every 10 min.** The hub dates each value by its
  last report and learns each sensor's usual gap between reports from its
  last ten: a value goes stale after three of its usual gaps (the second
  longest counts, so one outage does not loosen it), never sooner than
  5 min, since such sensors drop a report now and then (RAT-027), and
  never later than 30 min. A sensor that reports every 10 to 30 min goes
  stale on one late report. Right after the start, or after another sensor
  is picked, nothing is learned yet: then a value goes stale after 10 min.
  All these numbers are own rules (SD-035). A sensor that reports only on
  a change goes stale once its value stays the same for longer than that.
  "unavailable" in Home Assistant shows at once as "Gerät antwortet nicht"
  (device not answering).
- **VPD** is computed while air temperature and humidity are both valid,
  each by its own learned limit, however far apart their last reports are:
  a sensor that reports on a change keeps its value until it changes. The
  hub's own climate head still pairs its two readings within 60 s (SD-035).
- **A sign of life for calm channels (SD-042):** Home Assistant's ESPHome
  integration drops a state that did not change unless `force_update` is
  set, so `last_reported` does not move [3]. A calm water temperature then
  looks like a silent one. Better than `force_update` (which records every
  reading in Home Assistant's database) is a sign of life per sensor, see
  below: the sensor is then fresh by its last measurement.
- **Map the sensor itself, not a rounding template:** a `round()` template
  sensor only writes when its source changes, so a steady value would go
  stale (assumption from how templates work, not tested here).
- **Availability instead of fallback numbers:** template sensors written
  as `| float(0)` turn "unavailable" into 0, which the hub cannot tell from
  a real 0 (RAT-006). Give them an `availability` template instead.
- **Units:** see below. Without a unit only pH is taken.

## Sign of life (SD-042, SD-047)

Home Assistant does not report a value again while it stays the same, so
the app cannot tell a calm sensor from a silent one by its reports. A sign
of life says when each sensor last measured:

- **A sign-of-life text** of the device, sent at least every 15 s:
  `n=<counter>|<code>=<s>|<code>=<s>|…`, for each channel the age of its
  last valid measurement in whole seconds, `-` for a channel without one
  since the device started. Each channel must be measured at least every
  30 s, or a healthy one would come too close to the 60 s limit. The
  counter (up to 20 digits) makes every send a new state, so Home
  Assistant never drops it; the same counter dated again counts as the
  old send, also after an outage of the text or of Home Assistant. Codes are 1–16 of `a–z`, `0–9` and `_`, at most 64 channels,
  ages up to 9 999 999 s, the whole text at most 255 characters; anything
  else is unreadable. Recommended codes, which the app marks as fitting:
  `ph`, `ec`, `wt` (water temperature), `at` (air temperature), `rh`
  (humidity), `co2`, `lvl` (level).
- **Or a timestamp sensor** (device class `timestamp`) that states when a
  sensor last read, for example a template sensor next to a 1-Wire probe.

Under Devices › Assignment, "Lebenszeichen" (sign of life) next to a
sensor lists what Home Assistant offers, fetched only while the list is
open: each text with its channels and their ages now, and the timestamp
sensors (at most 100 of each). The device's own text comes first and is
marked "vermutlich gleiches Gerät" (probably the same device: its entity
ID shares a whole word after `sensor.` with the sensor's, such as
`sensor.tank_`); another device's is marked as such, as a warning. Pick your
sensor's channel. Then:

- the age next to the value counts from the last measurement, green up to
  30 s, yellow from 31 s, red from 60 s;
- the value is stale when its last measurement is older than 60 s; a text
  older than 60 s makes all its sensors stale ("the device sends
  nothing"), and the monitoring shows one problem for it, titled with the
  readings it affects and naming the sign of life by its entity ID; a
  channel shown as `-` for more than 60 s is stale;
- an unavailable or unreadable sign of life, a missing channel, a time
  more than 2 s in the future, or a sign of life sent after the sensor's
  last report that dates the measurement more than 3 s before it (it
  cannot belong to this sensor) makes the sensor stale; it never falls
  back to the learned limit;
- the standstill check and a calibration point count by the last
  measurement too.

Without a sign of life the learned limit stays (above), and the age says
"unverändert seit" (unchanged for), since the app sees only the last
change. The sign of life is stored with the sensor in the hub's
configuration and in a backup; it is set in the web app, where what Home
Assistant offers is checked, not in the mapping file.

An example for an ESPHome device, in outline: every measuring sensor
notes the time of its last valid output (`on_value`, a NaN does not
count), and a template text sensor publishes the ages every 15 s with a
counter in front. Keep the text out of Home Assistant's recorder; it says
nothing worth keeping.

```yaml
globals:
  - { id: life_n, type: uint32_t, initial_value: "0" }
  - { id: wt_at, type: int64_t, initial_value: "0" }   # ms since start of the last valid output, 0 = none yet
sensor:
  - platform: dallas_temp
    name: "Water temperature"
    update_interval: 15s          # at least every 30 s: ESPHome's default of 60 s leaves no margin
    on_value:
      then:
        - lambda: 'if (!std::isnan(x)) id(wt_at) = esp_timer_get_time() / 1000;'
text_sensor:
  - platform: template
    name: "Sign of life"
    update_interval: 15s
    lambda: |-
      const int64_t now = esp_timer_get_time() / 1000;   // 64 bit: millis() wraps after 49 days
      auto age = [now](int64_t at) -> std::string {
        return at == 0 ? "-" : std::to_string((now - at) / 1000);
      };
      id(life_n) += 1;
      return "n=" + std::to_string(id(life_n)) + "|wt=" + age(id(wt_at));
```

```yaml
# configuration.yaml of Home Assistant
recorder:
  exclude:
    entities:
      - sensor.hub_sign_of_life
```

## Raw values calibrated in the app

A pH probe that reports its voltage instead of a pH is calibrated in the
app (SD-038, SD-041):

- **What is taken:** a voltage in `mV` or `V`, at most 5 V, from a front
  end without gain, glass electrode on the + input, so the voltage rises
  as the pH falls. The hub accepts 47–74 mV per pH step (80–125 % of the
  ideal 59.16 mV at 25 °C, RAT-025). The picker suggests it for pH when
  Home Assistant calls it pH, or its entity ID or name contains "pH" as a
  word (also "pH1"), unless it says ORP or redox; any other voltage up to
  5 V is under "Andere Sensoren anzeigen", higher ones are not passed on
  (5 V: an assumption about the inputs such front ends feed). Until it is
  calibrated, the overview, the assignment and the calibration show its
  voltage in mV, marked "nicht kalibriert" (not calibrated).
- **Not taken yet:** a board with gain (for example 1500 mV at pH 7 and
  about 177 mV per pH step at its output, the defaults of a common
  amplified board) fails the check, and the app says so in mV per pH step;
  such boards need a rule first (SD-041). pH is not temperature
  compensated (SD-054). Swapped buffers or a reversed probe are refused as
  running the wrong way, a worn probe as too weak.
- **Calibrating:** Devices › Assignment › "Kalibrieren" (calibrate), pH 7
  then pH 4. Each step takes the sensor's first report clearly after
  "Wert übernehmen" (take value), at least 2 s later, and waits for it as
  long as the sensor's freshness limit, at most 30 min (RAT-006, SD-035):
  leave the probe in the buffer until the step moves on. If the sensor
  goes offline meanwhile (Home Assistant restarting), the step fails and
  can be taken again. Closing the window ends the calibration. One left
  for 30 min without a step no longer explains jumps (RAT-042), but keeps
  pH and EC control waiting until it is saved or cancelled, since the
  probe may still be in a buffer.
- **On the node:** no calibration there (no `offset`, `multiply` or
  `calibrate_linear` filter), or the app checks the wrong thing;
  `force_update: true` or a sign of life (above); no `delta` filter and no rounding coarser than the
  probe's noise (keep at least 0.1 mV, or 4 decimals in V; assumption), a
  report every 30 s or faster (assumption). A probe that keeps reporting
  one value goes "frozen" after 15 min of its reports (RAT-023); one that
  stops reporting goes stale. With a sign of life, `force_update` is not
  needed and both checks count by the last measurement (SD-042).
- **If the unit changes** to anything other than mV or V (the node now
  reports pH), the sensor has no value, and the assignment says so: choose
  "Kein Sensor" (no sensor), then pick it again. The old calibration never
  meets a new meaning (RAT-006). mV and V are the same scale.
- **The mapping file is only a start:** a pick from the list decides
  whether a sensor is read as a raw value, and after a restart the
  configuration does, whatever the file says.

Two things to know while it runs:

- **After a Home Assistant restart** restored states carry the restart
  time, so an old value looks fresh until the freshness limit runs out:
  10 min, as nothing is learned after a restart of the app either, and up
  to 30 min while the app keeps running.
- **Calibrating in Home Assistant** is not announced to the hub: buffer
  tests show up as jumps. Calibrate only while the hub is in maintenance
  mode.

Sources (retrieved 2026-10-09):
[1] Home Assistant developer blog, "New state timestamp State.last_reported",
2024-03-20,
https://developers.home-assistant.io/blog/2024/03/20/state_reported_timestamp;
the version 2024.4 is inferred from that date (assumption, not confirmed
from release notes).
[2] ESPHome source: `esphome/components/sensor/__init__.py`
(`force_update` default false), https://github.com/esphome/esphome
(branch `dev`).
[3] Home Assistant source: `homeassistant/components/esphome/entry_data.py`
(`async_update_state`),
https://raw.githubusercontent.com/home-assistant/core/dev/homeassistant/components/esphome/entry_data.py
(retrieved 2026-10-11 by the `domain` review).

## Before any of this may feed control

What the hub would need before pH, EC or level from Home Assistant may
steer anything. Each point is open unless it says what is built.

1. **The hub's own check, with a date.** Built for a pH probe's voltage
   (SD-041): calibrated in the app with two buffers and the hub's check
   (RAT-025, 47–74 mV/pH), dated. Raw means no calibration on the node: an
   `offset`, `multiply` or `calibrate_linear` filter changed there goes
   unnoticed unless it moves the value by more than the jump threshold
   (1 pH in 5 min, RAT-039). Open: level as an immersion probe's
   voltage and a compensated EC (SD-041, next steps); a check of sensors
   calibrated in Home Assistant (they stay display only); pH boards with
   gain, EC from a voltage, level as a current or pressure, each needing a
   rule first.
2. **The calibration state from the node where it has one.** An EZO pH
   circuit reports how many points it was calibrated with (`Cal,?`; at
   least two, RAT-025) and its slope, which can be mapped as an entity and
   checked against the same band.
3. **A calibration change in Home Assistant voids the check,** seen as a
   change of that entity or as a jump. The reading goes back to "not
   checked".
4. **EC temperature compensation:** an EZO-EC circuit assumes 25 °C unless
   it is sent the water temperature; uncompensated EC is off by roughly
   2 % per degree (a typical value, not measured here). Who compensates
   a value from Home Assistant, and how, is a product question; the
   ESP32-S3 will compensate the EC of its own probes if the user chooses
   so (SD-054, SD-060).
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
   value is dated at the restart and needs a change or a second report
   before it counts (open; a calibration point fails when the sensor goes
   offline during its wait, so it cannot be one). A raw
   pH voltage gets the hub's standstill check (RAT-023, RAT-059); it needs
   `force_update: true`, no `delta` filter and no rounding coarser than
   its noise. A value calibrated in Home Assistant still needs a check of
   its own. A calibration point takes only a report after the step started
   (RAT-006).
10. **Freshness for control:** the learned limit (5 to 30 min) and VPD
    from inputs any distance apart are for display (SD-035). Control needs
    its own limits first: for example the humidifier interlock reads any
    valid humidity, a VPD-led climate would act on inputs minutes apart,
    and the jump check sees nothing in sensors that report less often than
    every 5 min (RAT-039).
11. **What a sensor measures:** a sensor picked from the other sensors
    (SD-036) carries no word from Home Assistant on what it measures, only a
    fitting unit; a CPU load in % or a VOC sensor in ppm would pass as
    humidity or CO2. Control should accept only sensors whose device class
    confirms the measure, or that pass a check of the hub's own, and check
    the device class again every round.
12. **Which sign of life belongs to a sensor** (SD-042, SD-047) rests on the
    user's pick. The app marks a text of another device and catches a
    channel that dates the measurement before the sensor's own report, but
    a wrong pick that measures often (another channel, a clock-like
    timestamp sensor) could keep a dead sensor fresh. Before control reads
    such a value, the pick must be confirmed, for example that the text
    and the sensor belong to the same device in Home Assistant's registry.
    The jump check also needs the held value of a calm sensor kept while
    its sign of life is fresh (today a value unchanged for 5 min drops out
    of the jump window).

## Units

| Measure | Taken as it is | Converted |
|---|---|---|
| pH | no unit, `pH` | |
| pH as a raw signal (SD-041) | | `mV`, `V` (× 1000): 7 − mV/59.16, then calibrated in the app |
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
  refuses its own calibration for such devices. `GET /api/v1/catalog`
  states the mark for every class, so the setup shows such a device as
  calibrated elsewhere without guessing it (#71).
- A state is a value only as a plain decimal number (`6.2`, `-0.5`,
  `1e-05`); a space, a leading plus, a comma, hex or `inf` gives no value
  and the fault "is no plain number", never a guess (RAT-006, #71). Text from Home Assistant in a name or a fault keeps
  only printable, well-formed UTF-8.
- A raw signal has a class of its own, `ha_ph_raw` (SD-041): it offers the
  pH calibration and is not marked `externalCalibration`, also set in code.
  The bus turns the voltage into the hub's raw pH scale (`kNernstMvPerPh`);
  calibration, slope band and standstill check are the hub's own. A
  calibration point of a sensor on its own schedule waits for its next
  report (`Hub::tendProbeSessions`); a session left for 30 min no longer
  explains jumps but holds pH and EC control until saved or cancelled. A device whose reported class differs from the configured one
  has no value, and an import gives a calibration only to a class that
  offers it, so a file cannot make a sensor calibrated in Home Assistant
  one the hub checks.
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
- Of the states, the bus keeps the candidates (the suggestions): sensors whose device
  class or unit says pH (`ph`, unit `pH`), EC (`conductivity`, `µS/cm`,
  `mS/cm`), temperature (`temperature`, `°C`, `°F`), humidity (`humidity`
  with `%`), CO2 (`carbon_dioxide`) or level (`volume_storage` or `volume`,
  in `L`, not adding up like a meter); at most 100 of each kind, so device
  temperatures cannot crowd out the tank's pH. `%`, `ppm` and `L` alone
  also stand for batteries, VOC or water meters, so they need the device
  class. A candidate whose unit the hub does not convert exactly (TDS in
  ppm, EC in mS/m, temperature in K) is listed without a value and says
  why; no factor is assumed (RAT-006, RAT-015). Only picked sensors become
  devices.
- The other sensors (SD-036), sent to the web app only when asked for
  (`GET /api/v1/ha/candidates?others=1`, while the picker shows them),
  sorted by name; the hub keeps no more of them than someone could use:
  - a sensor without a device class whose unit fits a measure exactly, by
    the same conversion as any pick (`%` humidity, `ppm` CO2, `L` level,
    no unit only pH; a litre counter that only adds up is no level), with
    its value: these can be picked;
  - without a value, so the picker can say why it is not offered: one
    whose unit would fit but whose device class says it measures something
    else (a phone battery in %), and one of the hub's own device classes
    with a unit it cannot use or as a meter;
  - at most 1000; past that the picked sensors and those that can be picked
    stay. Everything else (energy, power, text, dates, people, switches) is
    dropped at once, never kept or logged.
  The list shows the names of these sensors to whoever is signed in to
  growcontroller, like the suggestions (`docs/SECURITY_MODEL.md`).
- What the hub cannot tell: whether a sensor with the right unit sits in
  the right place. A soil sensor's EC or a pool's pH looks like the tank's;
  the picker says so, and the name and live value are the guard. pH, EC
  and level calibrated in Home Assistant stay display only (RAT-025); a pH
  probe's voltage calibrated in the app is valid (SD-041); temperature,
  humidity and CO2 are
  used as valid readings, which matters once anything is switched.
- `ha/ha_assign.*`: one step from the web app, `POST /api/v1/ha/assign`
  with a measuring role and an entity: select the entity, accept the device
  under its Home Assistant name, bind the role; an empty entity takes it
  away again (unbind, remove, stop reading). A new sensor is bound before
  the role's former one is dropped, so a failed change keeps the old one;
  if any part fails, nothing this call added stays behind.
  `GET /api/v1/ha/candidates` lists the candidates with their value, their
  value and unit in Home Assistant, a unit problem, the measure a picked
  one serves, the number of other sensors and the connection state
  (`starting`, `ok`, `unreachable`, `refused`), so the web app can tell "no
  sensors" from "Home Assistant not answering"; `?others=1` adds the other
  sensors; `lifeCount` and `lifeNames` count and name the signs of life,
  `?lives=1` adds `lives` with the texts (channels and ages) and timestamp
  sensors, and a picked sensor carries its `life`. `POST /api/v1/ha/life` with a role, `source` and `channel` sets
  a sensor's sign of life (SD-042): only one Home Assistant lists now, a
  text's channel it has now, a timestamp sensor without one; `source` ""
  removes it. All need a signed-in session.
- The picks live in the hub's configuration (devices `ha.<entity>` of
  class `ha_<measure>`, a sign of life as `life: {source, channel}`),
  saved at once after each pick; at start they are handed back to the bus,
  the signs of life also with every tick, so an import takes effect at
  once. There is no second file to get out of step. The configuration
  takes a sign of life only for a class whose sensors report on their own
  schedule (set in code, R7) and only in the platform's own form.
- Text from Home Assistant is cut and cleaned before it is shown, without
  control or invisible format characters: a unit in a fault to 32 bytes, a
  sensor's name to 60 in the list (40 as the device's name).
- A report's age is measured in Home Assistant's own time (its `Date`
  header minus `last_reported`), so the two computers' clocks need not
  agree; the same report keeps the time it got when first seen.
- The device classes are also marked `ownReportSchedule` in code: the
  sensor truth (`core/src/truth.cpp`, `SensorTruth::freshLimit`) learns
  their gaps between reports; the hub's own probes keep the catalog's
  fixed limit (SD-035, R7).
- `ha/main.cpp`: the server (`gc_ha_server`), following the simulator's.
  `--app` sets what the app needs (above) and refuses `--config` and
  `--token-file`; the Supervisor's token is read from the environment and
  removed from it (also under its old name `HASSIO_TOKEN`), like
  `GC_HA_TOKEN`, so nothing the server starts would inherit it. The
  process's own start environment (`/proc/<pid>/environ`) and the
  container's settings still hold it. `tools/ha_server.test.mjs` runs
  these options.
- `ha/app/`: the app (`config.yaml`, `Dockerfile`, store texts);
  `repository.yaml` at the root makes the repository an app repository.
  The image is built in stages: the web app with its npm packages, the
  server without node (static-pie against musl, 1 MiB thread stacks), the
  license texts, and the final image with nothing but their results.
  `.github/workflows/app.yml` builds it for `amd64` and `aarch64`, each on
  its own architecture, and starts it like the Supervisor (a token in the
  environment, no arguments) next to a stand-in for the Supervisor reached
  by name: labels and size, refused without a token or with `--config`, it
  answers, serves the web app, refuses a foreign host name, survives long
  paths, asks the stand-in only `GET /core/api/states` with the token,
  stops cleanly on SIGTERM, keeps a private data folder, and carries the
  license texts and the source code link. This runs on every code change
  and for a release; `tools/app.test.mjs` keeps `config.yaml`, the server,
  the Dockerfile and the release workflow in step.
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
  host,** reachable by every device in your network (and, with a router
  that opens the host over IPv6, from outside); the growcontroller
  password travels in the clear there. The first password needs the
  one-time setup code the app prints to its log, which only Home
  Assistant's administrators read, so nobody else in the network can set
  it first (#79, SD-068). Clearing the port hides the web app from the
  network, not from Home Assistant's internal network. Home Assistant's
  ingress (its own login and HTTPS) would need the web app to work below a
  path; that is a follow-up.
- **The Supervisor's token is not read-only:** Home Assistant has no
  read-only access for apps; the Supervisor passes every call except its
  own API on to Home Assistant as its administrator. growcontroller sends
  only `GET /api/states` (tested), but a flaw in its web server could give
  an attacker in the network control of Home Assistant. The image has no
  shell and the app no other rights (`tools/app.test.mjs` refuses any
  other key in `config.yaml`). The server runs as root in the container
  with Docker's default capabilities; dropping them and an AppArmor
  profile are follow-ups (#80).
- The session cookie is not tied to the port: the browser also sends it
  to Home Assistant and other apps under the same host name. It is
  HttpOnly and only valid for growcontroller.
- If the Supervisor ever refuses the token, reading stops until the app
  is restarted, while the app keeps answering (so its watchdog sees no
  fault), and the web app's hint speaks of a token file. A follow-up
  (#78).
- The app store reads `ha/app/config.yaml` from `main`: between merging a
  new version and the release workflow's end, Home Assistant offers a
  version whose image is not there yet, and an install fails until it is.
