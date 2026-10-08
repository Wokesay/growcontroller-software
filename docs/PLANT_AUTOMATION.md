# Complete plant automation: areas, network actuators, phases

Status 2026-10-06; §9 updated 2026-10-07. Assignment from the project
owner: the software covers the whole installation, not only mixing and
tank – light with dimming, irrigation, climate (exhaust fan, circulation
fan, humidifier, dehumidifier, heater) and phases with their own recipes
and settings. 230 V devices run via switchable sockets, Shelly (local)
first. The app uses neutral wording: universal plant automation for
greenhouse, indoor growing and hydroponics. Basis: drafts by `architekt`,
`anwender` and `hardware` (agents of the product repository) of
2026-10-06.

> **Draft.** Everything here is a proposal, even where it sounds settled
> ("fixed in code", "plug-in devices only"). Decisions are made as PDs in
> the product repository; open points are in §9.
> Decided on 2026-10-06: scope, neutral wording and German/English
> (PD-018; English is the default, PD-035), 230 V devices via sockets
> (PD-019), behaviour after a power failure (PD-020), pH/EC heads (PD-021).
> Decided on 2026-10-07: the questions in §9 except item 2 (PD-025 to
> PD-030) and irrigation without a readable level (PD-031, §2).

## 1. Areas in the data model

Until now, all roles lived in `tanks[0].roles`. New (schema v2, with
migration):

```
config
├─ tanks[]  id, usable volume, minimum level, roles{tank.*}
└─ zones[]  id, kind (room|tent|greenhouse), name, tank, roles{zone.*}
```

- The data model provides for several zones; v1 allows one zone.
- A cultivation run belongs to one zone.
- Roles use the neutral prefix `zone.*` instead of `tent.*`. The migration
  renames them.
- Roles accept several capabilities (`accepts[]`), may be bound more than
  once where needed (`multi`) and carry a safety profile (§3).

| Area | Roles |
|---|---|
| Tank | `tank.ph`, `tank.ec`, `tank.water_temp`, `tank.level`, `tank.circulation`, `tank.inlet`, `tank.heater` (follows with the emergency cut-off) |
| Light | `zone.light` (switching), `zone.light_dim` (0–10 V or 1–10 V, switchable; PD-028) |
| Climate | `zone.air_temp`, `zone.humidity`, `zone.co2`, `zone.exhaust`, `zone.circulation_fan`, `zone.humidifier`, `zone.dehumidifier`, `zone.heater` (follows with the emergency cut-off) |
| Irrigation | `zone.irrigation_pump` |

**Derived values** (fixed code, no script; implemented 2026-10-06):
`zone.vpd` from air temperature and humidity. It is computed only if both
values are valid and at most 60 s apart; otherwise there is a gap (R5).

- Saturation vapour pressure: es(T) = 0.6108 · exp(17.27·T / (T + 237.3)) kPa (FAO-56, eq. 11,
  https://www.fao.org/4/x0490e/x0490e07.htm; not yet cross-checked online,
  the proxy blocked the page on 2026-10-06).
- Air VPD = es(T) · (1 − RH/100).
- Leaf VPD = es(T + Δ) − es(T) · RH/100. The offset Δ is optional and is
  shown as "estimated"; the default is air VPD (Rationale: RAT-017).

## 2. Outputs and network bus

**Capabilities:**

| Capability | Meaning |
|---|---|
| `switch.12v` | 12 V output on the hub |
| `switch.mains` | 230 V via a network device |
| `switch.dry` | potential-free (dry) contact |
| `dim.0_10v` | dimming value 0–100 % |
| `measure.power` | power per channel in W, optional |

Fixed in code: dosing never runs via network devices, only via the dosing
block [PD-010, PD-011].

**Shelly (Gen2 and newer), locally via JSON-RPC** (API docs
https://shelly-api-docs.shelly.cloud/gen2/; models, mDNS, Digest and
fields such as `auto_off` are not yet cross-checked online, the proxy
blocked the page on 2026-10-06). For end customers only plug-in devices
are suitable: Plug S Gen3, Outdoor Plug S Gen3, Power Strip 4 Gen4.
Built-in devices (1PM, Pro 4PM, Dimmer 0/1-10V) are for qualified
electricians. The software supports them anyway.

