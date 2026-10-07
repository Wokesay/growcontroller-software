# Software decisions (SD)

Binding decisions for the software. Format `## SD-XXX: Title`, sequential,
chronological, two blank lines between entries. No status field; entries
are not deleted, a later SD replaces an earlier one and refers to it.

Product decisions are in the product repository (PD). If a PD affects the
software, an SD "Product requirement (from PD-0xx)" stands here.

**This log starts with the move to this repository (PD-023).** Before
that, decisions were made as PDs in the product repository.

**Decided as PDs in the product repository (2026-10-06 and 2026-10-07):**

- E1 (PD-013), E2 (PD-014), E7 (PD-015), E8 (PD-016), E9 (PD-017).
- E10 decided differently: AGPL-3.0-or-later for code, docs, catalog and
  templates, no CLA, no DCO (PD-022, PD-032, PD-033).
- E11 dropped: there is no subscription (PD-024).
- E12 decided differently for the channels: Stable, Beta, Development
  (PD-036). Versions are self-describing (PD-037). Signed OTA without
  eFuses for the first devices (PD-043, PD-047). SemVer stays a draft.

Product requirements are recorded below as SD entries. The other drafts are the
basis of the prototype and become SD entries once the project owner says
"entschieden" (decided). What needs a PD is marked.

## Log

All entries below were recorded on 2026-10-07. Entries titled "Product
requirement" name what applies from a product decision; the full decision
is in the product repository. From SD-027 on, decisions about this
repository itself are recorded here in full.


## SD-001: Product requirement (from PD-013): standalone software

- The hub is operated through its own web app, without cloud and without
  an account. Home Assistant can be connected optionally over MQTT; the
  logic never lives in Home Assistant.


## SD-002: Product requirement (from PD-014, PD-038): hub platform ESP32-S3

- Control, safety, API, web app and one year of history run on an
  ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB PSRAM). No Linux in the hub.
- An optional companion (Docker, NAS, Home Assistant add-on) provides only
  convenience, never control or safety. A Raspberry Pi runs only the
  simulator (Beta and Development) and, later, the companion.


## SD-003: Product requirement (from PD-015): no dosing without a calibration value

- A pump without a calibration value does not dose; planning and the
  actuator gateway refuse. A missing value is never 0 (RAT-006, RAT-015).


## SD-004: Product requirement (from PD-016): level 0 does not dose pH blindly

- pH correction doses only with a secured measurement path (RAT-044).
  Without a pH probe (level 0) there is a manual measurement and a hint, no
  pH dose by recipe.


## SD-005: Product requirement (from PD-017, PD-023): own repository, moved with history

- The software lives in this repository and was moved with its history;
  `LICENSE` is part of every moved commit. CI, agents and `CLAUDE.md`
  moved along.


## SD-006: Product requirement (from PD-018): the whole plant automation, neutral wording

- The software covers the whole installation: mixing and tank, light with
  dimming, irrigation, climate and phases with their own recipes and
  settings.
- Texts are neutral (greenhouse, indoor growing, hydroponics) and name no
  plant species. The app exists in English and German.


## SD-007: Product requirement (from PD-019, PD-029): 230 V devices via switchable sockets in the local network

- 230 V devices are switched through switchable sockets in the local
  network, Shelly first, without cloud. The hub sets the protection in the
  device and reads it back.
- Inlet and irrigation pump may also run on 230 V sockets. Nothing is ever
  dosed over the network; dosing pumps run only on the dosing block.


## SD-008: Product requirement (from PD-020): behaviour after a power loss

- After a power loss the installation must not put the crop at risk.
  Sequences (dose, pulse, inlet) are never resumed, only reported (RAT-007).
- Fans tend to be on; irrigation only when it is safe or the plants are
  too dry; light depending on how long the power was out. This replaces
  the part of R6 for state functions once implemented; until then
  everything stays off after a restart. The limits are decided in SD-025.


## SD-009: Product requirement (from PD-021): pH and EC as one head or as two

- There are three sensor heads: pH/EC together (with water temperature),
  pH alone, EC alone (with water temperature). Roles stay the same; if two
  heads provide the same value, the user chooses the assignment.


## SD-010: Product requirement (from PD-022, PD-032, PD-033): license AGPL-3.0-or-later

