# Changelog

All notable changes to growcontroller-software. Format according to
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/), versions
according to [SemVer](https://semver.org/). Security-relevant changes are
listed in every release under "Security".

## [Unreleased]

### Added
- **Home Assistant, read-only spike** (`docs/HOME_ASSISTANT.md`): a
  server (`gc_ha_server`) runs the core next to Home Assistant, reads
  mapped sensor entities (pH, EC, water and air temperature, level in L,
  humidity, CO2) through its REST API, runs them through the sensor truth
  and serves the web app. It switches nothing and only reads states from
  Home Assistant. pH, EC and level from it are shown but are no values for
  control, since the hub has not checked a calibration done in Home
  Assistant (RAT-025); the hub offers no calibration of its own for them.
  A missing unit (except pH) or an unreadable report time gives no value.
  A refused token stops reading until restart, so Home Assistant does not
  ban the computer. Their tiles say "display only" with age and trend
  instead of "not calibrated", the monitoring says "calibrated outside the
  hub", and the setup step does not ask for a probe calibration. The
  setup for the hub's own dosing hardware is not offered on a read-only
  hub.
- **Fan sockets come back on after a power loss** (PD-050, SD-028):
  binding a socket to the exhaust or circulation fan sets it to "on after
  power loss"; every other socket stays "off". After a restart the hub
  leaves fans as they are. A socket that loses its fan role (unassigned,
  moved, device removed, configuration imported) goes back to "off"; the
  hub reads the setting back and reports a socket that keeps "on". The
  catalog marks such roles with `afterPowerLoss: "on"`; the core accepts
  it only for the exhaust and the circulation fan, as continuous loads
  without a maximum run time. Fan sockets bound with an older version
  report "Schutzeinstellung weicht ab" and need to be assigned again.
- **The emergency stop survives a restart** (PD-076, SD-028): it is kept
  in the run-time state; after a power loss or restart everything stays
  off, fans included, and the hub reports "Not-Halt besteht weiter" until
  someone resumes. During the stop the fan sockets are set to "off after
  power loss"; resume restores their setting.
- **Internal error keeps the fans running** (PD-077): the safe state after
  a caught error switches everything off except the fans on sockets.
- **Secured time and the continued clock** (PD-069, PD-073, SD-028): the
  hub knows whether its time is secured (in the firmware after the first
  network time sync). It saves its time and an operating time with its
  run-time state. Without a secured time after a start it continues from
  the saved time (a newer event moves it forward by at most 1 h) and
  reports "Uhrzeit
  nicht gesichert" after 2 min; when the time is secured later it reports
  the jump. `GET /api/v1/state` shows `time` (`secured`, `source`,
  `operatingS`). Intervals within one start use operating time; across a
  restart they use wall time only if both moments were secured, otherwise
  operating time, so an outage never stretches them (prepared for the
  dosing intervals that follow). A clock step keeps the remaining time of
  jump locks, maintenance and controller pauses; a jump lock never holds
  longer than 15 min after a step. An unreadable run-time state keeps the
  hub stopped (copy in `state.broken.json`) until someone resumes.
- **Simulator:** a power cut can last a while (`outageMin`, the world runs
  on without power) and leave the hub without a secured time
  (`timeSecured: false`); `mainsLost: false` restarts only the hub. The
  action `time` secures the time later, takes it away again or steps it
  (`stepS`). The socket fault `crash` makes the hub's tick fail, to test
  the internal-error path.
- **Public repository** (PD-055 to PD-062, SD-026): a safety notice and a
  trademark notice in the README; `SECURITY.md` takes reports only through
  GitHub private vulnerability reporting, with targets of 7 days
  (acknowledgement), 30 days (first assessment) and at most 90 days to
  disclosure.
- **Quick checks** (`.github/workflows/checks.yml`): architecture rules,
  `reuse lint` and the dependency license check run on every PR, also for
  docs-only changes; the full CI skips its heavy jobs for them (PD-059,
  SD-030).
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
- **Decision log** `docs/DECISIONS.md` starts: SD-001 to SD-026 record the
  product decisions that apply to the software; SD-027 is the first
  decision about this repository itself.
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
- **A damaged event cannot break the app** (SD-032): the web app reads
  the hub's messages defensively (only its own table keys, only strings
  as text, at most four nested levels). If a page or its event list
  still fails to draw, it shows a notice with a reload button while
  navigation and STOP stay usable. The hub caps the key and text of
  events it loads.