- **Status 2026-10-06:** `INetBus` with Shelly in the simulator, the
  profiles `dauer`, `puls` and `kompressor` in the gateway, assignment with
  read-back, a check before every switch-on, testing and manual mode are
  implemented (§8 steps 3–4). Open:
  - Heating roles only with the latching emergency cut-off (level, water
    temperature, overtemperature, missing effect, failure of the sensor
    assessment, run time; Rationale: RAT-060) and `power_limit`;
  - profile `versorgen` (supply) for devices with their own thermostat
    (Rationale: RAT-069);
  - discovery via mDNS and Digest on the device, "Off not confirmed" with
    retry on emergency stop (implemented for protective cut-offs), watchdog
    via power ("should be off, draws > 2 W");
  - the button and the app on the Shelly bypass the locks: treat "on
    without a command from the hub" as manual mode (an unacknowledged latch
    of circulation pump or inlet already switches off again); disable
    `auto_on` in the device and check it; throttle retries;
  - total load per strip (16 A total, 12 A per outlet: search results of
    2026-10-06, among them the Conrad data sheet
    https://asset.conrad.com/media10/add/160267/c1/-/gl/003593788IN00/informacije-3593788-shelly-power-strip-4-gen4-schwarz-uticnica.pdf,
    not retrieved; `current_limit` 12 A per channel read on the device, see
    RAT-060) and inrush currents (light, compressor) – measure on a sample.
  - Irrigation pump: decided in PD-031. With an assigned but unreadable
    level sensor, irrigation is locked and reported; if the plants are too
    dry, the hub gives a limited emergency dose and reports it. Irrigation
    also works without a level sensor; the hub detects a dry run from the
    current draw, switches off and reports. Implementation open: the
    prototype still requires a valid level (Rationale: RAT-068).
- **Separate network bus** `INetBus` next to the RS485 bus `IBus`. It has
  no `startRun`, so the type alone rules out dosing over the network. Only
  `Actuators` switches (R1).
- **Discovery:** mDNS `_shelly._tcp` or IP by hand.
  - The device is recognised by its Shelly ID; the IP is only a runtime
    attribute.
  - It needs Digest authentication. The hub generates the password per
    device if the user agrees.
- **Adding:**
  - The hub shows the planned safety configuration, writes it and reads it
    back.
  - Roles with a safety profile are bound only when the read-back matches.
- **Sockets are named after their purpose**, e.g. "Plug: Exhaust".
  - Each socket has a "Test" button (on for 3 s).
  - Strip outlets are numbered from 1. The Shelly interface counts from 0;
    the app says so once (Rationale: RAT-019).

## 3. Safety of the network actuators

| Profile | Roles | After a power failure | Limit in the Shelly | Software |
|---|---|---|---|---|
| `dauer` (continuous) | light, circulation fan, exhaust fan, circulation pump | off; fans rather on per PD-020 (implementation open) | – | – |
| `puls` (pulse) | humidifier, irrigation pump, inlet | off | **auto-off mandatory**, just above the software limit | maximum run time, wait time |
| `kompressor` (compressor) | dehumidifier | off | – | minimum run 10 min, minimum pause 5 min (Rationale: RAT-034) |
| `heizen` (heating) | heater without its own thermostat (e.g. immersion heater) | off | **auto-off mandatory** (6000 s at a 90 min software limit; Rationale: RAT-060), `power_limit` | locks on sensor truth, latch |
| `versorgen` (supply) | device with its own thermostat; the relay only provides power | off | no auto-off, but `power_limit` as a safety net for a stuck thermostat (Rationale: RAT-069) | assessment by the watchdog |

Fixed in code (R7):

- Humidifier and dehumidifier never run at the same time (Rationale:
  RAT-034).
- The hub raises a dimming value below the switch-on threshold to the
  threshold; 0 means off, and the relay switches off too (Rationale:
  RAT-013, RAT-029).
- After a restart the hub sends "off" to all channels. Sequences (dose,
  pulse, inlet) are not resumed. State functions (light, fans, climate)
  recalculate only once time and sensor truth are trusted. Without a
  trusted time the light stays off (refinement of R6). PD-020 replaces the
  part of R6 for state functions; implementation open.
- Emergency stop switches everything off, including the fans. Goal:
  unreachable outputs show "Off not confirmed", and the hub repeats the
  command (open for the emergency stop, implemented for protective
  cut-offs).

The watchdog also assesses:

- target ≠ actual;
- "should be off, draws > 2 W" (runaway; Rationale: RAT-073);
- light during the dark period;
- dry run from the power draw (Rationale: RAT-049).

It switches nothing (R2).

**[SAFETY] For the manual:**

- Network devices stay outside the growing room.
- Everything in the water is behind a 30 mA RCD.
- Heaters that carry the warning "do not operate with a timer" from
  EN 60335-2-30 are not connected (standard not consulted; statement from
  the `hardware` draft).
- Built-in devices are connected only by a qualified electrician.

## 4. New functions (catalog)

