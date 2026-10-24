# Interaction design of the web app

As of 2026-10-15. Basis: the grower view (agent `anwender`) and the
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

The menu is grouped by topic, with a small heading above each group
(SD-056): Overview and History first, then **Nutrient solution**,
**Room** and **System**.

| Group | Section | Content |
|---|---|---|
| – | Overview | Monitoring, tank with readings and 6-h trend, control lines, running job, stock, cultivation run, latest events |
| – | History | pH, EC, water temperature, level with target band and dosing markers; air temperature, humidity, VPD, CO2; events with filters; CSV |
| Nutrient solution | Mixing | Recipe, water, "New batch"/"Top up", amounts "To target with probe" (offered with EC top-up on and a probe and circulation pump assigned, preselected while it can run, else the recipe's amounts with the reason: measures first, round 1, then EC top-up takes over the rest; a top-up without a level asks for the litres in the tank now, SD-102), as in the recipe, or to a target EC without measuring (only with EC top-up off, SD-098), preview in ml, guided sequence; manual dose |
| Nutrient solution | Tank & control | Control lines with checklist, monitoring in detail, acknowledging latches, tank, outputs, maintenance mode, cultivation run and phases |
| Nutrient solution | Recipes & bottles | Bottle ↔ pump, pairs, stock, "Bottle changed"; recipes, templates |
| Nutrient solution | Watering | Watering pump in manual mode with dry-run protection and a pH/EC check (watering schedule follows); in the same group as the tank, as it draws from it |
| Room | Climate | Air temperature, humidity, VPD, CO2; exhaust, circulation fan, humidifier, dehumidifier in manual mode (control follows) |
| Room | Light | Light output in manual mode (light schedule and dimming follow) |
| System | Devices | Ports with test measurement, devices, adding devices, pump and probe calibration; assignment; Expand |
| System | Functions | Configuration tree by stage: state, what is missing, switches, settings |
| System | Settings | System, access, updates with "What's new", report a problem, data, display |

Climate, light and watering carry the label "Handbetrieb" (manual mode)
as long as they only show values and switch by hand. The hub switches on
only when tapped and, at every restart, off everything but the fans (R6,
PD-050): the light page advises plugging the light straight into the
grower's own timer for fixed light hours, and the watering page says the
dry-run lock and the pH/EC check protect only a pump the hub switches. With nothing
assigned, the page says what to connect and links to Devices, where the
smart plug's card takes the choice, and says how to add a plug while
none has been added. The Home Assistant app switches nothing there and
shows the choice per area instead (SD-037).

On a phone: Overview, Mixing, Tank, History, More; "More" lists the
other pages under the same headings. The **STOP** button
is always at the top right, except where nothing is
switched (the Home Assistant trial): there "Nur Anzeige" (display only)
says why there is no Stop and how to switch off in an emergency (SD-040).

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
| – | Initial password (before anything else), the language choice above it (English preset, German selectable; SD-014) | yes, cannot be skipped (EN 18031-1 AUM-5-1) |
| 1 | Language (as chosen with the password), name, time zone (from the browser); on the hub also Wi-Fi | yes |
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
| Watering right after a dose, or from an overdosed tank | with a pH or EC probe assigned, the watering pump stays off while a dose runs and mixes in (at least 4 min), and while pH is outside 4.0–8.5 or EC is more than 1.0 above the target; the refusal and the event say why and when to try again; no override (SD-093) |
| Leftover solution in the tank | mode "Top up: X L fresh water" |
| Very small dose | warning below 1 ml; pump runs under 1 s are blocked |
| pH bottle in the recipe | the recipe editor does not list pH bottles; the bottle editor does not save a bottle a recipe holds as a pH bottle and says how to take it out (#159) |
| Pump blocks in the middle of a pair | "Part B not fully dosed … Part A is already in … Complete part B" |
| Power failure during a run | everything off; event "Mix interrupted by a restart": the step, what is in, and the part that was running as "up to … ml of it may be in – how much is unknown"; then to mix first (or that EC and pH wait for the circulation pump), measure EC and add only what is missing (SD-104) |
| Walked away during a guided mix without a circulation pump | after 30 min without "Continue" the mix stops, names a part of a pair still missing with its amount, offers "Resume mix" / "End mix"; the minutes left show while it waits (SD-057, SD-089) |
| Water's EC not entered for a mix to a target EC | never taken as 0: said at its field, Start off; 0 is entered for RO water, a value above the EC target's range is refused as a likely µS/cm (SD-098) |
| A target EC the recipe's amounts cannot reach, or a chart EC typed too low | at most the recipe's amounts without measuring; the plan says it cannot reach the target and names the EC to expect, worked out, not measured (SD-098) |
| A dry or misplaced probe while mixing to the target | the probe reads about 0 and still counts as valid: a first step that did not raise EC locks EC top-up at once; the event asks whether the probe is in the water, the pumps deliver and the litres are right, the card says EC top-up is locked (SD-102) |
| Mixing to the target with water not yet mixed at the probe | the hub circulates and measures first, shows the time left and what it measured, and works the amounts out again; a jump meanwhile is expected, not locked; it can be cancelled with nothing in (SD-102) |
| Mixing to the target and EC top-up off, locked or without a probe reading at the end | the card says what happened instead of promising the rest: the rest waits, or EC top-up waits for the probe; locked, it shows in the warning colour with a link to Tank & control (SD-102) |
| Mixing to the target stopped with a pair half in | EC top-up and pH correction wait (the refill does not), their lines and EC top-up's checklist say a part of the pair is missing and to resume or end the mix; resuming completes the pair; ending, a Stop or a restart names the part still missing (SD-102, RAT-054) |
| A new batch planned before the old solution is drained | the probe reads the old tank: "To target with probe" says nothing goes in and to drain and fill fresh water first, as a note, not an error; "As in recipe" says the same with the EC it would reach (SD-102) |
| The hub ends a mix to the target while it measures | the card shows "not mixed" in the warning colour with the reason and what to do; screen readers hear the reason (SD-102) |
| Mixing to the target and the tank already at the target once measured | the card says "nothing needed", not "cancelled" (SD-102) |
| Skipping the mixing-in at the end | not offered, nor "Cancel": nothing is left to dose, and control decides only on a mixed tank; the Stop stays for an emergency (SD-057) |
| Cap plugged straight into a hub port | port red: "Pump cap plugged straight into the hub. Please plug it into the dosing block." |
| Cap after replugging | while the hub runs: event "Pump A not responding", then "Pump A is back" with "Pump 2 on the dosing block. Is it still on the bottle 'Part A'?". A start logs neither, also for devices that answer later, so a pump that first answers after a start asks nothing; Devices shows it offline until it answers (#115) |
| Bottle removed by a slip of the finger | "Remove bottle?" with its name and what follows (stock gone, pump free and still calibrated if it was, the other bottle of a pair no longer paired, #147); only "Remove" removes it, "Cancel" keeps it. A bottle a recipe uses says "Bottle still in use", names the recipes and how to take it out (a pair as a whole); with exactly one recipe it offers "Edit recipe", with several "Got it" (#120) |
| Bottle refilled without telling the hub, run dry, or stock entered too low | the stock goes below zero by the count and stays there; one event says what to do (empty? change it; otherwise enter the amount left; recalibrate only if it keeps happening), the monitoring stays red and a mix is refused until the amount left is entered or "Bottle changed" is tapped (SD-066) |
| Bottle edited out of its pair | the bottle editor says before saving "After you save, "Part B" is no longer paired." (read out with Save), the event log afterwards; an edit that would leave a recipe with half a pair is refused and nothing is saved (#152) |
| Recipe with only part of a pair, or a bottle added later | the recipe editor lists every nutrient bottle (an amount puts it in, clearing it takes it out); before saving, one line above Save names the bottles missing from a pair, a missing name or amount, or an amount below 0, and Save stays off until fixed. The bottle editor does the same when a pair label would leave a recipe with part of a pair, and for a pH bottle a recipe holds (#159) |

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
  writes and every text of the app and the hub with seconds, minutes, hours
  or days (checked by `tools/i18n_keys.test.mjs`). Other numbers with units
  (ml, ml/L) do not yet (#148).
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