- **Release hardening** (#29): `release.yml` checks that a release tag
  points at a commit on `main`; every file is built in jobs without write
  access and without a cache; the publishing job only checks the files
  against their checksums, attests their build provenance (check with
  `gh attestation verify` naming the release workflow and tag, see
  `docs/RELEASE.md`) and creates the release. A second SBOM lists
  the C++ libraries of the simulator. All actions are pinned by commit SHA
  and updated by Dependabot after a 7-day cooldown; a test keeps them
  GitHub's own and pinned. Pull requests that change the release
  machinery run the release as a dry run.
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
- **The overview points to devices waiting to be accepted:** a banner
  names how many were detected and links to Devices.
- **Probe calibration accepts only the kinds a device class offers**
  (RAT-025): a pH calibration on an EC head, for example, is refused with
  `probe.not_offered` instead of starting a calibration session that
  paused control on that device for 10 min.
- **The hub speaks English with keys** (#18, SD-032): controller lines,
  their checklists and the monitoring (headline and assessments) come
  from the hub as a key, the values and an English text. A German page
  shows them in German, with numbers in the page language; a single
  passing check now reads "1 Prüfung" instead of "1 Prüfungen". Events,
  job messages and errors follow the same way: the history stores key
  and values, so it switches language too; older events keep their text.
  The remaining hub texts follow. For API clients: `watchdog.headline` and each item's
  `label` and `text` in `/state`, and each event's `title` and `text`
  in `/events`, are now `{key, text, args}` instead of a string; a few line keys are new (`ph.start`, `circ.on_dosing`,
  `circ.on_always`, `circ.on_interval`, `circ.latched`, `circ.off`).
  When the dosing of a job step cannot start or go on, the answer now
  has the error key `job.start_failed` and the cause in `args.reason`
  (before: `mix.start`, `job.step`, `dose.start`, `cal.start`,
  `prime.start`, and for `/dose` the cause's own key such as
  `dose.too_small`); checks of the request itself keep their keys. The job message keys
  `job.dosing` and `job.stopped` are now `job.dosing_step` and
  `job.emergency_stop`, and a single failed dose reports
  `job.dose_failed`. A pump calibration's result also returns `changed`
  and `message`. Going back to an older version keeps the events but
  shows them without titles, since it cannot read the new form.
  Events name a released latch, the kind of a sensor calibration, the
  removed role and the trigger of an emergency stop in words instead of
  internal IDs; a cancelled mix reads "Mix cancelled". The calibration
  window shows the hub's result, including the warning to check the
  tubing when the rate changed clearly, and then offers to calibrate
  again; after a failed run, closing the window cancels the run. A pump
  that reports a stored rate of 0 no longer counts as a previous rate.
  The actuator gateway and the doser follow: why an output or pump may
  not run (`act.*`), how a dose ended (`dose.*`), switch-off reports and
  protective cut-offs carry keys, so dose events, "may still be on" and
  dry-run or inlet cut-offs read in the page language; details a device
  reports itself (for example why a smart plug refused), the names of
  outputs from the catalog (for example "Zulaufventil") and a sensor's own
  reason still show in German. A latch's `why` is now a message; a state saved
  before keeps its text, while going back to an older version shows such
  a reason as "[object Object]" on the Tank page. For API clients, three
  refusals of `/roles/{role}/switch` got their own keys: a refusing
  output `act.output_refused` (was `act.bus`), humidity too high
  `act.humidifier.rh_high`, and the watering pump's `act.irrigation.level`
  split into `act.irrigation.min_missing` and `act.irrigation.low`.
- **The app's texts move into the language tables** (#18): every page,
  the shared widgets and the simulator panel show their own texts in
  English when English is chosen; the hub's messages follow later. Numbers use
  a decimal point on an English page and a comma on a German one, also
  in input fields and range hints, and a field shows its value again
  when the language changes (PD-035). The issue that "Report a problem"
  prepares is always in English, as the repository is (PD-034). German
  stays the default until the hub's messages are done. The buttons that
  reorder a recipe now name the bottle for screen readers, and in English
  releasing a latch reads "Release" everywhere.
- **No collection box** (PD-078, SD-031): every sensor head and the
  dosing block hang directly on a hub port. The device issue form no longer offers the
  collection box, the catalog attaches the climate and CO2 heads to a hub
  port, and the docs no longer list it as a connection.
- **Merging gateway changes** (SD-033): a technical change to the
  actuator gateway or to protective cut-offs that changes no logic is
  merged once `reviewer`, `qa` and `security` accept it; logic changes
  there still wait for the project owner's "mergen".
- **CI** (SD-030): the full CI runs on every pull request and push to
  `main`; for docs-only changes it skips its heavy jobs. A summary check
  `ci-ok` lets GitHub block a merge while the full CI is red or still
  running.
- **Product decisions** (PD-100, SD-029): the software is worked on in a
  session of its own with only this repository; product decisions are
  written only in the product repository's session. Requirements arrive
  here as issues, product questions are named instead of decided, and SD
  and RAT IDs are still assigned here (`CLAUDE.md`, `docs/WORKFLOW.md`).
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
- **Recipes from a template keep the language of the page** (#32): a
  recipe created from a template was always stored with the German name
  and note, also in English. The recipes page now sends its language
  (`lang`), the hub's system language is the default; the list of missing
  bottles uses that language, and bottles named "Part A"/"Part B" match
  the template's parts.
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
