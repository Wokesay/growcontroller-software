# Changelog

All notable changes to growcontroller-software. Format according to
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/), versions
according to [SemVer](https://semver.org/). Security-relevant changes are
listed in every release under "Security".

## [Unreleased]

### Added
- **Ready to go public** (PD-055 to PD-062, SD-026): a safety notice and a
  trademark notice in the README; `SECURITY.md` takes reports only through
  GitHub private vulnerability reporting, with targets of 7 days
  (acknowledgement), 30 days (first assessment) and at most 90 days to
  disclosure.
- **Quick checks** (`.github/workflows/checks.yml`): architecture rules,
  `reuse lint` and the dependency license check run on every PR, also for
  docs-only changes; the full CI skips docs-only changes (PD-059).
- **Own repository with history:** the software moved from the product
  repository into `Wokesay/growcontroller-software` with its 17 commits;
  every moved commit carries `LICENSE`.
- **Licensing:** AGPL-3.0-or-later for code, docs, catalog and templates
  (`LICENSE`, `LICENSES/`, `REUSE.toml`, an SPDX header in every source
  file); an additional permission under AGPL §7 for the binary-only ESP-IDF
  libraries (`ADDITIONAL_PERMISSION.md`); `THIRD_PARTY_NOTICES.md`. CI runs
  `reuse lint` and a license check of all npm dependencies
  (`tools/check_licenses.mjs`).
- **Source code link** (AGPL §13): the login page and Settings › Display
  and info link to the source code of exactly the running build (the
  commit, set at build time). CI and release builds fail without a
  revision; otherwise the app says the version is unknown.
- **Rationale register** `docs/RATIONALE.md`: one entry per domain rule
  (RAT-001 … RAT-084) with rule, reason, evidence, implementation and
  tests. Code and docs cite only these IDs.
- **Decision log** `docs/DECISIONS.md` starts: SD-001 to SD-027 record the
  product decisions that apply to the software.
- Simulator download for Windows, macOS (Apple silicon and Intel) and
  Linux: one file with the web app embedded; a double-click starts the demo
  and opens the browser, and the data is stored next to the program. If the
  port is busy, it takes the next free one; if the folder is not writable,
  the demo runs in memory only. Releases attach the packages automatically,
  with the license notices of the bundled libraries
  (`THIRD_PARTY_LICENSES.txt`, also in the web package; with the
  third-party parts in nlohmann/json and the Apache-2.0 text). A firmware
  image goes into a release only once it is signed offline
  (`docs/RELEASE.md`); CI still builds the firmware on every run.
- **Setup in five steps** (Start, Devices, Tank, Nutrients, Calibrate): a
  fixed bar with Back, Skip and Next; completed steps can be clicked;
  explanations (ⓘ) for usable volume, minimum level, bottle, pair and
  calibration; Nutrients starts with a template and its preview, and
  existing recipes are visible.
- **Recipe templates** with a preview, mapped to your own bottles instead of
  matching by name: Two-part nutrient, "Vegetative wk 1–4" and "Flowering
  wk 1–2" per the Athena Blended Feed Program A01.004. API:
  `POST /recipes/template` takes `map` (part → bottle), reports missing
  parts in `missing` and rejects one bottle for two parts; a pair from the
  template carries over to bottles without a pair of their own, as "AB2"
  and so on if the name is already taken. The manufacturer source is also
  available in English (`sourceEn`). The previous templates `athena_pro_veg`
  and `ab_basic` are dropped.
- **Growing area (schema v2):** setup asks where the plants are (room,
  tent, greenhouse), with a name of your own. In the data model this is a
  zone with its own roles (`zone.*`, previously `tent.*` on the tank); the
  configuration is migrated at startup. API: `PUT /zone`. For third-party
  API clients: `/roles/tent.*` is now `/roles/zone.*`; series recorded
  before the update keep the old name.
- **pH and EC as one head or two:** new device classes "Sensor head pH"
  and "Sensor head EC" (with water temperature) next to the combined pH/EC
  head. Measurement roles are assigned only when exactly one device fits.
  Calibrate and Devices show the calibrations per head from the catalog; in
  the simulator both variants can be plugged in, and faults hit the
  matching head. Which hardware variant will exist is open (PD to follow);
  the software supports both.
- **Switchable sockets (Shelly, local), in the simulator:** sockets (Plug S
  Gen3, Power Strip 4 Gen4) appear in setup and under Devices. For each
  outlet you say what is plugged in (circulation pump, light, exhaust fan,
  circulation fan, humidifier, dehumidifier, irrigation pump, inlet) and
  test it (on for 3 s). When a socket is added, the hub sets every outlet
  to "off after power loss"; when an outlet is assigned, the hub writes the
  profile's protection setting into the device (auto-off for humidifier,
  irrigation pump and inlet) and assigns it only once the read-back
  matches; before every switch-on it checks the setting again. On the
  device, discovery via mDNS, login and RPC follow. Which sockets are
  recommended is open (PD to follow). API: `POST /roles/{rolle}/test`,
  `POST /roles/{rolle}/switch {on}` (manual mode).