- Code, docs, catalog and templates are licensed under AGPL-3.0-or-later.
  No CLA, no DCO: contributions come in under the same license (inbound =
  outbound). No dual licensing, no commercial license; income comes only
  from the hardware.
- An additional permission under AGPL §7 covers the binary-only ESP-IDF
  libraries (`ADDITIONAL_PERMISSION.md`).
- Devices do not lock out third-party firmware: no Secure Boot against the
  user; users can install their own firmware. Signed updates and any Secure
  Boot must allow this.
- The web app links to the source code of the running version (AGPL §13).


## SD-011: Product requirement (from PD-024): no subscription

- There is no subscription. All functions are free, alarms included.


## SD-012: Product requirement (from PD-025): phase change selectable per phase

- Per phase, the user sets whether the hub moves to the next phase after
  its days by itself or only after confirmation. The harvest is always an
  event the user confirms (RAT-002, RAT-077).


## SD-013: Product requirement (from PD-026): configurable maintenance mode

- The user configures what maintenance mode pauses. Principle: whatever
  directly affects an open tent pauses or at least causes no harm.
- Preset: water and CO2 pause; "water" as proposed means dosing, refill,
  irrigation and the circulation pump automation. Selectable in addition,
  among others: whether climate pauses or humidifier, dehumidifier and
  circulation fan keep running.
- Sensor truth and locks always stay active (RAT-023, RAT-075).


## SD-014: Product requirement (from PD-027, PD-035): language and units

- Setup asks for the language first (English preset, German selectable),
  then for metric or imperial, independent of the language. Units are
  selectable from v1.


## SD-015: Product requirement (from PD-028): dimming output on the hub

- The hub has a dimming output, switchable between 0–10 V and 1–10 V. The
  collection box stays passive.


## SD-016: Product requirement (from PD-030): manufacturer tables as templates

- Manufacturer feeding charts ship as recipe templates. Each template
  carries its source, date and the note "manufacturer data, not binding".
- Templates are data in this repository; new or changed charts come as
  pull requests and are checked before the merge.


## SD-017: Product requirement (from PD-031): irrigation without a readable level

- With an assigned but unreadable level sensor, irrigation is blocked and
  reported; if the plants are too dry, the hub gives a limited emergency
  dose and reports it. Irrigation also works without a level sensor.
- The hub detects from the current draw whether a pump starts or draws
  air and switches off and reports on a dry run (RAT-049, RAT-068).


## SD-018: Product requirement (from PD-034): everything in English

- Everything in this repository and on its GitHub pages is English: docs,
  code, comments, test names, commits, PRs, issues, releases. Code
  comments and test names follow in a separate PR after the move.


## SD-019: Product requirement (from PD-036, PD-037): channels and versions

- Three channels: Stable (live firmware without any simulation code),
  Beta (pre-release for real devices plus simulator download), Development
  (build per commit with all switches, e.g. time-lapse). Simulator
  downloads exist in Beta and Development, not in Stable.
- Versions are self-describing: name and description say what a version
  is and contains; no code names.


## SD-020: Product requirement (from PD-039): rationale register

- Every domain rule learned from operating practice has an entry in
  `docs/RATIONALE.md` with its own ID (RAT-001 …). Code, tests and docs
  cite only this ID.


## SD-021: Product requirement (from PD-043): signed OTA without eFuses

- For the first devices: updates over the network are installed only with a valid signature,
  checked by the firmware (application image), without hardware Secure
  Boot.
- Over USB every owner can always install their own firmware; after that,
  their own key applies to network updates.
- No eFuses are burned; downgrade protection is in software.
- Secrets (for example password hash, sessions, Wi-Fi credentials) are to
  be stored encrypted in NVS.
- Exception (SD-024): the HMAC key for encrypted NVS. Open: whether a test
  lab accepts the USB path with the BOOT button for own firmware, and
  whether and when hardware Secure Boot with owner key slots comes.


## SD-022: Product requirement (from PD-044): a pure level latch releases itself

- A latch caused only by a low level (circulation pump, inlet) releases
  itself once a valid level is back above the minimum level plus a margin;
  the release is reported.
- Dry run, emergency limit and latches with an unknown cause still hold
  until acknowledged (RAT-051, RAT-062, RAT-063). Not implemented yet.


## SD-023: Product requirement (from PD-045): who merges