| Function | Requires | Parameters (P = from the phase) |
|---|---|---|
| `light_schedule` | `zone.light`, trusted time | `on_at`, `light_hours` P, `intensity_pct` P, `ramp_min` 15 (Rationale: RAT-035), switch-on threshold (Rationale: RAT-013) |
| `circulation_fan` | `zone.circulation_fan` | mode (always, interval, with light) |
| `climate_control` | air temperature, humidity, at least one climate device | day/night temperature P, RH or VPD day/night P, hysteresis, minimum times (Rationale: RAT-034), heating limit (Rationale: RAT-060) |
| `vpd_watch` | air temperature, humidity | VPD target day/night P, tolerance, leaf offset |
| `irrigation` | `zone.irrigation_pump`, time | doses per day P, factor of the first dose P, duration per dose P (Rationale: RAT-010), minimum level in the tank (Rationale: RAT-067) |

- "Day" and "night" follow the light, not the clock.
- When VPD leads, the RH setpoint is calculated from the VPD target and
  the **target** temperature (Rationale: RAT-009, RAT-017).

## 5. Phases

Phases are parameter sets across all areas (R4). The vocabulary is defined
centrally in the catalog (`phaseParams`), grouped by light, climate, water
and irrigation. The phase editor shows only groups for which hardware is
present; beginners see the basic values, everything else is under "More
settings".

| Value | Vegetative | Flowering | Rationale |
|---|---|---|---|
| Light hours | 18 h | 12 h | RAT-066 |
| Dimming | 50 % | 75 % | assumption |
| Day temperature | 26 °C | 27 °C | RAT-009 |
| RH or VPD | 70 % ≈ 1.0 kPa | 62 % ≈ 1.35 kPa | RAT-009 |
| pH target | 5.9 | 5.9–6.1 | RAT-066 |
| EC target | manufacturer chart | manufacturer chart | RAT-066 |
| Doses per day | 6 | 8–9 | RAT-010 (rock wool) |

If the EC target drops by more than 0.2 mS/cm at a phase change, the app
advises draining the tank completely (Rationale: RAT-012). If the residue
plus fresh water lies below the new target and can be dosed up, the drain
is skipped; on a change of product it stays (Rationale: RAT-065). Partial
draining would be an assumption of our own and is not planned. The hub
cannot dilute.

## 6. User interface

- **Navigation by areas:** Overview · Phases · Tank & mixing ·
  Irrigation · Climate · Light · Recipes & nutrients · History · Devices ·
  Settings (with "Functions").
  - Areas without hardware are hidden. Devices › Expand shows the way to
    more. Prototype status: Climate, Light and Irrigation are always in the
    navigation and show what is missing without hardware (deviation, to
    follow).
  - On the phone: Overview · Tank · Phases · History · More.
- **Overview by urgency:**
  1. banner (emergency stop, maintenance mode, disconnected);
  2. status line;
  3. cultivation run;
  4. "Running now";
  5. cards for tank, climate, light, irrigation;
  6. bottles;
  7. events.

  Cards without a device are left out entirely.
- **Full screen width:** the cards flow into columns. On small screens
  they stack, and nothing overflows its box.
- **Setup wizard in 5 steps:**
  1. Start;
  2. Devices;
  3. Tank;
  4. Nutrients: template first, with preview;
  5. Calibrate.

  Light, climate, irrigation and probes are skippable mini-wizards on their
  own pages. The fixed footer has "Back" (also on "Done"), "Skip" and
  "Next"; "Next" saves.
- **Templates map by role** (part A, part B, CalMag, additive), not by
  exact names.
- **Terms** are explained in the glossary (German and English,
  `web/src/lang`). Renamed:
  - dry-run limit → "Minimum level";
  - latch → "Locked until released";
  - EC gate → "pH lock at low EC";
  - dosing block port → "Pump 1–6";
  - hub port → "Port 1–8".
- **Devices** show a diagram:
  - hub with ports and outputs;
  - below it the dosing block and the sensor heads, each on a hub port;
  - network devices per channel;
  - "Expand" with what a device unlocks.

  The data for it comes from `GET /topology`.

## 7. pH and EC: one probe or two

Decided in PD-021 (answers the roadmap question from stage 1). There are
three device classes:

- `head_ph_ec` (pH, EC, water temperature);
- `head_ph`;
- `head_ec` (EC, water temperature).

The roles stay the same. The hub assigns a measurement role only if
exactly one device provides it; if two heads provide the same value, the
user chooses under Devices › Assignment. Implemented in the simulator
(schema v2). Two heads need two ports and a galvanic isolation of their
own each.

## 8. Order of implementation

Each step can be tested in the simulator.