- **Validation:** a switched output serves only one role, and the channel
  must be within the device's range; the setting "valve open at most"
  allows no more than 25 min.
- **Climate, Light and Irrigation areas:** own pages in the navigation with
  readings and switched outputs, including manual mode; the overview shows
  the room climate and all assigned switched outputs. Automatic control
  (light schedule, climate, irrigation schedule) does not exist yet.
- **VPD:** air VPD without a leaf offset; if an input value is missing,
  there is a gap instead of 0 (Rationale: RAT-017). Formula FAO-56 eq. 11.
  The two values may be at most 60 s apart (own rule, assumption). Shown in
  the history together with air temperature, humidity and CO2.
- **Simulator:** simple room climate (light and heating warm the room, the
  exhaust fan exchanges air, humidifier and dehumidifier, evaporation while
  the light is on; assumptions); climate and CO2 heads deliver values. The
  demo has a climate head and a power strip with light, exhaust fan,
  circulation fan and humidifier. An outlet without Wi-Fi keeps powering
  its load (room climate, circulation). New fault `stuck`: the outlet
  rejects switching commands and keeps its state; the device's auto-off and
  power loss still take effect. The simulator counts the switch-ons per
  outlet (`switchOns`), so tests reliably detect short pulses.
- **German and English:** setup, navigation, app shell, numbers and dates;
  more pages follow. The language is stored on the hub and can be chosen
  per browser.

### Security
- **Safety profiles in the actuator gateway** for sockets and 12 V
  outputs: `dauer` (continuous), `puls` (pulse), `kompressor`
  (compressor).
  - Humidifier and dehumidifier never run at the same time; if the state
    of the other device is unknown, this one stays off (Rationale: RAT-034,
    R5).
  - Dehumidifier: 5 min pause after switching off, also after an emergency
    stop and a restart (Rationale: RAT-034).
  - Maximum run time for the humidifier (5 min, assumption), the irrigation
    pump (10 min, assumption) and the inlet (25 min, Rationale: RAT-079);
    the device switches off by itself shortly after.
  - Irrigation pump only above the minimum level, switched off when the
    level drops below it while running; locked when the level is
    unreadable (deviation from RAT-068; since decided in PD-031, which adds
    a limited emergency dose and irrigation without a level sensor, not
    implemented yet).
  - Humidifier: if a humidity sensor is assigned, only with a valid value
    below 85 % (assumption).
  - Reassigning or removing first switches the old output off; if that
    fails, the event log shows "Off not confirmed".
  - Protective cut-offs (dry run of the circulation pump, inlet emergency
    limit, irrigation pump, maximum run time): if switching off fails, the
    hub reports "Off not confirmed" instead of "off", once per reason; the
    inlet distinguishes level, emergency limit and open time. The hub keeps
    trying as long as the reason or the latch persists, and reports "Off
    confirmed" as soon as the output reads as off. Before, the log said
    "off", and for the circulation pump and the inlet it added a new entry
    on every cycle.
  - If the circulation pump runs or the inlet is open although the latch
    has not been released yet (switching off failed, button on the
    device), the hub switches it off again.
  - Maximum run time: if the device has already switched off by itself
    (auto-off), there is no further switching attempt. After reassigning,
    tracking of the old output ends without an all-clear. An emergency stop
    ends the retries at the maximum run time (open; fallback: auto-off in
    the device).
  - Unassigning a role: if the output does not switch off, the log shows
    "Off not confirmed" (as with reassigning).
  - Emergency stop and restart switch off all switched roles; circulation
    fan and exhaust fan do not run on (Rationale: RAT-036).
  - Heaters are not available as a role yet: only once the latching
    emergency shutdown exists (Rationale: RAT-060).

### Changed
- **Repository rules** (SD-027): pull requests are merged by squash only;
  `main` accepts changes only through pull requests with green quick
  checks; `v*` release tags can only be set by the project owner.
- **Recipe templates from a manufacturer chart** are named after the
  phase with the source behind it ("Vegetative wk 1–4 (per Athena
  A01.004)") and say "manufacturer data, not binding; not affiliated with
  the manufacturer" with edition and date of the source: in the template
  dialog of the recipe page, in recipes created from it and in the
  template cards of the setup wizard (PD-030, PD-062). The source line is
  easier to read, and long recipe names wrap on phones.
- **CI:** caches compiler output (ccache, saved only on `main`) for the
  core and the firmware; every job has a time limit; only superseded PR
  runs are cancelled.
- The README no longer mentions selling hardware (PD-061); `CLAUDE.md`
  names the project owner neutrally (PD-057).
- **English:** docs, README, CONTRIBUTING, SECURITY, templates, agents,
  workflows and `CLAUDE.md` are in English; docs, templates and packaging
  files have English names (for example `docs/CONCEPT.md`,
  `docs/INVARIANTS.md`, `sim/README.txt`, `tools/package.sh`). Code
  comments and test names follow in a separate PR.
