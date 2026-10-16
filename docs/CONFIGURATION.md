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
- **deviceClasses**: stage, attachment (`hub_port`, `dosing_port`,
  `builtin` inside the hub, `net` for mains sockets), channels, provided
  capabilities, calibration need, shop text.
- **roles**: place → accepted capabilities (`accepts`, e.g. 12 V output
  or mains socket), whether it goes into the history as a series, and for
  switched outputs the safety profile (`profile`: `dauer` continuous,
  `puls` pulse, `kompressor` compressor, `heizen` heating) with a maximum
  run time (`maxOnS`). `afterPowerLoss: "on"` makes a mains socket with
  this role come back on after a power loss (exhaust and circulation fan,
  PD-050); it is allowed only with the profile `dauer`. Every other
  socket stays off after a power loss.
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
itself), `latched` (latched, needs acknowledgement; Rationale: RAT-062;
the circulation pump's dry-run latch from a low level also releases
itself once the tank is refilled, SD-067).

A set-up device that is not responding right now does not make a
function `unavailable`. It shows "blocked: device not responding".

## 4. Configuration tree (`config.json`)

```
config {schemaVersion, revision}
├─ system      name, time zone, language, update channel, update check, what the app is to do per area (control, SD-037)
├─ limits      cap per manual dose, run-time limits
├─ devices[]   device ID → class, name, a sign of life (SD-042)   (flat inventory)
├─ tanks[]     usable volume, minimum level, water, roles{tank.* → device/channel}
├─ zones[]     growing area: name, kind (room/tent/greenhouse), tank, roles{zone.* → device/channel}
├─ canisters[] name, type (nutrient/pH−/pH+), pump, pair, colour, size
├─ recipes[]   steps in dosing order (canister, ml/L, fixed: a fixed addition per litre of fresh water, never scaled by EC control, RAT-085); the chart's EC at these amounts (refEc) and its pH range (ph [low, high]), each null when not given (SD-057)
├─ functions{} enabled, parameters
├─ calibrations{device → ph | ec | tank_curve}
└─ grow        cultivation run: none | running | completed; phases = parameter sets
```

Stored separately:

- `state.json`: stock, known volume, learned effects, latches, jump locks,
  the time of the last self-release of a latch (SD-067), manual
  measurements. It changes all the time; that needs no new
  configuration revision.
- `auth.json`: hash and salt only.
- `events.json` and `history.bin` (snapshots: at every start, once a day,
  and early when the event journal reaches 5000 events) with the journals
  `events.log` and `history.log` (appended as events and samples come;
  `docs/HISTORY.md`).

Writing (#68): `config.json`, `state.json`, `auth.json` and `job.json` are
written whole and atomically (temporary file, then rename). A new
password, a configuration change and a STOP are on disk when the click
returns (Linux: `fsync` of the file and its folder; macOS and Windows:
best effort) and survive a power cut right after it. Other changes to the
run-time state (stock, a latch acknowledged, a manual measurement) follow
with the next tick, within a second. Files are created for the owner only.
If the data folder takes nothing (full, not writable), the hub keeps
running and logs one alarm "Data is not being saved". Every 10 s it writes
the small files that failed again from what it holds (state with the STOP
first, a configuration changed meanwhile included), and the large
snapshots, which also replace a failed journal, with a growing pause up to
10 minutes (from the start again once a small file can be written, and
once more when the hub shuts down). A write that fails leaves no
temporary file behind, so a STOP still fits on a nearly full card. "Data
is being saved again" comes once all of them are on disk. A new password is
refused with "Not saved" instead of pretending. The Home Assistant server
does not start with a data folder it cannot write.

Reading (SD-081): a file that is there but cannot be read (no permission,
a read error) is kept as it is. The hub starts as with a broken file, says
so with one alarm per kind of data, and does not write over the file while
it is kept; a snapshot and its journal are kept together. `state.json`: the
hub starts stopped, and the first Stop or Resume, or a new lock or latch,
writes it anew ("Saved state replaced"). `config.json`: factory settings. `auth.json`: no first
password over it (423 `auth.kept`). `job.json`: a second mix within 30
minutes of the start needs confirmation, and the next job writes it anew.
Changes the kept files would hold stay in memory; a restart after the file
is readable again reads it as it was.

**Versioning:**

- `schemaVersion` is an integer. Migrations are pure functions
  vN → vN+1 (`migrateConfig`) and are tested.
  - v1 → v2: roles `tent.*` on the tank become `zone.*` on the first zone.
    Series and jump locks under the old names are not renamed; the climate
    history from before the update stays under `tent.*`.
- If the file cannot be read, the hub starts with factory settings and all
  actuators off. It reports this loudly and saves the broken file as
  `config.broken.json`. A file it cannot open or read at all is kept as
  it is instead, and not written over in that run (SD-081).
- `revision` counts every change. It is meant as an ETag against
  concurrent editing.

**Validation in three stages:**

1. Structure, when reading: a count or index (phase, days, channel,
   schema version) is a whole number, never null or a fraction cut to
   one; a time the hub cannot have written reads as not set. A saved
   file with such a value still loads (a schema version that is no whole
   number starts the factory settings, as a broken file does); the next
   change is refused with the reason, and for a grow cycle's phase or
   days only while that cycle runs: finishing it is the way out.
2. References: role → device with a matching capability; recipe →
   canister.
3. Domain and safety:
   - pH never in the recipe;
   - pairs complete;
   - a pump on one canister only;
   - a canister only once per recipe;
   - roles in the right place (`tank.*` on the tank, `zone.*` on the zone), at most one zone, and it has a name;
   - a switched output for one role only, channel within the device's range;
   - a running grow cycle's phase is one of its phases, and its phases' days are 0 or more;
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
- **If a cap comes back after re-plugging** while the hub runs, after
  the hub logged it as not responding, the hub asks in the event log:
  "Is it still on the bottle 'Part A'?". It cannot detect electrically which canister
  the cap sits on. A start asks nothing, also for a cap that answers
  later (#115).

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
