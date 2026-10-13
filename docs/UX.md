# Interaction design of the web app

As of 2026-10-06. Basis: the grower view (agent `anwender`) and the
outside view of the customer (agent `kunde`, with a critique of
competitors' apps). Implemented in the prototype unless noted otherwise.

## 1. Principles

- **Display follows the actual state.** "Running" appears only once the
  dosing block reports it. Rationale: RAT-037.
- **Every block is a visible line with its reason.** It disappears by
  itself when the block ends. Rationale: RAT-037, RAT-039.
- **Plain language instead of codes.** The colour shows the worst entry.
  Normal case: "Everything OK (13 checks)"; only what deviates is listed
  individually. Rationale: RAT-042, RAT-072. One cause is one entry: a
  device that sends nothing is one problem titled with the readings it
  affects (SD-047, #107).
- **Keep three states apart:** "Sensor not responding", "not calibrated",
  "not applicable" (for example water temperature with an empty tank).
  Rationale: RAT-021, RAT-048. **Calibrated outside the hub** (Home
  Assistant trial) is a state of its own, not "not calibrated": the value
  shows with its age and its trend (the history records it, SD-039) and is
  marked "anderswo kalibriert" (calibrated elsewhere); stale, implausible
  and jumps still show. Rationale: RAT-025.
- **Show only what the hardware can do.** What is missing is listed in one
  place: Devices › Expand. Empty "–" tiles and probe advertising get in
  the way (`anwender`). On a Home Assistant hub there is nothing to plug
  in: Expand and the ports are hidden, and a measurement without a fitting
  sensor is named once under Assignment.
- **No jargon up front:** no Modbus, no RS485. Ports are called "Port 3"
  with a picture; wrong connections are explained in a sentence. A
  hobbyist look puts buyers off (`kunde`).
- **Names the app fills in follow the page language:** names and notes
  taken from a template or the setup use the language of the page at that
  moment. After that they are your data: they don't change when you switch
  the language, and you can edit them.
- **Honest about the state:** "just now / 20 s ago" for each reading;
  "disconnected" when the app cannot reach the hub. "Control keeps running
  even when the app is closed."

## 2. Navigation

| Section | Content |
|---|---|
| Overview | Monitoring, tank with readings and 6-h trend, control lines, running job, stock, cultivation run, latest events |
| Mixing | Recipe, water, "New batch"/"Top up", preview in ml, guided sequence; manual dose |
| Tank & control | Control lines with checklist, monitoring in detail, acknowledging latches, tank, outputs, maintenance mode, cultivation run and phases |
| Climate | Air temperature, humidity, VPD, CO2; exhaust, circulation fan, humidifier, dehumidifier in manual mode (control follows) |
| Light | Light output in manual mode (light schedule and dimming follow) |
| Irrigation | Irrigation pump in manual mode with dry-run protection (irrigation schedule follows) |
| History | pH, EC, water temperature, level with target band and dosing markers; air temperature, humidity, VPD, CO2; events with filters; CSV |
| Recipes & bottles | Bottle ↔ pump, pairs, stock, "Bottle changed"; recipes, templates |
| Devices | Ports with test measurement, devices, adding devices, pump and probe calibration; assignment; Expand |
| Functions | Configuration tree by stage: state, what is missing, switches, settings |
| Settings | System, access, updates with "What's new", report a problem, data, display |

On a phone: Overview, Mixing, Tank, History, More. The **STOP** button
(emergency stop) is always at the top right, except where nothing is
switched (the Home Assistant trial): there "Nur Anzeige" (display only)
says why there is no stop and how to switch off in an emergency (SD-040).

## 3. Overview per expansion stage

- **Stage 0:**
  - Tank tile with "Last mixed …" and input for the manual reading (pH).
  - "Mix" button, stock as "4 ok".
  - No empty pH/EC tiles.
- **Stage 1:**
  - pH, EC and water shown large, with target band, trend and age of the
    reading.
  - Control lines for EC and pH.
- **Stage 2:**
  - Measured volume (L and bar), control line for refilling.
  - "not applicable" when the tank is empty.
- **Stages 3–4:** Room climate (air temperature, humidity, VPD, CO2) and
  all assigned switched outputs are implemented; the next irrigation shot
  and drain in % are open.

**Control line** (the answer to "Why isn't it dosing right now?"):

- Controlling: "pH 6.40 → 5.80 · partial dose 2 of 8 · waiting 2:10 for
  mixing"
- Resting: "pH on target (5.82)"
- Blocked: "EC 0.10 below 0.50: pH can't be measured like this, nutrients
  first"
- Tapping opens the checklist: ✓ probe delivers · ✓ rest time over · ✗ EC
  gate …

## 4. Setup wizard

Customer view: at most 5 steps to the first success, without an account.
Devices are detected, not selected. Exception: on a Home Assistant hub
(docs/HOME_ASSISTANT.md) the user chooses one sensor per measurement under
Devices › Assignment, because Home Assistant offers dozens of sensors and
only a few belong to the grow. The list shows each sensor's name, entity ID
and live value, a pH probe's voltage first (marked as a raw value the app
calibrates, SD-041), then by name (rows never move as values come and
go); one tap saves; a sensor used for another
measurement is shown but cannot be taken; a unit the hub cannot use is
named instead of a value, and a converted value also shows Home
Assistant's own; "Home Assistant not answering"
and "access refused" are said as such, never as "no sensors".

| # | Step | Required |
|---|---|---|
| – | Initial password (before anything else) | yes, cannot be skipped (EN 18031-1 AUM-5-1) |
| 1 | Name, time zone (from the browser); on the hub also Wi-Fi | yes |
| 2 | Detect devices, "Add all" | until the dosing block and a cap are present |
| 3 | Tank: usable volume, water, circulation pump/inlet on the hub output | usable volume yes |
| 4 | Assign bottles to the caps (suggestion: part A/B as a pair, CalMag, pH−) | yes |
| 5 | Prime tubing, calibrate pumps (measuring cup; actual run time) | without a calibration value the pump does not dose |
| 6 | Calibrate probes (pH 7/4, EC 1.413, level curve) | can be skipped |
| 7 | Recipe: own or template | yes |
| 8 | Done: what works now, what is missing; "Go to first mix" (suggestion: 10 L bucket) | – |

During steps 3–7 the wizard shows live what is still missing for "Mix
nutrient solution". This comes straight from the resolver.

**Still open** (`kunde`):

- QR code on the device.
- Fallback address of the hotspot (example WLED: 4.3.2.1).
- Showing the new address after the Wi-Fi change.
- Hint "Add to home screen".

## 5. Operating errors the app catches

| Error | Safeguard |
|---|---|
| Wrong water volume (100 instead of 10, gallons) | blocked above the usable volume; ml per bottle shown large in the preview |
| Mixed twice | "This tank was mixed 12 min ago. Dosing again doubles the nutrients." – only with confirmation |
| Leftover solution in the tank | mode "Top up: X L fresh water" |
| Very small dose | warning below 1 ml; pump runs under 1 s are blocked |
| pH bottle in the recipe | rejected ("pH always comes last") |
| Pump blocks in the middle of a pair | "Part B not fully dosed … Part A is already in … Complete part B" |
| Power failure during a run | everything off; event "Mix run interrupted by restart at step 2/3. In the tank: …" |
| Cap plugged straight into a hub port | port red: "Pump cap plugged straight into the hub. Please plug it into the dosing block." |
| Cap after replugging | event "Cap … is back. Is it still on part A?" |
| Bottle removed by a slip of the finger | "Remove bottle?" with its name and what follows (stock gone, pump free and still calibrated if it was); only "Remove" removes it, "Cancel" keeps it. A bottle a recipe uses says "Bottle still in use", names the recipes and how to take it out (a pair as a whole); with exactly one recipe it offers "Edit recipe", with several "Got it" (#120) |

## 6. Notifications (concept, not implemented yet)

| Level | What | When |
|---|---|---|
| immediately, also at night | pump does not switch off/blocks, inlet emergency cut-off, overflow, jump lock while control is running, hub without a sign of life | repeated after 5, 15, 60 min |
| during the day | block > X min, pH/EC clearly outside (alarm band), stock will not last | quiet hours 22–07 → morning report |
| in the app only | stock low, calibration due, hints | – |

Rules:

- Title ≤ 40 characters, body ≤ 200. Rationale: RAT-045.
- Acknowledging only pauses the repetition. Rationale: RAT-022.
- Channels the user chooses, without a manufacturer cloud: ntfy, e-mail,
  webhook, MQTT (PD-024). Once the ESP is reached through the relay
  with its own certificate, the web app on the home screen could send
  Web Push; whether it does is decided with M5, whether a store app
  comes later (SD-062).
- All notifications are free, safety alarms included; there is no
  subscription [PD-008, PD-024].
- Say it honestly: "No notifications without internet."

## 7. Visual design

- Own design tokens, light and dark, no external fonts: the app runs
  offline from flash.
- Colours for status: OK green, problem red, hint yellow, resting grey.
  Colours for quantities: pH violet, EC orange, temperature turquoise,
  level blue, air temperature amber, humidity light blue, VPD green, CO2
  grey.
- Mobile first: bottom bar, large buttons, numbers in the format of the
  chosen language (decimal point in English, comma in German; PD-035).
  Durations and ages join the number and its unit with a no-break space
  ("45 s", "vor 3 min"; #106), so they never break apart: the ages the app
  writes and the hub's spans (checked by `tools/i18n_keys.test.mjs`). Other
  numbers with units (ml, ml/L) do not yet (#148).
- Dialogs (`Modal`) take the focus when they open: the dialog itself, so a
  screen reader names it by its title and a phone opens no keyboard; Tab
  and Shift+Tab stay inside, and bring the focus back in when it has
  fallen out; on closing (Escape, the close button or an action) the focus
  returns to what opened it, if it is known and still there, else to a
  place the dialog names (after removing a bottle: "+ Bottle"); a dialog
  opened from one that closed ("Edit recipe" from "Bottle still in use")
  names none yet (#148); the title is the dialog's accessible name (#105).
- A step that cannot be undone should ask first, in a confirmation (`Modal`
  with a message, role `alertdialog`): the title asks the question, the
  message names the thing and what follows and is read out with the title,
  and the button that does it says the verb ("Remove"), next to "Cancel".
  What the hub would refuse is said up front, with the way out, instead of
  offered. Removing a bottle (#120) and mixing a tank again do; removing a
  recipe or a device still acts at once (#148).