- **Contributions** need no sign-off any more (no DCO, no CLA);
  inbound = outbound under the AGPL.
- "Report a problem" and the issue templates point to this repository; the
  label of a bug report is `bug`.
- Decisions: E1, E2, E7, E8 and E9 are decided as PD-013 to PD-017 in the
  product repository; the plant automation concept refers to PD-018 to
  PD-021 (scope and languages, sockets, power loss, pH/EC heads). The
  behaviour after a power loss (PD-020) is not implemented yet.
- Embedded texts (catalog, changelog) as byte arrays, so the core also
  compiles with MSVC.
- Catalog version 2 (roles `zone.*`, separate pH and EC heads),
  configuration schema 2.
- Naming: "Port 1–8" on the hub, "Pump 1–6" on the dosing block, "sensor
  head", "Room" instead of "Tent", tagline "Plant automation". Ports show
  the device's icon.
- The app uses the full screen width; tiles wrap, and setup tables become
  cards on phones.
- Messages say "minimum level", "Port n" and "Pump n on the dosing block".

### Fixed
- Reading tiles: when a sensor failed, the notice and the chart stuck out
  of the tile.
- Demo: the phase "Blüte" (flowering) pointed to a missing recipe. IDs now
  transliterate umlauts ("Blüte" → "bluete").
- Recipes that use the same bottle twice are rejected.
- Names (hub, area, tank, device, bottle, recipe, cultivation run) are cut
  to 40 bytes without splitting a character; before, an umlaut at the
  limit made the configuration unreadable.
- When you add a device, the hub assigns only that device's measurement
  roles; a role you unassigned on purpose stays unassigned.
- Simulator: "freeze value" keeps the last value instead of "no value".
- `PUT /system`: if one field is rejected, the others do not change
  either.
- Simulator: on macOS the server binds without `SO_REUSEADDR`; a data
  folder that is not writable is remembered ("in memory only") instead of
  forgotten; the packaging script also detects Windows when run locally.

## [0.1.0-proto.1] – 2026-10-06

First prototype. Runs in the simulator; the firmware for the ESP32-S3 is a
skeleton without a bus. No compatibility promises.

### Added
- **Core (C++17, platform-neutral):** catalog with capabilities, device
  classes, roles, functions and templates; configuration with migration
  and validation in three stages; resolver with setup states and
  checklists; phases provide parameters.
- **Sensor truth:** freshness, frozen values, plausibility, jump lock
  (survives a restart), calibration of pH, EC and the level curve;
  announced changes explain jumps (Rationale: RAT-042).
- **Dosing:** mixing by recipe with A:B as a pair, manual dose, pump
  calibration and tube priming; partial runs with actual run time and a
  booking per run; a job ID per attempt; "catch up" after a block.
- **Control:** EC and pH (Rationale: RAT-053 and others, see
  `docs/RATIONALE.md`), refilling via the inlet, circulation pump; states
  idle, controlling, waiting, blocked, latched; a checklist per controller.
- **Watchdog** that only evaluates (OK, problem, neutral).
- **History** in three tiers (10 s, 1 min, 15 min), event log, CSV export.
- **API** `/api/v1` with a live channel (SSE) and simulator endpoints.
- **Web app:** Overview, Mixing, Tank & control, History, Recipes &
  bottles, Devices, Functions, Settings, setup wizard in 8 steps; light and
  dark, mobile.
- **Simulator** as a digital twin with the scenarios `neu` (new, empty
  hub), `stufe1` (stage 1) and `demo`, fault injection, time-lapse and
  restart.
- **Firmware skeleton** for the ESP32-S3 (ESP-IDF): core, web app in the
  image, API, storage, 12 V outputs, A/B partitions.
- **Tests:** unit and scenario tests (doctest, also with ASan/UBSan),
  end-to-end tests (Playwright), architecture check
  `tools/arch_check.sh`, CI for simulator, web app and firmware.
- **Updates and changelog** in the app (a mock-up in the simulator).

### Security
- Mandatory first password, no default password; PBKDF2-HMAC-SHA256 with
  salt; lockout after 5 failed attempts; session as an HttpOnly/SameSite
  cookie.
- Security headers (CSP, `X-Frame-Options`, `nosniff`).
- Actuators only via the actuator gateway with fixed limits in the code;
  the configuration (also via import or a phase) can only tighten them.
  After a restart everything is off; no dosing without a calibration
  value.
- Doser with a deadline per run; an abort books what has already run; bus
  job IDs are unique across restarts.
- Origin check against CSRF and DNS rebinding; constant-time hash
  comparison; if the password is lost, setup over the network is locked.
- Broken inputs and files lead to 400/500 or to "everything off" with an
  alarm, not to a crash. Simulator: scenario reset only when logged in.
- Not included yet: HTTPS, signed OTA, Secure Boot
  (`docs/SECURITY_MODEL.md`).
