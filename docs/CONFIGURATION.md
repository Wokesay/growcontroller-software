# Configuration model: catalog, roles, functions, resolver

As of 2026-10-06. Draft by `architekt`, implemented in the prototype.

## 1. The chain in one sentence

A **device** reports **capabilities**. A **role** binds a capability to a
place, such as "pH in the tank", via the **device ID**. A **function**
requires roles, calibrations and settings. For each function, the
**resolver** says whether it works and what is missing. The UI then
expands or collapses settings accordingly.

```
Device (DB-7A31C0, PHEC-3F2A91 …)        ← detected at the port, accepted by the user
  └─ Capability (measure.ph, dose.peristaltic, switch.12v …)
       └─ Role (tank.ph, tank.circulation, canister "Part A" → pump)
            └─ Function (control pH) ← requirements from the catalog
                 └─ Parameter (target pH …) ← default ⊕ setting ⊕ active phase
```

## 2. Catalog (`catalog/catalog.json`)

The catalog is data. It is embedded at build time and ships with the
firmware. Contents:

- **capabilities**: unit, decimal places, plausibility band, freshness
  limit, jump threshold.
- **deviceClasses**: stage, attachment (hub port, dosing block port,
  collection box, inside the hub, `net` for mains sockets), channels,
  provided capabilities, calibration need, shop text.
- **roles**: place → accepted capabilities (`accepts`, e.g. 12 V output
  or mains socket), whether it goes into the history as a series, and for
  switched outputs the safety profile (`profile`: `dauer` continuous,
  `puls` pulse, `kompressor` compressor, `heizen` heating) with a maximum
  run time (`maxOnS`).
- **functions**: stage, group, text, hard and soft requirements,
  parameters with type, range, default and the flag "can be overridden by
  the phase".
- **templates**: recipe templates.

**Fixed requirement kinds** (new kind = code, new function = data only):

| Kind | Example | Checks |
|---|---|---|
| `role` | `{"role":"tank.ph","calibrated":true}` | hardware present → role bound → calibrated → value valid (runtime) |
| `canisters` | `{"canisters":{"min":1,"kind":"ph_down","calibrated":true}}` | canister of this type with a pump, calibrated, detected |
| `config` | `{"config":"recipe"}` | recipe present, usable volume set |
| `function` | `{"function":"circulation"}` | other function enabled |
| `device` | `{"device":"pump_cap"}` | device class present |

Scripting languages (Lua/JS) in the catalog were rejected: they enlarge
the attack surface, and safety would depend on the script (`architekt`).

## 3. States per function (two axes)

**Setup** (resolver):

| State | Meaning | UI |
|---|---|---|
| `unavailable` | no device of the required class | "For this you need: pH/EC head", visible under Devices › Expand |
| `needs_setup` | device present; assignment, calibration or setting missing | checklist with "Do it now →" |
| `limited` | all hard requirements met, a soft one missing | "Limited: without a circulation pump … stir" |
| `ready` | complete | switch enabled |

**Runtime** (controller): `off`, `idle` (resting), `working`
(controlling), `waiting` (waiting), `blocked` (switch-on lock, clears
itself), `latched` (latched, needs acknowledgement; Rationale: RAT-062).

A set-up device that is not responding right now does not make a
function `unavailable`. It shows "blocked: device not responding".

## 4. Configuration tree (`config.json`)

```
config {schemaVersion, revision}
├─ system      name, time zone, language, update channel, update check
├─ limits      cap per manual dose, run-time limits
├─ devices[]   device ID → class, name   (flat inventory)
├─ tanks[]     usable volume, minimum level, water, roles{tank.* → device/channel}
├─ zones[]     growing area: name, kind (room/tent/greenhouse), tank, roles{zone.* → device/channel}
├─ canisters[] name, type (nutrient/pH−/pH+), pump, pair, colour, size
├─ recipes[]   steps in dosing order (canister, ml/L)
├─ functions{} enabled, parameters
├─ calibrations{device → ph | ec | tank_curve}
└─ grow        cultivation run: none | running | completed; phases = parameter sets
```

Stored separately:

- `state.json`: stock, known volume, learned effects, latches, jump locks,
  manual measurements. It changes all the time; that needs no new
  configuration revision.
- `auth.json`: hash and salt only.
- `events.json`, `history.bin`.

**Versioning:**

- `schemaVersion` is an integer. Migrations are pure functions
  vN → vN+1 (`migrateConfig`) and are tested.
  - v1 → v2: roles `tent.*` on the tank become `zone.*` on the first zone.
    Series and jump locks under the old names are not renamed; the climate
    history from before the update stays under `tent.*`.
- If the file cannot be read, the hub starts with factory settings and all
  actuators off. It reports this loudly and saves the broken file as
  `config.broken.json`.
- `revision` counts every change. It is meant as an ETag against
  concurrent editing.

**Validation in three stages:**

1. Structure, when reading.
2. References: role → device with a matching capability; recipe →
   canister.
3. Domain and safety:
   - pH never in the recipe;
   - pairs complete;
   - a pump on one canister only;
   - a canister only once per recipe;
   - roles in the right place (`tank.*` on the tank, `zone.*` on the zone), at most one zone, and it has a name;
   - a switched output for one role only, channel within the device's range;
   - parameters within range, tolerance never 0.

**Phases:** effective parameters = catalog default ⊕ setting ⊕ active
phase (only parameters with `phase: true`). Controllers see only this
view. Renaming a phase changes nothing (test M15-1).

## 5. Binding to the device ID

- **The port is a runtime attribute** ("last seen on port 3"), not
  configuration. Re-plugging changes nothing.
- **When a device is accepted**, a measuring role is bound automatically
  if there is exactly one candidate. The hub never binds dosing or
  switching roles automatically.
- **If a cap comes back after re-plugging**, the hub asks in the event
  log: "Is it still on Part A?". It cannot detect electrically which
  canister the cap sits on.

## 6. Open points

- **Multiple tanks:** the data model is ready; UI and logic use one tank.
- **Declare loads on switched outputs** (kind, "normally closed", power)
  and attach rules to them. Example: heater only on `switch.mains` with
  auto-off.
- **Swap assistant:** the old device is missing, a new one of the same
  class is present.
- **Calibration age:** a notice after N days; whether soft or hard is
  open.
- **JSON Schema for the configuration**, so the UI can validate in
  advance.