- Claude merges own PRs with green CI, no open review threads and no
  blocking findings from `reviewer` and `qa` (plus `security`/`ux` where
  needed).
- The project owner's "mergen" is needed for releases, license and
  security rules (for example PD-022), and changes to the actuator gateway
  or to protective cut-offs. The move PR waits for "mergen" (PD-023).


## SD-024: Product requirement (from PD-047): one eFuse for encrypted NVS

- The only eFuse that may be burned is the HMAC key for encrypting the
  NVS (SD-021 otherwise unchanged). It locks out no firmware: every
  firmware on the device, own firmware included, can use it through the
  HMAC peripheral; nobody can read it out.
- It protects secrets (for example Wi-Fi credentials, password hash)
  against read-out flash images, not against someone who loads their own
  code with the device in hand; that stays a documented residual risk.
- Planned alongside (recommended with the question, not chosen
  separately): USB-JTAG off at run time without a further eFuse
  (feasibility open), a recessed BOOT button, sessions stored only as
  hashes.


## SD-025: Product requirement (from PD-048 to PD-053): limits after a power loss

- **Light (PD-048):** after a restart the light comes on only with a
  secured time and only inside the planned light window (with ramp);
  missed light time is not made up, the day ends as planned; without a
  secured time the light stays off and this is reported.
- **"Too dry" (PD-049):** more time has passed since the last dose than
  the maximum pause per phase (default: twice the normal interval, at most
  24 h). Then exactly one emergency dose of one normal dose, a report, and
  waiting again. With a substrate sensor its value counts later. The time
  of the last dose survives a restart; if it is unknown, it is never
  taken as 0 (R5).
- **Fan sockets (PD-050):** sockets of exhaust and circulation fan are
  set to "on after power loss"; light, humidifier, dehumidifier, pumps,
  inlet and heater stay "off after power loss". A socket that changes its
  role gets the setting of its new role (RAT-019 otherwise unchanged).
- **Hub failure (PD-051):** the planned light window is also written as a
  local schedule into the light socket, so the light keeps its rhythm if
  the hub fails (without dimming and ramps); the hub keeps both schedules
  equal. While the hub runs, its own schedule has priority; without a
  secured time it disables the schedule in the socket until the time is
  secured again (PD-053). The ramp runs over the hub's dimming output.
  Irrigation pump, inlet, humidifier and a heater without its own
  thermostat stay protected by their auto-off in the device (the heater
  also by its power limit); a heater with its own thermostat has only the
  power limit. Fans keep their last state. Circulation pump and
  dehumidifier have no auto-off and keep running if they were on (open).
- **Climate devices (PD-052):** humidifier, dehumidifier and heater
  control again after a restart once sensor truth is secured, like the
  fans; the dehumidifier waits for its minimum pause, the heater runs only
  without an active emergency cut-off (neither blocking nor latched), and
  humidifier and dehumidifier never run together. The heater's latch must
  survive a restart.