1. User interface:
   - full width and reordering;
   - fix overflow;
   - neutral wording;
   - German/English (groundwork);
   - wizard with 5 steps, Back, explanations;
   - templates with preview;
   - pump naming.
2. Schema v2:
   - `zones[]`, roles per instance, `accepts`/`multi`;
   - migration;
   - `phaseParams`;
   - separate pH and EC head classes.
3. Gateway for switching roles in general:
   - profiles, conflict pair, minimum times;
   - dimming with threshold;
   - `stopAll` across all instances.
4. `INetBus` with Shelly in the simulator (auto-off, power, offline,
   button); adding with read-back.
5. Functions in this order: `light_schedule`, `circulation_fan`,
   `climate_control` with `vpd_watch` and the series `zone.vpd`,
   `irrigation`.
6. Navigation by areas, overview cards, `/topology` with the device
   diagram.
7. Firmware: mDNS and HTTP RPC client with Digest.

## 9. Questions for the project owner

All answered except item 2: item 6 on 2026-10-06 (PD-020), the others on
2026-10-07 (PD-025 to PD-030). The answers are product decisions (PD) in
the product repository; their implementation is open.

1. **Phase change:** automatically after a number of days, or only after
   confirmation?

   Decided in PD-025: selectable per phase. Each phase sets whether the
   hub moves to the next phase by itself once its days are over, or only
   after the user confirms. Harvest stays an event that the user confirms
   (Rationale: RAT-077). Open: the default per phase; an announcement
   before the change and extending a phase by one day.
2. **Light when the hub fails:**
   - Auto-off: the next day stays dark.
   - Local schedule in the Shelly.

   Open. PD-020 covers only the power failure (light depending on how long
   it lasted); it lists a failure of the hub alone as open.
3. **Maintenance mode:** what pauses besides dosing – irrigation too?

   Decided in PD-026: the user configures what pauses. Principle:
   everything that directly affects an open tent pauses, or at least must
   not cause harm. Default: water and CO2 pause; "water" means dosing,
   refilling, irrigation and the automatic circulation pump (proposal).
   Selectable among others: whether climate pauses too, or whether
   humidifier, dehumidifier and circulation fan keep running. Sensor truth
   and locks always stay active (Rationale: RAT-075). Open: the default
   for light, exhaust fan and heater; duration and end of maintenance
   mode.
4. **Units in English:** litres/°C fixed, or also gallons/°F and EC as
   ppm?

   Decided in PD-027: from v1, units are selectable in the settings, for
   input and output: litres or gallons, °C or °F, EC in mS/cm or as ppm
   (500 or 700 scale). PD-035 adds: language and unit system are chosen
   separately; after the language, setup asks for metric or imperial;
   English is the default language. Open: compute internally in metric
   and convert only in the UI (proposal); US or UK gallons; the ppm scale
   must be visible everywhere so that no wrong EC values arise.
5. **Dimming:** via a separate dimming head as a bus device (recommendation
   of `hardware`, stage 4) and for now via Shelly dimmers? No 0–10 V on the
   hub (affects PD-006).

   Decided otherwise in PD-028: the dimming output for LED drivers sits on
   the hub (there is no collection box in the tent any more, PD-078); it is switchable
   between 0–10 V and 1–10 V. Open: number of channels, galvanic
   isolation and protection of the output, cable path from the hub to the
   lamp driver, Shelly dimmers as an interim solution until our own
   hardware exists.
6. **Exhaust and circulation fan after a power failure:** decided in
   PD-020: rather on, the crop must not be put at risk (exception to R6;
   implementation open).
7. **Inlet and irrigation pump only on 12 V** (SELV, recommendation of
   `hardware`) or also via 230 V sockets?

   Decided in PD-029: both may run on a 12 V output of the hub or on a
   switchable 230 V socket. The hub switches sockets only on the local
   network, without cloud and without internet. Auto-off in the device
   and the hub's locks apply (level if present, emergency limit, maximum
   run time). Open: additional overflow protection for an inlet on 230 V,
   because auto-off alone does not prevent an overflow (Rationale:
   RAT-079); a note on the RCD in the manual.
8. **Manufacturer tables as templates** (e.g. Athena Blended): may they go
   into the product? Who maintains them?

   Decided in PD-030: they ship with the app as recipe templates. Each
   template carries its source, its date and the note "manufacturer data,
   not binding". The templates are data in this repository; new or changed
   tables come by PR and are reviewed before the merge. Before
   publication, `regulatorik` (product repository) checks brand mentions
   and liability. The first template is the Athena Blended Feed Program,
   Metric, A01.004 (Rationale: RAT-066).