- Not implemented yet (issue #20); until then R6 applies.


## SD-026: Product requirement (from PD-055 to PD-059, PD-061, PD-062): going public

- **Public repository** (PD-055): this repository becomes public once
  its history no longer carries private references. The history moves
  into a new repository under the same name; the old one stays private
  (PD-056). Open issues are created again with the same numbers; pull
  request #1 and closed issues stay in the old repository, and closed
  placeholders keep their numbers here.
- **Project owner** (PD-057): `CLAUDE.md` names the project owner
  neutrally.
- **Security reports** (PD-058): only through GitHub private vulnerability
  reporting; targets: acknowledgement 7 days, first assessment 30 days,
  disclosure at the latest 90 days after the report.
- **CI** (PD-059): Windows and macOS packages run for every PR that
  changes code and after every merge to `main`, also for release tags and
  on demand. Docs-only changes run only the quick checks (architecture
  rules, `reuse lint`, dependency licenses); for them, "green CI" means
  green quick checks. Superseded PR runs are cancelled; every job has a
  time limit.
- **README** (PD-061): no reference to selling hardware; a safety notice
  and a trademark notice.
- **Recipe templates from a manufacturer chart** (PD-030, PD-062): named
  after the phase with the source behind it, marked "manufacturer data,
  not binding; not affiliated with the manufacturer" with edition and
  date, in the recipe list and in the setup wizard.


## SD-027: Squash only; `main` and release tags are protected

Squash only was decided by the project owner on 2026-10-07; the settings
below were set when the repository went public.

- Pull requests are merged by squash only; the squash commit carries the
  PR title and description. Head branches are deleted after the merge.
- A ruleset protects `main`: changes only through a pull request, the
  quick checks (`quick`) must pass, review conversations must be
  resolved, linear history, no force push, no deletion. Required
  approvals: none (merging follows SD-023). GitHub enforces only the quick
  checks; for code changes "green CI" still means the full CI as well
  (SD-023, SD-026). Only the repository admin can bypass the ruleset; the
  Claude app cannot.
- A ruleset protects `v*` tags: only the repository admin creates,
  moves or deletes them, so a release always needs the project owner.
- Actions: only actions created by GitHub; workflows get read-only tokens
  by default and cannot create or approve pull requests; workflows from
  outside contributors need approval.


## SD-028: Product requirement (from PD-063 to PD-077): restart, clock and hub failure

What the software has to do; hardware details stay in the product
repository.

- **Emergency doses (PD-063, PD-064):** after an emergency dose and
  another maximum pause, the next one follows; at most one per 24 h, each
  reported, until someone acknowledges or watering is safe again. The time
  of the last emergency dose survives a restart. If the time of the last
  dose is unknown, the maximum pause counts from the restart, and "last
  dose unknown" is reported.
- **Light socket without a schedule (PD-065):** the hub sets an auto-off in
  the socket of the remaining light window plus a short buffer, at every
  switch-on and when the window changes. Setup warns that there is no
  fallback for the day rhythm.
- **Circulation pump and dehumidifier (PD-066):** keep their state if the
  hub fails; no auto-off. Setup recommends a dehumidifier with its own
  hygrostat and full-tank cut-off.
- **Time server (PD-067):** the hub offers network time to the sockets and
  sets itself as their time server; it gives out time only while its own
  time is secured.
- **Climate after a restart (PD-068):** if the dehumidifier's last off
  time is unknown, its full minimum pause counts from the restart. Without
  a secured time the night targets apply and "time missing" is reported.
- **Continued clock (PD-069):** the hub saves its time and operating time
  regularly. Without a secured time after a start it continues from the
  saved time; a newer event moves the start forward by at most 1 h. The
  outage counts as 0. This clock is not a secured time (light, socket
  schedule, time server, day and night targets). Intervals within one
  start use operating time; across a restart they use wall time only if
  both moments were secured, never less than the operating time, otherwise
  operating time. This refines PD-069, which asks for wall time whenever
  both moments were secured: within one start a network time step cannot
  stretch an interval. Emergency doses need no
  secured time; the normal watering plan waits for a secured time and
  sensor truth.
- **EC and pH control after a restart (PD-070, PD-071):** every controller
  dose is saved before the pump starts and its result when it ends. If a
  dose was in flight or not yet mixed in, the controller waits the full
  settle time, counted from when circulation runs again; while EC waits,
  pH waits too. Counters, locks and pauses survive a restart. The
  interrupted round is reported with the booked amounts, not resumed. If
  the run-time state is unreadable, the controller waits the full settle
  time once.
- **Secured time (PD-072, PD-073):** sources are network time, a buffered
  clock in the hub, and the device time of the app after the user
  confirms it; the device time never overwrites a secured time. The age of
  the last sync only warns (from about 2 min estimated error); the time
  counts as unsecured only from about 15 min estimated error.
- **Dimming output and 12 V outputs (PD-074, PD-075):** the firmware gives
  a sign of life from the control loop to an external watchdog. After a
  restart the hub sets the light to its planned value again (PD-048).
- **Emergency stop and internal error (PD-076, PD-077):** a manual
  emergency stop stops everything, fans included, until someone resumes,
  also across a power loss or restart. During it the fan sockets are set
  to "off after power loss"; resume restores their setting. The restart
  of fans is configurable per socket (default on, the app warns on a
  change); every other socket stays "off after power loss". An internal
  error switches everything off except the fans on sockets.
- Implemented so far: fan sockets (PD-050); the emergency stop across a
  restart with fan sockets off during it (PD-076); fans kept running on
  an internal error (PD-077); the continued clock with operating time
  (PD-069); clock steps that keep the remaining time of deadlines and
  locks; secured time from network time in the firmware. Intervals across
  restarts are prepared (`elapsedS`) but not used yet. The per-socket fan
  setting in the app and the rest follow in stages (issue #20).


## SD-029: Product requirement (from PD-100): product decisions only in the product repository's session

- The software is worked on in a session of its own that has only this
  repository. The product repository's session works on hardware and
  product and is the only place where product decisions (PD) are
  written, so PD numbers do not collide.
- Product requirements arrive as English issues in this repository and
  are recorded here as "Product requirement (from PD-0xx)" where useful.
- A product question met here is named in the issue and to the project
  owner, not decided here; it is decided and recorded in the product
  repository's session.
- SD and RAT IDs are still assigned here. The private mapping of RAT IDs
  to their sources stays in the product repository.

---

## Drafts (waiting for "entschieden")

| # | Draft | Core | Needs PD |
|---|---|---|---|
| E1 | Standalone software | Web app on the hub, without cloud and account. Home Assistant only optional via MQTT, then read-only with a few commands; logic never lives in HA. | decided: PD-013 |
| E2 | Platform ESP32-S3, option C | Control, safety, API, web UI and 1 year of history on the ESP32-S3-WROOM-1-N16R8. No Linux in the hub. Optional "companion" (Docker/NAS/HA add-on) only for convenience. | decided: PD-014 (confirmed by PD-038) |
| E3 | One core, two platforms | C++17 without platform headers; host simulator as a digital twin; ESP-IDF instead of Arduino; re-evaluate Rust later. | no |
| E4 | Web app | Preact + TypeScript + Vite, gzip in the app image (atomic update, rollback takes the UI along), budget 250 KB, live via SSE, hash routing, no external resources. | no |
| E5 | Configuration model | Catalog as data with fixed kinds of prerequisites; roles bound to the device ID; two state axes (setup/run time); phases as parameter sets; run-time state separate from the configuration. | no |
| E6 | Architecture rules R1–R8 | Gateway is the only actuator path; watchdog sees only the read model; a missing value is never 0; after a restart everything is off; the catalog can only tighten safety; one event loop with an injected clock. CI checks them by machine. | no |
| E7 | No dosing without a calibration value | stricter than RAT-015; gateway and planning refuse | decided: PD-015 |
| E8 | Stage 0 does not dose pH blindly | pH correction only with a secured measurement path (RAT-044); in stage 0 manual measurement and a hint | decided: PD-016 |
| E9 | Own repository | Private repository `Wokesay/growcontroller-software` (PD-017), moved with its history (PD-023); one-way mirror `ref/software/` in the product repository | decided: PD-017, PD-023 |
| E10 | License | Decided differently: AGPL-3.0-or-later for firmware, UI, docs, catalog and templates; no CLA, no DCO (inbound = outbound); no dual licensing; additional permission under AGPL §7 for the binary-only ESP-IDF libraries; devices do not lock out the owner's own firmware. The draft had proposed GPL-3.0-or-later with DCO, Apache-2.0 for interfaces and CC BY-SA 4.0 for docs. | decided differently: PD-022, PD-032, PD-033 |
| E11 | Free and subscription | Dropped: there is no subscription; all functions are free. | dropped: PD-024 |
| E12 | Versions and updates | SemVer, Keep a Changelog with a "Security" section, self-describing versions without code names (PD-037), channels Stable, Beta and Development (PD-036, instead of stable/beta), GitHub Releases with SBOM and checksums, OTA A/B signed (the owner can still install their own firmware, PD-022), update only when idle, sign offline | channels: PD-036; signed OTA: PD-043/PD-047; SemVer: draft |
| E13 | Access | Mandatory password before any function, PBKDF2, lockout after failed attempts; local HTTPS before the first device at third parties | no (required by EN 18031) |
| E14 | Bug reports | technical issues via GitHub with forms; in the app "Report a problem" with a case number and diagnostic package, without telemetry | no |
| E15 | Roles and flow | agents `triage`, `reviewer`, `qa`, `security`, `release`, `ux`, `domain`; flow in `WORKFLOW.md`; issue workflows read-only | no |

Reasoning, options and sources: `CONCEPT.md`, `CONFIGURATION.md`,
`RELEASE.md`, `SECURITY_MODEL.md`, `WORKFLOW.md`.
