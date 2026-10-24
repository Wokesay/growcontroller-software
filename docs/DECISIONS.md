# Software decisions (SD)

Binding decisions for the software. Format `## SD-XXX: Title`, sequential,
chronological, two blank lines between entries. No status field; entries
are not deleted, a later SD replaces an earlier one and refers to it.

Product decisions are in the product repository (PD). If a PD affects the
software, an SD "Product requirement (from PD-xxx)" stands here. Control
details (locks, releases, thresholds, sequences, which devices and pumps
are allowed) are decided here as SDs along the product's guideline; a PD
on such a detail is a proposal, and an SD recorded from it stays until a
later SD changes it (SD-091).

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

SD-001 to SD-030 were recorded on 2026-10-07; later entries name their
date. Entries titled "Product requirement" name what applies from a
product decision; the full decision
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
- Changed by SD-076: on either of the DevKit's USB ports no BOOT button is
  needed to install firmware; whether a test lab accepts that is open.


## SD-022: Product requirement (from PD-044): a pure level latch releases itself

- A latch caused only by a low level (circulation pump, inlet) releases
  itself once a valid level is back above the minimum level plus a margin;
  the release is reported.
- Dry run, emergency limit and latches with an unknown cause still hold
  until acknowledged (RAT-051, RAT-062, RAT-063). Implemented for the
  circulation pump with SD-067 (#27); the inlet waits for a product answer.


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
- Changed by SD-076: the recessed BOOT button guards flashing on neither
  of the DevKit's ports: on the board's USB port and, through its
  auto-program circuit (to be confirmed on the schematic), on the UART
  port a connected computer starts download mode without it.


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
  a sign of life from the control loop to an external watchdog. Changed
  by SD-045: the sign of life comes from the safety component only. After a
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

- The software is worked on in a session of its own with access to this
  repository only. The product repository's session works on hardware and
  product and is the only place where product decisions (PD) are
  written, so PD numbers do not collide. Changed by SD-069, which SD-072
  replaces: the session does not access the product repository.
- Product requirements arrive as English issues in this repository. A
  requirement that comes from a PD is recorded here as "Product
  requirement (from PD-xxx)".
- A product question met here is named in the issue and to the project
  owner, not decided here; it is decided and recorded in the product
  repository's session. Changed by SD-069, which SD-072 replaces: it
  becomes a `product-question` issue in this repository. Changed by
  SD-091 (from PD-318): a control detail is not a product question; it is
  decided here.
- SD and RAT IDs are still assigned here. The private mapping of RAT IDs
  to their sources stays in the product repository.


## SD-030: GitHub enforces the full CI through the summary check `ci-ok`

Replaces the sentence in SD-027 that GitHub enforces only the quick
checks (issue #35).

- The full CI (`ci.yml`) starts on every pull request and every push to
  `main`. It no longer uses `paths-ignore`: a workflow that does not start
  leaves a required check pending forever.
- Its first job `changes` compares the change with its base
  (`tools/ci_changes.mjs`, in the repository because only GitHub's own
  actions are allowed, SD-027). A docs-only change (the paths ci.yml
  ignored before, PD-059: `docs/`, `*.md`, `LICENSES/`, `.claude/`, issue
  templates) skips the heavy jobs: core with sanitizers and E2E, firmware,
  simulator packages. Any other change, or a range the filter cannot
  compare, runs them all.
- Its last job `ci-ok` always runs. It fails if a job failed or was
  cancelled, if the filter gave no result, or if a job was skipped
  although the change touches code.
- The ruleset on `main` requires `quick` and `ci-ok`, both from GitHub
  Actions; the project owner adds `ci-ok` once this change is on `main`.
  For a docs-only change "green CI" still means green quick checks
  (SD-026); `ci-ok` passes for it within about a minute.
- The merge rule (SD-023) is unchanged; GitHub now enforces its "green
  CI" as well. The repository admin can still bypass the ruleset.
- `ci-ok` judges a PR against the base it had when CI ran. After the base
  branch of a PR changes, a new push to the PR (for example "Update
  branch") runs CI again before the merge; re-running the old run would
  test the old base again.
- "Docs only" means only that CI cannot check a file. `CLAUDE.md`,
  `.claude/` and `SECURITY.md` steer agents and process and are reviewed
  as configuration.


## SD-031: Product requirement (from PD-078): no collection box

Recorded on 2026-10-08.

- Every sensor head and the dosing block hang directly on a hub port;
  there is no collection box in the tent. This replaces the sentence on
  the collection box in SD-015.
- The catalog attaches the climate and CO2 heads to a hub port; the issue
  form for devices no longer offers the collection box (issue #46).


## SD-032: The hub speaks English with keys; the web app translates

Recorded on 2026-10-08 (#18).

- Every text the core produces (controller lines, checks, monitoring,
  errors, events) carries a stable key, its values as arguments and an
  English text. The English text is what other API clients and logs see.
- The web app shows a hub text in German through its own table
  (`web/src/lang/msg.ts`, by the same key, filled with the arguments) and
  in English as the hub sends it. The German texts move from the core
  into the web app.
- Events store key and arguments, so the history follows the page
  language too. Events written before keep their stored text.
- Numbers in arguments are numbers, not formatted text, so the web app
  writes them in the page language (PD-035).
- A test fails when the core can send a key that has no German text in
  the web app.


## SD-033: Gateway and cut-off changes without a logic change merge after the subagents accept them

Decided by the project owner on 2026-10-08 in the software session, and
introduced with PR #66, which the owner approved with "mergen". It refines
SD-023 for this repository until the product session adapts PD-045; if
that session decides otherwise, its decision applies and a later SD
records it.

- A technical change to the actuator gateway or to protective cut-offs
  that changes no logic is merged by Claude under SD-023's other
  conditions (green CI, no open review threads, `ux` where the UI is
  touched), once `reviewer`, `qa` and `security` have accepted it with no
  blocking findings. Examples:
  messages and texts, signatures, refactoring with unchanged behaviour,
  tests.
- `security` confirms in its review that switching behaviour, cut-offs,
  latches and the emergency stop are unchanged; the PR says so and names
  the tests.
- Texts of warnings and cut-offs keep their safety meaning; `security`
  checks this too.
- Existing gateway and cut-off tests stay green with no assertion removed
  or weakened. A change to persisted safety state (latches, cut-offs) has
  a test that loads a state saved before.
- A decision about the logic (what is switched and when, limits, latches,
  cut-off conditions) is put to the project owner with its reasons, and
  the PR waits for "mergen". So does any change where it is unclear
  whether it touches the logic, or where `security` cannot confirm
  unchanged behaviour.
- Releases, license and security rules, and changes to this rule, still
  wait for "mergen" (SD-023).


## SD-034: The Home Assistant trial as an app with a ready-made image

Decided by the project owner on 2026-10-10 in the software session
("GitHub baut vorab"). The trial stays read-only and a trial (PD-038: a
Raspberry Pi never runs the control); whether the product goes the Home
Assistant way is still a product question.

- The read-only Home Assistant trial installs as a Home Assistant app
  (formerly add-on) from this repository: `repository.yaml` at the root,
  the app in `ha/app/`.
- GitHub builds the image ahead of time; Home Assistant downloads it
  instead of building it on the Raspberry Pi. The image is
  `ghcr.io/wokesay/growcontroller-ha` with the version as its tag, for
  `amd64` and `aarch64` under one name. It holds only the static
  `gc_ha_server`, the web app and the license texts (`FROM scratch`).
- Only GitHub's own actions (SD-027): the image is built with the plain
  `docker` command, on a runner of each architecture, not with Home
  Assistant's builder actions. `app.yml` builds and starts it without
  write access, on every code change and for a release; only
  `release.yml`'s `publish` pushes it to ghcr.io, for a tag `vX.Y.Z`,
  after checking it against the checksum the build gave, and attests it.
- `gc_ha_server --app` takes Home Assistant's address and token from the
  Supervisor (`http://supervisor/core`, `SUPERVISOR_TOKEN`); nothing is
  configured and no token is made by hand. The app asks only for
  `homeassistant_api` and one port; any further right in `config.yaml`
  needs a review (`tools/app.test.mjs`).
- The web app is served on port 8099 of the Home Assistant host, plain
  HTTP, for the own network. Ingress (Home Assistant's login and HTTPS) is
  a follow-up. Changed by SD-077 (from PD-225): from 0.1.0-proto.5 on the
  app runs behind Ingress, and port 8099 is closed by default.
- The project owner makes the package public once, after the first
  release with the app; releases still wait for the owner's "mergen" and
  tag (SD-023, SD-027).
- Accepted by the project owner on 2026-10-10 for the experimental phase:
  any workflow of the repository can be given `packages: write` and could
  overwrite the published image, outside the tag-gated release job, so
  everyone with write access to the repository could. No separate release
  credential for now; this is looked at again before the app leaves
  `stage: experimental` (`docs/SECURITY_MODEL.md`).

Notes from the implementation (Claude, after the reviews of PR #77; they
apply the rules above and add no new ones):

- `ha/app/config.yaml` on `main` is the app's distribution manifest:
  every installed app compares itself with its version. A version change
  there is therefore part of a release and, like the release, waits for
  "mergen" (SD-023). This is a process rule; no GitHub rule enforces it.
- The release workflow's `check` job rehearses the push in every dry run
  against a throwaway `registry:2` service on the runner. That image is
  third-party code, not an action, so SD-027 does not cover it: it is
  pinned by digest, gets no token, runs only in the read-only job and is
  updated by hand. The smoke test's stand-in for the Supervisor is the
  build's own pinned Alpine base image.


## SD-035: Freshness of sensors that report on their own schedule is learned

Decided by the project owner on 2026-10-11 in the software session, after
the first test of the Home Assistant app showed pH, EC and water
temperature as outdated although their sensors reported every few minutes
("Takt je Sensor lernen", then "mindestens 5 min" and, for VPD, "solange
beide gültig sind"). It applies RAT-023 and RAT-059 to such sensors and
changes RAT-017's pairing for them.

- A device class whose sensors report on their own schedule, not when the
  hub asks, is marked `ownReportSchedule` in code, never by catalog data,
  so the catalog cannot loosen freshness (R7). The Home Assistant classes
  are marked; the hub's own probes are not and keep the catalog's fixed
  limits (`maxAgeS`).
- For a marked sensor the sensor truth learns the gaps between its reports
  from the last ten. The usual gap is the second longest, so one outage
  does not loosen the limit. A value is stale after three usual gaps,
  never sooner than 5 min and never later than 30 min. Until three gaps
  are seen (after a start, or when another sensor is picked for the role)
  the limit is 10 min. A report dated again by less than 2 s is the same
  report. Nothing of this is saved; a restart learns again. Factor, count,
  5, 10 and 30 min are own rules (assumptions).
- The 5 min lower limit follows RAT-027: sensors that report by
  themselves, often over radio, drop a few reports now and then; RAT-027's
  evidence put the line between a dropout and a defect at about 5 min.
- "unavailable" in Home Assistant shows at once (the device is offline),
  as before.
- VPD: if either reading is on its own schedule, both only need to be
  valid, however far apart their reports are, because such sensors often
  report only on a change. Two readings of the hub's own stay paired within
  60 s (RAT-017).
- The learned limit and this pairing are for display. Before a value from
  Home Assistant may feed control, control needs its own freshness check
  (`docs/HOME_ASSISTANT.md`, "Before any of this may feed control").
- The stale message carries the limit (`limitS`) for the web app.
- Changed by SD-042: a sensor with a sign of life is stale after 60 s
  without a measurement; the learned limit stays for sensors without one.

## SD-036: Any numeric Home Assistant sensor can be picked, with a search

Decided by the project owner on 2026-10-11 in the software session.

- Besides the suggestions (by Home Assistant's device class and unit), the
  picker offers "Andere Sensoren anzeigen" (show other sensors): every
  numeric sensor in Home Assistant, searchable by name and entity ID.
- A sensor is taken only in a unit that fits the measurement or that the
  hub converts exactly (RAT-006, RAT-015). Otherwise the app says why and
  does not take it.


## SD-037: An onboarding after the first sign-in to the Home Assistant app

Decided by the project owner on 2026-10-11 in the software session.

- After the first sign-in a short guide leads through: basics (name of the
  controller, time zone, language, colour scheme), one sensor per
  measurement, and what the app does automatically: per function
  "automatisch" (automatic) or "außerhalb der Steuerung" (outside the
  control, for example in Home Assistant or by hand). Then done.
- It can be skipped and opened again from Settings.
- While the trial switches nothing, the guide records the choice and says
  that the app does not switch yet.
- Texts that say "von Hand" (by hand) where the app cannot operate a
  switch are reworded ("manuell", "außerhalb der Steuerung"), so nobody
  expects to switch a device through the app.
- Built as: `/willkommen` with four steps (basics, sensors, control, done),
  opened by itself on a read-only hub until the setup is done; skipping it
  or finishing it marks the setup done (`POST /setup/complete`). The
  choice per area (climate, light, irrigation, refill, circulation,
  dosing) is `system.control` in the configuration, set with
  `PUT /system {control}`; an area not chosen is not listed. It feeds no
  control yet: today it only decides what the area pages show instead of
  switches, where it can also be changed in one tap. The colour scheme stays in the browser, as in Settings.
- A choice recorded while the app switched nothing is no consent to
  switch. The first version that lets `system.control` feed control asks
  again, or treats an earlier "auto" as not chosen; until then "outside"
  is what the step shows when nothing is chosen.


## SD-038: Raw values are preferred, and calibrated in the app

Decided by the project owner on 2026-10-11 in the software session.

- A Home Assistant sensor that delivers a raw value (for example a pH
  probe's voltage in mV) is calibrated in the app, as the hub's own probes
  are. Its values then pass the sensor truth's calibration check (RAT-025).
  Whether such values may feed control through Home Assistant is part of
  the product decision on switching through Home Assistant.
- A sensor calibrated in Home Assistant stays display only (RAT-025).
- Where a sensor offers both, the picker suggests the raw value.
- Whether growcontroller's own hardware should deliver raw values to Home
  Assistant is a product question for the product repository.
- Which raw values, and how they are checked: SD-041.


## SD-039: The history records shown values, hides empty tiles and has a combined view

Decided by the project owner on 2026-10-11 in the software session.

- The history records every value that passes the sensor truth's checks,
  also one calibrated elsewhere (display only, RAT-025); a gap stays a gap,
  never 0 (R5). Control still reads only usable values.
- A history tile shows only when its series can have data: a sensor is
  assigned, or for the volume from mixes, the hub mixes.
- A switch between single tiles and a combined grow view: every curve on
  one time axis, stacked, each on its own scale, with one cursor that shows
  all values at the same moment. One chart with one axis is not used: pH,
  EC, temperature, humidity and CO2 differ too much in their units and
  ranges.
- A tile's small trend line shows for such values too, as the history
  holds them.


## SD-040: Emergency stop, pause and ending a grow

Decided by the project owner on 2026-10-11 in the software session.
Changed by SD-045: the function is called "Stop", in German "Stopp".

- Where nothing is switched (the Home Assistant trial), the web app shows
  no emergency stop, which would stop nothing. A notice "Nur Anzeige"
  (display only) says to switch off in Home Assistant or at the device. A
  stop set by an earlier version can be cleared ("Not-Halt aufheben"), and
  the overview then does not claim that everything is off.
- On the hub the emergency stop stays one tap and one confirmation: in an
  emergency nobody types.
- The stop button gets a menu: the emergency stop, and the maintenance mode
  as the pause, with a chosen duration and a simple confirmation. There is
  no pause without an end: a forgotten pause would leave the plants
  without water.
- Ending or aborting a grow asks for confirmation and for the controller's
  name, typed in.


## SD-041: Which raw values from Home Assistant the app calibrates

Decided by the project owner on 2026-10-11 in the software session, after
the `domain` review of SD-038. Built in three steps.

- **pH:** a probe's voltage in mV or V from a front end without gain. The
  app takes it as the pH an ideal probe would read (7 − E/59.16, the Nernst
  slope at 25 °C) and calibrates it with two buffers, with the hub's own
  check: the slope band 0.8–1.25 is 47.3–74.0 mV/pH (RAT-025). No
  temperature compensation, as on the hub's own probes.
- **Level:** an immersion (pressure) probe's voltage behind a
  current-to-voltage converter, calibrated with litre points like the
  hub's own level sensor (RAT-078). No ultrasonic level sensing (project
  owner).
- **EC:** a conductivity the sensor already compensates to 25 °C, which the
  user marks as not calibrated in Home Assistant, calibrated with one
  reference solution (RAT-025, factor 0.5–2).
- Not yet, each needs a rule from the product repository first: pH boards
  with gain, EC from a voltage, level as a current or a pressure. Such a
  sensor shows no value rather than a guessed one.
- A calibration point takes the first report after it was asked for, and
  waits at most the sensor's learned freshness limit (SD-035); a report
  from before may still be from the tank, not the buffer (RAT-006).
- A raw signal whose unit changes after the pick has no value: the old
  calibration never meets a new meaning (RAT-006).
- Hardware recommendations (an EC module with compensation, a combined
  pH/EC probe, a converter with noise filtering for the immersion probe)
  are product questions.
- Built so far: pH (step 1). The device class `ha_ph_raw` offers the
  calibration and is not marked `externalCalibration`; both are set in
  code, never from catalog data (R7). A voltage is suggested for pH when
  Home Assistant calls it pH or its ID or name says pH; any other voltage
  can be picked from the other sensors, also with Home Assistant's device
  class `voltage`, an exception to SD-036. The app's calibration refuses
  one that does not move between the buffers. A pH from Home Assistant in
  pH stays display only (SD-038).
- Own rules of step 1, from the reviews (no RAT; numbers marked as
  assumptions where they are): a voltage above 5 V is no pH signal and is
  not passed on (assumption: no front end without gain reads more); a
  point takes a report more than 2 s after the request (as SD-035 takes
  reports under 2 s apart as one) and fails at once if the sensor goes
  offline during its wait; a calibration ends when its window closes and
  with its device; left for 30 min without a step it stops explaining
  jumps (RAT-042) but keeps pH and EC control waiting until it is saved or
  cancelled, as the probe may still be in a buffer (a confirmation that
  the probe is back in the tank is a product question); a device whose reported class differs from
  the configured one has no value, and an import gives a calibration only
  to a class that offers it; the standstill check of a sensor on its own
  schedule counts by its reports. Until it is calibrated, the app shows
  the voltage in mV, not a guessed pH. The Nernst slope is one constant
  (`gc::kNernstMvPerPh`); the hub's own probe head must report on the same
  raw scale.
- Changed by SD-042: with a sign of life, the standstill check and a
  calibration point count by the last measurement.


## SD-042: Freshness from a sign of life per sensor

Decided by the project owner on 2026-10-11 in the software session, after
the first test of the Home Assistant app showed a calm water temperature
as "1 min" old although its sensor reported every 30 s. Changes SD-035
and SD-041 for sensors with a sign of life; RAT-027 is updated with it.

- **Why:** Home Assistant drops a state equal to the previous one (its
  ESPHome integration does unless `force_update` is set, and helpers
  written only on a change do the same). A calm sensor and a silent one
  then look alike, and the app showed the time since the last change.
  `force_update` on every sensor is no way out: it makes Home Assistant
  record every reading.
- **A sensor is fresh by its last measurement, not by its last change.**
  The app takes the time of the last measurement from a sign of life,
  assigned per sensor:
  - a sign-of-life text of the device, sent at least every 15 s: the age
    of each of its channels in whole seconds, as
    `n=<counter>|<code>=<s>|<code>=<s>|…` (`-` for a channel without a
    value since the device started). The counter changes the text on
    every send, so Home Assistant never drops it. The app suggests the
    channel code for a sensor; the user confirms it at the assignment.
    The age is the channel's age plus the age of the text;
  - or a timestamp sensor that gives the time of the sensor's last
    reading (Home Assistant's device class `timestamp`).
- **60 s:** a sensor with a sign of life is stale when its last
  measurement is older than 60 s. Stale has no usable value, as a missing
  value (RAT-006). A sign-of-life text older than 60 s means the device
  sends nothing at all: all its sensors are stale, with one message for
  the device, not one per sensor. A channel shown as `-` is stale once the
  app has seen it so for more than 60 s in a row (counted again after the
  app starts).
- **Never looser by accident:** an assigned sign of life that is
  unavailable or unreadable, a channel code missing from the text, or an
  age that lies in the future makes the sensor stale with a message; it
  never falls back to the learned limit. Ages up to 2 s in the future
  (whole-second rounding) count as 0.
- **Standstill and calibration** use the same clock: with a sign of life
  the frozen check (RAT-023, SD-041) counts by the last measurement, as
  Home Assistant drops the unchanged reports of a stuck signal; a
  calibration point (SD-041) takes the first measurement more than 2 s
  after the request and fails when the sensor turns stale.
- **Display:** the age stands next to the value, in whole seconds rounded
  half up: green up to 30 s, yellow from 31 s, red from 60 s (the limit).
  A healthy channel read every 30 s often shows yellow; that is accepted.
  The colour is for display only; whether a value may be used shows its
  state.
- **Without a sign of life** SD-035 stays (learned, never sooner than
  5 min, never later than 30 min), and the app says "unchanged since"
  instead of "ago", since it cannot see the last measurement.
- Substrate sensors read every 30 s over the slow sensor bus get 90 s once
  they exist. The hub's own sensors keep the catalog's limits; the
  firmware knows each reading's time.
- 60, 30 and 90 s are a rule from operating practice (RAT-027). The text
  format is the app's own; it will be documented in
  `docs/HOME_ASSISTANT.md` with an example for an ESPHome device and the
  advice to keep the text out of Home Assistant's recorder.
- Changed by SD-047: details from the implementation (each channel
  measured at least every 30 s, picking the channel from a list, an own
  rule for a sign of life that does not fit, a 3 s margin for a
  calibration point, what goes out, only classes on their own schedule).


## SD-043: Firmware base: own ESP-IDF firmware that speaks the ESPHome native API

Decided by the project owner on 2026-10-11 in the software session. It is
the proposal that PD-151 asks of this repository (#91), reviewed by
`security`; it goes back to the product session through #91.

- The firmware stays our own ESP-IDF project with the same core, REST API
  and web app as the simulator and the Home Assistant app. It is not built
  on ESPHome.
- **Why not ESPHome as the frame:** ESPHome fixes its entities when the
  firmware is built [1]; our roles and devices change at run time in the
  web app. The safety component must control every flash write and the
  order of an update (actuators off first, #94); in ESPHome its own
  preference writes and its update path decide that [2]. The core already
  runs on ESP-IDF. No GPL-3.0 runtime next to our AGPL code (ESPHome's
  C++ runtime is GPL-3.0 [3]); the libraries keep their own licences.
- **Home Assistant** connects through the ESPHome native API, implemented
  in our firmware:
  - off by default, switched on by the owner after login (PD-142);
  - encrypted connections only, with a 32-byte key from the hardware RNG,
    different on every device, shown once, replaceable, stored in
    encrypted NVS;
  - limits on connections, frame size and handshake time;
  - read-only first; later commands only as jobs through `Actuators`
    (R1), with the same checks as the REST API, and logged;
  - its protocol logic platform-neutral in a module outside `core/` (no
    network headers there), with a socket binding per platform, so the
    simulator speaks it too; CI tests it against the client library
    version Home Assistant uses, and a fuzz target covers frames and the
    handshake.
- **No own cryptography:** noise-c with libsodium (as ESPHome itself uses
  [4]) and nanopb, pinned by hash, listed in the SBOM and in
  `THIRD_PARTY_NOTICES.md`.
- **Updates** (SD-021, SD-024): signed app images without hardware Secure
  Boot, checked before an image may boot; an own downgrade check against
  the version in the signed image; only when idle; a self-test before the
  version counts as valid; no eFuse burned except the HMAC key for NVS
  encryption; the owner can always flash their own firmware over USB.
- **Flashing** (PD-148): ESP Web Tools in the browser, shipped with the
  release and pinned (no CDN at run time); the release lists SHA-256
  checksums of the images. Two builds: DevKitC-1-N32R16V (octal flash,
  32 MB) and N8R8 as the fallback with a smaller history (PD-162, #91).
- Whether product texts may name ESPHome is a product question (trade
  mark). MQTT may come later as an option.
- Accepted risk: more code of our own (API, updater, Wi-Fi setup) to
  maintain, fix and disclose (CRA).
- Sources, retrieved 2026-10-11: [1] https://github.com/esphome/esphome/blob/dev/esphome/core/application.h ;
  [2] https://github.com/esphome/esphome/blob/dev/esphome/components/esp32/core.cpp ;
  [3] https://github.com/esphome/esphome/blob/dev/LICENSE ;
  [4] https://github.com/esphome/esphome/blob/dev/esphome/components/noise/__init__.py


## SD-044: Wi-Fi setup of the ESP: over USB while flashing, a setup network with a device key for later changes

Decided by the project owner on 2026-10-11 in the software session;
reviewed by `security`.

- **First setup over USB:** right after flashing in the browser, ESP Web
  Tools offers "Configure Wi-Fi" (Improv Wi-Fi over serial, on the
  DevKit's UART port, not USB-Serial-JTAG, SD-024) and then opens the web
  app on the device, where the user sets the password.
- **One-time setup token:** the link back carries a token that setting
  the first password requires, so nobody else on the home network can set
  it first. It is the only secret that ever goes over the serial port:
  valid for 15 min, used once, void once a password is set; every new
  Improv session over USB gives a new one (a closed tab means: connect
  over USB again).
- **Later Wi-Fi change:** over USB again, or through a setup network of
  the ESP:
  - only after a long press on the BOOT button, open for at most 10 min,
    one client, closed after setup;
  - WPA2 with a random key per device (at least 80 bits from the hardware
    RNG, made at the first start, stored in encrypted NVS), shown in the
    web app after login to print;
  - never an open network, never a fixed, shared or derivable key; a lost
    key means setup over USB.
- Improv and the setup network only set the Wi-Fi credentials. They never
  set or reset the password and never return a stored secret.
- Setting up or changing Wi-Fi never switches an actuator and never
  restarts the device while a job runs; running jobs carry on (R6,
  PD-161). The credentials are stored only when no job runs, ordered by
  the safety component like every flash write (#94).
- Open: whether the BOOT button can be reached on the carrier board
  (product question); whether the UART counts as a physically protected
  interface under EN 18031 (`regulatorik`).
- Changed by SD-076: Improv also runs on the board's USB port
  (USB-Serial-JTAG), where opening the port can restart the ESP, as can
  opening the DevKit's UART port (DTR and RTS), so "never restarts the
  device while a job runs" does not hold over USB (the restart counts as
  a power loss); the
  BOOT button can be reached (PD-175) and is held 3 s for the setup
  network; pre-flashed devices get the key at production, on the label
  (random per device, so not the "fixed key" SD-044 rules out, though a
  factory reset brings it back),
  have the setup network open while no Wi-Fi is configured and set the
  first password in a 15-min window instead of with the token.


## SD-045: Product requirement (from PD-145 to PD-174): control and safety on the ESP32-S3 on carrier board v0.1

Recorded on 2026-10-11 from #91 and #94 (SD-029). The issues hold the
full behaviour; this entry names what changes earlier decisions.

- The ESP32-S3 DevKit on the carrier board runs all control and all
  safety without Home Assistant or any other computer; Home Assistant and
  the Home Assistant app are optional and hold no logic (PD-149, PD-150).
- A safety component apart from control enforces the limits: the board's
  outputs (dosing pumps, refill valve) are off after boot and stay locked
  until the safety component releases them; after a normal start it does
  so itself once its checks pass (our reading of #94, to be confirmed in
  the product repository), after a stop only on the user's request (below).
  Smart plugs keep their state as before (PD-050, SD-028). Every outside command
  a job with a fixed end; one dosing pump at a time with a dose end by
  timer; per pump a maximum run time, a volume budget and a pause, tested
  on their own, because the pumps have no time limit in hardware (PD-147,
  PD-160); a job from outside runs to its end when the connection drops,
  new ones only with a connection (PD-161).
- The sign of life to the board watchdog comes from the safety component
  only, never from the control loop; it replaces SD-028's "from the
  control loop" (PD-165). A flash erase can hold up the sign of life past
  the watchdog's window, so no pump or refill job runs during an update or
  a write to NVS; the write waits for the job's end (#94). Smart plugs,
  light and circulation are not switched for it.
- After a stop, outputs are released only when the user chooses "Resume"
  and the button is released, never by themselves, also not after a
  restart (PD-174). The function is called "Stop" ("Alles stoppen" in
  German), never "emergency stop" or "Not-Halt" (PD-172).
- Pin assignment, bus devices and conversion values of the board are
  public firmware configuration (PD-157, PD-168, PD-170).
- Built as M1 in `docs/ROADMAP.md`.
- Changed by SD-084: in the pause between two runs of a job, with no pump
  running, the hub saves the bookings (PD-239); and PD-222 adds a cut-off
  of about 90 s per pump run on a later board, independent of the
  firmware. Which board brings it is the product side's.


## SD-046: MPL-2.0 is allowed for lightningcss as a build tool

Decided by the project owner on 2026-10-11 in the software session, for
Vite 8 (Dependabot #58), which needs lightningcss.

- The license check (`tools/license_policy.mjs`) allows MPL-2.0 for
  lightningcss and its platform builds as build tools: Vite runs them on
  our CSS, and none of their files end up in the hub, the simulator or the
  Home Assistant app. MPL-2.0 binds only copies of its own files; the CSS
  that comes out stays ours.
- Only these packages: other build tools such as Vite and Rolldown put
  code of their own into the app, so being a build tool does not by itself
  mean never shipped. Another package under MPL-2.0 needs a decision of
  its own, and shipped packages stay limited to the permissive list.
- Other copyleft licenses (GPL, LGPL, AGPL, EPL, MPL-1.1) stay excluded.
- `tools/check_licenses.test.mjs` tests both: lightningcss as a build tool
  passes, the same package shipped or any other package under MPL-2.0
  fails.


## SD-047: Sign of life: details from the implementation

Decided by the project owner on 2026-10-12 in the software session, from
the reviews of the implementation of SD-042. Changes SD-042; what is not
named here stays as SD-042 says.

- **30 s per channel:** besides the text every 15 s, each channel is
  measured at least every 30 s, or a healthy one would come too close to
  the 60 s limit.
- **The same counter dated again** is the old send, also after an outage
  of the text or of Home Assistant: a restored or retained text gives no
  fresh sign of life.
- **Picking instead of a suggestion:** the user picks the sensor's
  channel from a list at the assignment; SD-042 had the app suggest a
  code. Which channel belongs to a sensor cannot be told from the times
  alone, as every live channel reports often. The list shows the
  device's own text first (its entity ID shares a whole word after
  `sensor.` with the sensor's), marked as probably the same device,
  another device's marked as a warning, and each channel with its age
  now; recommended codes (`ph`, `ec`, `wt`, `at`, `rh`, `co2`, `lvl`)
  are marked as fitting.
- **A silent device:** each of its sensors is stale with the device's
  message ("the device sends nothing"), not a message about its own age.
  SD-042 had one message for the device; readings are per sensor, and one
  message per device in the watchdog is a follow-up (done in #107: one
  problem titled with the readings it affects).
- **Own rule for a sign of life that does not fit:** one sent more than a
  second after the sensor's last report that dates the sensor's last
  measurement more than 3 s before that report belongs to another sensor
  and makes it stale (each time is dated to a second). Right after a
  reconnect this can show until the next measurement; it fails safe.
- **Only a class whose sensors report on their own schedule** (set in
  code, R7) takes a sign of life; the hub's own probes keep the catalog's
  limits.
- **What goes out:** the list of signs of life Home Assistant offers
  (texts and timestamp sensors, at most 100 of each) is sent only while
  someone picks one; otherwise only their count and the names of those in
  use. A sign of life is set in the web app and stored in the hub's
  configuration, not in the mapping file.
- **Calibration point:** the first measurement more than 3 s after the
  request (SD-042: 2 s), as two whole-second roundings and the delay add
  up.
- **Standstill clock:** only switching a sensor's sign of life on or off
  starts it over; a round offline or without a value does not.
- **Open:** the jump lock of a calm sensor from Home Assistant (a value
  unchanged for 5 min drops out of its window) must be solved before such
  values feed control. The 90 s for substrate sensors is not built yet.


## SD-048: One release carries every package

Decided by the project owner on 2026-10-12 in the software session.

- The firmware for the ESP32-S3, the Home Assistant app and the simulator
  are one software: the same core and the same web app. They are released
  together: one version (`VERSION`), one tag `vX.Y.Z`, one changelog, one
  GitHub release.
- The release carries every package: the firmware image per board profile
  (DevKitC-1-N32R16V and N8R8, SD-043) once firmware goes into releases,
  which happens only signed offline (`docs/RELEASE.md`); the Home Assistant
  app image; and in Beta and Development the simulator (PD-036).
- Every package carries the version, also when it did not change; the
  release notes say per package what is new. Home Assistant then offers an
  app update that brings nothing new for the app; that is accepted.
- Firmware and Home Assistant app of the same version belong together;
  there is no compatibility matrix between version lines. How the app
  handles an ESP of another version is part of the design in SD-050.
- Open (M2, update manifest): whether an ESP is offered an update whose
  firmware did not change. A firmware update restarts the controller (R6),
  so it should not come for nothing.
- Not chosen: separate releases with their own version lines for the app
  and the firmware (two changelogs, a version matrix, a fix in the shared
  core shipped twice).


## SD-049: A hardware compatibility list; the flashing guide follows with M1

Decided by the project owner on 2026-10-12 in the software session.

- `docs/COMPATIBILITY.md` lists what works with the software, by
  connection: ESP32 boards, the Home Assistant app and the sensors it
  reads, the carrier board's inputs and outputs, wireless sensors and smart
  plugs. The README links it.
- Four statuses: **Tested** (with version, date and a link to the report),
  **Expected** (should work by how it is built; not tested yet), **Not
  yet** (names what is missing: a milestone, a feature or a rule), **No**
  (names why).
- The list describes the code on `main`; what is not in the latest
  release yet is marked as such.
- Reports come through the issue form "Device or sensor"; an entry is
  added by a pull request with review. Only "Tested" needs a report; a
  test of our own counts too, with its version and date.
- Product names are used only to describe compatibility (README,
  trademarks).
- The list describes connections, not the board. It takes from the board
  only what is public in issues #91 and #94 (pin assignment, bus devices
  and conversion values: PD-157, PD-168, PD-170), and of that only what a
  user needs to choose a sensor, pump or valve; the rest of the board
  stays private (PD-103).
- A step-by-step guide for flashing the ESP32-S3 (board, cable, USB port,
  browser, steps, duration, problems, other tools, pictures) is written
  with M1, once a flashing page and a release with firmware exist. Until
  then `firmware/FLASHING.md` describes building and flashing own
  firmware.
- A link to buy hardware is a product question: PD-061 says the README
  makes no reference to selling hardware. It waits for the product
  repository's session (SD-029).
- Not chosen: a data file with a generated page (more work before there
  are entries; possible later, if the web app is to show known devices),
  and the GitHub wiki (no review, not versioned with the software).


## SD-050: The Home Assistant app opens the ESP's web app in Home Assistant

Decided by the project owner on 2026-10-12 in the software session;
the review questions below come from the `security` review of this entry.

- The ESP32-S3 controls in both ways of use (PD-149): on its own with its
  web app, or with Home Assistant as well. Home Assistant holds no control
  logic.
- With Home Assistant, the firmware sends all values and states, the
  watchdog's assessment included, as Home Assistant entities over the
  ESPHome native API (SD-043, M3). No app is needed for that.
- From M3 on, the Home Assistant app also opens the web app of an ESP in
  Home Assistant's sidebar (ingress), so the ESP can be set up from Home
  Assistant. The app is a window: it holds no control logic, sends no
  commands of its own and never repeats a write.
- Before it is built, `security` reviews the design. The review answers at
  least:
  - **Login:** whether the ESP's own login stays in front of its web app;
    Home Assistant's login does not replace it without a decision. How
    lockout and the event log work when every request comes from one peer,
    the app.
  - **Trust across the proxy:** the ESP's pages run on Home Assistant's
    origin. An XSS in the web app or a forged ESP could reach Home
    Assistant's session tokens, and other apps on that origin could act
    with the ESP's session. What this means for sessions, cookies, CSRF
    and the ban on framing (`X-Frame-Options: DENY`, CSP), which is not
    simply dropped; the `Host` and origin checks of the ESP and of the app
    (DNS rebinding) are not loosened for the app's own port.
  - **Who reaches the window:** only through ingress (connections only
    from the Supervisor), never on the app's published port; which Home
    Assistant users see the panel.
  - **Transport and identity:** whether the way from the app to the ESP may
    be plain HTTP before local HTTPS (M2) or waits for it; how the app
    knows it talks to the right ESP; which request headers (cookies,
    `Authorization`) reach the ESP.
  - **Addresses:** the ESP's address is set in the configuration, never
    per request, and never points to loopback or Home Assistant's internal
    network.
  - **Secrets:** the app stores no password, session or key of the ESP;
    Home Assistant's backups include the app's data. If the app's present
    mode stays (below), the Supervisor token, an administrator's right in
    Home Assistant, sits in the same process as the code that reads the
    ESP's answers.
  - **From outside:** Home Assistant is often reachable from outside the
    home network, and the window would make the ESP's web app reachable
    the same way. Which functions that may allow (setup, manual runs,
    Stop) and what it does to the lockout; the Stop never depends on Home
    Assistant.
  - **Versions:** with an ESP of another version, the window shows a
    message and does not change requests (SD-048).
- **Product question** for the product repository's session (SD-029),
  before the window is built: whether the ESP's web app may be reached
  from outside the home network through Home Assistant at all. Remote
  access is a product question so far (PD-150, M6).
- Open for `regulatorik` in the product repository: whether access
  through Home Assistant changes the EN 18031-1 assessment (access
  control, authentication, secure communication) and CRA Annex I.
- Not chosen: first only a link on the device page in Home Assistant, with
  the window later (the project owner wants the window from the start of
  M3); no app at all for the ESP.
- Open: Home Assistant installations without apps (Container) have no
  ingress; whether they get the link on the device page instead. Open
  too: whether the app's present mode (reading sensors from Home Assistant
  without an ESP, switching nothing, SD-034) stays once the window exists.
- Changed by SD-059: access from outside is no product question any more;
  the user chooses a level at setup. Requests through the window and
  commands over the native API count as from outside and follow that
  level, so the ESP is set up through the window only as far as the level
  allows; what SD-059 keeps to the home network is done only directly
  there.


## SD-051: The sign-in lockout counts per client

Decided by the project owner on 2026-10-12 in the software session (fix
it right away as a PR); the design follows `security`'s review. A security
rule: the PR waits for "mergen".

- **Why:** the lockout after 5 failed attempts (EN 18031-1 AUM-6) had one
  count for all clients. A device in the network that sent a wrong
  password every 15 minutes kept everyone locked out, the owner's phone
  included, and with it "Stop" for anyone not signed in.
- **Per client:** a client that signed in successfully (also by the
  initial setup) gets a count of its own; at most 16, the one seen longest
  ago (signed in or used its session) drops out first. Every other client
  shares one count, which is also the brake: guessing from addresses that
  never signed in is no faster than from one. A success resets only the
  client's own count; a success from a new address does not lift the
  shared lockout. The schedule stays: from
  the 5th failure 30 s, doubling up to 15 min; a count forgets its
  failures after a quiet day.
- **The client** is the peer address of the connection, normalised in the
  core (`clientKey`): IPv4 as is, IPv6 by its full address without the
  zone (a /64 is shared by every device of a home network), an
  IPv4-mapped address as IPv4, anything unreadable as unknown (the shared
  count). Never from `X-Forwarded-For` or another header.
- **Password change:** a wrong old password counts as a failed attempt
  of the caller and is logged; a locked caller gets 429 before any
  hashing. A change forgets the other known clients; the caller, who just
  proved the old password, keeps its own count.
- **Event log:** only counted failures are logged; neither a try refused
  during the wait nor one before the first password is (flash wear, and
  the log is not flushed by guessing).
- **No header:** `tools/arch_check.sh` refuses `set_trusted_proxies`,
  with which cpp-httplib would take the address from `X-Forwarded-For`.
- **In memory only:** the known clients are not stored. After a restart
  every device shares the count until it signs in again.
- **Accepted:** during an attack, a device that has not signed in since
  the hub started (a new phone, or after a restart) can be locked out with
  the others; a device that is signed in keeps its session and "Stop". A
  device whose IPv6 address changes counts as new. Behind one peer (Home
  Assistant's ingress, SD-050, or a Docker proxy) all clients share one
  count. A host that takes over the address of a known device (a static
  address, a reused lease) gets that device's count: it can lock that
  device and has one more guessing budget per known address, at most 17
  in all. The hardware Stop never depends on the web.
- **Later:** device cookies (OWASP) as the sign of a known client, with
  encrypted NVS for their key; they need no address and survive address
  changes. Open for `regulatorik`: whether addresses held in memory for
  the lockout count as processing personal data.
- Extended by SD-059: a client from outside never shares a count with the
  home network.


## SD-052: The ESP32-S3 reads its own sensors; Home Assistant only receives

Decided by the project owner on 2026-10-12 in the software session. It
applies PD-149.

- The ESP32-S3 reads only the sensors connected to it directly: on the
  carrier board and, later, over Bluetooth (PD-153). It does not read
  sensors from Home Assistant.
- It sends its values and states to Home Assistant, which shows them
  (SD-050). Devices and sensors of growcontroller are set up on the ESP,
  not in Home Assistant.
- The Home Assistant app's present mode (reading sensors from Home
  Assistant without an ESP, switching nothing, SD-034) is not affected;
  whether it stays is open (SD-050).


## SD-053: Fans only on and off; climate devices on an invalid climate value

Decided by the project owner on 2026-10-12 in the software session. It
deviates from PD-153 in favour of PD-050; the product session is told,
and if it decides otherwise, its decision applies and a later SD records
it.

- Exhaust and circulation fans on smart plugs are only switched on and
  off. Their own setting (speed, their own thermostat) stays in the fan.
  A fan must start again in its last setting when power returns (black
  start); the ESP sets its plug to "on after power loss" (PD-050).
  `docs/COMPATIBILITY.md` names this as a requirement.
- When a climate value that controls a device is outdated or invalid,
  from a wired or a wireless sensor:
  - every exhaust and circulation fan is switched on or kept on, also one
    the control had switched off (the reasoning of PD-050: without fans
    heat and humidity build up);
  - a dehumidifier with its own hygrostat gets power and regulates itself
    (PD-066 recommends one; outlet profile "supply", RAT-069), after its
    pause and with the humidifier off first (RAT-034);
  - a humidifier, and a dehumidifier without its own hygrostat, switch
    off (PD-153); the dehumidifier's pause counts from then (RAT-034);
  - the air heater switches off and, if it was running, latches (RAT-060,
    RAT-062);
  - the app reports the sensor.
- "Stop" still switches everything off, fans included (PD-076).
- Open: the freshness limit for Bluetooth sensors (the catalog's 120 s
  allows about two missed reports; a rule like RAT-027 is needed); how the
  ESP learns that a dehumidifier has its own hygrostat (a setting at
  setup, an assumption).
- When built: RAT-019, RAT-034, RAT-060 and RAT-068 are updated, and the
  gateway gets cut-offs for humidifier and dehumidifier on an invalid
  humidity (today it only blocks switching the humidifier on,
  `core/src/dosing.cpp`).
- Changed by SD-060: a Bluetooth climate sensor is outdated after three
  missed reports, never sooner than 120 s.


## SD-054: EC compensated by the hub; pH not compensated; a zero-point check on the hub's pH input

Decided by the project owner on 2026-10-12 in the software session. It
deviates from issue #94 (§4.8) for pH; the product session is told, and
if it decides otherwise, its decision applies and a later SD records it.
None of this is built: today nothing is compensated (`core/src/truth.cpp`),
and the simulator's raw EC has no temperature term (`sim/world.cpp`).

- **EC of the hub's own inputs** will be compensated to 25 °C by the hub,
  from the water temperature. An EC that the sensor already compensates
  (SD-041) is not compensated again.
- **With a water temperature sensor set up:** while it gives no valid
  value (missing, outdated, implausible, jump-locked), EC is shown, marked
  "without temperature compensation", and never used for control; it is
  never taken as if at 25 °C (R5). pH correction then waits too, since it
  needs a usable EC (EC gate, RAT-046); the app says why (decided by the
  project owner).
- **Without a water temperature sensor,** the user can choose to control
  EC without temperature compensation, knowing it is less exact (decided
  by the project owner): about 2 % per °C away from the temperature at
  calibration, so 5 °C off puts the tank about 10 % off its target. A tank
  colder than at calibration reads low, so the hub doses too much, which
  only dilution undoes (RAT-012); a warmer one gets too little. The choice
  is explicit, shown with the number and the direction, and the EC stays
  marked.
  Without the choice, EC is not used for control. The choice applies only
  while no water temperature sensor is set up; a sensor that is set up
  and fails stops control as above, since otherwise EC would jump by
  10–20 % and control would go on at another scale. Removing a failed
  sensor does not switch the choice on; the user confirms it again.
- **Calibration:** an EC calibration point is compensated with the
  temperature at that moment and refused without a valid water
  temperature, unless the user chose control without compensation.
  Stored EC calibrations without a temperature, and those made under that
  choice once a water temperature sensor is set up, need a new calibration
  or a migration.
- **Jump lock and history** judge the compensated value: the water
  temperature dropping out and coming back does not lock EC, and an EC
  without compensation is a gap in the history of the compensated EC.
- **The coefficient** comes as a rule from the product repository and
  gets a RAT entry and tests when built. About 2 % per °C is named for
  fresh water and KCl standards [1, 2]; no measured value for nutrient
  solutions was found (an assumption).
- **pH is not temperature compensated.** The error of the electrode's
  slope is about (pH − 7) · ΔT / 298 pH: about 0.04 pH at pH 5.8 and 10 °C
  away from the calibration (calculated), below the smallest tolerance of
  0.05 (RAT-008). Under this entry only EC carries the mark "without
  temperature compensation".
- **Zero point:** the pH calibration on the hub's own pH input also
  checks the probe's zero point: the calibration line's value at pH 7
  (extrapolated when the buffers are pH 4 and 10), as the electrode's own
  voltage after the firmware removes the input's known bias (board data,
  PD-170). A probe outside a band around 0 mV is refused with its reason,
  as a slope outside 47–74 mV/pH is today (RAT-025). Voltages from Home
  Assistant keep today's check (slope only): their front end may lift the
  signal on purpose, and they never feed control (SD-052); the test
  "the offset does not matter" in `tests/core/test_ha.cpp` stays. The band
  comes as a rule from the product repository; makers name ±30 mV at
  pH 7 [3], and the tolerance of the board's bias reference counts
  against it (a product question). Until then it is not built.
- **Simulator,** when built: the twin's raw EC gets a temperature term
  with a coefficient deliberately different from the hub's, recorded as
  an assumption in `docs/SIMULATOR.md`.
- Sources, retrieved 2026-10-12 by the `domain` review from search
  summaries (the pages themselves did not load): [1] USGS, WaQI 06.01,
  https://water.usgs.gov/usgs/owq/WaQI/WaQI06.01.pdf ; [2] Reagecon, "The
  Effect of Temperature on Conductivity Measurement",
  https://knowledge.reagecon.com/wp-content/uploads/2021/09/Technical-Paper_The-Effect-of-Temperature-on-Conductivity-Measurement.pdf ;
  [3] Hamilton, "The Zero Point of a pH Sensor",
  https://www.hamiltoncompany.com/knowledge-base/article/the-zero-point-of-a-ph-sensor
- Changed by SD-060: compensating EC is the user's choice at setup, also
  with a water temperature sensor; the coefficient is 2 % per °C; the
  zero-point band is ±30 mV, and a probe outside it is to be replaced.


## SD-055: Prototype 0.1.0-proto.5 after a polish block; English complete before it

Decided by the project owner on 2026-10-12 in the software session, after
a review by `ux`, `qa` and `release` and the outside views of an end
customer and a grower.

- **No release before the polish.** 0.1.0-proto.5 (sign of life, raw pH
  calibration, phone fixes, the security fixes in the CHANGELOG) comes after a block of
  work that fixes what the review found visible:
  - phone: a device row that breaks letter by letter (Devices), the
    connection state hidden on phones;
  - a message when Stop, Resume or a manual switch fails;
  - developer words and codes out of the interface ("Sensorwahrheit",
    "Quelle: RAT-039", "Aktor-Gateway" and the like);
  - "Stopp" / "Alles stoppen" everywhere instead of "Not-Halt" (SD-045,
    PD-172);
  - the history chart's time axis in the page's language;
  - setup: its step count, and the browser's time zone no longer
    overwriting the saved one;
  - the German Mix page showing an English sentence;
  - the test of the 3 s margin of a sign of life (#109);
  - the menu (SD-056) and the target EC (SD-057).
- **English is complete before proto.5** (#18, #73): every text the core
  sends gets a key and both languages (errors, sensor reasons, checklists,
  mix plan), the catalog's names and descriptions, and the CSV export.
  Setup offers English first (SD-014). Not chosen: English marked "beta"
  for proto.5.
- **Then the release:** CHANGELOG with a summary and "Known limitations",
  the version raised everywhere; the release waits for "mergen". Before
  the tag the project owner tests the Home Assistant app on a real Home
  Assistant: install, password, sensors, restart, update from proto.4,
  backup, a sign of life and a raw pH calibration.
- **Work split:** the software session that wrote this entry does the
  polish block and then M1; the other software session does not touch
  these topics. SD numbers are
  taken right before the pull request opens, after a look at `main`.
- Changed by SD-065: the project owner's test comes in two parts, before
  "mergen" with the release branch's server and after the tag with the
  published app.


## SD-056: Menu grouped by topic; pages without automation say "manual"

Decided by the project owner on 2026-10-12 in the software session
(proposal by `ux`, in line with the grower's view).

- The menu on the left, with small headings:
  - Übersicht (Overview), Verlauf (History)
  - **Nährlösung** (Nutrient solution): Mischen, Tank & Regelung, Rezepte
    & Kanister, Bewässerung
  - **Raum** (Room): Klima, Licht
  - **System:** Geräte, Funktionen, Einstellungen
- Watering moves next to the tank, as it draws from it and its dry-run
  lock needs the tank's level; recipes move next to mixing.
- The bar at the bottom on phones stays as it is.
- Climate, light and watering carry the label "Handbetrieb" (manual) as
  long as they only show values and switch by hand, with the advice to
  keep their own timer or controller (as the Home Assistant introduction
  already says). With nothing assigned, a page shows an empty state that
  says what to connect.
- Not chosen: a menu by daily routine with new pages for phases and the
  room (a larger rebuild), and leaving the menu as it is.


## SD-057: Mixing to the phase's target EC; the recipe gives ratio and order

Decided by the project owner on 2026-10-12 in the software session, after
a check by `domain` of the manufacturer's guidance; the safeguards marked
"(review)" come from the reviews by `domain` and `reviewer` of this entry.
It replaces the fixed targets of the grow dialog; the EC correction
(RAT-054, RAT-055, RAT-080) already keeps the recipe's ratio. Not built
yet; built in the polish block (SD-055), with a RAT entry, tests and
simulator numbers per `docs/SIMULATOR.md`.

- **The target is the phase's target EC** (`ec_target`), as the total EC
  of the solution, the water's own EC included, as the manufacturers'
  charts give it. Athena, for example, scales the amounts of its parts in
  a fixed ratio to a target EC and gives its charts for water with an EC
  up to about 0.4 mS/cm [1][2].
- **The recipe gives the way there:** the ratio of its parts to each
  other and their order. Its EC at its chart amounts is kept as a
  reference point, not as a target.
- **Amounts are real millilitres** from the measured flow of each pump,
  never run times. Without a measured flow nothing is dosed.
- **Three ways to mix,** chosen by what the user has:
  1. **To the target:** a valid EC of the water being mixed from the
     hub's own probe, usable for control under SD-054 (compensated, or
     uncompensated by the user's explicit choice), and the water
     circulated. The litres come from the level sensor or are entered.
     The hub measures the water's EC first, then doses about 80 % of the
     gap to the target in the recipe's ratio and order, waits the settling
     time (RAT-052, at least `settle_min`; a factor by volume is still an
     assumption) and only then lets the EC correction close the rest
     (review). If the EC fails during the mix,
     the mix stops and says why; it does not go on as a fixed pattern
     without the user (review). Water at or above the target gets
     nothing, with a note. Changed by SD-102: the details of measuring,
     the first step, the hand-over and its check.
  2. **A fixed pattern** (no valid EC in the water being mixed: no probe,
     or no circulation): the user enters the litres and either the
     recipe's amounts per litre, or a target EC within the catalog's range
     together with the EC of the water, taken from a meter or entered as
     0 for reverse-osmosis water. Without the water's EC only the recipe's
     amounts are offered; it is never taken as 0 (R5, RAT-006, review).
     The hub works out the amounts from the recipe's ratio and reference,
     runs the pattern without feedback and says so. Changed by SD-098:
     from the larger of the reference's and the learned effect, and never
     more than the recipe's own amounts.
  3. **Stirring by hand:** after each part the hub stops and asks to stir,
     and goes on only when the user confirms, in the recipe's order. The
     wait has a time limit; cancelling or a timeout after one part of a
     pair ends the mix with the pair's message (RAT-054); after a restart
     the mix is reported, not resumed (R6) (review).
- **The grow** takes each phase's target EC and pH from the recipe as a
  proposal (for a pH band its middle, RAT-066); the fixed 1.4 and 1.8 of today's dialog go. Without a grow the
  overview shows which default targets apply and offers to set one up.
- **Safeguards (review):**
  - The step limit (`max_ec_step`, RAT-055) is converted to ml/L with
    the largest of the start effect fixed in code (R7), the recipe's
    reference and the learned effect, so a typo in a recipe can never
    raise a dose. A reference far from the start effect (below a quarter
    or above four times) is refused.
  - The learned effect belongs to a recipe; another recipe starts from
    its reference. Changed by SD-092: from the larger of the start
    effect and its reference's, so a reference only ever makes a round
    smaller.
  - A part marked as a fixed addition (a cleanser, for example) is dosed
    per litre of fresh water, also when the water is already at the
    target, and never scaled by an EC correction; its expected EC counts
    in the first step's gap.
  - Water above the charts' 0.4 mS/cm gets a note on a new fill.
  - The mix plan says when EC runs without temperature compensation
    (SD-054); cold tap water then reads low and the tank gets too much.
- **Open:** mixing outside the tank (a bucket for soaking) and whether the
  pumps then dose into it, given that consumption goes only into the tank
  (RAT-040, RAT-070) and rounds stay clean (RAT-057), now decided by
  SD-084 (PD-238: a bucket or watering can, nutrients only, Fine modules
  only); large tanks, where a correction round may need more partial
  runs than allowed (RAT-054);
  a low target whose reserve falls below the EC gate for pH (RAT-046,
  RAT-053).
- Not chosen: the target as the EC on top of the water (it does not match
  the charts), and fixed ml/L with the target only as a check (with tap
  water or a probe that reads differently the tank misses the target).
- Sources, retrieved 2026-10-12 (search results; athenaag.com could not be
  loaded): [1] https://www.hydroponics.eu/db_img/pdf/Athena-Pro-Dosage-Guidelines-Metric.pdf ;
  [2] https://www.rivierahorticulture.co.uk/pages/how-to-use-the-athena-feed-schedules


## SD-058: The web app's npm packages are checked against their registry signatures

Decided by the project owner on 2026-10-12 in the software session, for
#100 (from `security`'s review of the dependency updates #96, #98, #99).

- Wherever the web app's packages are installed, `npm audit signatures`
  runs right after `npm ci`, before any package runs there: in `tools/ci.sh`
  (CI's `checks` job), the firmware job, the simulator packages, the
  release build and the Home Assistant app image. Each place installs the
  builds for its own platform (Rolldown, lightningcss and the TypeScript
  binaries run native code in every build), and only installed packages
  are checked. `tools/workflows.test.mjs` keeps the step after every
  `npm ci`, with no way around it on that line.
- A missing or invalid registry signature fails, and so does an invalid
  provenance attestation, a key npm does not know, or no answer from the
  registry or from Sigstore.
- A package without provenance passes; npm counts only the verified
  attestations. Many packages publish none, TypeScript and lightningcss
  among them. No list of them is printed; a warning that always comes is
  soon not read. Changed by SD-063: a package fails without provenance
  once a lower version of it had provenance.
- What it proves: the registry's own record of each installed
  `name@version` is signed, so a record changed on the registry's side
  without npm's key fails while it is served. It does not compare that
  record with the bytes on disk. Those are bound by the sha512 in
  `web/package-lock.json`, which `npm ci` checks, and
  `tools/workflows.test.mjs` requires every package there to resolve to
  that very record (`https://registry.npmjs.org/<name>/-/<file>-<version>.tgz`).
- What it does not do: it adds nothing against a hijacked maintainer
  account, since the registry signs those releases too and a hijacked
  publishing pipeline even produces valid provenance. Against that stand
  Dependabot's cooldown of 7 days, the lockfile pin and
  `npm ci --ignore-scripts`. The gain of this check is small; it is cheap
  and fails closed. Against a hijacked account SD-063 adds a check of its
  own: a package must not lose the provenance a lower version had.
- The check needs the npm registry and Sigstore's TUF repository
  (`tuf-repo-cdn.sigstore.dev`), also for a local `tools/ci.sh`.


## SD-059: Access from outside the home network in three levels, chosen at setup

Decided by the project owner on 2026-10-12 in the software session, as a
question the software solves; the rules for building it come from the
`security` review of this entry. PD-150 left the way of remote access
open; if the product session decides otherwise, its decision applies and
a later SD records it (as with SD-033). Recorded as SD-051 in PR #123 and
renumbered: PR #122 recorded the lockout per client as SD-051 at the same
time, and SD-055 to SD-058 were taken by then.

- At setup the user chooses how the ESP's web app may be reached:
  - **Home network only** (preselected): the ESP answers nothing from
    outside, not the login and not "Stop" either. Values still go to Home
    Assistant over the native API once it is connected, but no command
    from there is accepted.
  - **View from outside:** values, states, history and messages, from an
    explicit list; never the diagnostic package or the configuration
    export.
  - **View and control from outside:** in addition, operating from an
    explicit list.
- **Only in the home network, at every level:** calibration, limits,
  firmware updates (rollback included), importing a configuration,
  assignments and the protection settings of sockets, passwords, and
  acknowledging a latch (a person at the tank has checked, RAT-031).
  From outside, whatever is not on the explicit list is refused; `domain`
  sorts out the operations that need a person at the tank.
- **"Stop"** works from outside at both outside levels. "Resume" counts as
  control: after a Stop from outside at "View", the setup says, only
  "View and control" or the home network lifts it, and fans stay off
  until then (PD-076).
- **Raising the level** only from the home network, with the password
  asked again. Lowering it, from wherever, ends outside sessions and their
  live connections at once; whether it can be lowered from outside is
  open (the safe direction).
- **What counts as the home network:** the ESP checks this itself from
  the connection (the peer address is in its own subnet or link-local),
  never from a header. Everything else counts as from outside.
- **The Home Assistant window and Home Assistant's commands over the
  native API count as from outside** (decided by the project owner): they
  come from the Home Assistant host, and the ESP cannot tell whether the
  person is at home. So at "Home network only" the window is closed. This
  changes SD-050. The ESP recognises the window by the Home Assistant
  host's address set on it, never by a marker the app sends; how, and
  what happens when that address changes, is part of SD-050's review.
- **Ways in:** the outside levels apply only to defined ways, the Home
  Assistant window (SD-050) and the way of M6, each with its own review of
  transport, so no password crosses the internet as plain HTTP. A direct
  request from a public address (a port forward, an open IPv6 address) or
  from another private network that is no defined way (a VPN with its own
  subnet, another VLAN) is refused at every level; whether further home
  subnets can be listed, set from the home network, is open.
- **Lockout:** failed logins from outside never lock out the home
  network; they count separately. The lockout per client (SD-051) is the
  start: a client from outside must never share a count with the home
  network.
- **The level** is stored and enforced on the ESP and checked on every
  request, before `/info`, the login and the first password. Until the
  first password is set, and after a factory reset, it is "Home network
  only". An import or a restored backup never raises it. Every change is
  logged with its origin (home network or outside).
- **What Home Assistant shows** is outside the level: once Home Assistant
  is connected (SD-050) and reachable from outside, values and history
  leave the home network that way. That is the user's choice; the setup
  says so.
- **Residual risks, named in the docs:** a proxy or tunnel the user runs
  in the home network gets the rights of the home network; the way from
  the internet to Home Assistant is Home Assistant's own transport, which
  the user sets up; someone on the internet can lock the outside logins
  while the owner is away, and a "Stop" from outside then works only from
  a session already open.
- Open for `regulatorik` in the product repository: EN 18031-1 (access
  control, authentication, secure communication; whether a password alone
  is enough from the internet), CRA Annex I, and GDPR for history, events
  and logged addresses seen from outside.
- Open for the design (M3, M6): the explicit lists; a shorter session from
  outside; IPv6 prefixes; what `/info` shows from outside; the downgrade
  check of M2 also refuses firmware without this gate.
- Extended by SD-062: the way of M6 is a relay; at "View from outside" a
  message can be acknowledged, which only pauses its repetition.


## SD-060: EC compensation as a choice, 2 % per °C; pH zero point ±30 mV; Bluetooth climate sensors after three missed reports

Decided by the project owner on 2026-10-12 in the software session. It
settles the freshness limit SD-053 left open and the rules SD-054 left
open, so they need nothing from the product repository; the product
session is told. Recorded as SD-056 in PR #125 and renumbered, as SD-055
to SD-058 were taken by then.

- **Compensating EC is the user's choice at setup:** "with temperature
  compensation" (needs a water temperature sensor) or "without". This
  changes SD-054, where the choice existed only without a sensor. With
  compensation chosen, a sensor that fails still stops EC and pH
  correction (SD-054). Changing the choice needs a new EC calibration,
  since the calibration factor refers to another scale, and like a
  calibration it is done only in the home network (SD-059). Setting up a
  water temperature sensor does not change the choice or void the EC
  calibration; only changing the choice does. This replaces SD-054's
  sentence on calibrations made under the choice. Until the user has
  chosen (an existing configuration, after a factory reset), EC is not
  used for control; the setup asks for the choice.
- **Coefficient:** 2 % per °C, linear to 25 °C (SD-054, sources 1 and 2).
  No measured value for nutrient solutions was found; the project owner
  accepts the figure.
- **The hint at "without":** about 2 % per °C away from the temperature
  at calibration; between 18 and 24 °C up to about 12–14 %, about ±6–7 %
  when calibrated at about 21 °C, the larger figure on the overdosed side
  (calculated with the linear model). A tank colder than at calibration
  is overdosed (RAT-012), and as EC control only raises EC, the tank ends
  at the target as read in its coldest hour. Advice: calibrate when the
  tank is at its coolest usual temperature (for example at the end of the
  night); then the error only goes towards too little.
- **"Without" with a water temperature sensor present:** EC is marked as
  a chosen setting (a note, not a fault) and stays usable, so pH runs
  normally (EC gate, RAT-046); a failing water temperature sensor is only
  its own finding.
- **With compensation and a failing sensor,** the app names the way out:
  replace the sensor, or switch to "without" and calibrate EC again.
- **The calibration value** is the reference solution's value at 25 °C
  (printed on the bottle), under both choices; the calibration asks for it
  in these words.
- **pH zero point:** ±30 mV at pH 7, the tolerance makers name (SD-054,
  source 3), on the hub's own pH input after the firmware removes the
  input's known bias. Outside it, the calibration is refused with the
  message to replace the probe; as a first step it suggests cleaning the
  probe, soaking it in storage solution and fresh buffers, since a used
  buffer or a dirty junction shifts the zero point too (RAT-026).
- **After a refused zero point** (decided by the project owner): pH
  correction pauses and the app warns that the probe's zero point has
  moved. Only after the user confirms the warning, in the home network
  like acknowledging a latch (SD-059), does it dose again with the old
  calibration; the warning stays visible until a valid calibration is
  saved. The zero point is stored with each calibration, so its trend
  shows. The tolerance of the board's bias
  reference is measured on the board samples; if it takes a noticeable
  part of the band, the band is looked at again.
- **Bluetooth climate sensors:** outdated after three missed reports,
  never sooner than 120 s (the catalog's limit for air temperature and
  humidity): three times the device's send interval, so 180 s for the
  Shelly BLU H&T, which sends every 60 s. The project owner first set a
  fixed 120 s; the `domain` review found that this makes such a sensor
  outdated after one missed report, against RAT-027, while a false
  outdated value switches climate devices and latches a running heater
  (SD-053), which only the home network can acknowledge (SD-059), and
  the project owner then decided on three missed reports. The send
  interval is the device's own; it is never learned looser. A
  repeated report with the same packet counter is no new measurement.
- The RAT entries for EC compensation and the zero point, and their tests,
  come with the implementation (M1); they take their IDs then.


## SD-061: A new SD or RAT ID is final only when its PR is merged

Decided by the project owner on 2026-10-12 in the software session, after
parallel sessions took the same numbers twice in one day (SD-051 in #122
and #123; SD-055 and SD-056 in #125 and #127).

- Right before merging, the session checks the last SD and RAT ID on
  `main` and renumbers its PR's new entries, and every reference to them
  in the PR (docs, code comments, test names), to the next free IDs.
- A number in a PR's text or title can therefore still change until the
  merge; the squash commit carries the final one.
- `CLAUDE.md` names the rule.
- Not chosen: reserving numbers through issues (public issues for
  bookkeeping, and a race stays possible); fixing clashes by hand.


## SD-062: The way from outside is a relay the project runs; no port forwarding

Decided by the project owner on 2026-10-13 in the software session, one
question at a time; the rules for building it come from the reviews of
`security`, `domain`, `reviewer` and `qa`. It fills "the way of M6" that
SD-059 leaves open and extends SD-059 by one write at "View from
outside". The way and the levels are questions the software solves, as
in SD-059. That the project runs the relay, what it costs and whether a
store app comes touch PD-013 and PD-024: the project owner takes these
to the product session, which decides and records them (SD-029); until
then they are the software's working basis.

- **For every user, off by default:** reaching the ESP's web app from
  outside needs no network knowledge. At setup, or later in the settings,
  the user picks the level (SD-059); the ESP then shows a QR code with
  its own address, the phone opens it, the user adds it to the home
  screen and signs in with the ESP's password. Whether a password alone
  is enough from the internet stays open (SD-059). Changing the level
  later follows SD-059. "Off" means no tunnel, no registration with the
  relay and no certificate: every certificate appears in the public
  Certificate Transparency logs, so an ESP's name exists only once its
  user switches access from outside on.
- **The way is a relay:** the ESP opens an outbound, encrypted connection
  over its Wi-Fi to a relay. No port is opened on the router, so it works
  without router settings and behind a shared public address (CGNAT,
  DS-Lite). The relay is built not to read the traffic: TLS ends on the ESP,
  with a key made on the ESP that never leaves it and a certificate for
  the ESP's own name, and the web app always comes from the ESP, never
  from the relay. For the design:
  - each name is bound to the ESP's key (for example derived from it),
    and the ESP proves the key when it opens its tunnel; the relay routes
    a name only to the tunnel that holds its key, the certificate's
    check of the name included;
  - every request that comes through the tunnel counts as from outside
    (SD-059), whatever the relay's own address, and the relay never
    connects to the ESP's local port;
  - every ESP is a site of its own to the browser, because anyone can
    flash their own firmware (PD-022) and get a name under the same
    domain: the relay's domain belongs in the Public Suffix List, the
    ESP's `Host` check accepts only its own name there, and its session
    cookie is `Secure` with the `__Host-` prefix;
  - nothing that protects the plant depends on the relay; after an outage
    ESPs reconnect with backoff and jitter; the relay limits connections
    per name and the ESP limits its parallel TLS sessions, so load from
    outside does not disturb control (RLM);
  - the relay's address is set only from the home network, is logged and
    is never changed by an import; a factory reset makes a new key and
    deregisters the old name;
  - residual risk, named in the docs: whoever controls the relay's domain
    or its DNS could obtain a certificate for an ESP's name and read or
    change its traffic, the password included; Certificate Transparency
    shows this but does not prevent it. Names are public in those logs,
    so security never rests on a name staying secret.
- **No port forwarding:** a direct way from the internet (a port forward,
  UPnP, an open IPv6 address) is not offered, and the docs advise
  against it. SD-059 already refuses such requests at every level.
- **Who runs it:** the project runs a relay for all users. Its software
  is open source, so anyone can run their own on a server reachable from
  the internet and set its address on the ESP. The relay stores no
  values; it keeps the registry of names and sees the connections'
  addresses, times and amounts of data. No account per user (PD-013);
  how an ESP registers is part of the design.
- **Cost:** free for the user, carried by the device price; PD-024 (no
  subscription) stays. How long it stays free, and a fair-use limit
  against abuse, are open for the product session.
- **"View from outside"** (extends SD-059) covers values, states, history,
  the watchdog's assessment and messages; "Stop"; and acknowledging a
  message, which only pauses its repetition (RAT-022) and changes no
  lock, no output and nothing the gateway or a controller may do. At
  every level only in the home network: acknowledging a latch (SD-059,
  RAT-031) and confirming a pH zero point (SD-060). "View and control"
  stays the user's choice; `domain` sets its explicit list (SD-059).
- **A store app is decided later,** once the relay and the web app exist
  and the effort and fees are known (estimate below). Until then the web
  app is used from the home screen; with the ESP's own certificate it
  can be installed as an app, and Web Push through the browser makers'
  push services becomes possible (Android; iPhone from iOS 16.4, from the
  home screen); whether it is used is decided with M5. ntfy and e-mail
  stay for everyone (M5, PD-024).
- **Needed besides M2's local HTTPS:** the ESP's own key and certificate
  through the relay; a device cookie instead of the address to tell
  devices apart, because behind the relay every visitor comes through
  the same tunnel (SD-051); the ESP never uses a visitor's address that
  the relay passes on.
- **Traffic, measured** on 2026-10-13 in the simulator (demo scenario,
  `--prefill 48`, speed 1, no job running; 1 KB = 1,000 bytes): the live
  view's stream (`/api/v1/events/stream`, the whole state once a second)
  carried 182,673 bytes in 10 s, about 18 KB/s or 65 MB per hour with
  the app open; compressed as one gzip stream across its frames, 6,663
  bytes, about 0.7 KB/s. For the relay the ESP sends the stream
  compressed or as changes only, about 25 times less; a value that goes
  missing is sent as `null`, never left at its old value (R5, RAT-006).
- **Estimate, 2026-10-13** (assumed: every user with access from outside
  has the app open 15 min a day; two servers each in Europe, the US and
  Asia; 10 to 50 KB of memory per idle connection):
  - Traffic: about 0.5 GB per user and month with today's stream, about
    0.02 GB compressed.
  - Servers: six small cloud servers, assumed to carry 100,000 ESPs, cost
    about €120 to €300 a month at Hetzner's prices after June 2026, with
    most traffic included (per server 20 TB in the EU, 1 TB in the US,
    0.5 TB in Singapore; beyond that €1 per TB, in Singapore €7.40). The
    same traffic on AWS (about $0.09 per GB) would cost about $4,000 a
    month at 100,000 users with today's stream.
  - Per user and month, servers only: about €0.12 to €0.30 at 1,000
    users, €0.012 to €0.03 at 10,000, €0.001 to €0.003 at 100,000.
    Running the relay (updates, monitoring, incidents, abuse, privacy)
    costs more than the servers.
  - Certificates: Let's Encrypt issues 50 new certificates per registered
    domain and week; renewals through ARI are exempt. More needs an
    override (a few weeks) or the Public Suffix List entry, which the
    design needs anyway (above); the list refuses entries whose only
    purpose is to avoid rate limits and is hard to join for a small
    project.
  - A store app: Apple $99 a year, Google $25 once; a Mac for the build;
    for an organisation a D-U-N-S number; in the EU the trader's address,
    phone and e-mail are shown; a new personal Google account first
    needs a closed test with 12 testers for 14 days. Assumed 3 to 6
    person-weeks at first, then 1 to 3 a year (yearly SDK and target-API
    rules, review rounds); Apple may refuse an app that is only a
    repackaged website. Push through Apple and Google needs a server with
    their keys; whether the relay is that server is open, as it is more
    than forwarding bytes. To weigh then: a store app could pin the ESP's
    key from the QR code, the only measure against the residual risk
    above.
  - Web Push needs no store and no developer account; the ESP can send it
    itself. That an ESP is offline only something outside the home can
    tell; whether the relay reports it is open, for the same reason.
  - Sources, retrieved 2026-10-13:
    https://developer.apple.com/programs/enroll/ ;
    https://developer.apple.com/distribute/app-review/ ;
    https://github.com/letsencrypt/website/blob/main/content/en/docs/rate-limits.md ;
    https://github.com/publicsuffix/list/wiki/Guidelines ;
    https://docs.hetzner.com/general/infrastructure-and-availability/price-adjustment/
    (search summary). Not checked against a primary source: Google
    Play's fee and test rule
    (https://support.google.com/googleplay/android-developer/answer/14151465)
    and AWS's price for outbound traffic.
- Open for `regulatorik` in the product repository: whether a relay run
  by the project falls under the CRA as part of the product; EN 18031-1
  for access from the internet; GDPR for what the relay sees. A license
  question for the product session (SD-010, PD-022): AGPL code and the
  App Store's terms may conflict (GNU Go was removed in 2010, VLC, then
  under GPLv2, in 2011); an App Store exception would need the consent of
  every copyright holder.
- Open for the design: if the Public Suffix List refuses the relay's
  domain, a fallback that keeps ESPs apart in the browser anyway (CSRF
  protection that does not rely on SameSite, the `Origin` check and the
  exact `Host`); what happens to an ESP's name when a factory reset or
  switching access off finds the relay unreachable (for example, the
  relay drops names unused for a set time).
- Open for the design (`domain`): at "View", whether a paused alarm of
  the level "immediately, also at night" (`docs/UX.md` §6) repeats after
  a shorter time (proposed: 60 min) and at once when it gets worse (for
  example "Off not confirmed"); whether a pause holds per recipient, so
  one person away does not silence another at home; the Stop dialog from
  outside says that circulation and the air pump stay off until someone
  resumes (RAT-036).
- Not chosen: port forwarding as an option for experts; a relay that only
  users run themselves (too hard for most); a third-party tunnel that
  needs an account per user (PD-013); a subscription for the relay
  (PD-024).


## SD-063: A web app package must not lose its provenance

Decided by the project owner on 2026-10-13 in the software session
("128 umsetzen", from `security`'s review of #100); the accepted cases
below with the merge.

- Before the web app's packages are installed, `tools/ci.sh` runs
  `tools/check_provenance.mjs`. For every package in
  `web/package-lock.json`, every platform build included, it finds the
  highest lower version that was published with provenance. If there is
  one, the locked version must have provenance too, from the same source
  repository and, where both provenances give it, the same GitHub
  repository id (a freed name taken over by someone else changes it).
  Otherwise the check fails.
- Why: a hijacked maintainer account typically publishes by hand from a
  stolen token, and the registry signs that release too (SD-058).
  Comparing with the highest lower version that has provenance, not with
  the next lower one, keeps two releases by hand in a row from hiding
  each other.
- A case accepted after review goes into `tools/provenance-exceptions.json`
  with the exact problem it accepts and why. It applies to that problem
  only, and an entry that no locked package has any more fails. Changes to
  that file or to the check need a `security` review. Accepted with the
  start: `@rolldown/pluginutils@1.0.1` (moved between two repositories of
  the rolldown organisation) and `undici-types@6.21.0` (6.19.3 to 6.21.0
  published without provenance in 2024 by the same maintainer, with
  provenance again from 6.23.0; `@types/node` 22 pins `~6.21.0`).
- No answer from the registry fails. The check runs where `tools/ci.sh`
  runs, in CI's `checks` job; the release, the packages and the app image
  install the same lockfile after it passed there on `main`.
- Not caught: a package that never had provenance; a release by a
  hijacked CI or trusted publisher of the same repository; a release by
  hand on a version line that never had provenance.


## SD-064: A Stop from the app needs a signed-in session; a session lasts while it is in use

Decided by the project owner on 2026-10-13 in the software session, one
question at a time: `ux` asked during the polish for 0.1.0-proto.5
(SD-055, #130) whether a Stop should work without a session, and
`reviewer` and `security` then found that the browser dropped the cookie
12 hours after signing in, even on a page in use.

- **A Stop from the app needs a session,** like every other function
  (EN 18031-1 access control, `docs/SECURITY_MODEL.md`).
- **A session lasts while it is in use:** the hub ends one after 12 hours
  without a request (`Auth::kSessionIdle`), on a restart and on a password
  change. An open page asks every 10 minutes and when it is woken
  (`GET /auth/session`), and the hub sends the cookie again for another
  12 hours, so a phone on the wall keeps its session. There is no fixed
  longest lifetime: a page in use stays signed in. This holds in the home
  network; a shorter session from outside stays open (SD-059).
- With 16 sessions, a 17th sign-in ends the one used longest ago (#131),
  not the page left open.
- When the session has ended, the sign-in page says that what was just
  clicked, a Stop included, was not carried out, and names the way in an
  emergency (#130); a page left open says so as soon as it next asks.
- **In an emergency:** unplug the hub, and switch off devices on smart
  plugs at the plug, since unplugging the hub leaves them as they are; on
  the carrier board (M1) also its Stop button (PD-164), which the sign-in
  hint will name then. After a restart, every device that has not signed
  in since shares one lockout count (SD-051), so someone guessing in the
  network can delay signing in; from outside, SD-059's residual risk
  applies.
- **Residual risk of no fixed longest lifetime:** a session once obtained
  (a lost or shared phone, a browser left signed in, a cookie read off the
  unencrypted home network until HTTPS) stays valid as long as it is used
  at least every 12 hours. Changing the password ends every session at
  once; a "sign out all devices" button can come later (`Auth::logoutAll`
  exists). OWASP ASVS 4.0.3 V3.3.2 asks for signing in again after 30 days
  even while in use; whether EN 18031-1 accepts a session without an upper
  limit while in use is a question for `regulatorik` in the product
  repository.
- When the way from outside comes (SD-059, SD-062), its renewal follows
  the shorter lifetime chosen for outside, not the home network's.
- Not chosen: a Stop without a session (any device or malware in the home
  network could keep the system stopped, so plants get no water, and
  EN 18031-1 would need a documented reason for a function without access
  control, a question for `regulatorik` in the product repository); a
  cookie that ends 12 hours after signing in whatever the use (a phone on
  the wall loses its session while still showing live values); a session
  kept for a fixed 30 days on a device (signing in again each month, and a
  lost phone stays signed in that long).


## SD-065: The owner's acceptance test of a release with the Home Assistant app comes in two parts

Decided by the project owner on 2026-10-13 in the software session.
`release` found that SD-055's test "before the tag" cannot be done in
full: the app image is published only for a `v*` tag (`release.yml`),
and Home Assistant cannot build it itself, since `ha/app/Dockerfile`
needs the whole repository.

- **Part one, before "mergen" of the release pull request:** the release
  branch's server runs against the project owner's real Home Assistant
  with a token, as `docs/HOME_ASSISTANT.md` describes. The owner checks
  the password, the sensors, a restart with the same data folder, and
  what the release brings; the release pull request lists these checks.
  For 0.1.0-proto.5: a sign of life, a raw pH calibration (swapped
  buffers refused with the reason) and the phone (the app opens at
  100 %). Automatic updates of the app stay off on the owner's Home
  Assistant (they are off unless switched on), so part two's update is a
  deliberate one, with a backup made first.
- **The risk between the parts:** from the merge on, `config.yaml` on
  `main` offers the new version to every installed app (SD-034), before
  part two has passed. The app's documentation, which Home Assistant
  shows (`ha/app/DOCS.md`), and the release notes therefore ask users to
  make a backup of the app before updating.
- **Part two, right after `publish`:** `docker buildx imagetools inspect`
  lists both architectures; a backup of the previous version first, then
  the update from the store keeps password, sensors and history; a fresh
  install on a second host; a restart keeps the data; another device
  guessing the password does not lock out the phone, which also shows
  that the app sees each device's own address through Home Assistant's
  port mapping (SD-051); a new backup holds the new version, and its
  entry for the app is not empty.
- **If part two fails:** a fixing release (the next `-proto.N`) follows at
  once, and the release notes and an issue tell users to keep automatic
  updates off until then. Users who made a backup before updating restore
  it and lose what they changed since; a restore pulls the backed-up
  version's image again, so a published image is never deleted. A
  reinstall is no way back: `main` already carries the failed version,
  and removing the app deletes its data.
- Changes SD-055's "before the tag".
- Not chosen: a test image published before the tag under its own name (a
  second way to publish, to be reviewed by `security`); the whole test
  only after the tag.


## SD-066: Product requirement (from PD-187 to PD-190): one dosing pump type with speed stages, feedback and an ID chip

**Replaced in part by SD-084** (2026-10-16): the product side answered
Q1 to Q16 and replaced the pump with speed stages by dosing modules with
a fixed rate (PD-223).

Recorded on 2026-10-13 from #132 (SD-029). The project owner decided in
the software session to plan it now and build it after 0.1.0-proto.5 as
part of M1, simulator first. `domain` checked the requirement against
`RATIONALE.md` and `INVARIANTS.md`. The issue holds the full behaviour.
This entry says how the software fits it to the existing rules. Where a
point reads a product decision, it is the software's proposal until the
product side answers the question named with it (Q1 to Q16, below).

- **The requirement in short:** one 12 V peristaltic pump type with a
  built-in driver, a speed signal and speed feedback as pulses, about
  20:1 (the first candidate about 10:1), doses from about 0.5 ml to 4 L
  and the shortest dose about 2.5 s; 10 pump ports; an ID chip in each
  module with identity, calibration, product and tubing counter; the
  module sits on the nutrient container; third-party 12 V pumps run
  on/off at full speed without feedback or chip. Booking stays as PD-147
  says: a fixed end, booked before the start, an interrupted run counts
  as run; the feedback only checks.
- **A long dose is a job of short runs** (proposed until Q4 is answered).
  "Up to about 20 min" applies to a dose as a job. Each run keeps its
  fixed end of at most 60 s (`kHardMaxRunS`) and the pause after it, as
  mixing already does (RAT-055). If control stops sequencing a dose
  midway (it loses the job's state, or a stop command does not reach the
  output), the run's fixed end stops the running run and no further run
  starts: at most one run goes in (about 200 ml at 200 ml/min), not the
  whole dose. If the whole firmware hangs, the board watchdog cuts the
  pumps (#94). A dropped connection to the app is not such a case: the
  job runs to its end (PD-161). All of this holds only while the per-run
  cap works, and on board v0.1 it runs in the same firmware (PD-160): a
  firmware that keeps running but does not end a run, or an output stage
  stuck on, has no cut-off in hardware. Against a stuck output stage the
  new pump has a second stop path, the speed signal at standstill (to be
  confirmed on samples, as some drivers run at full speed without a
  signal), and the current check below switches the group off; a
  third-party pump has only the current check. Control rounds keep their
  6 partial runs (RAT-055); a slower stage means more runs. Not chosen:
  one run of up to 20 min, which would change RAT-007, RAT-018 and
  RAT-055 and could put 4 L into the tank on one failed end.
- **Two separate bookings** (proposed until Q5 is answered). The safety
  budget is booked at the 100 % rate times the planned run time before
  the run starts, and an interrupted run counts in full (PD-147). Stock,
  consumption and mix progress are booked at the stage's rate times the
  actual run time (RAT-040, RAT-070). Before the first run, the worst
  case of the whole job (every run and every part of a pair at the
  100 % rate) must fit the budget, so the budget never cuts a pair after
  part A (RAT-054). Budget and minimum pause survive a restart, a Stop
  and a clock step (SD-028). Not chosen: one booking for both, which
  would take up to the stage ratio (10–20 times) too much off the stock,
  or break the catch-up of a pair (RAT-054).
- **Saving bookings during a long job** (proposed until Q16 is
  answered). SD-045 says a write waits for the job's end, because a
  flash write can hold up the sign of life while a pump runs. A job of
  20 min would then save nothing until its end. Proposed: each run's
  booking is saved in the pause before it, with no pump running, so a
  restart mid-job knows what went in (RAT-003, RAT-054). This would
  change SD-045 for this case. If the answer is no: the whole job's
  worst case is booked once before the first run, the result is saved at
  the job's end, and after a restart mid-job the progress and the stock
  are unknown (RAT-015): reported, the stock left for the user to
  confirm, and neither resumed nor caught up without the user.
- **A slow stage only within the dose's upper bound.** The worst case is
  the duration times the 100 % rate. A lower stage only lasts longer, so
  if the highest stage that reaches the minimum run time fails, every
  stage fails: the dose is refused with the reason, never dosed outside
  the bound. Proposed bounds (Q1): for pH the RAT-050 cap, which keeps
  own pH doses within 0.3 pH, the reason they stay unannounced (RAT-042);
  for an EC round the largest EC step (RAT-056), shared across the parts;
  for a hand dose the hand-dose cap (RAT-039). **A mix part has no bound
  today** apart from 200 runs per step: if its bound were the planned
  amount, no slow stage could ever be used for a mix. Its bound is open
  (Q1), for example the planned amount plus one EC step (RAT-056) or the
  alarm margin (RAT-033).
- **What the bound means for small tanks,** at a 100 % rate of about
  200 ml/min and the start effect for pH (RAT-050):
  - Automatic pH dosing is refused below about 56 L with a minimum run
    time of 1.0 s, and below about 139 L with 2.5 s, the shortest dose
    #132 names. pH control cannot learn a weaker effect without dosing,
    so below that size it never starts. Today's pumps (42–53 ml/min)
    work from about 12–15 L. What decides it is the 100 % rate of the
    module, not the number of stages (Q2).
  - With 2.5 s, a hand dose at the default cap of 5 ml is never possible
    (5 ml at full speed lasts 1.5 s; any slower stage has a worst case
    of at least 8.3 ml), an EC round with three parts fails below about
    7–10 L, and a 5 L mix that needs 5 ml of a part is refused unless
    the mix bound allows more. A default cap above 5 ml would change
    RAT-039 (product repository).
- **Calibration per stage.** Until Q6 is answered, only calibrated
  stages are used, and never beyond the support points (#132). Without
  a rate at 100 % there is no dose at any stage (R5, RAT-015, PD-015).
  Today's single calibration value counts as one stage at 100 %, so
  existing setups keep working. Setup calibrates only the stages the
  tank size needs; when one pump of a pair is recalibrated, it offers the
  partner too (RAT-054). Runs before any calibration (prime, calibration
  run, test run) are capped by a ceiling per pump type in code; a value
  from the chip can only tighten it (R7, Q7).
- **The dose ends by the timer** (PD-147); ending it by revolution count
  is not decided (Q13). The feedback checks every run. #132 asks: no
  pulses stop the dose (whether it also latches is Q11), clearly too fast
  stops the dose, any other deviation is reported. `domain` adds: pulses
  at speed 0 or clearly too fast latch the pump until the user
  acknowledges it (RAT-062); after any deviation the round counts as not
  clean (RAT-057); a run stopped for no pulses counts in full for the
  budget, and stock and progress take its actual time, as for any
  interrupted run (RAT-070). The volume from revolutions is compared
  with the stock booking and only reported. These checks get rows in
  `INVARIANTS.md` with step 5, citing this entry.
- **Speed signal at standstill** at boot, on Stop, on an internal error
  and before every release (R6, RAT-007, RAT-036, PD-077); the firmware
  sets it before it switches 12 V on. If the board watchdog trips (#94),
  the firmware may be hung: the board cuts 12 V and, as #132 says, sets
  the speed signal to standstill itself.
- **Prime** is capped in ml at the 100 % rate (hose volume plus a
  margin) instead of the 20 s today, which is 67 ml at 200 ml/min. It is
  not consumption only where it goes into a cup or fills the tubing
  (RAT-040, RAT-070). Whether the outlet points into the tank, and
  whether prime reduces the stock, is Q9; whatever goes into the tank is
  a dose and stays within that dose's bound (RAT-039, RAT-050).
- **Stock.** A stock driven below 0 is reported (RAT-015); today the hub
  stops it at 0 without a report, which step 7 changes. A short part of a
  pair blocks the whole mix (RAT-054). Whether a short stock warns or
  blocks, and for which jobs, is Q10.
- **Current check.** #94 measures the current of the pump group. Current
  while no pump is switched on switches the group off and latches it
  until the user acknowledges it (RAT-062), for every pump type.
- **Third-party pumps** get a device ID of their own per port, so roles
  still bind to a device ID, never to a port. Their budget uses the
  calibrated rate times a margin (Q8). Two residual risks for setup to
  name: a third-party pump moved to another port is driven for that
  port's role and doses its own liquid in its place, which nothing can
  detect; and the chip stores the product set at setup but cannot tell
  which container the module sits on, so a nutrient module put on the
  pH− container doses acid at the recipe's ml/L. Step 7 asks for the
  liquid at each container change.
- **Three-way valve** (#50: it waits while dosing or filling runs).
  Proposed (Q15): it may move in a pause between runs, and the pause is
  lengthened to the valve's travel time plus twice the 0.2 s start offset
  of #50. Otherwise watering waits for the whole mix, about 35–45 min for
  1,000 L at 200 ml/min, depending on the recipe. Watering would then
  draw from a tank in the middle of a mix (part A without B, not yet
  mixed in, before the pH step), which Q15 asks about too.
- **Settle time in large tanks.** #132 covers mixes up to 1,000 L. In
  the simulator's mixing model a 1,000 L tank is only about 17 % mixed
  after the 4 min settle time, so an EC correction would overdose, which
  cannot be undone (RAT-012). Until a settle time from volume and
  circulation is measured (RAT-052, product repository), mixing to the
  target and EC and pH rounds wait at least the model's time to
  tolerance at that volume with RAT-058's margin, marked as an
  assumption; configuration can only lengthen it (R7). The expected
  duration includes all waits.
- **RAT-007 and RAT-018** name a 90 s cut-off in the dosing block. Board
  v0.1 has none (PD-160, SD-045). Both entries say so now; the
  simulator's 90 s check stays for the earlier dosing block only.
- **Built in steps** (M1, after proto.5, each its own PR with tests):
  1. Calibration table and stage picker as pure functions in the core,
     backwards compatible.
  2. Simulator: speed and pulses on the bus, the board v0.1 pump twin
     with 10 ports and its faults; existing scenarios stay on today's
     pump profile. The new bus method for the speed signal joins the R1
     check in `tools/arch_check.sh`, with a test case.
  3. Planning uses the picker: mix, EC and pH rounds, hand doses, and an
     expected duration that includes pauses, waits and settle time.
  4. Safety limits in a core module that only decides: `Actuators` calls
     it, and it never calls the bus (R1). It holds 60 s per run; a
     ceiling per job, counted in pumping time, from the dose's upper
     bound at the 100 % rate and never below the longest valid job
     (RAT-018), as the job's fixed end (SD-045); the budget at the 100 %
     rate; the minimum pause; and the stock at the actual time.
  5. Feedback checks, latches and the speed signal at standstill.
  6. ID chip and third-party pumps with the current check.
  7. Container change: prime in ml, remaining volume, liquid check,
     tubing counter with the reminder to both pumps of a pair, stock
     below 0 reported.
  8. App: stage table from the tank size, expected duration, "Which pump
     is running?", with `ux`.

  Steps 3 to 7 change when doses are refused, the gateway or protective
  cut-offs: they need `security` and the project owner's "mergen"
  (SD-033).
- **Simulator numbers** for the new pump will be listed as assumptions in
  `SIMULATOR.md` with step 2, until the samples are measured: the 100 %
  rate itself (about 200 ml/min, derived from #132), the stage table,
  rate and spread per pump and stage, start and stop volume, minimum
  stable speed, pulses per revolution, currents, detection times and
  tube wear. A 100 % rate of 200 ml/min would make the demo tank
  (31–40 L) unable to dose pH, which is why existing scenarios keep
  today's profile.
- **Open for the product side** (taken there by the project owner,
  SD-029):
  - Q1 What is a dose's upper bound per purpose, and for a mix part?
    Proposed: the existing caps, and for a mix part one of the options
    above.
  - Q2 Does pH− (and later pH+) use the same pump? Then automatic pH
    needs at least about 56 L or 139 L. Proposed: a low-flow module
    (10–20 ml/min, usable from about 3–6 L at 1.0 s, 7–14 L at 2.5 s).
  - Q3 Minimum run time of the new pump: 1.0 s or 2.5 s? With 2.5 s the
    default hand-dose cap needs to change (RAT-039).
  - Q4 Does "up to about 20 min" mean per run or per job? Proposed: per
    job.
  - Q5 Does PD-147's booking cover stock and mix progress too, or only
    the safety budget? Proposed: only the budget.
  - Q6 Calibration runs last 10–60 s, doses as little as 1–3.5 s, where
    start and stop volume dominate. How are short doses calibrated? May
    stages between support points be filled in, for example from ml per
    revolution?
  - Q7 Which ceiling per type applies before any calibration? It lives
    in code; a value from the chip can only tighten it.
  - Q8 Budget values and time window: from the tank capacity? What
    margin for third-party pumps?
  - Q9 Where does the outlet point during prime and the test run, tank
    or cup? Does prime reduce the stock?
  - Q10 Short stock: warn or block? For mixes only, or also EC and pH
    rounds and hand doses?
  - Q11 Does "no pulses" latch the pump until acknowledged?
  - Q12 Tubing counter: what it counts, at which threshold, and stored
    on the chip (write wear) or on the board?
  - Q13 May a dose end by revolution count? That would change PD-147.
  - Q14 Mixing outside the tank, for example in 5 L buckets (open in
    SD-057).
  - Q15 May the three-way valve move in a pause between runs, and may
    watering draw from a tank in the middle of a mix?
  - Q16 May a booking be saved in the pause between two runs of a job,
    which changes SD-045's "the write waits for the job's end"?
- **RAT entries the product side may change later,** depending on the
  answers: RAT-039 (the hand-dose default, if Q3 is 2.5 s), RAT-040 and
  RAT-070 (if prime reduces the stock, Q9), RAT-055 (if Q4 is per run),
  RAT-052 (once settle times are measured). No RAT rule changes with
  this entry; RAT-007 and RAT-018 only say now what PD-160 decided
  instead of "still open".
- **2026-10-14, ahead of step 7:** at the project owner's request, before
  0.1.0-proto.5, the hub keeps a stock booked below zero and reports it
  each time it falls below zero, instead of stopping it at 0 without a
  report. Below zero means at or below -0.5 ml (`kStockBelowZeroMl`), so it
  shows as at least -1 ml and a rounding difference under 0.5 ml raises no
  alarm. A stock the user enters is never below zero; an unchanged one
  still saves, so such a bottle can be renamed, and the bottle editor
  sends the amount left only when it was changed there. The texts lead
  with the bottle: if it is empty, change it; otherwise enter the amount
  left; calibrate the pump again only if it keeps happening. A mix sees
  the stock as used up. This changes no dose refusal, switching or
  cut-off, and a mix is refused exactly when it was refused before; the
  only new refusal is an amount entered below zero (an input check).
  `security` checked it under SD-033; the rest of step 7 still needs
  "mergen".


## SD-067: A low-level latch releases itself: which latch, margin, wait and what holds (#27)

Implements SD-022 for the circulation pump. Decided in the software session
on 2026-10-13 after the `domain` and `security` reviews. The margin, the
wait and the reasons to hold are own rules, not from a source.

- **Which latch:** the circulation pump's latch `circulation.dry`. It is
  set only when a usable level falls below the minimum while the pump runs
  (RAT-062), so its cause is a low level; it now stores that
  (`cause: "level"`). Its texts say "dry run" because that is what it
  prevents. "Dry run" in SD-022 ("dry run … still holds") is read as a dry
  run detected from the pump itself (current draw, RAT-049, PD-031); such
  a latch will get a cause of its own and hold. This reading follows RAT-051
  (the reference installation's level-type dry-run lock released itself);
  if PD-044 meant otherwise, that is a product question.
- **The inlet is unchanged:** none of its latch reasons is a low level (an
  invalid level is an unknown cause, the capacity reached is the emergency
  limit, "open too long" is an unknown cause), so all of them keep holding
  (RAT-031). Which inlet latch PD-044 means is a product question, asked
  in #27.
- **Release:** a valid level has to stay at or above the minimum plus
  max(0.5 L, 2 % of the capacity), and not above the capacity, for 5 min
  (60 L tank, minimum 3 L: 4.2 L), with the pump read as off (an unknown
  state, a plug that does not answer, waits). Any other reading starts the
  wait again, and so does a restart. Not in maintenance mode (hands in the
  tank); after it ends, the 5 min start then. Where the release level lies
  above the capacity, the latch never releases itself and says so. The
  release is an event with the level ("Dry-run lock released itself"); the
  circulation controller then decides whether the pump runs. While such a
  latch stands, the controller shows "blocked", not "needs release"; the
  Release button stays. During an emergency stop the latch may release
  itself too; the pump starts only after Resume, a person's act.
  - The margin is at least the switch-on hysteresis of 0.5 L (RAT-078) and
    grows with the tank above 25 L, whose level sensor moves more in
    litres (10 L: 0.5 L, 60 L: 1.2 L, 200 L: 4 L).
  - 5 min let a refill settle, waves and a moving sensor pass, and still
    let mixing resume soon.
- **Holds until acknowledged** (stored as `hold`, the reason named in the
  texts):
  - `off_failed`: switching off was not confirmed, or the pump ran again
    after it had been read as off (a button on the device, a relay that
    does not hold), so it may have run dry; the "Off not confirmed" alarm
    asks for a person. This reason replaces any other: check the pump
    first;
  - `repeat`: the level was low again within 60 min of the last
    self-release: something keeps draining the tank or the sensor flutters.
    The time of the last self-release is kept in the runtime state
    (`selfReleased`), survives a restart and keeps its distance across a
    clock jump. One stored before the clock was known, or damaged, counts
    as the moment the clock is known: one more careful hour, then it ages
    out; the other latches are kept;
  - `jump`: the level jumped while latched (RAT-042) without an inlet run,
    maintenance mode or, since SD-101, a watering run to explain it, for
    example a sensor stuck high,
    condensation, or a hose without maintenance mode (one tap then, with
    the person at the tank);
  - a latch without a stored cause (saved by an older version), with any
    other key (written by another version) or damaged (RAT-063);
  - all other latches: `inlet.fault`, `ec.no_effect`, `ph.no_effect`, the
    planned heater cut-off; jump locks lift by their own rule (RAT-042).
- **Texts say which kind it is:** a latch that releases itself says from
  which level ("the lock releases itself once the level has stayed at
  4.2 L or more for 5 min") and asks for maintenance mode when filling by
  hand; one that holds says why and what to check; without a level sensor
  it says to tap "Release". The texts speak of dry-run protection, since
  the pump was switched off before it ran dry. Acknowledging stays
  possible at any time.
- **Saved at once:** a latch set or released by the hub, any latch, is
  saved in that tick, also when a later step of the tick fails; an
  acknowledgement within a second (RAT-063).
- **Residual risks, accepted with "mergen":** a level sensor that reads a
  refilled tank that is not can restart the pump, which then may run dry
  until the level reads low again or, later, the current draw shows it
  (PD-031). Before SD-022 a person looked at the tank. The `jump` hold
  only sees a change of more than 20 L within 5 min (RAT-042, catalog),
  so it does not see:
  - a sensor that drifts high slowly or sticks without such a jump; in a
    tank under about 20 L between its minimum and its capacity no stuck
    reading is a jump at all;
  - a wrong reading while the inlet is open and for 5 min after it, since
    a refill explains a change; nor, since SD-101, a fall of the level,
    or a rise back to where a run started, while the watering pump runs
    and for 10 min after it;
  - a wrong first reading after a restart, since the jump check starts
    without a reference.
  The level has no standstill check, and its plausible range is 0–1000 L.
  The repeat guard catches a second low reading within the hour. A
  restart in maintenance mode ends it (#154), so the 5 min can then start
  with hands in the tank. A jump threshold that scales with the tank is a
  follow-up (#154).
- **Operating patterns:** in NFT or RDWC, drain-back after the cut can
  raise the tank by more than the margin: one self-release, one more cut
  at the minimum, then `repeat` holds. In ebb and flow with floods more
  than an hour apart, each flood that drains the tank below the minimum
  gives an alarm that clears itself without asking a person; that is what
  SD-022 asks for.
- This changes a protective cut-off in the actuator gateway, so the merge
  waits for the project owner's "mergen" (SD-023, SD-033).


## SD-068: The Home Assistant app's first password needs a setup code from its log

Decided by the project owner on 2026-10-13 in the software session (#79,
option 1 of the issue); the details come from a review by `security`.
Until now anyone in the home network who opened the app's page first could
set its password, see the names and values of Home Assistant's sensors and
lock the owner out until the app was reinstalled. The setup code of the
Home Assistant app is replaced by SD-077 (from PD-225): the app moves
behind Home Assistant's Ingress. The rest stays, its rule on random
numbers included.

- **The code:** at every start without a password, `gc_ha_server` makes a
  setup code and prints it to its output, which as an app is the app's log
  in Home Assistant, readable by its administrators only (and by apps they
  gave the Supervisor role `manager` or `admin`, which are as privileged). 16 characters of
  32 (no 0, 1, I or O; 80 bits), in groups of four. The first password
  needs it; it is typed in any case, with dashes or spaces.
- **Kept in memory only,** never in a file, the event log, the state or the
  diagnostic package; used up by the password; a new start makes a new
  one and voids the old. No time limit for the app; the core takes one,
  which SD-044's setup token for the ESP (15 min) will use.
- **No lockout for wrong codes:** 80 bits leave nothing to guess (rate
  limiting is asked for below 64 bits, NIST SP 800-63B revision 3,
  §5.1.2.2; not checked against revision 4), and a
  count shared by every device that never signed in (SD-051) would let
  anyone in the network block the owner's setup. Wrong codes are counted;
  the event log gets the first one and then at most one entry per 15 min
  with the count, and the setup's entry names the count, so a loop cannot
  fill the log (SD-051).
- **The order of the checks:** a password already set (409), then the
  code (403 `auth.code`, the same for a missing and a wrong one, from the
  JSON body only), then the password's strength (422); no password is
  hashed before the code is right.
- `GET /info` says `setupCode: true` while one is needed, so the setup
  page asks for it and says where it is.
- No code while the credentials are lost (setup answers 423 then).
- **Random numbers** for the code, salts and session tokens come from
  `getentropy()` (on Windows the system's generator), not from
  `std::random_device`: libstdc++ tries RDSEED first and takes the zero
  that a Zen 5 processor without its fix reports as success (AMD-SB-7055).
- The simulator asks for no code: it listens on 127.0.0.1 unless told
  otherwise, its data is simulated and it switches nothing real; started
  with `--host 0.0.0.0`, whoever comes first sets its password, and the
  demo has a fixed one. The ESP gets SD-044's token; until it is built,
  whoever comes first sets the ESP's password.
- Not chosen: Home Assistant's ingress now (its login and HTTPS, but the
  web app would have to work below a path); it stays the follow-up before
  the app leaves `stage: experimental`. A lockout for wrong codes (see
  above).
- Open for the product session (PD-100): whether a code of 80 bits without
  a rate limit meets EN 18031-1 AUM-6, and whether the app's log counts as
  a channel only the owner reaches.


## SD-069: The software session takes its product questions to the product repository itself

Decided by the project owner on 2026-10-14 in the software session.
Changes SD-029: the software session attaches the product repository for
its product questions and takes them there itself. It still names them in
the issue and to the project owner. Replaced by SD-072 (from PD-226):
product questions are issues in this repository, and the software session
no longer accesses the product repository.

- A product question met here goes into the note
  `docs/notizen/fragen-aus-der-software.md` of the product repository
  `Wokesay/growcontroller`, as a pull request there, in German like that
  repository: per question the context, the options and the software's
  recommendation, with references to SD, RAT and pull requests of this
  repository.
- The software session writes nothing else there: no PD, no other file,
  no merge. The product repository's session decides, records the PD
  (PD-100), and sends what it means for the software as an English issue
  to this repository, as before.
- The session attaches the product repository only for this, with the
  project owner's approval of the access.
- First use: three questions in Wokesay/growcontroller#77 (the setup
  code and EN 18031-1, build tools in the SBOM, CRA Art. 14 and
  advisories).


## SD-070: CI runs one pinned Node; it is raised by hand and checked at every release

Decided by the project owner on 2026-10-15 (#163, PR #183). Adds to
SD-058, whose check runs with the npm that comes with Node.

- **The pin:** `.nvmrc` names one exact Node version, 22.23.3 when this
  was decided, the newest of Node 22 (raised to 22.23.4 on 2026-10-22).
  Every job that sets up Node reads it (`tools/workflows.test.mjs`). Before, `node-version: 22` took whatever
  the runner image had, so the npm that checks the signatures changed
  without review, and a failure could not be reproduced.
- **Raising it:** by hand, in a pull request of its own, with a `security`
  review. Dependabot does not read `.nvmrc`.
- **When:** at every release, in step 1 "Prepare" (`docs/RELEASE.md`):
  `.nvmrc` is compared with the newest Node 22 security release
  (`https://nodejs.org/dist/index.json`, `"security": true`) and raised
  if it is lower. Until the next release CI may run on a Node without the
  newest patch. Node is not shipped, though it builds what is (the web
  app in the firmware, the packages and the release), so that risk is
  small.
- **Node 22 ends on 2027-04-30:** the move to the next LTS, together with
  the `@types/node` major, comes before then.
- **What it does not cover:**
  - The app image checks the signatures with Alpine's Node and npm (24
    and 11 in Alpine 3.24 on 2026-10-15). It prints their versions; a pin
    there is open (#163). Changed by SD-071: the image runs the Node of
    `.nvmrc` too.
  - setup-node downloads the pinned Node without checking a checksum when
    it is not in the runner's cache: the same trust in GitHub and
    nodejs.org over TLS as for the runner's own Node, accepted.
- **Not chosen:**
  - A monthly reminder to check the pin: more pull requests and a
    standing scheduled job, for a tool that is not shipped.
  - No pin, as before: patches come by themselves, but the checker
    changes unseen.


## SD-071: The app image checks the signatures with the Node of `.nvmrc`, from the official node image

Decided by the project owner on 2026-10-15 (#163, PR #189), and
confirmed the same day once the `security` review had found the gap on
amd64 below. Completes SD-070 for the Home Assistant app image (SD-034).

- **What:** the image's web stage, which installs the web app's packages
  and checks their registry signatures (SD-058), starts from the official
  `node` image at the version of `.nvmrc` on the Alpine release of the
  other stages, pinned by digest (`node:22.23.4-alpine3.24@sha256:…`). It
  brings the npm of that Node release, the one CI checks with (npm
  10.9.9 on 2026-10-15). Before, the stage installed Alpine's `nodejs`
  and `npm` (24 and 11 on 2026-10-15), so the image checked with a
  different npm than CI, and its version changed with every Alpine
  update.
- **Kept together:** `tools/app.test.mjs` fails when the web stage's tag
  names another Node than `.nvmrc`, another Alpine than the other
  stages, or the stage installs anything from Alpine's packages. Docker
  pulls by the digest, not the tag, so the stage itself checks
  `node -v` against `.nvmrc` before npm runs, and the build fails on a
  digest of another Node. Raising `.nvmrc` (by hand, SD-070) raises the
  image in the same pull request.
- **Dependabot:** a newer Node 22 fails until `.nvmrc` follows: such a
  pull request is the reminder for the pull request that raises `.nvmrc`
  (SD-070), and is closed when that one is merged. Node majors are not
  proposed, as for `@types/node`, and Dependabot keeps the `-alpine3.24`
  suffix. A new digest of the same `node` tag (the image rebuilt, for
  example on a fixed Alpine) passes CI on its own, though what changed is
  not in the diff, so it is merged only after a review like a raise of
  `.nvmrc`: docker-node's changes to `22/alpine3.24/` since the pinned
  digest, the build log's `node -v` and `npm -v`, and `security`. Such
  digests come only while the tag is current: once a newer Node 22 is
  out, Docker no longer rebuilds `22.23.3-alpine3.24`, and fixes of the
  web stage's Alpine come only with a raise of `.nvmrc`.
- **Where Node comes from:** the `node` image is maintained by the
  Node.js Docker team (`https://github.com/nodejs/docker-node`) and built
  and published on Docker Hub by Docker's Official Images program. Up to
  Node 22.23.3, on amd64 it unpacked the musl build of
  `unofficial-builds.nodejs.org`, checked against a checksum written in
  docker-node's Dockerfile, not against a signature of the Node release
  keys, although `nodejs.org` publishes a signed musl build for amd64
  too; on arm64 it builds Node
  from the `nodejs.org` sources, checked against those keys
  (`22/alpine3.24/Dockerfile` there and
  `https://nodejs.org/dist/v22.23.3/SHASUMS256.txt`, read 2026-10-15).
  That gap on amd64 ended earlier than expected: from Node 22.23.4 on
  (docker-node commit `80f02a5`, read 2026-10-22), the image takes the
  musl build from `nodejs.org` on amd64 too, checked against the release
  keys' signature of `SHASUMS256.txt.asc`. Node is not shipped, though
  it builds what is (the web app in the image) and checks the packages,
  so a tampered Node could pass a bad package and change the web app.
- **Docker Hub:** both base images come from it, as alpine did before.
  The digest pins their content, so a changed image on the registry
  fails the build instead of entering it; what remains is availability
  (outages, pull limits). The images' attestations are not checked:
  Docker retired its signing of Official Images in 2025
  (`https://www.docker.com/blog/retiring-docker-content-trust/`, read
  2026-10-15), so the digest taken in the pull request is the anchor of
  trust.
- **Same Alpine:** a security raise of `.nvmrc` needs a `node` tag on the
  Alpine of the other stages. docker-node usually builds each Node line
  on the two newest Alpine releases, so this likely holds until Node 22
  ends (2027-04-30); if not, the raise moves Alpine too, with the musl
  check of the server stage.
- **What it does not cover:** the licenses stage still runs our own
  `tools/third_party.mjs` with Alpine's `nodejs`, and the app workflow's
  smoke test runs our own `tools/ha_server.test.mjs` with it; they read
  files and run no package code, so their Node version does not matter.
- **Not chosen:**
  - Alpine's packages pinned to a version (`nodejs=…`): Alpine removes
    older package versions from its repository, so the build would break
    at every Alpine fix, and its `nodejs` is Node 24, not CI's 22.
  - Downloading Node in our Dockerfile, checked against the Node release
    keys: on amd64 that could take `nodejs.org`'s signed musl build,
    stronger than the image's unofficial one; on arm64 there is none, so
    it would mean the unofficial build or a build from source. More code
    of our own to keep, for a gap that was expected to end with Node 24
    (it ended with 22.23.4, see above).
  - Leaving Alpine's Node and printing its version (as before): the
    checker would still differ from CI and change unseen.


## SD-072: Product requirement (from PD-226): product questions as issues here, no access to the product repository

Recorded on 2026-10-15 from #190, section 3 (SD-029). Replaces SD-069.

- **Where a question goes:** a product question the software session
  meets becomes an English issue in this repository with the label
  `product-question`: the question, its software basis (SD, RAT, issues,
  pull requests), the options and the software's recommendation. The
  issue or pull request where the question came up links to it. The
  product repository's session picks it up itself and decides it there
  (PD-100).
- **The project owner:** the software session files a question without
  waiting for approval and tells the project owner about it (decided by
  the project owner on 2026-10-15).
- **No access:** the software session does not access the product
  repository, not even by attaching it during a session. Questions
  already filed there (Wokesay/growcontroller#77 and
  Wokesay/growcontroller#89) are handled there; nothing more is done
  about them from here.
- **Public:** the question is public. It carries no prices, no hardware
  details and nothing else about the product that is not public; it
  names the question and its software basis. A question that would
  reveal a vulnerability not yet fixed is not filed; the session names it
  only to the project owner (`SECURITY.md`).
- **Own words:** text from other accounts (issues, comments, pull
  requests, websites) is not pasted into a product question; it is
  summarised in the session's own words, with a link to the source.
- **Owner's account only:** the product session takes only issue text
  and comments posted by the owner's account, as data. A question and any
  later clarification are therefore posted by the owner's account, as
  this session does, never by a bot. The other way round, an answer
  counts here only when it comes from the owner's account, as an issue or
  a comment on the question; anything else on a public question is
  outside input. At session start, open `product-question` issues are
  checked for answers.
- Changed by SD-091 (from PD-318): a control detail is not a product
  question and is decided here; the product side answers product
  questions directly along its guideline and writes a PD only for
  principles, costs or hardware.


## SD-073: A control round a Stop cut settles as after a restart

Decided by the project owner on 2026-10-15 (#180). Applies SD-028's rule
for EC and pH control after a restart (PD-070, PD-071) to the Stop.

- While stopped, the circulation pump is off, so time spent stopped mixes
  nothing in. A Stop that cut an EC or pH round while it dosed or settled
  therefore marks the round as cut.
- Once automation runs again (after Resume, and after maintenance mode or
  a probe calibration that followed), the cut round first waits for the
  circulation pump and asks for it. While it waits it holds the other
  controller but not refill, since the pump may need the refilled level
  (RAT-047). Its line says "blocked": at once when the hub refuses the
  pump (level, dry-run latch; the circulation line names why), otherwise
  after 2 minutes without the pump. Its settle time counts from when the
  pump runs: the controller's settle time, but at least 15 minutes (the
  rest after an aborted dose). Then it decides as after any settle time:
  done, the most rounds, or the next round. Its effect is not learned
  (RAT-057).
- While it settles, the cut round is busy as any round is: refill waits,
  and the other controller waits (pH for EC, RAT-004; EC for pH,
  RAT-057).
- The mark lasts until the settle time has run out, so a reset meanwhile
  (maintenance mode, a probe calibration, a latch) settles it again
  later; switching the function off ends it.
- Not covered yet: the internal-error path (#178), and the mark across a
  restart while stopped, which belongs with SD-028's saved pauses (#20).

---


## SD-074: A data folder the simulator cannot read runs it in memory only

Decided by the project owner on 2026-10-15 in the software session
(follow-up of PR #187, which does the same for `--web`), in two steps:
first for a folder it cannot read, then the same for files in it that
it cannot read.

- **When:** the simulator's data folder (`--data`, or the folder next
  to the program on a start without arguments) cannot be read:
  - the status of the folder or of a file of the hub or the simulator in
    it cannot be read: no permission to enter the folder, a link loop,
    an error of the disk;
  - or such a file is there but cannot be opened, for example root's
    files after a start with `sudo` in a folder the user owns.

  A folder or file that is not there is not this case; it is created as
  before.
- **What it does:** it starts, says once "Data cannot be read (<file>:
  <reason>). Running in memory only.", and runs in memory only, as it
  already does for a folder it cannot create. It no longer names the
  folder as its data folder at the start.
- **The folder is left alone:** it may hold data of an earlier start, so
  the simulator reads nothing from it, writes no new scenario over it and
  removes nothing in it, also when the scenario is changed
  (`tests/core/test_storage.cpp`, `tools/sim_server.test.mjs`).
- **Why:** before, a folder it cannot read ended the simulator at the
  start with a filesystem error; started by a double click, its window
  just closed. A file it cannot open was taken as missing: the hub
  started without its configuration and password and later saved over
  the old files. Likely cases come from a start with `sudo`: it makes
  the folder next to the program (the owner's only), so the next start
  as the user cannot enter it; or it writes root's files into the user's
  folder.
- **Not chosen:** refusing to start with exit code 2 and a message.
  Nothing would be lost unseen, but on a double click the window closes
  before the message can be read.
- Applies to the simulator. The hub's storage on the device comes with
  the firmware.
- Not covered: a file that can be opened but is not a regular file (for
  example a folder named `config.json`).


## SD-075: Product requirement (from PD-228): the SBOM lists what ships; build tools in a tool list

Recorded on 2026-10-15 from the PD-228 comment on #190 (SD-029).

- **The SBOM lists only what ships in the product.** Build tools (for
  example Vite, TypeScript, compilers, base images) do not go into it.
- **Gaps to close:**
  - the bundler's helper code: the check for tool code covers Rolldown
    (with the next release; done, below). If its helpers could not be
    avoided, Rolldown would be listed as a shipped package;
  - the statically linked runtimes (musl, the GCC runtime) in the SBOM;
  - an SBOM for the Home Assistant app image (#81);
  - the firmware's runtimes, once the firmware ships.
  musl, the GCC runtime and the image SBOM probably come with the release
  after the next one.
- **Technical documentation:** names the build tools, generated per
  release from the lockfile and the image digests.
- **A public tool list with every release:** npm and C++ build tools,
  compilers and base images with their digests, as a plain file with the
  header "not an SBOM, not shipped", part of `SHA256SUMS` and the
  attestation. No second "build" SBOM, and no `scope: excluded` in one.
- **Decided on the software side** (proposals to the project owner,
  decided on 2026-10-15, SD-079 points 5 and 6): how the build inputs
  Dependabot does not watch are tracked (`cmake/deps.cmake`, the ESP-IDF
  image, the unpinned Alpine packages), and whether a CVE in a build tool
  counts as a security item (PD-220).
- **Open on the product side:** what the implementing act on the SBOM
  format (CRA Art. 13(24)) will require.
- **Done: the build checks every module the bundler puts into the app.**
  A Vite plugin (`web/scripts/tool-code-guard.mjs`) looks at every module
  of every chunk and fails the build on one that is neither our source,
  the page nor a runtime package of `web/package-lock.json`: Rolldown's
  runtime helpers, Oxc's lowering helpers, Vite's preload helper and
  polyfill, any other module of a build tool, and a build-time package.
  It also fails when it checked nothing (no chunk lists `src/main.tsx`),
  and on a script that is not a chunk of the build (a web worker).
  `web/scripts/tool-code-guard-selftest.mjs` (in `tools/ci.sh`) builds two
  throwaway apps with the pinned bundler to show the guard names
  Rolldown's and Vite's helpers and passes plain modules. The text check
  of `web/scripts/no-tool-code.mjs` stays for the output, the page
  included. On 2026-10-15 the app holds no such module, and the built app
  is the same byte for byte with and without the guard.
- **What it does not see:** code the build tools write into our own
  modules (for example helpers a transform inlines), text added after the
  modules are rendered, the page, files the app imports as URLs, CSS
  `@import` and `url()` files, and `web/public/`. A web worker (also one
  inlined into a chunk) and a script that is not a chunk of the build (a
  script imported as a URL) fail the build rather than pass unseen.
  That is ours as built by the tools in the tool list. It keeps the SBOM
  accurate against honest tools and mistakes; it is no defence against a
  malicious build tool, which runs in the same process (`docs/SECURITY_MODEL.md`).
- **Done: the web SBOM lists exactly the packages the guard lets into
  the app,** the runtime packages of the lockfile (every entry not marked
  `dev`; `tools/sbom_web.mjs`, #201). `npm sbom --omit dev` left out
  `preact`, which a build tool has as a peer, in 0.1.0-proto.2 to
  proto.4.


## SD-076: Product requirement (from PD-179 to PD-186, PD-191, PD-192): self-flashed now, pre-flashed later, a setup network with a label key, first password in a window, support period

Decided by the project owner in the product repository on 2026-10-12
(#133); PD-180 is replaced by PD-186. The firmware facts come from a
review by `security` (sources in #133); the BOOT button and the factory
data were decided by the project owner on 2026-10-13 in the software
session. Changes SD-044 and SD-024 where said below. Firmware protection
on the DevKit (signed updates, encrypted secrets, eFuse, USB at run time)
is re-evaluated in the product repository and is not part of this entry.

- **Two kinds of device (PD-179):**
  - *Self-flashed*, until the series with hardened firmware: the customer
    flashes the DevKit in the browser over USB, on the DevKit or through
    the board's USB port (PD-148, PD-175). SD-044's setup over USB with
    the one-time token stays. After flashing, the flash page shows the
    link to the web app and starts a short function test of the board,
    only over the USB connection that just flashed it or after login,
    never by a request over the network without login; it switches
    outputs only through `Actuators` as jobs with a fixed end (R1) and
    doses nothing, as no calibration value exists yet (PD-015); which
    outputs it tests is set with M1.
  - *Pre-flashed*, from that series on: the DevKit ships flashed, packed
    loose with the board. The project is then the manufacturer of a radio
    device: an EN 18031-1 and -2 self-assessment for every firmware
    version shipped to these devices. Before the first one ships, the
    list in `SECURITY_MODEL.md` ("Before the first device goes to third
    parties") applies, as amended by this entry; PD-179 does not relax it
    or its timing.
- **Improv over the board's USB port** (changes SD-044, which named the
  DevKit's UART port): the board's port is the ESP's own USB
  (USB-Serial-JTAG). Improv runs there as in ESPHome's ESP32-S3 builds;
  at run time the firmware cuts JTAG off with the bridge bit
  (`USB_JTAG_BRIDGE_EN`) and keeps the serial port. Both are still to be
  proven on a sample with our pinned ESP Web Tools; if Improv fails right
  after the install, "Connect" and "Configure Wi-Fi" again. The DevKit's
  UART port keeps working where it can be reached.
- **Residual risk of USB:** any computer connected to the board's port
  can reset the ESP into download mode and flash other firmware without a
  button press; on the ESP32-S3 only an eFuse stops this for good, which
  PD-022 and SD-024 exclude. The DevKit's UART port has the
  auto-program circuit of Espressif's development boards (DTR and RTS
  drive EN and GPIO0; to be confirmed on the DevKitC-1's schematic), so
  the same holds there. Such firmware can keep the board watchdog fed and
  use the HMAC key. So:
  - the docs say to unplug USB, on either port, in operation;
  - the firmware notices a USB host and shows a message as long as one is
    connected;
  - opening the port can restart the ESP; a restart by the USB host
    (`ESP_RST_USB`) counts as a power loss: everything off except what
    comes back after a power loss (fans, PD-050), reported (R6).
    SD-044's "never restarts while a job runs" cannot hold on this port;
  - SD-024's recessed BOOT button guards flashing on neither DevKit
    port, only on a serial connection without that circuit;
  - an alternative for the open product question on the port: switch the
    USB port to USB-OTG once the app runs. A host can then no longer
    reset the ESP or reach JTAG in operation, but flashing needs a BOOT
    press, the window during boot stays, Improv on that port stops once
    the app runs (the self-flashed setup path), and the DevKit's UART
    port stays a way to flash without a button where it can be reached
    (to be proven on a sample).
- **Setup network** (PD-181, PD-186; changes SD-044):
  - the only way to set up Wi-Fi without a cable; no setup over
    Bluetooth;
  - WPA2 with a random key per device of at least 80 bits. Pre-flashed
    devices get it at production, in the factory data (below), printed on
    the label. Self-flashed devices make it at the first start and show
    it after login, as in SD-044;
  - on a pre-flashed device, open without a button press while no Wi-Fi
    is configured (out of the box, after a factory reset). Whether it
    gets a time limit there is open for the product session; the
    software proposes 60 min after each power-on. A self-flashed device
    is set up over USB;
  - on a configured device only after BOOT is held for 3 s, for at most
    10 min, one client, closed after setup. No fallback access point:
    losing Wi-Fi or a restart opens no setup path;
  - it sets only the Wi-Fi credentials; it never sets or resets a
    password and never returns a stored secret (SD-044).
  - New: the user can change the Wi-Fi in the web app, logged in and only
    from the home network, under SD-044's rules for storing (no actuator
    switched, stored only when no job runs).
  - Whether the BOOT press and the setup network apply to self-flashed
    devices before the series is open for the product session; the
    software supports it with SD-044's key shown after login.
- **BOOT button** (decided by the project owner on 2026-10-13):
  - held at power-on: ROM download mode, in hardware; the firmware never
    sees it;
  - held 3 s in operation: opens the setup network. On a pre-flashed
    device without a password it also starts a new 15-min password
    window: at once if the device is in the home network, otherwise when
    it joins it. PD-192 names pre-flashed devices only; a self-flashed
    one gets a new token from a new Improv session over USB (SD-044). A
    password that is set is never touched;
  - held 10 s to 30 s: factory reset (below); held longer than 30 s:
    nothing, reported as a stuck button, so a button that sticks never
    erases the device;
  - the action happens when the button is released; while it is held,
    an LED shows which one it would be (pattern with M1 and `ux`), so
    holding for 10 s never opens the setup network on the way;
  - a restart ends what a press started (the setup network, a window
    waiting for the home network), so nothing is open after a power cut.
  - The 30 s limit, the action on release and the end at a restart came
    from the reviews and were confirmed by the project owner on
    2026-10-13.
  - Not chosen: separate gestures for the setup network and the password
    window (harder to remember, more mistakes); no upper limit for the
    factory reset (a stuck button would reset the device when it comes
    free).
- Changed by SD-080 (PD-197 to PD-199, PD-217 to PD-219): the factory
  reset starts with BOOT held 15 s, switches the plugs with a water or
  CO2 role off first, and erases only after a short BOOT press within
  30 s; the web app and our flash page run the same switch-off. This
  replaces "held 10 s to 30 s: factory reset" here and the trigger in
  "Factory data and factory reset" below. Its "switches everything off
  through `Actuators` first" becomes: the board's dosing pumps and
  refill valve and the plugs with a water or CO2 role go off through
  `Actuators`; light, fans, room
  heater and air pump keep their state, and the drain with its own pump
  stays on (SD-080). An access reset with BOOT held long (proposed: 8 s)
  also replaces a password that is set, so "a password that is set is
  never touched" no longer holds. Wherever this entry names BOOT held for
  10 s, SD-080 applies.
- **First password on a pre-flashed device** (PD-191, PD-192; changes
  SD-044 for these devices; self-flashed devices keep the token):
  - mandatory, cannot be skipped; set in the home network within 15 min
    from the device's first join to it; no second code on the label;
  - after the window, setting it is locked until BOOT is held for 3 s.
    PD-192 also names a restart. The ESP32-S3 cannot tell a press on
    RESET from a power cut (both report power-on, and nothing documented
    on the chip survives the one but not the other; to be confirmed on a
    sample), so a restart would reopen the window after every power cut,
    with nobody at the device. Open for the product session; until it
    decides, a restart never reopens the window. The software also
    proposes that a restart ends a window that is still open
    (fail-closed), so a power cut in the first 15 min needs a BOOT press;
    this changes PD-191's first window and is open for the product
    session too. "Window used" is stored when the window opens, not when
    the password is set;
  - fail-closed: the password hash, the lock against a second setup and
    "window used" are kept in encrypted NVS, and a "set up" marker in
    `storage` as well. If one of the two is lost and the other is not,
    the device reports lost credentials (423) and needs a factory reset;
    it never behaves like a new device. The firmware never erases NVS or
    formats `storage` by itself but reports it: today `main.cpp` erases
    NVS when it finds no free pages or a new NVS version, and `storage`
    is formatted when mounting fails, which deletes `auth.json` and the
    `passwordSet` marker together. Without NVS there is no Wi-Fi, so the
    LED reports it and the way out is BOOT held for 10 s; a rollback past
    an ESP-IDF update that changed the NVS format also ends there.
    Residual risk: if both are lost, the device is as out of the box
    (setup network open with the label key, a new window);
  - the open window is shown on the setup network's last page (the
    address and the deadline), by a blinking LED and by a countdown on
    the first-password page; after it the page says "hold BOOT for 3 s";
  - for 60 min after the password is set, the login page shows when it
    was set ("Not you? Hold BOOT for 10 s"); after login the web app and
    the event log also show from which address.
  - Residual risk, named in the docs: within the window anyone in the
    home network (guests, another device, a compromised device) can set
    the password first and lock the owner out; the way back is a factory
    reset with BOOT, no computer needed. HTTPS (M2) with a certificate per
    device is trusted on first use, so an active attacker in the home
    network can still read the first password.
  - Proposed by `security` for the product session: bind the first
    password to the label key, so the setup network's form sets password
    and Wi-Fi together while no password exists, and the window in the
    home network goes away.
- **Factory data and factory reset** (decided by the project owner on
  2026-10-13):
  - a read-only partition `fctry` (data, NVS, 16 KB) at 0x1C000 on both
    boards, in the gap before the first app; nothing else moves. It is in
    the partition tables now, as OTA cannot change a partition table, and
    the installer never writes it (tested);
  - content: format version, serial number, board (checked against the
    flash size), SSID, setup key, production month, and the end of the
    support period if it is written at packing (below). Plain, not
    encrypted: the key is on the label anyway, and encrypting it at
    production would need production to make the HMAC key, against
    SD-024's "nobody can read it out". The `readonly` flag only stops
    ESP-IDF's partition functions: own firmware can read it, and esptool
    over USB or raw flash access can overwrite it (PD-022). Production
    keeps no copy and no hash of the key;
  - the factory reset in the firmware switches everything off through
    `Actuators` first (R1), marks the reset in NVS, erases `history` and
    `storage`, then the rest of the user NVS last (CRA Annex I(2)(m)),
    and keeps `fctry` and the app: the device is as delivered, label key
    included. A device that starts with the mark finishes the reset
    before anything else and reports it: a named exception to R6's "not
    resumed", allowed because it switches nothing. It erases in steps and
    keeps sending the sign of life to the board watchdog (SD-045), and
    skips what is already erased, so a watchdog reset cannot loop it.
    Triggers: BOOT held for 10 s to 30 s, and the web app, logged in,
    only from the home network, after asking for the password again;
  - the flash page offers no whole-chip erase, or only with a warning
    that the label's key is lost: ESP Web Tools' erase (the checkbox when
    installing, and "Erase User Data" once Improv runs) is a whole-chip
    erase and no manifest option turns it off, so our pinned copy needs a
    change. After a whole-chip erase the device behaves like a
    self-flashed one (Improv with the token, a new key after login); the
    label no longer applies;
  - secrets the device makes itself (SD-044's key on a self-flashed
    device, session keys) come from the hardware RNG with its entropy
    source on (`bootloader_random_enable()` while the radio is off); the
    label key comes from the operating system's cryptographic random
    generator on the provisioning PC;
  - production needs from this repository, with the series: the
    partition tables (done); a provisioning tool (pinned
    `nvs_partition_gen` and esptool) that checks board and table, makes
    the key, writes only `fctry`, reads it back and tests joining the
    setup network; firmware that reads `fctry` and handles a blank or
    broken one; a specification of key and label (proposed: 20 characters
    from 31 unambiguous lowercase letters and digits, about 99 bits, in
    groups of 5); a label template (SSID, key, Wi-Fi QR code, the address
    after setup, serial, board, source link);
  - residual risks, named in the docs: a photo of the label (the key can
    be changed in the web app, until a factory reset brings the label's
    key back); the key passes the production PC and the printer (an
    offline PC, no key database); after a resale the seller knows the
    key, so the buyer resets the device and changes it; a whole-chip
    erase loses `fctry`.
- **Bluetooth** (PD-153, PD-186): only a passive observer for sensors the
  user adds; no advertising, no connections, no pairing, no setup; off
  while no such sensor is set. When Bluetooth comes, NimBLE is built
  with the observer role only and `tools/arch_check.sh` checks it (NimBLE
  turns on all roles by default).
- **Support period** (PD-182 to PD-185): 7 years from the delivery of
  each board, also for boards delivered before 2027-12-11, ending in
  December (`SECURITY.md`,
  `RELEASE.md`); the web app shows the end date and a notice once it has
  passed (Art. 13(19)). The device does not know its delivery date; the
  end date is written into `fctry` at packing or the web app says "see
  the delivery note", never a guessed date (open for the product
  session). The second reporting channel follows before devices are
  offered (PD-185, `SECURITY.md`).
- **ESP-IDF and a successor DevKit over the 7 years:** Espressif supports
  each minor ESP-IDF release for 30 months (12 months service, 18 months
  security and severe fixes only); a newer one comes in as a normal
  release with the full test run on both boards, about once a year and
  always before the one in use ends (source:
  https://raw.githubusercontent.com/espressif/esp-idf/master/SUPPORT_POLICY.md,
  retrieved 2026-10-13). The partition tables are fixed for the life of a
  device, so each app must fit its `ota_0`. A successor DevKit gets a
  board profile of its own (PD-162); boards already delivered keep their
  builds from the same source for their 7 years.
- **If the project stops** before a support period ends, `RELEASE.md`'s
  exit plan applies: release the keys or offer an owner unlock, and tell
  users and market surveillance in advance (Art. 13(23), PD-005). As no
  device locks out its owner's firmware (PD-022), anyone can keep
  building it from the published source.
- Open for `regulatorik`: whether "whoever is first in the home network
  sets the password" passes EN 18031-1 (AUM); whether a key printed on
  the device needs protected storage (SSM); whether a test lab accepts
  flashing over USB without a button and JTAG on during boot; whether a
  whole-chip erase by the owner conflicts with "reset to the original
  state" (CRA Annex I(2)); whether pre-releases count for the
  self-assessment per firmware version.
- Open for the product session: the time limit before the first setup;
  whether a restart reopens the password window, and whether it ends one
  that is still open; binding the first
  password to the label key; a neutral SSID (a name like
  "growcontroller-xxxx" tells neighbours what is there); placing the USB
  port, a switch for its data lines, or USB-OTG in operation (above);
  where the label goes; the packing step that writes the support end.


## SD-077: Product requirement (from PD-225): the Home Assistant app behind Ingress; a password of its own only for direct access

Recorded on 2026-10-15 from #190, section 2 (SD-029). It does SD-034's
follow-up (Ingress). PD-225 left to the software, to propose before
building it, how the app signs users in behind Ingress and what applies
on port 8099; the project owner decided both on 2026-10-15 in the
software session, after reviews by `security`. If the product session
decides otherwise, its decision applies. Replaces SD-068's setup code
for the app, and with it SD-068's plan to keep the code until the app
leaves `stage: experimental`.

- **Product requirement (PD-225):** from the next prototype release
  (0.1.0-proto.5) on, the whole web app of the Home Assistant app runs
  behind Home Assistant's Ingress, with Home Assistant's login. Port 8099
  is closed by default. The setup code in the log goes away.
- **Through Home Assistant (Ingress): Home Assistant's login only,** no
  password of the app's own there. Only Home Assistant's administrators
  may open it (`panel_admin`, left at its default): the Supervisor
  refuses other users from version 2026.10.2 on; before that it only hid
  the panel from them. The event log names the Home Assistant user who
  made a change.
- **Direct (port 8099, a bookmark): a password of its own.** Without one,
  port 8099 shows only a notice that growcontroller opens in Home
  Assistant's sidebar, with the link to the source code, and no API. With
  one, it asks for it as today (the lockout per device of SD-051, the
  sessions of SD-064). Home Assistant's login does not count there: the
  direct address is often another one than Home Assistant's own, and
  Home Assistant's session is not visible to the app outside Ingress.
  Users of 0.1.0-proto.4 keep their password for direct access.
- **What building it keeps:**
  - **Two listeners.** Ingress gets a port of its own in the container
    (`ingress_port`, not 8099, which is Home Assistant's default for
    it), never listed under `ports`. It answers only connections whose
    socket peer is the Supervisor, 172.30.32.2, never an address from a
    header. It takes the user only from exactly one `X-Remote-User-Id`
    in the Supervisor's spelling; a request with none, two, or a copy
    spelt otherwise is refused, so it fails closed. Only `/info` answers
    there without a user, for the Supervisor's watchdog. No password,
    session or token of the app counts there, and it keeps no lockout
    count. Its origin check compares `Sec-Fetch-Site`, not a host name.
  - **Port 8099 takes nothing from Home Assistant:** it never reads the
    Supervisor's user headers or `X-Ingress-Path`, whoever sends them.
    Only its own session counts there, and that session counts nowhere
    else. Its host and origin checks stay as today. The window onto the
    ESP's web app (SD-050) is never served there.
  - **The password for direct access** is set, changed and removed only
    through Ingress; port 8099 offers none of these, signed in or not,
    not even with the old password. Through Ingress no old password is
    asked, and it also works when the password file was lost (where
    setup over the network answers 423 today). Each of them ends every
    session of port 8099 and forgets its known clients (SD-051, SD-064). Removing it turns port 8099 into the notice
    at once; it never reopens a first password over the network. The
    settings show whether a password is set; the event log names who set,
    changed or removed it. The page says the password travels in plain
    text on port 8099 and must not be the Home Assistant password.
  - **User names** from Home Assistant are text users choose: they are
    kept to printable, well-formed UTF-8 like other text from Home
    Assistant (#71), always shown as text, and left out of the diagnostic
    package, which may go to public GitHub.
  - `tools/app.test.mjs` keeps `panel_admin` at its default, the Ingress
    port out of `ports`, and 8099 closed by default; the new keys get
    SD-034's review.
- **Port 8099 closed by default,** as PD-225 says, for a new install and
  for an update where the app's network settings were never saved:
  direct access is a deliberate choice in two steps, a password in the
  app and the port opened in the app's network settings. A bookmark from
  0.1.0-proto.4 works again after that; the release notes and
  `ha/app/DOCS.md` say so.
- **The ESP keeps its own login** (SD-044, SD-076); this entry does
  not change it.
- **Not chosen:**
  - a password of its own also behind Ingress, or only for imports and
    calibration: little gain, since an administrator of Home Assistant
    reaches the app's data through backups and file access anyway, one
    lockout shared by everyone, since all requests come from the
    Supervisor, and two logins;
  - only a notice on port 8099, without a direct login (`security`'s
    recommendation): no access from a bookmark without Home Assistant;
  - port 8099 open by default: an old bookmark would keep working, but a
    login page with a password in plain text would be on the network by
    default, against PD-225.
- **The setup code never ships:** it came after 0.1.0-proto.4, and
  0.1.0-proto.5 brings Ingress. So the count of wrong codes in the event
  log, which PD-225 asks for while the code is in a release, is not
  needed.
- **Before the release:** a review by `security` of the switch (an XSS
  flaw on Home Assistant's origin reaches an administrator's session of
  Home Assistant; framing; only the Supervisor on the Ingress listener;
  the origin check behind Ingress; the host check; the sessions of port
  8099), the reasoning in `docs/SECURITY_MODEL.md`, including why the app
  is protected more strictly than the ESP with its first-password window
  (SD-076, #190), and the product side's
  decision on remote access through Ingress, which #190 asks for before
  this release.
- **Residual risks,** for the docs:
  - the web app runs on Home Assistant's origin: an XSS flaw in it
    reaches the session of the Home Assistant user who opened it, and
    anything else running there (other apps' panels, cards added to Home
    Assistant's frontend) can use the app through Ingress as that user,
    the password for direct access included;
  - HTTPS only where Home Assistant uses it;
  - Home Assistant bans an address after failed sign-ins only when a
    limit is set in its HTTP settings (by default there is none), and has
    no lockout per user;
  - whoever may open the panel (above) reaches the app wherever Home
    Assistant is reachable, through its remote access too, until remote
    access is decided;
  - port 8099 sends its password and session in plain text, as today;
    closing the port hides it from the network, not from Home Assistant
    and the other apps on the host;
  - a password kept from 0.1.0-proto.4 may have been set by whoever came
    first in the network (SD-068). A port mapping saved in the app's
    network settings before the update stays, so for those users port
    8099 and that password work at once. The settings reached through
    Ingress say that a password is set and offer to change or remove it,
    and the release notes advise doing so.
- **Open on the product side:** remote access through Ingress (named in
  #190); the legal points for this app (authentication behind Home
  Assistant's login, plain text on the direct port, user names as
  personal data, whether EN 18031-1 applies) in #220 (SD-072).
- **Merging:** a security rule; the pull requests that record and build
  it wait for the project owner's "mergen".
- Sources (retrieved 2026-10-15): only 172.30.32.2 and `ingress_port`
  (default 8099):
  <https://raw.githubusercontent.com/home-assistant/developers.home-assistant/master/docs/apps/presentation.md>;
  `panel_admin` enforced from Supervisor 2026.10.2:
  <https://github.com/home-assistant/supervisor/pull/7308>,
  <https://github.com/home-assistant/supervisor/releases>; no ban limit
  by default (`login_attempts_threshold` defaults to
  `NO_LOGIN_ATTEMPT_THRESHOLD`, which is -1):
  <https://raw.githubusercontent.com/home-assistant/core/dev/homeassistant/components/http/__init__.py>,
  <https://raw.githubusercontent.com/home-assistant/core/dev/homeassistant/components/http/const.py>;
  a saved port mapping overrides the default:
  <https://raw.githubusercontent.com/home-assistant/supervisor/main/supervisor/apps/app.py>.


## SD-078: A session claims an issue before it works on it

Decided by the project owner on 2026-10-15 in the software session, after
parallel sessions had picked up the same issue. All sessions post as the
owner's account, so assigning an issue cannot tell them apart. The claim
is therefore a comment that names its session, and the label
`in-progress` shows that at least one claim is live.

- **Words:** a session's *link* is its Claude-Session URL, the one in its
  commit trailers. A claim belongs to its branch: only that session
  pushes to it. A *linked pull request* is an open pull request, draft or
  not, whose description says "Closes #N", "Fixes #N", "Resolves #N" or
  "Part of #N".
- **Live claim:** a "Claimed by", "Taken over by" or "Continued by"
  comment from the owner's account, posted after the issue was last
  opened or reopened. It ends with a later "Withdrawn", "Released" or
  "Taken over by" comment that names its branch (and its part, if it has
  one), or when its pull request is merged or closed. Every comment of
  this rule therefore names its link and its branch. Comments from other
  accounts never count.
- **Taken or free:** an issue, or a part of it, is taken if a live claim
  or a linked pull request covers it. A "Part of #N" pull request that
  names no part covers the whole issue. While any part is taken, a
  session claims only free parts. A session decides by reading the
  comments and the linked pull requests, not by the label alone.
- **When to claim:** when a session starts work towards a change, after
  the owner's go-ahead where one is needed. Reading or assessing an issue
  needs no claim. The claim itself needs no further approval.
- **Claim:** the session fetches the issue and its comments again,
  comments "Claimed by <link>, branch `<branch>`" (plus ", part: <item or
  heading>" for a part) and adds `in-progress`. Then it reads the
  comments again. If a live claim on another branch for the same issue or
  part is listed before its own, it comments "Withdrawn: <link>, branch
  `<branch>`" and does not touch the label: the first comment wins.
- **Several parts:** an issue made of independent parts (a checklist or
  sections) is claimed per part, naming the issue's own item or heading.
  There the label only says that some part is taken.
- **While working:** the session opens its draft pull request with its
  first commit, with "Closes #N", or "Part of #N, part: <item or
  heading>". Before each push and before merging it fetches the issue
  again. If its claim was taken over, or a live claim on another branch
  now shows before its own (a comment that arrived late), it stops and
  tells the owner.
- **Waiting:** a claim with a linked pull request is never stale; it ends
  when that pull request is merged or closed. A claim without one that
  waits for the owner or for a product answer comments "Waiting for
  <what>: <link>, branch `<branch>`" and is not stale while it waits.
- **Stale:** any other live claim with no push to its branch and no
  comment naming its branch for 24 hours may be taken over: "Taken over
  by <link>, branch `<branch>`, from <old link>, branch `<old branch>`,
  quiet since <UTC time, ISO 8601>." The new session continues on the old
  branch or starts afresh and says which, then reads the comments again
  as for a claim, so of two takeovers the first comment wins.
- **Same work, new session:** a session that goes on with its own
  earlier work on a claim's branch, for example after a restart in a new
  container, comments "Continued by <link>, branch `<branch>`" and keeps
  the claim. Continuing another session's branch, or taking over a live
  claim at once, needs the owner's word ("Taken over by <link> … on the
  owner's request").
- **End:** the session that merges a pull request with "Closes #N"
  removes `in-progress` right after the merge. After a "Part of #N" merge
  it comments "Released: <link>, branch `<branch>`, part: <part>: merged
  in #<PR>". Work that stops otherwise ends with "Released: <link>, branch
  `<branch>`: <reason, where the work stands>". In each case the session
  removes the label if no other live claim is left.
- **The owner asks for a taken issue:** the session tells him who holds
  it and takes it over only on his word.
- **What it does not cover:** `product-question` issues, which the
  product repository's session answers (SD-072). Work that follows an
  answer gets a regular issue, which is claimed. Work the owner asks for
  directly in a session needs no issue; if it touches an open issue, that
  issue is claimed.
- **Work begun before this decision:** right after this decision is
  merged, the merging session records each open pull request that names
  an issue as that issue's claim: a comment "Claimed by <the pull
  request's session link>, branch `<its branch>`" naming the pull
  request, and the label. This also covers a pull request whose
  description names the issue without "Closes" or "Part of".
- `CLAUDE.md` has the short form; `docs/WORKFLOW.md` §3 shows it in the
  flow of a change.
- **Not chosen:** a GitHub project board (more for the owner to keep up,
  and the same race when two sessions move a card); only an early draft
  pull request (it needs a commit first, and it does not cover work
  without code). SD-061 rejected reserving SD numbers through issues;
  here the issue is the work item itself, and the comment order settles
  a race.


## SD-079: Product requirement (from PD-220): a GitHub advisory for every exploitable security item

Recorded on 2026-10-15 from #190, section 1 (SD-029); the software side's
proposals decided by the project owner on 2026-10-15 in the software
session, after `security`'s proposals.

- **The requirement:** every fixed security item gets its own GitHub
  Security Advisory (GHSA) in this repository, not only an entry in the
  release notes; this includes the seven items of the next release, which
  affect 0.1.0-proto.2 to 0.1.0-proto.4. Point 1 below limits this to
  vulnerabilities until product question #200 is answered; if #200 says
  the other items are vulnerabilities too, they get advisories as well. The release notes give per item
  what it is, the affected versions, the impact, the severity and the
  remedy, the form of CRA Annex I, Part II (4). The severity is rated per
  item. The changelog no longer says that no GHSA or CVE of our own is
  issued.
- **Reporting (CRA Art. 14, the product side's assumption):** at the
  latest from the first offer or the first device lent, actively
  exploited vulnerabilities and severe incidents are reported to the
  coordinating CSIRT and ENISA (an early warning within 24 hours, a
  notification within 72 hours, a final report within 14 days of a fix or
  mitigation for a vulnerability and within a month of the notification
  for an incident), and users are informed. None of the
  seven items is reportable. `SECURITY.md` keeps the voluntary commitment
  to report until then.

The software side's proposals, as decided:

1. **Which items get an advisory:** a vulnerability, that is a flaw that
   someone without the needed rights can trigger: a device in the
   network, a crafted file or another input. A fix in the Security
   section that normal use, a faulty probe or someone with the password
   triggers gets no advisory; it is listed there with the same fields, in
   a block "Safety and robustness fixes, no vulnerability". Product
   question #200 asks whether such defects are vulnerabilities under the
   CRA; until it is answered, this split applies.
2. **When advisories are published:** the drafts are kept private until
   the release with the fix passes part two of its acceptance (SD-065),
   then published. If that release takes longer than 30 days after the
   fix is on `main`, the advisory is published anyway, with "fixed in the
   next release" and the workaround; for the items of 0.1.0-proto.5 the
   date is 2026-11-14, 30 days after this decision. Home Assistant's
   ingress (PD-225, #190 section 2) does not hold up the security fixes: if needed,
   0.1.0-proto.5 ships without it.
3. **CVEs:** for each vulnerability of our own we request a CVE from
   GitHub, which is a CVE Numbering Authority. The project owner requests it in the
   advisory form at least 3 working days before the tag; GitHub's review
   takes about 72 hours. A flaw that comes from a component (a library,
   the processor) gets no CVE of its own; its advisory names the
   component's CVE.
4. **Telling users:** the Home Assistant app gets `ha/app/CHANGELOG.md`,
   with a section per release that lists the security items first, with
   their GHSA links; Home Assistant shows it in the update dialog. The
   app's `DOCS.md` gets a section "Security updates". The web app's
   settings get a fixed link "Security advisories" to this repository's
   advisories, with no network call. Later the firmware marks an update
   with its severity and GHSA link.
5. **Components Dependabot does not watch** (PD-228, SD-075): a weekly
   scheduled job, without write rights and without secrets, checks
   OSV.dev for cpp-httplib and nlohmann/json, and Alpine's security
   database (secdb) for the packages in the last app image; a failed run
   mails the project owner. Inputs used only to build (doctest,
   `registry:2`, and the ESP-IDF image until the firmware ships) stay
   checked by hand at every release. Once the firmware ships, ESP-IDF's
   libraries ship in it and join the weekly job. Before the first
   firmware release, ESP-IDF moves to a current v5.4.x or newer.
6. **A CVE in a build tool** (PD-228, SD-075; for example Vite,
   TypeScript, a compiler) is a security item with an advisory of its own
   only if the tool's code ships (then it is a component), or if the CVE
   could have changed a published artifact: then the artifacts are
   checked, and it may be a severe incident under CRA Art. 14. Otherwise
   the tool is updated and listed under Security › "Hardening, without a
   vulnerability behind it", with the CVE or GHSA and "no published
   version affected".

- **The items of 0.1.0-proto.5** (`security`'s ratings, CVSS 3.1; the
  product side's estimate for A and C was the same):
  - A. Any device in the home network could set the Home Assistant
    app's first password (#79, SD-068): 6.4 medium,
    `AV:A/AC:H/PR:N/UI:N/S:U/C:L/I:L/A:H`, CWE-1188. A CVE from GitHub.
  - B. A crafted backup could make a pH calibrated in Home Assistant
    count as checked by the hub (SD-041): 3.3 low,
    `AV:L/AC:L/PR:N/UI:R/S:U/C:N/I:L/A:N`, CWE-807. A CVE from GitHub.
  - C. A device guessing the password locked every other device out of
    signing in (SD-051): 4.3 medium,
    `AV:A/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:L`, CWE-645. A CVE from GitHub.
  - D. Session tokens partly predictable on AMD Zen 5 hosts without
    AMD's fix (RDSEED, SD-068): 4.2 medium,
    `AV:A/AC:H/PR:N/UI:N/S:U/C:L/I:L/A:N`, CWE-331. No CVE of its own; it
    refers to CVE-2025-62626 (AMD-SB-7055).
  - No advisory: two defects in the sensor truth's jump check (RAT-042),
    in the simulator only, triggered by normal use or a faulty probe; a
    session ended at random, which needs the password.

  All four: affected up to 0.1.0-proto.4 (published: proto.2 to
  proto.4), patched in 0.1.0-proto.5. `qa` confirmed the range on
  2026-10-15 with each fix's test, or one adapted to the older code,
  against builds of v0.1.0-proto.2 and v0.1.0-proto.4 (proto.3 changed
  none of them; RDSEED by the code): all seven items of the Security
  section affect proto.2 to proto.4. Later items get the same check
  before they are published.
- **Open:** product question #200 (point 1); on the product side, what
  counts as the first offer, which CSIRT coordinates (probably the BSI),
  and a lawyer's view of Art. 14.
- Settled by SD-097 (PD-320, #200): point 1 stands; the block of the
  other fixes is labelled "Safety and robustness fixes, not exploitable
  by an attacker (CRA Art. 3(41)); disclosed under Annex I, Part II (4)".
- **Where:** in this pull request the changelog's Security section (its
  blocks, the fields per item), `SECURITY.md` (advisories, what triggers
  reporting, severe incidents) and `docs/RELEASE.md` (the advisory
  steps). In later pull requests `ha/app/CHANGELOG.md`, the section in
  `ha/app/DOCS.md`, the link in the web app and the weekly job.


## SD-080: Product requirement (from PD-197 to PD-199, PD-217 to PD-219): factory reset after BOOT 15 s and a confirmation, plugs off first, what it erases, access reset

Recorded on 2026-10-15 from #133 (SD-029). The project owner decided
PD-197 to PD-199 on 2026-10-13 and PD-217 to PD-219 on 2026-10-15 in the
product repository; they describe behaviour only. They replace two parts
of SD-076: the factory reset by BOOT held 10 s to 30 s, which switched
everything off first, and "a password that is set is never touched".
The rest of SD-076 stays, except where this entry says otherwise. Where
the product side left a point to the firmware, this entry gives the
software side's view, marked "Proposed by the software side (to be
confirmed)"; it holds until the product side decides otherwise.

- **Factory reset with BOOT (PD-197, PD-199):**
  1. The user holds BOOT for 15 s.
  2. The firmware switches off the plugs listed below, before anything
     is confirmed.
  3. A 30-s window opens. The board's status LEDs show whether every one
     of these plugs has reported "off" (PD-212). Which plug is missing is
     shown only in the web app.
  4. A short BOOT press in the window erases, on purpose also when a plug
     did not report "off".
  5. Without that press nothing is erased, and control takes the plugs
     back.
  - To switch off or unplug a missing plug by hand first, the user does
    not confirm and starts again afterwards.
- **Plugs at the start of a factory reset (PD-198, PD-219):**
  - off: every plug with a water or CO2 role: irrigation pump
    (`zone.irrigation_pump`), inlet (`tank.inlet`), inlet pump (PD-120),
    humidifier, dehumidifier, CO2, circulation pump (`tank.circulation`,
    as on a Stop and an internal error, PD-076, PD-077) and the heater in
    the nutrient tank (`tank.heater`). They go off together and count for
    "all plugs off";
  - keep their state: light, exhaust, circulation fan
    (`zone.circulation_fan`), room heater (`zone.heater`) and the air
    pump in DWC and RDWC;
  - not switched off: the drain with its own pump, as on a Stop, a power
    loss and an internal error (PD-135, PD-144). A reset never switches
    anything on;
  - when control switches the dehumidifier on again, without the
    confirmation or after the plug is adopted again, its minimum pause
    applies (PD-052, RAT-034). The gateway knows the pause only from the
    last switch-off it saw, and after a factory reset there is none.
    Proposed by the software side: binding a compressor role starts its
    pause, and an unknown last switch-off counts as now (R5);
  - a Stop, an internal error, a power loss and a loss of control do not
    change.
- **The same switch-off on every own path (PD-217):** the factory reset
  in the web app and the one our flash page starts over USB switch the
  same plugs off first; then the firmware erases. The web app comes
  first, the flash page before the first device is sold. In the web app
  it needs a login from the home network (SD-076) and the password again,
  and cannot be started by Home Assistant or an API token; it shows the
  result per plug before erasing (the product side's view of the
  firmware). Our flash page erases the whole chip
  only when the firmware does not answer, and only after a warning that
  the label's key is lost with it (the warning's text is open).
- **What the factory reset erases (PD-197, PD-218):**
  - everything except the factory data with the label's key (`fctry`):
    Wi-Fi, the password, settings, calibrations, history and the
    passwords of adopted plugs. Calibrations and history are erased, not
    only made unreadable; SD-076's mark finishes an interrupted erase at
    the next start. This covers the ESP's own storage. A pump's
    calibration also sits in its cap's ID chip (PD-010), and some sensor
    circuits keep their own; whether the reset clears those too is open
    (product question, below);
  - it makes a new random value from which the keys of the encrypted
    storage are derived with the eFuse HMAC key, so nothing written
    before can be read with the new keys and a previous owner learns
    nothing new (PD-208, #176). The value is kept apart from `fctry`; the
    eFuse stays as it is;
  - it runs only while no dose runs; the board's dosing pumps and refill
    valve (SD-045) are off during it, and the LEDs show it. The board's
    dimming output (`zone.light_dim`) keeps its level in the window, as
    the light keeps its state;
  - afterwards a pre-flashed device opens its setup network and starts a
    new first-password window when it first joins the home network
    (PD-186, PD-192); a self-flashed device is set up over USB again
    (SD-044);
  - without a valid calibration nothing is dosed or controlled (PD-015,
    PD-218);
    each plug is reset at its own button and adopted again (about 3 to 5
    min each);
  - before erasing, the web app offers an export without passwords. Its
    format and its import are open; both must hold under CRA Annex I,
    Part I (2)(m);
  - kept as device state, not user data (the software side's reading of
    "everything except `fctry`", to be confirmed): the floor of the
    downgrade protection (SD-021, SD-043), so signed older firmware with
    known flaws cannot be installed again after a reset, `otadata` and
    both app slots. Erasing `otadata` would start `ota_0`, which may hold
    an older image;
  - before NVS is erased, the device deregisters its name at the relay
    (SD-062).
- **Access reset with BOOT held long (PD-218):**
  - it opens a 15-min window in the home network, as for the first
    password (PD-191, PD-192), in which the user sets a new password;
  - the new password counts only after a short BOOT press at the device;
    until then the old one stays valid. So it needs someone at the
    device (our reading);
  - afterwards every session, every token and the Home Assistant key are
    invalid, and Home Assistant must be paired again. Proposed by the
    software side: their live connections end at once (the live stream,
    native API connections, relay tunnels), as on lowering the level of
    outside access (SD-059); the lockout forgets its known clients, as
    after a password change (SD-051); "the Home Assistant key" is the
    native API key (SD-043), replaced by a new one shown once; the Home
    Assistant app's own password for direct access (SD-077) is another
    thing and is not touched;
  - neither Home Assistant nor an API token can start it or the factory
    reset. Proposed by the software side: the request comes only directly
    from the home network, decided by the interface it arrives on; not
    through the relay (SD-062), the Home Assistant window or the native
    API (both count as from outside, SD-059), and not over the setup
    network, which serves only the Wi-Fi form;
  - an "access reset" entry stays in the event log until a factory reset
    and is shown after login and on the LEDs;
  - Wi-Fi, plugs, calibrations and history stay, and control keeps
    running;
  - two requests in one window end it without a change (the product
    side's view of the firmware);
  - without a password it works as before (SD-076);
  - the web app and the user guide keep "reset access, data stays" apart
    from the factory reset, and say: factory reset before passing the
    device on;
  - the setup network still never sets or resets a password (SD-044,
    SD-076).
- **Proposed by the software side (to be confirmed):** the points that
  PD-197 to PD-219 leave to the firmware. LEDs and texts with `ux`;
  everything to be checked on a sample.
  - *Hold times:* 3 s setup network (SD-076), 8 s access reset, 15 s
    factory reset. Each acts when BOOT is released; held longer than
    30 s does nothing and is reported as a stuck button. So the plugs go
    off when BOOT is released after 15 s, not while it is still held: a
    stuck button switches nothing, and a user who sees the warning at
    15 s can hold on past 30 s to cancel. This departs from PD-219's
    "already at 15 s" and rests on SD-076's action on release, which the
    project owner confirmed on 2026-10-13. The window's 30 s count from
    the release. A stage counts from its time on (3.0 s, 8.0 s, 15.0 s);
    held longer than 30.0 s is a stuck button. The access reset gets a
    time of its own, unlike SD-076's one gesture for the setup network
    and the first password, because it replaces a password that is set:
    changing Wi-Fi must not open it.
  - *LEDs and buzzer:* while BOOT is held, one pattern per stage reached
    (3 s, 8 s, 15 s, past 30 s), the 15-s one clearly a warning. In the
    window, one pattern for "all plugs off" and another for "a plug is
    missing", changing as plugs report; one for erasing; one while an
    access reset waits for its press. The buzzer sounds for the 15-s
    stage and the window only, an exception to PD-171 for a deliberate
    act at the device.
  - *The confirming press:* released within 2 s, and counted only from
    2 s after the window opened, so releasing the 15-s hold or a bounce
    never counts. A longer press ends the window without erasing.
  - *Plugs in the window:* all at once, as far as the ESP's connections
    allow; each attempt waits at most 2 s and is repeated every 3 s until
    the plug reports "off" or the window ends. The window does not wait
    for them. "Reported off" means the plug's read-back output state,
    not an accepted command; where a plug measures power, near-zero power
    also counts. After the press the firmware keeps trying the missing
    plugs from a list in memory until right before NVS is erased, as
    Wi-Fi and the plugs' credentials are still there while `history` and
    `storage` are erased. "All plugs off" shows only when the list of
    plugs was read and every plug on it reported "off"; a plug whose state is
    unknown counts as missing, never as off (R5), and so does an empty
    or unreadable list. If Wi-Fi is down, no plug can be reached: the
    firmware keeps reconnecting and the LEDs show "a plug is missing". The web
    app shows the plugs only after login, so a user who lost the
    password sees the LEDs only; there is no page without login, which
    would name the plugs to the home network.
  - *Control in the window:* paused. The gateway switches on nothing the
    reset switched off and starts no job; the plugs that keep their state
    are not switched either; cut-offs still act. Without the
    confirmation, control runs again: plugs go to the state control sets,
    running plans are evaluated again, and the safety component releases
    the board's dosing pumps and refill valve as after a normal start
    (SD-045). An interrupted
    dose or refill is reported, not resumed (R6), and a control round
    settles as after a Stop (SD-073). A Stop set before or during the
    window still holds afterwards.
  - *No dose running:* made true at the start, not waited for. On release
    after 15 s the board's dosing pumps and refill valve go off first: a
    running dose ends and is booked as on a Stop (RAT-036), and a running
    refill (PD-161) counts as a dose. Then the plugs go off, so the refill
    valve is never open while the plugs are off. In the window no dose
    can start; at the confirming press the firmware checks again before
    it marks the reset.
  - *Web app and flash page:* the same switch-off, 30-s window and result
    per plug. In the web app "Erase" in the window confirms, or a BOOT
    press if the product side decides so (the software side recommends
    the press: no reset from afar, and all paths end the same way). The
    flash page sends a command over its USB serial connection, and a
    short BOOT press at the device confirms; it asks for no password, as
    whoever is on USB can already install any firmware (SD-076). If the
    firmware does not answer within 10 s, or the serial port is closed
    in operation, only the whole-chip erase with the warning is left.
  - *Roles:* a role `tank.air_pump` (mains, runs continuously) that keeps
    its state at a factory reset, like the fans. Both heater roles stay:
    `tank.heater` (a water role: off) and `zone.heater` (the room: keeps
    its state), each only with RAT-060's latching cut-off, as
    `PLANT_AUTOMATION.md` plans. The classes live in code next to the
    fans' list (PD-050), as the catalog can only tighten safety (R7); a
    role on neither list goes off. The inlet pump, CO2 and drain roles
    get theirs when they enter the catalog.
  - *Access reset:* a request waits in memory only, so a restart ends it
    (SD-076). The same request again, with the same page token and the
    same password, is not a second one; anything else, or a password
    change in the window, ends the window and is logged. Only the first
    request is hashed and kept. The press counts only within 2 min of
    the pending request, so a press made unknowingly does not confirm a
    request that waited long; the guide says "send first, then press when
    the page says so". The client whose request was confirmed gets a
    lockout count of its own, so someone locking a shared count cannot
    keep the owner out of the new password. The access reset sets the
    level of outside access to "home network only" and the relay address
    to its default, as after a factory reset (SD-059, SD-062), and after
    the first login the web app lists the outward settings and
    notification targets for review. While a window is open, signed-in
    sessions show a banner with "Cancel". The page that sent the request says when to
    press BOOT. The "access reset" entry is shown in the event log and
    stored apart from it, so the log's rotation never drops it; the last
    16 are kept. The LEDs, a notice after login and a notice on the
    login page itself ("password replaced at the device on <date>") show
    it for 7 days (PD-218 sets no limit). Without a password each kind of device keeps
    its own way to the first password (SD-044, SD-076); there BOOT held
    8 s acts as BOOT held 3 s. The login page's "Not you?" (SD-076)
    points to the factory reset and names the access reset as the way
    that keeps the data: after a stranger set the first password, the
    access reset would keep what they changed (limits, calibrations,
    adopted plugs, notification targets), so the factory reset is
    recommended there (PD-197).
  - *Overlaps:* a 15-s hold ends a pending access reset; opening the
    access-reset window closes an open setup network; a press in the
    30-s window never counts as a 3-, 8- or 15-s hold; an update and a
    factory reset exclude each other (updates only when idle,
    `RELEASE.md`); a factory reset
    started in the web app or from the flash page while a window is open
    is refused, and the other way round. Maintenance mode does not stop
    the switch-off and stays as it was afterwards. A window ended by a
    restart erases nothing and is reported at the next start (R6).
  - *The random value (#176):* it must last until NVS is erased last, as
    the reset's mark is in NVS. So it cannot sit in `storage` (erased
    before), in `fctry` (kept, read-only) or in the NVS it unlocks.
    ESP-IDF v5.4.1 derives the NVS keys from the eFuse HMAC key with
    fixed seeds, so nothing per reset enters; it accepts keys the
    application computes itself and a registered scheme of our own
    (`nvs_sec_provider.c`, `nvs_flash.h`, retrieved 2026-10-15) [1][2].
    The partition tables are full (`firmware/boards/*.csv`), and a new
    partition can never come by OTA, also not to self-flashed devices.
    So, proposed: before the first firmware release anyone flashes (M1),
    take about 8 KB from `nvs` for a small partition that holds the
    reset's mark and the value together (two records with a sequence
    number), so the mark no longer depends on the NVS it unlocks. A
    blank NVS means erased flash (all pages 0xFF), checked without keys,
    never "nothing readable with the current keys", and makes a new
    value first. The value is made while the hardware random number
    generator is truly random: with Wi-Fi up, or at the next start before
    Wi-Fi with `bootloader_random_enable()` [3], as SD-076 already asks.
    Own firmware (PD-022) needs the key derivation and the partition;
    both are documented in the source.
  - *Flash page details:* opening the serial port can restart the ESP
    (SD-076), so the page opens it without toggling DTR and RTS, the
    firmware answers the command early in its start, and the 10 s cover
    a restart. An older firmware without the command always leads to the
    whole-chip erase with its warning.
- **Residual risks, for the docs (PD-199, PD-218, PD-219):**
  - a plug reset at its own button runs on its factory settings (its own
    power-loss behaviour, no auto-off) until it is adopted again;
  - whoever confirms without looking at the LEDs erases anyway: a missing
    plug then runs unchecked until the new setup, as on a hub failure
    (SD-028); the circulation pump without a limit, the tank heater only
    with its own thermostat;
  - BOOT held 15 s switches the plugs off also without the confirmation,
    until control takes them back. It is no Stop: control may switch them
    on again; whoever works at the tank uses Stop (PD-172, PD-174);
  - "all plugs off" rests on the plugs' own reports: a welded relay, a
    wrong role or an unreachable plug stays on;
  - from the erase on, room heater and exhaust keep their last state,
    limited only by the plug's auto-off and power limit or the device's
    own thermostat, without the hub's heater cut-off (RAT-060); the light
    follows its plug's schedule (PD-051) or its auto-off (PD-065); the
    board's light output (`zone.light_dim`, PD-028) is off after the
    restart, like every board output (R6);
  - in DWC and RDWC the water stands without circulation and tank heater
    until the new setup; the drain pump runs on its own float, which the
    hub no longer watches. Until the plugs are adopted again nobody
    controls humidity (a long pause in flowering risks botrytis), and a
    dehumidifier switched on by hand runs without the hub's tank lock;
  - whoever is at the device and in the home network can take over access
    with the access reset, with the plugs and the history; the entry only
    makes it visible. A stranger's changes to outward settings stay after
    an owner's access reset, apart from the level of outside access and
    the relay address it resets; the review list after login is the
    owner's chance to see them. Anyone in the home network can end an access reset
    with a second request; the way out is then the factory reset;
  - whoever means the access reset but holds BOOT past 15 s switches the
    plugs off, and a short press then erases everything; the LEDs warn
    first;
  - if USB is off in operation or the firmware hangs, our flash page
    erases without the switch-off and the plugs stay in their last state;
    the warning can be clicked away; esptool and other flash pages never
    switch off;
  - a factory reset from afar in the web app switches the circulation
    pump and the tank heater off until someone sets the device up again
    on site;
  - the user guide: look at the LEDs before confirming; BOOT is recessed
    and needs a pen; put the air pump above the water level or fit a
    check valve.
- **Open on the software side:** whether the access reset may also end
  SD-076's lost-credentials state, which today needs the factory reset.
- **Open on the product side:** whether the downgrade floor, `otadata`
  and the app slots count as device state that the reset keeps (above);
  whether the serial channel stays open in
  operation, and whether a factory reset in the web app also needs a
  BOOT press at the device (PD-217); the warning's text; the export's
  format and import; whether adopting a plug again also sets its
  schedule (PD-051) and switches its cloud off again (the power-loss
  setting and the auto-off are set and read back at every adoption
  already, RAT-019, RAT-060); whether the reset also clears calibrations
  outside the ESP's storage (a pump cap's ID chip, a sensor circuit's
  own calibration); HTTPS for
  the password in the home network; whether the web app never shows plug
  passwords and the Wi-Fi key (PD-218); whether the air pump also keeps
  running like a fan on an internal error and after a power loss, and
  whether setup asks where the dehumidifier drains (PD-219).
- **Merging:** a safety and security rule (the actuator gateway, the
  login); the pull requests that record and build it wait for the
  project owner's "mergen".
- Sources, retrieved 2026-10-15: [1] https://raw.githubusercontent.com/espressif/esp-idf/v5.4.1/components/nvs_sec_provider/nvs_sec_provider.c ;
  [2] https://raw.githubusercontent.com/espressif/esp-idf/v5.4.1/components/nvs_flash/include/nvs_flash.h ;
  [3] https://raw.githubusercontent.com/espressif/esp-idf/v5.4.1/docs/en/api-reference/system/random.rst


## SD-081: A file of the hub that cannot be read is kept like a broken one

Decided by the project owner on 2026-10-15 in the software session (#211;
#212 gives the one list of the hub's files it needs); when a kept
`state.json` or `job.json` is written anew was decided the same day after
two rounds of reviews. Follows SD-074, which covers the simulator's whole
data folder, for the hub itself: the Home Assistant server and, later,
the device.

- **When:** a file of the hub is there but cannot be read: no
  permission (for example root's files after a start with `sudo`), a
  read error of the disk or the flash. `IStorage::read` gave no value for
  it, the same as for a missing file; `IStorage::unreadable` now tells
  them apart. A file that is not there is not this case.
- **What the hub does**, as with a file whose content is broken, with
  one alarm per kind of data that names the files and says what to do:
  - `state.json`: it starts stopped, everything off, since whether a
    Stop was in force is unknown (PD-076); locks and stock levels are
    unknown, so the alarm says to restart first and to check the tank
    and bottles before Resume;
  - `config.json`: it starts with the factory settings;
  - `auth.json`: no first password can be set over it (423 `auth.kept`),
    no setup code is armed, and the alarm for lost sign-in data is left
    out: its advice, a factory reset, would wipe the password that is
    kept;
  - `job.json`: a mix it may have held counts as cut off by the restart,
    so a second mix within 30 minutes of the start needs confirmation
    (RAT-003);
  - the events and the history: earlier entries are missing, new ones
    stay in memory (so do sign-ins, failed logins and other security
    events while the events are kept);
  - the Home Assistant server also prints each such file at the start,
    with the reason.
- **An unknown run-time state errs on the safe side**, whether it is
  broken or cannot be read: besides starting stopped, a mix counts as
  having ended at the start (RAT-003), and a dry-run self-release as
  having happened then, so a dry-run latch in the first hour holds for a
  person (SD-067).
- **The file is kept:** the hub does not write over it while it is kept,
  also not by a later save. A snapshot and its journal (`events.json` and
  `events.log`, `history.bin` and `history.log`) are kept together from
  the moment one of them is read. Changes they would hold stay in memory;
  a restart after the file is readable again reads it as it was.
- **Written anew when keeping it would lose something that protects the
  tank:**
  - `state.json` at the first Stop or Resume, and when a new lock or
    latch is set while the hub is stopped (for example a jump of a probe
    taken out to be cleaned). From then on a Stop, latches, jump locks
    and stock levels are saved as usual and survive a power loss
    (PD-076); the new file holds the Stop, so the next start needs
    Resume. Kept for the whole run, a Stop or a lock set in it would be
    lost once the file can be read again, and the hub could run on
    without Resume. The hub started with no lock or latch, so any one it
    holds when it saves the state is new. "Saved state replaced" says
    that the old locks and stock levels are gone, and a Stop that ends
    the keeping is logged, also when the hub was stopped already.
  - `job.json` when a job of this run is written: its old content no
    longer says what is in the tank, and a restart during the new job
    must be reported (R6, RAT-003).
  - Entries made while the state is kept (a stock, an acknowledged
    latch) are lost by a restart before that.
- **Why:** before, such a file was taken as missing. The hub started
  without the Stop that was in force, without its settings or password,
  and later saved over the old files, so their content was lost without
  a word.
- **Not chosen:**
  - running in memory only as a whole, as the simulator does since
    SD-074: on the device one bad file would stop all saving, history
    and settings included;
  - refusing to start: the device would have no interface and no
    control at all;
  - for `state.json`, writing a new stopped state at once, as for a
    broken file: simpler, but a read error that a restart would clear
    would lose the locks and stock levels;
  - for `job.json`, refusing every job while it is kept: on the device
    mixing would wait for a factory reset (#219).
- **Storage:** the simulator's and the Home Assistant server's storage
  reads with `fopen`/`fread` and reports a file it cannot open (other
  than not there) or read to the end as unreadable, with the reason, not
  as a missing or shorter one. The firmware's (`EspStorage`) does the
  same and falls back to the `.tmp` only for a file that is not there
  (ENOENT). ESP-IDF 5.4's SPIFFS gives ENOENT for a file not found, but
  also for a deleted entry or one that is not a file, which the file
  layer cannot tell apart
  (https://raw.githubusercontent.com/espressif/esp-idf/v5.4.1/components/spiffs/esp_spiffs.c,
  `spiffs_res_to_errno`, retrieved 2026-10-15). A scenario change in the
  simulator still removes a kept file: it is the user's own wipe.
- **Not covered yet:**
  - the web app does not show a kept file before the user sets up again
    or signs in, and settings changed while `config.json` is kept answer
    as saved (#217);
  - on the device, a file that stays unreadable is kept at every start,
    and the only way out is a factory reset (#219);
  - with `config.json` kept and `auth.json` missing or broken as well,
    whether a password was set is unknown and the first password stays
    open (to whoever has the setup code on the Home Assistant server); as
    before this decision, until SD-076's marker exists;
  - when SD-076, SD-077 and SD-080 are built: a kept `auth.json` counts
    as a password set (no password window, no countdown, no advice to
    reset), and setting, changing or removing a password through Ingress,
    or an access reset, answers `auth.kept` first instead of a new
    password that cannot be saved.


## SD-082: A refill stays within the inlet's open time, checks its rise and fills in steps

Decided by the project owner on 2026-10-15 in the software session
(#155), on the proposal of `domain`.

- **Problem:** a refill planned longer than the valve may stay open
  (`max_open_min`, 15 min by default) ran into that limit. The limit is
  the inlet's fault cut-off (RAT-031, RAT-032), so every refill from a
  nearly empty tank latched the inlet and waited for a person. And no
  refill checks whether the level rose: with an empty water supply, a
  leak or a stuck valve, the inlet opens again after every pause for as
  long as the level stays below the start level.
- **Within the open time:** one fill keeps the valve open at most
  `max_open_min` minus 1 min, or minus half of it if that is shorter, so
  a late controller tick still closes it before the limit. The limit
  itself is unchanged and still closes a valve the hub opened, however
  it opened it. An inlet that adds less than 0.5 L in one fill blocks the
  refill with a line naming the rate and the open time.
- **In steps:** what does not fit follows in steps after a 10 min pause,
  also above the start level, each named in the refill's line. The total
  is computed once, at the first step (RAT-001); the steps only split it,
  their number is fixed then, and a step never plans more than is left,
  fits, or reaches the target. A step ends the refill early when the
  level passes the target by 1 L (RAT-038). The steps end on Stop,
  maintenance mode, the function switched off, a block other than the
  pause, a latch, a mix or hand dose (also one that ran only during a
  fill or its pause), or a change of the target, the rate or the open
  time.
- **Before every fill** the level reading must have been undisturbed
  for 3 min: no irrigation pump running, no Stop, no maintenance, no
  level calibration, no valve opened by hand, no start of the hub. While the
  level is being calibrated the refill waits, its line says so, and no
  fill starts from readings taken meanwhile (#256). The reading lags 1.5–2 min (RAT-038), so
  a fill opened right after irrigation or draining would start from a
  level read too high and could look like a missing rise. A reading that
  irrigation keeps from settling for 30 min (irrigation every few
  minutes) does not keep the tank short: the fill goes ahead, marked as
  irrigated. Only irrigation may end the wait early: it reads the level
  too high, so a fill comes out smaller. A Stop, maintenance, a
  calibration, a valve opened by hand and a start always get the full
  3 min, since a hand fill read too low would make the fill larger. The
  refill does not take over a valve opened by hand; its line says that
  valve closes and locks after `max_open_min`.
- **The rise is checked:** at the end of each fill's pause, on a level
  read after the valve closed, the level must have risen by at least
  half of what flowed in by the set rate while the valve was open.
  Otherwise the inlet is latched with a reason of its own and an alarm
  that names what to check, the set rate included; it stays so until a
  person releases it (RAT-031: a leak, an empty supply, a stuck valve and
  a level sensor that does not follow look the same).
  - A rise that is enough counts, also when the irrigation pump ran: it
    only lowers the level.
  - Not judged, with an event saying why, and the steps end: a rise too
    small while the irrigation pump ran or its state was not known (it
    draws from the tank; no estimate, R5); no valid level at the check;
    no level read since the valve closed; a level curve changed since
    the fill; a fill cut by a restart.
  - A fill smaller than 4 L, or than twice the volume at the level
    curve's first point when it starts at or below that point, is too
    small for the level's uncertainty (RAT-030, RAT-078) to judge alone.
  - **Fills whose rise is not confirmed** (too small, or not judged) are
    added up from where the first began, until a rise is confirmed or the
    level is back above the start level. Together they never add more
    than the target allows from there, nor stay open longer than one fill
    may; when nothing more fits, they are judged now. So a plug offline,
    a calibration left open or a level stuck low gets no second full fill
    unchecked. Judged now, they latch as "did not rise" only when the
    rise could have shown; fills the irrigation pump drew from, a restart
    cut, too small to judge or a damaged record latch as not checkable,
    with that reason. Measured on another level curve than the one now,
    they cannot be compared and latch as not checkable.
  - **A person at the tank** (Stop, maintenance mode, the function
    switched off, a level calibration) ends a fill and keeps a pending
    check from being judged: a rise checked now would say nothing. What
    flowed is not dropped, though: it is kept as not confirmed, with the
    first valid level read during the person's work. If the reading has
    moved from that by 1 L or more (the level's uncertainty, RAT-030,
    RAT-078) by the next fill, the sensor follows the tank (a water
    change, a calibration), and that fill is judged on its own. If it has
    not, as a stuck sensor would not, the next fill gets only what is left
    and the fills are judged together. A move across a new level curve
    counts only while the curve is the one a level calibration of the
    level sensor committed during the person's work: its points need a
    sensor that follows. A calibration cancelled or a curve imported,
    also after such a commit, shows nothing of the sensor, so the fills
    latch as measured on another curve. A curve committed from a raw value
    that only jitters is not refused yet (#264). So a Stop, maintenance or
    switching the refill off and on gives a level stuck low no second
    full fill. Such a fill is not counted as not judged. Switching the
    refill off during a fill starts the pause, as a Stop does.
  - **Two fills in a row that could not be judged latch the inlet too,**
    with their reason, as a backstop. The count starts again only when a
    rise passes or a person releases the latch, not when the level is back
    above the start level: a leak while the irrigation pump runs in every
    pause would otherwise never latch.
  - A check due while the level is not valid (right after a start) waits
    up to 3 min for one. A due time is never more than one pause ahead,
    so a clock that was not set before a start cannot hold EC and pH.
  - A fill closed outside the refill (the device's own auto-off, a person
    in the app, an internal error) is checked too, after the pause; one
    the gateway's emergency limit or a person's Stop or maintenance
    closed is not.
- **Across a restart:** the open fill, the pending check, the fills not
  confirmed and the count of fills not judged are saved in the same tick
  (RAT-063), and a damaged record is cleaned when read, so it never stops
  the hub. After a restart a pending check is still judged; a fill the
  restart cut counts as not judged, with what may have flowed until it
  was last seen open (saved once a minute), not through the outage after;
  steps it ended are reported and not resumed (RAT-007, R6). A value in
  the record that is missing or not a number of zero or more is read as
  not known (R5), and fills with such a record latch as not checkable.
  A hard reset between the valve opening and the save at the end of that
  tick loses the fill's record; the inlet is off after a start (R6). An
  inlet on a socket that keeps its mains power while the hub loses its
  own power or hangs flows on until its own auto-off or the off after the
  start; that water is not counted, so with a level reading stuck low the
  next fill can pass the target by it.
- **EC and pH wait** from the first opening of a refill until its last
  fill is checked (RAT-055, RAT-057): a round in a tank half filled would
  be diluted by the next step. The longest hold is the number of steps
  times the open time plus 10 min, plus waits for a running dose, for
  the reading to settle (at most 30 min each) and for a valid level after
  a start (3 min).
- **Assumptions, not measured (RAT-038):** 50 %, 4 L, the 1 min margin and
  the 3 min settling; the inlet rate stays constant during a fill (it
  falls with a pressure tank or a gravity barrel, which can latch); the
  level curve covers all the water the inlet fills (for example RDWC
  buckets); water draining back into the tank (ebb and flow) can hide a
  missing rise.
- **Residual risks:**
  - A level reading wrongly low gets fills up to the target or up to one
    fill's open time, whichever comes first, before the check latches.
    With an inlet faster than its set rate, more water flows in that time.
  - Between a start level and a target less than 4 L apart, a fill that
    rose too little but brings the level back above the start level is
    not noticed: a small leak repeats each time, and a level reading that
    hovers at the start level without following can let fills repeat.
  - What a person's work kept is dropped when the reading moves by 1 L
    or more, also when a failing sensor drifts or jumps that far without
    following the tank: the next fill then plans the whole gap, and can
    pass the target by up to one fill before its own check latches. A
    level never valid during the person's work, or valid only after a
    water change in it, is not taken as moved, so the kept fills are
    judged together with the next one, which can latch a sound inlet.
  - A person who drains the tank by hand in a refill's pause without
    Stop, maintenance or a level calibration makes the check judge a
    level that fell: it can latch a sound inlet. One who fills it by hand
    then can let a faulty inlet pass its check once.
  - Releasing the latch clears what was kept and the count: with a stuck
    sensor and no repair, each release gives one more fill of up to the
    gap before the next latch.
  - A circulation pump in interval mode holds water outside the tank in
    NFT and RDWC, and ebb and flow drains back for longer than 3 min:
    both can make a fill look short or overfill past the target. Changed
    by SD-099 for the irrigation pump: after irrigation a fill waits
    until the level stops rising, at most the same 30 min; the
    circulation pump's case stays open.
  - A level sensor that reads low by a fixed amount but follows the rise
    passes; the computed gap is then filled in steps.
  - Fills not judged count across refills until one passes: an
    irrigation pump that draws more than half a fill during the pause, in
    two refills with none passing between them, latches a sound inlet.
  - Importing a new level curve, cancelling a level calibration and then
    importing one, or committing a calibration that moves the reading by
    less than 1 L, while a check or fills not confirmed wait, with the
    level below the start level, latches the inlet: their rise cannot be
    compared.
  - When one fill's open time holds less than 4 L (a short
    `max_open_min` or a slow inlet), its fills are judged at that size: a
    reading off by the level's uncertainty (RAT-030, RAT-078) can latch a
    correct refill as not checkable.
  - An irrigation pump the hub does not know (an outside timer, a Home
    Assistant automation) can make a correct fill look like a missing rise
    and latch: it belongs bound as `zone.irrigation_pump` (#233).
  - A valve opened at the device itself (its button, its own app) gets no
    open-time limit from the hub, only its own auto-off and the capacity
    and level cut-offs (older than SD-082, #228).
  - A person who closes the inlet in the app sees the refill open it again
    after the pause, if the level is still low (#233).
- **One pull request:** the cap alone would have widened a gap. Before
  it, a large plan from a level reading wrongly low (a stuck sensor, one
  out of the water) ran into the open-time latch after its first fill; a
  capped fill would repeat every pause instead. The rise check closes
  that, so both go to `main` together, after the owner's "mergen" (a new
  trigger of the inlet latch).
- **Why:** a planned fill must never look like a fault, or the latch no
  longer means "cause unknown". A tank left short after a deep drop can
  run short of the day's irrigations. RAT-038 already names a re-check
  after the run with limited corrective top-ups as open; the reference
  installation re-checked 120 s after closing and allowed at most two
  corrective runs.
- **Not chosen:**
  - The cap alone: the tank stays short until it falls below the start
    level again, and the gap with an empty supply, a leak or a stuck level
    sensor widens (see above).
  - The check without steps: closes that gap, but the tank stays short.
  - Judging 2 min after closing, as the reference did: the level reading
    lags 1.5–2 min behind (RAT-038), and without irrigation the tank loses
    little in 10 min.

---

## SD-083: English complete for proto.5: English first, the CSV in the page's language, the catalog with keys

Points 2 and 3 and the sign-in page's language in point 1 were decided
by the project owner on 2026-10-16 in the software session; the rest of
point 1 carries out SD-014 (PD-027, PD-035) as SD-055 asks for proto.5
(#143, #18, #73).

1. **English first (SD-014):**
   - a new hub speaks English (system language `en`), also one whose
     stored configuration cannot be read (factory settings). A stored or
     imported configuration without a language, also one without a system
     part, is German, as it was saved under the old default; an update
     never changes a hub's language;
   - a browser without a choice of its own shows the hub's language, from
     the sign-in page on: the hub names it in `/info`, so a German hub
     stays German after the update and a new hub starts in English. The
     browser's own language is no longer used. Not chosen: the
     browser's language (a German browser would start a new hub in
     German, against "English preset"), and English on the sign-in page
     with a switch (German users would switch once in every new browser);
   - the first-password page shows the language choice first, with
     English chosen. The choice stays in the browser and carries into
     setup, whose first field is the language (the Home Assistant app's
     introduction too); setup saves it as the hub's language, also when
     the introduction is skipped;
   - the simulator's demo scenarios stay German, as their names and
     recipes are German (changed by SD-095: the demo speaks English);
   - a recipe template in an unknown system language falls back to
     English.
2. **The CSV export follows the page's language** (extends RAT-016):
   - German: semicolons, a decimal comma, the header `zeit_utc`, the
     file `growcontroller-verlauf.csv`;
   - English: commas, a decimal point, the header `time_utc`, the file
     `growcontroller-history.csv`;
   - without a language from the page, the hub's decides. A gap stays an
     empty field in both, never 0.
   - Not chosen: always semicolons and a decimal comma (an English
     spreadsheet does not read the numbers), always commas and a decimal
     point (a German one does not).
3. **The catalog in both languages with keys** (a later pull request):
   - `catalog/catalog.json` becomes English; the German names come from
     a table in the web app by catalog id, as the messages do (SD-032);
   - the hub sends a catalog name as a nested message with a key, so an
     event shows in the language of the page that reads it, also later;
   - a test checks that every catalog id has both languages.
   - Not chosen: `labelDe`/`labelEn` beside each entry, as the recipe
     templates have (a second way next to the keys, and events stored in
     the language of their day).
   - **A device without a name of its own** (decided by the project owner
     on 2026-10-16) is named by its class in the page's language.
     Accepting a device without a name stores none. A stored name that is
     only its class's name, now or as the German catalog had it, counts as
     none: until this change, a device accepted without a name stored the
     German class name. The file keeps such a name as it is. Not chosen:
     keeping such names as names of their own (a hub in use would show
     German device names on English pages until each is renamed), and
     clearing them when the file is read (it changes the user's data, and
     a return to an older version would show empty names).
   - Only what a page shows has a German text: the catalog's shop texts
     and a function's group stay English.


## SD-084: Product requirement (from PD-222, PD-223, PD-227, PD-232 to PD-234, PD-236 to PD-239, PD-260 to PD-266, PD-268, PD-270, PD-272, PD-273): dosing modules with a fixed rate, caps from the usable volume, runs into a cup, bucket mixing

Recorded on 2026-10-16 from the product decisions of 2026-10-15 and
2026-10-16 on #132 (SD-029). They answer SD-066's questions Q1 to Q16
and decide more. Where the two differ, the later one applies. PD-223
replaces PD-187 at its core: SD-066's pump with speed stages and
feedback is dropped. Where this entry says nothing, SD-066 stands, its
proposals decided as noted below. The issue's comments hold the full
behaviour; this entry says what applies to the software. Built after
0.1.0-proto.5 as part of M1, simulator first, as SD-066 says.

- **Dosing modules have a fixed rate** (PD-223, PD-227; the models
  PD-262, PD-263, PD-265, PD-266).
  - **No speed control:** a shop module has no speed signal and no
    feedback. Dropped from SD-066:
    - speed stages and calibration per stage;
    - pulse checks and the latches they set;
    - the plausibility check from revolutions;
    - the speed signal at standstill.
    Against a run that does not end, the board's cut-off (below) and the
    current check of #94 remain. Q11 and Q13 no longer apply.
  - **Two models:**
    - "Fine": about 12 ml/min, with its limits at a fixed maximum rate of
      about 16.6 ml/min. It is the only small model, for nutrients,
      additives and pH in tanks up to about 150 L, so pH runs on the
      low-flow model (Q2); there is no separate pH module. Its choice
      depends on sample tests and supplier documents; if it fails, the
      product side decides again.
      The 2026-10-15 decision had about 33 ml/min, with about 78 ml in
      a hung run.
    - "Large": about 220 ml/min, recommended from about 100 L usable
      volume.
  - **By tank size:**
    - Fine stays allowed for nutrients up to about 150 L; from about
      100 L the app shows the expected mixing time (about 2 h for
      150 L).
    - Above about 150 L only Large doses nutrients. Whether setup blocks
      Fine there or only warns is open.
  - **The user sets each module's role** (pH−, pH+, A, B, CalMag,
    additive) and may change it; all modules of a model are alike.
    - The app asks for the role when a container is set up and on every
      change. This replaces step 7's question for the liquid.
    - SD-066's residual risk of a module on the wrong container stays:
      the hub cannot tell which container a module sits on. It goes on
      the product side's list of residual risks.
    - Limits follow the role, the usable volume and the model's maximum
      rate.
    - A role the user sets never widens the catalogue role used for the
      emergency dose without a probe; that role comes only from the
      catalogue (see the terms below).
  - **Large in a pH role** only after a warning in the app that
    recommends Fine and that the user confirms; automatic pH− with Large
    from about 84 L. Whether the warning repeats on every role change is
    open.
- **Limits use the model's fixed maximum rate**, fixed in code (Q7,
  R7).
  - **Inside the window, a calibration value is never the limit.** It
    counts only there; a value from the module may only tighten the
    limit.
  - **Outside the window** (PD-261, PD-264):
    - a value above it treats the module like a third-party pump, whose
      limit uses its calibration value;
    - a value below it is rejected, and the app asks to measure again.
  - **A shop module whose ID cannot be read** never gets the looser
    third-party limit: it doses nothing into the tank until its ID is
    read (R5).
  - **Third-party pumps** (no module ID) have no fixed maximum, so their
    limit depends on run time and the calibration value, which is stored
    on the board. Their reserve is still open (a residual risk).
  - **Limits use the tube fitted at the factory;** replacement tubes come
    only in that size (PD-261, PD-264).
- **Runs and jobs** (PD-222; the per-job limit with the models).
  - **A cut-off on the board:** the board cuts every pump run after about
    90 s (worst case about 103 s, about 28 ml with Fine), independent of
    the firmware. Which board brings it is the product side's. Until
    then, RAT-007, RAT-018, `INVARIANTS.md`, `SIMULATOR.md` and SD-045
    say board v0.1 has no such cut-off (PD-160); each now names PD-222.
  - **A job is runs of at most 60 s** (Q4, as SD-066 proposed). The limit
    per job is raised from about 20 min, so that one job with Fine can
    still deliver about 0.9 L (up to about 55 min). The exact value is
    the firmware's; more per job comes back to the product side. How the
    raise applies to Large is not said.
    - The limit is per dose, one pump's job. It is not a timeout on the
      job lock, which RAT-018 forbids.
    - 0.9 L takes about 55 min only at the maximum rate. At the nominal
      12 ml/min it takes about 75 min. A valid job must never run into
      the limit (RAT-018). Whether a larger part is planned as several
      jobs, or the limit rises, is asked in #284.
- **Minimum run time and short doses** (Q3, Q6).
  - **Calibration uses long runs.**
  - **A minimum run of 1.0 s:** with Fine, automatic pH− works from about
    5 L usable volume, if 1 s doses prove accurate enough on samples. If
    they don't and 2.5 s is needed, pH− starts at about 12 L. At such a
    small volume only larger gaps can be dosed; smaller ones rest as too
    small (RAT-050).
  - **Until then, no dose under 1 s.** Whether start and run-on volume
    need a correction for 1 s doses is measured on samples.
- **Every dose has a cap from the usable volume** (PD-232; Q1, Q8).
  - **Per purpose:** pH, EC round, hand dose, mixing. A mix part's plan
    is at most the product's maximum ml/L times the usable volume; its
    cap is the plan plus one EC step (RAT-056).
  - **On top of the caps per litre:** the caps per litre of the solution
    in the tank now stay (RAT-050, RAT-056); the cap from the usable
    volume comes on top. A cap from the usable volume alone would let a
    200 L tank holding 30 L take nearly seven times its amount per
    litre.
  - **The fixed hard caps in code stay**, also for the largest allowed
    tank; caps derived from the volume sit below them (R7).
    - Today the hand dose (50 ml), the pH dose (20 ml, the catalogue's
      upper bound for its setting), the run (60 s) and the number of runs
      have such a cap.
    - Mix parts and EC rounds get a fixed cap in ml, sized for the
      largest allowed tank, so configuration alone never sets a limit.
      Whether that tank becomes a fixed value in code, and its value, is
      asked in #284.
  - **A missing usable volume** refuses mix, EC, pH and hand doses with
    a reason. A missing cap value never means no cap (RAT-006, RAT-056).
  - **The tank volume becomes a safety value:** a change needs a
    confirmation, is notified, and is checked for plausibility against
    the level.
  - **Open:** the values per purpose and product; the reserve for
    third-party pumps; plausibility without a level sensor.
- **Bookings** (PD-239; Q5 and Q16).
  - **Booked before the start:** only the safety budget (PD-147). Stock
    and mixing progress are booked from the actual run time (Q5, as
    SD-066 proposed).
  - **Saved in the pauses:** in each pause between two runs of a job,
    with no pump running, the hub saves the budget booked for the next
    run and the stock and progress of the run just ended (Q16). This
    changes SD-045's "the write waits for the job's end" for that pause.
  - **Open:** a run whose run time is unknown after a power loss. The
    product side proposes counting it as fully run for mixing progress.
- **Priming, calibration and test runs go into a cup, never into the
  tank** (PD-233, Q9).
  - The app asks first whether the outlet is in the cup.
  - Nothing pumped this way counts as a dose into the tank.
    - So these runs no longer announce a change of `tank.ec` or
      `tank.ph`, and they no longer explain a jump (RAT-042). Today every
      user job announces both, priming and calibration included.
    - Control still waits while they run.
    - Until "poured back" is decided, they book no stock, as priming and
      calibration don't today (RAT-070).
  - **Open:** the canister-change flow; what happens if priming is
    skipped; whether a "poured back" action corrects the stock (RAT-040,
    RAT-070).
- **Calibration** (PD-234, PD-261, PD-264).
  - **At the factory:** every shop module is calibrated before shipping.
    Its value is stored in the module with date and origin "factory",
    through a factory page in the firmware:
    - two timed runs, with the grams entered;
    - the mean is computed;
    - values outside the window, and two runs that disagree, are
      rejected;
    - the value is written to the module and read back.
  - **What the app shows:** each module's value in ml/min, who measured
    it and when.
  - **No valid value, no dose into the tank:** such a pump runs only for
    priming and calibration, into a cup, and the app guides the user
    through calibrating it.
  - **The user's calibration,** any time:
    - prime into a cup;
    - two timed runs into a measuring cylinder, then enter the volume;
    - a third run if the two disagree;
    - for shop modules, the window check.
  - **When to recalibrate:**
    - recommended on the first canister of a thick concentrate;
    - required after "tube changed" (a new pump head counts as one)
      before the next dose into the tank.
  - **Through Home Assistant,** calibrating needs a short BOOT press
    (PD-229, which the product side presents again).
  - **Open:**
    - whether a user value replaces the factory value or sits next to it;
    - a reminder by time or by the tube counter;
    - measuring in ml or by weight with density;
    - a rejected new value against the existing one;
    - returning to the factory value after a tube change;
    - the run length with the new Fine (a calibration longer than 60 s
      is several runs; today it takes 10 to 60 s, about 12 ml with Fine
      at its nominal rate);
    - how the factory page is protected in shipped firmware, a security
      point.
- **Tube counter** (Q12): it counts run time. It lives on the board,
  keyed by the module's ID, so it follows the module between ports.
  Its threshold comes from the pump maker and sample measurements.
- **Supply voltage** (PD-260): no dosing pump runs while the supply is
  above about 13.2 V ("check power supply"). The check sits in the
  gateway (R1).
  - **Until decided otherwise,** a missing reading blocks like a high one
    (RAT-006, RAT-015). This check protects the maximum rate (#284).
  - **Open:** the threshold, hysteresis, a lower limit, and what happens
    in the middle of a run.
- **Changing a role to or from pH− or pH+** (PD-268, PD-270, PD-273).
  - **Guided flushing into a cup:** empty the tube, run water through,
    fill with the new liquid, using the empty and fill flows; it counts
    on the cup window.
    - At the pH− container with a usable probe and running circulation,
      it runs in stages with check pauses.
    - Through Home Assistant it needs BOOT.
    - Until it is done, the module doses nothing into the tank in any
      role; afterwards its tube counts as filled.
  - **Between nutrient roles,** the app only shows a hint to flush.
  - **Locks stay through every role change,** also back and forth: the
    pH jump lock, "outlet checked", the release after the hardware
    cut-off, no pH− until "fill", "tube not filled".
  - **The learned pH effect** is learned from clean rounds (RAT-057) and
    never reset today. A new acid on the pH− module would start from the
    old one's effect. Whether a role change resets it is asked in
    #284.
  - **Without a usable probe or running circulation,** flushing for a
    change to or from pH− needs a short BOOT press and "outlet in cup"
    on every path, also on the local network. Otherwise it stays
    blocked, and the app says what is missing and offers the BOOT path.
    If circulation is set up but off, the app offers to switch it on
    first. For pH+ this is open (below).
  - **What it counts against:** with Fine, such flushing counts only on
    the cup window, not on the daily pH− limit; the cup window, Stop,
    the work lock and the lock after the hardware cut-off still apply.
    With Large it counts on the daily pH− limit until decided otherwise.
- **A recipe without a pH probe doses nutrients only**, never pH
  (PD-272).
  - This holds with a probe too: a recipe never holds pH (RAT-004).
  - Without a probe, no pH step follows the mix (RAT-044). With a probe,
    pH control follows the mix as a step of its own.
- **Mixing and irrigation** (PD-236, Q15).
  - An irrigation time inside a running mix waits until the mix,
    including its pH step, is finished.
    - The wait ends however the pH step ends: reached, given up, blocked
      or skipped (RAT-084).
    - The block sits in the gateway (R1), as RAT-068's does.
  - Nothing irrigates in the pauses between a mix's runs: the
    irrigation pump's smart plug stays off, and a three-way valve does
    not switch. SD-066's proposal to move the valve in a pause is not
    chosen.
  - Mixes are scheduled to avoid irrigation times where possible; a
    delay is reported.
  - **Open:**
    - irrigation from a tank whose mix was aborted (decided by SD-093:
      as from any tank, once mixed in and inside the band);
    - how long irrigation waits for a mix that waits for the user, and
      how the wait meets the "too dry" emergency dose (PD-049) (#284);
    - whether a bucket mix holds irrigation at all (#284).
- **Low stock** (PD-237, RAT-071; Q10 in PD-239).
  - **A warning** when a canister's stock does not cover the next mix
    with the current recipe and usable volume; 150 ml and 20 ml stay as a
    floor, with no setting per canister.
  - **A mix does not start** if the known stock does not cover it, as
    today, also into a bucket. EC and pH rounds and hand doses only warn.
  - **An unknown stock is not judged** (RAT-015, #184), as today.
    Otherwise an import, which makes every stock unknown, would block
    every mix.
  - **pH− has no recipe demand** (RAT-004), so only its 20 ml floor
    applies.
  - **Open:** an EC round that only warns can put A in without B when one
    part of a pair is short. EC cannot show that (RAT-054; #284).
- **Mixing into a bucket or watering can** (PD-238, Q14) is part of
  stage 0. This decides SD-057's open point on mixing outside the tank.
  - **What it doses:** nutrients by recipe, Fine modules only, no pH
    dosing (a hand measurement and a hint).
  - **Before and during:** the app asks "Bucket with water underneath?"
    first and pauses between parts ("stirred?"), since nothing
    circulates.
  - **Caps** use the bucket's volume as they use the tank's. A tank mix
    never runs into the bucket, nor the other way round. The data model
    gets a second vessel next to the tank.
  - **The bucket keeps its own state:** its volume and its last mix.
    - A bucket mix changes nothing of the tank's, and the tank's control
      never uses the bucket's litres.
    - The repeat-mix check (RAT-003) stays as it is: any mix within
      30 min of another needs a confirmation, whatever the vessel. A
      check per vessel is the software's proposal to the product side
      (#284).
    - It waits for "stirred?" instead of the tank's circulation.
    - Today a mix writes the tank's volume and last-mix time and waits
      for the tank's circulation.
  - **Until the outlets are back** (a safety point, until the product
    side decides how): tank doses through the modules used for a bucket
    mix stay blocked until the user confirms that the outlets are back in
    the tank. An EC round sized for 150 L run into a 10 L bucket is 15
    times its amount per litre.
  - **For the rules:** RAT-040 and RAT-070 book consumption as what goes
    into the tank. A bucket's doses are consumption too; how the rules
    read for them goes to the product side with this work.
  - **Open:**
    - getting the outlets back into the tank afterwards;
    - the smallest bucket for which Fine is accurate enough;
    - how the bucket volume is protected;
    - whether users with only Large modules see the mode;
    - jobs through Home Assistant (also remote and in maintenance mode)
      while the outlets are in the bucket, and confirmations given
      without seeing the system, a security point.
- **Values from the old Fine rate are being recalculated** on the
  product side: its hung-run volume, the cup window, the dose without a
  probe and similar. No limit is loosened because of the lower rate
  until the product side confirms the new values.
  - A hung run does not set the 5 L and 84 L thresholds; the minimum run
    does. A hung Fine run (about 28 ml) into 5 L is about 5.6 ml/L, 28
    times SD-085's cap. It belongs on the product side's list of
    residual risks.
- **Terms from earlier product decisions that no SD records yet:** the
  cup window, the daily pH− limit, the work lock, "outlet checked",
  "tube not filled", no pH− until "fill", the release after the
  hardware cut-off, the catalogue role of a dose without a probe (not
  the irrigation emergency dose of SD-025 and SD-028), and "6 free runs
  per hour".
  The product side is asked for them (#282).
- **What changes in SD-066's steps:**
  - **Step 1:** a table per model (the maximum rate and the calibration
    window), and the role per module, instead of the stage table and
    picker.
  - **Step 2:** the simulator gets the two models and the board's
    cut-off.
    - The cut-off is a cut after about 90 s, and that run delivers.
      Today's twin refuses a run requested over 90 s.
    - The models' rates and the cut-off are assumptions in
      `SIMULATOR.md` until measured.
    - The speed signal on the bus, and its R1 check, are dropped.
  - **Step 3:** planning without stages; caps from the usable volume; a
    second vessel for the bucket; the irrigation wait during a mix; a
    control round that needs more than 6 runs for a part with Fine
    (RAT-055, #284).
  - **Step 4:** the safety limits use the model's maximum rate instead
    of "the 100 % rate"; the per-job ceiling is raised; fixed caps in
    code for mix parts, EC rounds and the largest tank; the tank volume
    becomes a safety value.
  - **Step 5:** keeps the current check of #94 and the supply voltage;
    the feedback checks and their latches are dropped.
  - **Step 6:** the ID chip with factory calibration, the window and the
    third-party rules above.
  - **Step 7:** the cup flows for priming, calibration and role changes,
    which announce no tank change; the locks that stay through a role
    change; low stock blocking a mix; and the tube counter on the
    board.
  - **Step 8:** the app gets the role per module, asked for when a
    container is set up, and the expected mixing time; no stage table.
  - **Every step that adds or changes a limit, lock, refusal or cut-off**
    needs `security` and the project owner's "mergen" (SD-033).
- **Rules that change when this is built** (each step updates its entries
  and `INVARIANTS.md` rows):
  - RAT-003: only if the product side takes the check per vessel
    (#284).
  - RAT-007 and RAT-018: the board's cut-off and the raised per-job
    ceiling. Their "Decided" lines already name them.
  - RAT-039: the hand-dose cap per volume. At 5 L, today's 5 ml is
    1 ml/L.
  - RAT-041: a pH+ role exists; there is still no automatic pH+.
  - RAT-042: cup runs explain no jump.
  - RAT-050: the volume cap; the pH− thresholds at 5 L and 84 L.
  - RAT-056: the caps from the usable volume on top of the per-litre
    caps; fixed caps in ml for mix parts and EC rounds.
  - RAT-054 and RAT-055: the 6 runs with Fine. Mix runs get a tighter
    limit, and RAT-055's deviation is updated.
  - RAT-071: the forecast for the next mix.
  - RAT-084: the irrigation wait during a mix.
- **Still open on the product side:**
  - pH+ without a probe, including a role change;
  - whether the BOOT exception also applies to a canister change,
    calibration, first setup and flushing for removal at the pH−
    container without a probe (until decided: no);
  - the tube volume per model;
  - whether "6 free runs per hour" counts jobs or single runs;
  - the recalculated limits for Fine and for Large in the pH roles;
  - Large's maximum rate, and both models' calibration windows;
  - the residual risks for the risk assessment, and their exact texts;
  - the terms in #282 and the seven points of the `domain` check in #284.


## SD-085: Product requirement (from PD-235, PD-239): the hub's own pH dose and the jump lock

Recorded on 2026-10-16 from the product answers of 2026-10-15 to the
three product questions P1 to P3 of #162 (SD-029). Not built yet.
#162's software items: S2, the lock's reason text, was done in #278
(a jump lock names only causes that can be true). S1, the tails of
other announcements, stays open.

- **P1, both parts (PD-239).**
  - **A fixed cap per litre for the hub's own pH doses,** derived from
    the jump threshold (RAT-039, RAT-044). It is meant to keep an own
    dose from reaching a jump: about 1.0 pH ÷ 5.0 pH per ml/L = 0.2 ml/L.
    - The litres are those of the solution in the tank now, as for
      RAT-050's cap, not the usable volume (SD-084). In a half-full tank
      a cap from the usable volume could move pH by 2.0.
    - It is counted at the model's maximum rate (SD-084) and fixed in
      code (R7). It replaces RAT-050's 0.3 ml/L.
    - At the strongest measured effect, 5.0 pH per ml/L (RAT-050),
      0.2 ml/L reaches the threshold exactly. The lock trips only above
      it, but several things push the observed change past it: noise in
      the reading, a volume read too high, and a stronger effect in
      softer water.
    - Whether the cap keeps a margin below the threshold is asked as a
      product question (#283).
  - **After a lock in the direction of the hub's own dose, the learned
    pH effect resets to its start value.**
    - **When a lock counts as the dose's:** the value moves in the dose's
      direction (today a fall, RAT-041) while an own pH dose doses or
      settles, up to 1 min after its settle time. This includes a pH
      round cut by a Stop that settles after Resume (SD-073).
    - **When the reset happens:** at the moment the lock is set, saved in
      that tick (RAT-063). A flag stored with the lock survives a restart
      too, so the reason still names the dose after a restart.
    - **What the reason says:** it can then name the dose as the likely
      cause. Timing alone cannot rule out a probe fault in the same
      minutes (RAT-044). #278 names the dose only as one possible cause
      of any fall.
  - **Rules that change when this is built:**
    - RAT-050 and RAT-057 get the cap and the reset.
    - RAT-042 changes its basis: it leaves own pH doses unannounced
      because their cap stays below the jump threshold. Now the cap is
      derived from the threshold and at most reaches it (margin: #283).
    - `INVARIANTS.md` changes the rows on these rules.
  - **Tests in the twin** depend on its pH model, an assumption
    (`SIMULATOR.md`). The twin reaches 5.0 pH per ml/L only below EC
    0.68, while RAT-050 measured 5.0 at EC 2.96.
  - The cap and the reset change limits and the learned effect: they
    need `security` and the project owner's "mergen" (SD-033).
- **P2 (PD-239).** How far an EC round moves pH is measured on the
  product side: at the largest EC step (1.5 mS/cm in the catalogue), at
  EC 0.5 and at working EC. The twin's value is an assumption
  (`SIMULATOR.md`), so a test there says nothing about the real one.
  - **Today** an EC round announces `tank.ec` on every tick while it is
    busy, each time for 10 min, so until about 10 min after its last busy
    tick. It announces `tank.ph` only once its settling starts, after its
    last part, until 1 min after its settle time. With Fine a part runs
    up to about 6 min, so the time without a `tank.ph` announcement
    grows.
  - **Until it is measured, the product side's safe side:** an EC round
    announces `tank.ph` from its start too (RAT-042), with today's end:
    1 min after its settle time. A tail as long as `tank.ec`'s would
    widen the time in which a probe fault goes unlocked.
  - **A round cut by a Stop** that settles after Resume (SD-073) does the
    same while EC rounds announce `tank.ph` (from the review of #191).
    - It announces `tank.ph` from the tick its settling starts until
      1 min after its settle time, and `tank.ec` from that tick as any
      busy round does. Today `tank.ec` comes one tick late and `tank.ph`
      never.
    - It announces nothing while it waits for the pump: that wait can be
      long, and announcing then would hide faults.
  - **If the measured change is clearly below 1.0,** an EC round no
    longer announces `tank.ph` at all, also not from its settle time
    on, as for the hub's own pH doses (RAT-042).
  - Announcing more explains more pH changes during a round, so it
    changes a protective function: it needs `security` and the project
    owner's "mergen" (SD-033).
- **P3 (PD-235): the lock still lifts by itself.**
  - A pH jump lock still lifts after 15 min steady, without the user's
    acknowledgement; RAT-039 and RAT-044 stay as they are.
  - **The residual risk** goes into the risk assessment and the manual:
    a probe fault with a lasting offset lets control dose towards the
    offset once the lock has lifted.
    - The per-dose caps still apply, but they limit each dose, not the
      total: rounds repeat until the reading reaches its target.
    - With a probe that reads high, the real pH ends lower by the offset.
      The no-effect latch does not catch it, since the probe still
      follows the doses (RAT-041).
    - With a probe that reads low, control only rests.
  - Whether S1's tails also belong in the risk assessment is still open
    on the product side.

---

## SD-086: A missing state.json on a hub that ran before is unknown, not new

Decided by the project owner on 2026-10-16 in the software session
("umsetzen" for #238, a follow-up of SD-081 from the `domain` review).

- **When:** `state.json` is not there, but the hub's events are: their
  snapshot or journal is there, or kept unread (SD-081). Every start of the
  hub writes both (the state at the end of its start, the events in the
  same step), so a hub that ran before always has a `state.json`. One that
  is gone was deleted, or the flash reports a damaged entry as not found
  (ESP-IDF's SPIFFS gives ENOENT for a deleted entry or one that is not a
  file, SD-081).
- **What the hub does:** as with a broken run-time state (SD-081, "an
  unknown run-time state errs on the safe side"): it starts stopped
  (PD-076), a second mix within 30 minutes of the start needs
  confirmation (RAT-003), a dry-run latch in the first hour holds for a
  person (SD-067), and it says "Saved state missing" instead of
  "Stop still active". The new `state.json` written at the end of the
  start holds the Stop, so the next start stays stopped until Resume.
- **A new hub:** no events and no `state.json` (first start, after a
  factory reset, a new data folder) starts as before, not stopped.
- **Why the events:** they are the hub's own record of having run.
  `config.json` alone is not taken as that sign: settings can be put in
  place before a first start, and many tests and tools do so.
- **Not covered:**
  - when the events are gone as well, a hub that ran before cannot be
    told from a new one. On the device this is what a storage that does
    not mount does today, since the firmware then formats it; SD-076 and
    SD-080 plan that it no longer does so by itself;
  - SD-080's factory reset has to finish its erase before the hub
    starts, remove the events (not only make them unreadable), and
    nothing it leaves behind (its mark) may count as the hub's events;
    otherwise a hub just reset could say "Saved state missing", or read
    an old state;
  - a first start whose `state.json` could not be written while its
    events were (a failing card) stops once at the next start and says
    the file is gone: the safe side. Every release made here writes
    `state.json` at every start, so an update does not stop by mistake.

---

## SD-087: An unknown run-time state latches the inlet until a person releases it

Decided by the project owner on 2026-10-20 in the software session (#273,
option A of four, after the `domain`, `security` and `ux` reviews; a
follow-up of SD-081 and SD-086).

- **When:** the run-time state is unknown at the start: broken, there but
  unreadable (SD-081), or missing on a hub that ran before (SD-086). And an
  inlet valve is assigned, or the settings are unknown too (broken, kept
  unread, or gone on a hub that ran before, as the flash may report a
  damaged entry as not found), so whether one is assigned is unknown. Not
  on a platform that switches nothing (the Home Assistant app).
- **What the hub does:** besides what SD-081 and SD-086 do (stopped, the
  window against mixing twice, the dry-run guard), it sets the inlet latch
  (`inlet.fault`) with the reason "Saved state unknown", dated the start
  and marked by its own field (`cause`), not by the text's key.
  Whether the inlet was latched, a fill was open or a rise was being judged
  (RAT-031, RAT-032, SD-082) is unknown. RAT-063's evidence treats a latch
  of unknown kind as the stricter kind, released only by acknowledgement,
  and RAT-031 wants a person to look before the inlet opens after a fault.
  Resume brings back the fans and control; water flows again only after a
  person taps Release, also for an inlet assigned again in that run. The
  latch is saved with the state (see the keeping below), holds over
  restarts and never releases itself.
- **What the person sees:** the start's alarm names the inlet and that it
  must be released; the latch, the refill line and a refused switch of the
  inlet by hand give the reason ("saved state unknown") instead of "after
  a fault"; after Resume the app says the inlet stays shut until released
  under Tank & control. Texts reviewed by `ux`.
- **SD-081's keeping:** a `state.json` kept unread is written anew at a
  Stop, Resume, or a new lock or latch. The latch the start set itself is
  not new: it does not end the keeping, and releasing it while the file is
  kept writes nothing, so a start that still cannot read the file latches
  again. A real cut-off of the inlet (no valid level, at capacity, open too
  long) replaces the reason "Saved state unknown" and its time once, an
  exception RAT-062 names: that latch cut nothing, so there is no reason
  to freeze. The cut is then new and kept.
- **Not covered:**
  - the EC and pH no-effect latches are not set. Their loss allows at most
    two more capped rounds before they latch again (their counter starts
    again at every restart anyway), their texts would be wrong here, and
    three releases after every such start invite tapping through without
    looking. Setting them would be a product question;
  - the other holds of a lost state: a dry-run latch's "switch-off
    failed" and "jump" holds, and the hold of EC and pH by a pending rise
    check (RAT-055), so EC and pH may dose right after Resume into water a
    cut fill diluted; the 0.8 × gap rule and the caps bound it;
  - the first fill after a Release does not wait for the level to settle
    again; this holds for every inlet latch and is a follow-up (done in
    SD-088);
  - releasing is one tap, as for every inlet latch; SD-059 plans that a
    latch is released only from the home network (M6, not built yet).

---

## SD-088: The first fill after an inlet latch is released waits for the level to settle

Decided by the project owner on 2026-10-20 in the software session
("umsetzen" for #288, from the `domain` review of SD-087).

- **What the refill does:** while the inlet latch (`inlet.fault`) stands,
  for any reason, and up to the tick that sees it released, the level
  reading counts as disturbed, as during a Stop, maintenance or a level
  calibration (RAT-038). After a person releases it, the first fill waits
  until the reading has been undisturbed for 3 min (the refill line says
  so) and plans from a level read then, not from one read before the
  release. A latch the refill sets and a person releases before the next
  tick counts too.
- **Why:** the refill line and the alarm ask the person to check the tank
  and the inlet before releasing; they may bail, drain or fill it by hand
  right then, and the reading lags 1.5–2 min. A fill planned from the
  level before that could end in a false "did not rise" latch after
  draining; after a top-up by hand it could overfill, as the capacity
  cut-off reads the same lagging level (RAT-032), or a faulty inlet could
  pass its rise check on the water added by hand (SD-082). A Stop is
  already treated so: a person may be at the tank (RAT-031, RAT-038). 3
  min, the existing assumption, leaves a margin over the lag; a longer
  wait would open the valve after the person may have left.
- **Not covered:**
  - work at the tank that ends more than about a minute after the
    release, as the reading lags; then the rise check judges the fill as
    any fill (RAT-031). Longer work belongs under Stop or maintenance,
    which keep the wait going while they last;
  - EC or pH may start a round during the wait, and the fill then waits
    for it to settle and dilutes it (RAT-055); this happens after a Stop
    or a start as well (#289; done in SD-090);
  - switching the refill off and on again, which RAT-038 counts as a
    person at the tank, is not a disturbance of the reading (#289; done
    in SD-090).

---

## SD-089: Mixing to the target: three details from building SD-057

Decided by the project owner on 2026-10-21 in the software session, from
the choices `domain` listed in #143 for building SD-057 (S4, S9, S20).
They fill in what SD-057 leaves open and change one point of it: a
timeout while stirring by hand stops the mix instead of ending it (S9).
The rest of SD-057 stands.

- **Stirring by hand waits at most 30 min (S9).** After each part but
  the last, a guided mix waits at most 30 minutes of operating time for
  "Continue". Then it stops and stays open: "Resume mix" goes on with the
  next part, "End mix" ends it, and so do a prime, a calibration, a hand
  dose or a restart (R6). The message names every part of a pair still
  missing, with its amount (RAT-054). SD-057 had the timeout end the mix;
  a stopped mix lets a missing part of a pair still be caught up through
  the hub, which a hand dose (5 ml by default, RAT-039) cannot do.
  Cancelling ends the mix with the pair's message, as SD-057 says. Built
  in #268.
- **The first step to the target is capped like every EC step (S4).**
  Way 1 turns about 80 % of the gap into millilitres with the largest of
  the start effect fixed in code, the recipe's reference and the learned
  effect, and the step limit (`max_ec_step`, RAT-056) caps it, converted
  with the same largest effect, as SD-057's first safeguard says. An
  overshoot can only be undone by draining and diluting. With the cap,
  the overshoot of a product stronger than the largest known effect
  stays bounded, whatever the gap: at most the step limit times how far
  the product exceeds 1.25 times that effect (0.75 mS/cm for a product
  twice as strong, with the default limit); without it, the overshoot
  grows with the gap (1.2 mS/cm for a gap of 2.0). A large gap (above
  1.25 times the step limit, a fresh fill from RO water, for example)
  costs one more correction round; a product weaker than the start
  effect gets a smaller first step and more rounds. This replaces the
  owner's first answer (no step cap), which was given without the
  overshoot risk; `domain` assessed it (common concentrates are up to
  about 1.8 times the start effect, and the reference is optional). Not
  built yet; comes with way 1 (a later step of SD-057).
- **Mixing to the target needs EC control switched on (S20).** With EC
  control off, the hub offers way 2 (a fixed pattern) and proposes the
  probe's EC as the water's EC, only from a valid reading (SD-054),
  never a missing one (R5). There stays one correction path, with its
  latches and its learning. Not covered yet: EC control switched on but
  latched; covered by SD-098: a fixed pattern is offered then. Way 2 is
  built with SD-098; way 1 comes with a later step of SD-057.
- **Not chosen:** ending the mix at the stir limit, or a limit of 60 min
  (S9); the first step without the step cap, faster on a fresh fill
  but overshooting with a product stronger than 1.25 times the largest
  known effect, or uncapped only once the recipe has learned its own
  effect (S4); way 1 dosing its 80 % and stopping, or a correction of
  the mix's own next to EC control (S20).

---

## SD-090: EC and pH wait while a due fill settles; the refill off is a person at the tank; a released latch resets the count

Decided by the project owner on 2026-10-21 in the software session
("umsetzen" for #289, from the `domain` and `reviewer` reviews of
SD-088). Three gaps around the refill, each there before SD-088.

- **EC and pH wait while a due fill waits for the level to settle:** after
  a Stop, a start, a released inlet latch or irrigation, a fill that is
  due waits until the level reading has settled (RAT-038). EC and pH now
  wait then too (`RefillController::holding`), as they already do from the
  first opening of a refill to its last check (RAT-055). Before, a round
  could start in that wait, for example after a top-up by hand; the fill
  then waited for it to settle and diluted it.
- **No new round while a due fill waits for a dose:** a round already
  running is not cut: the refill waits for it to settle first, as before,
  and the round is learned from (RAT-057). But no next round of the same
  correction starts meanwhile (`ControlEnv::refillDue`); before, the next
  one started in the very tick the last settled, so the fill waited for
  the whole correction and then diluted it. The line of EC and pH reads
  "Waiting: refill first" in both cases (before "Waiting: refilling",
  also shown while the valve is closed; the refill's own line says what
  it waits for).
- **The refill switched off is a person at the tank:** RAT-038 already
  treats it so for what flowed (kept unconfirmed); now the level reading
  also counts as disturbed while it is off, so a fill after it is switched
  on again waits until the reading has settled, as after a Stop.
- **A released inlet latch resets the count of fills not judged:** a
  latch drops the pending check and the fills not confirmed, and starts
  the count of fills in a row that could not be judged again (SD-082,
  "once a person has looked"). Before, this happened only while
  automation ran; a latch released during a Stop, maintenance or with the
  function off left the old records, so the next fill not judged could
  latch the inlet at once. Now it happens in every tick the latch stands.
  It drops what flowed unconfirmed as well, as it does under automation:
  with a stuck level sensor the first fill after the release can plan the
  whole gap once more, bounded by the inlet's open time, before its rise
  check latches again (SD-082).
- **Not covered:**
  - how long EC and pH wait follows the refill's own waits: 3 min after a
    disturbance; during irrigation up to half an hour (RAT-038), which
    starts over when a tick does not reach the wait (for example a level
    read at the start level);
  - in ebb and flow, water still draining back after the irrigation pump
    stops makes the level read low; a fill planned then may overfill.
    That is a product question (#294).


## SD-091: Product requirement (from PD-317, PD-318): the minimal principle, and control details are decided here

Recorded on 2026-10-21 from #298 (SD-029). Changes SD-029 and SD-072 for
control details. Which band counts as "water not fit for watering", and
how it meets the rules for a cut mix, is decided here in a later SD for
#298. Open on the product side: the risk assessment and the manual are
aligned with the guideline before the first offer.

- **Guideline (PD-317):** the board is the ESP32-S3's extended arm: it
  reads inputs, passes them on and switches outputs; all control is in
  the software. Users connect the sensors and devices they want, also
  through Shelly sockets or in other ways, and the software uses their
  values for control. The product offers a reasonable safety frame, not
  complete protection: it is meant to be affordable and easy to handle,
  and it trusts its users to engage with it: they set it up, flash,
  connect and automate it themselves and write their own recipes. Not
  every imaginable case (a device wired wrongly, for example) is ruled
  out, blocked or tested in advance.
- **Baseline protection instead of single cases:** the worst case is a
  pump that ran too long or dosed too much. Three baseline rules stay
  with the product side:
  - a crash or a restart stops whatever could cause harm, dosing for
    example (R6; fans keep their state, PD-050, SD-028);
  - the hardware time limit of the pumps. Not on board v0.1 (PD-160): a
    later board brings it (PD-222; which board is the product side's,
    SD-084). Until then the gateway's cap of 60 s per run and the board
    watchdog (on a hang, #94) stand in; the safety component's limits are
    planned (SD-045, SD-066, not built yet). The software's limits stay on
    top of the hardware limit afterwards;
  - no watering when pH and EC say the water is not fit. When this was
    recorded, no pH or EC check guarded the irrigation pump; of the
    water, only the tank level blocked it (RAT-068). SD-093 builds it.
- **Freedom:** any pump must work on a pump output, wherever the pump
  sits (in the container or outside it); the output switches 12 V and
  the user chooses the pump. Other devices, connections and sensors must
  be usable too. That a pump may be used does not by itself mean that it
  may restart on its own. growcontroller is an open-source software
  project for which hardware can be supplied; its features stay such
  that a buyer can adapt the setup freely.
- **Control details are decided here (PD-318):** locks, releases,
  thresholds, sequences, and which devices and pumps are allowed are
  software decisions, made along the guideline and recorded as SDs. The
  product repository keeps the guideline, costs, hardware and the
  baseline rules above.
- **Earlier product decisions on such details are proposals:** the PDs
  and the product side's answers on control details in issues (for
  example #132, #156 and #164, and the rules for mixing and watering in
  the comments on #190) are decided along the guideline and simplified
  where it allows, without a new PD. This includes restrictions such as
  only certain pumps or only the shop's modules.
- **Product questions (changes SD-029 and SD-072):** a product question
  is one about what the product is and offers: its principles, costs,
  hardware, legal points and the manual. A control detail is not a
  product question. Product questions are still filed as
  `product-question` issues under SD-072's rules (public, own words,
  answers only from the owner's account). The product side answers them
  directly along the guideline and writes a PD only for principles,
  costs or hardware. An answer on a control detail counts here as a
  proposal.
- **Unchanged:** SD and RAT IDs are assigned here (SD-061); the session
  does not access the product repository (PD-226); the security and
  process decisions recorded from #190 are not control details and stay
  (SD-072, SD-075, SD-077, SD-079).

Added by the software side; the project owner decides them with this
entry's pull request ("mergen"):

- **How a control detail is decided:** Claude proposes options with a
  recommendation, the project owner decides in the software session, and
  the decision becomes an SD (`CLAUDE.md`). An SD already recorded from
  an earlier PD stays in force until a later SD changes it; nothing is
  loosened without one. A simplification never goes below the baseline
  rules or the architecture rules R1–R8, and a logic change to the
  actuator gateway or a protective cut-off still waits for the project
  owner's "mergen" (SD-033).
- **When:** earlier PDs on control details, and open product questions
  that ask for one (for example #261, #283 and #294), are decided when
  the function they belong to is built, or earlier where the owner asks.
  A part of such a question that concerns hardware or costs stays with
  the product side.
- **Domain rules:** a control detail decided here that rests on
  operating practice gets a RAT entry in our own words and a test, like
  the rules from the product repository (PD-039); its evidence follows
  `docs/RATIONALE.md` (measured, observed, calculated, found in a review,
  or "—"), and a number without such a source is marked as an
  assumption.


## SD-092: Each recipe learns its own EC effect; a reference only ever makes a round smaller

Decided by the project owner on 2026-10-21 and 2026-10-22 in the
software session, a control detail (SD-091), for step 4 of SD-057 (#143), after `domain` found that two of SD-057's
points cannot both hold: "another recipe starts from its reference" and
"a typo in a recipe can never raise a dose". A reference that is too low
(1.2 typed for 2.1 passes the reference check, RAT-056) would make EC
control's first round on that recipe larger than today, and an
overshoot can only be undone by draining and diluting (RAT-055). This
changes SD-057's point on the learned effect; the rest of SD-057 stands.

- **Each recipe learns its own effect** from EC control's clean rounds
  on it (RAT-057). A round learns into the recipe it dosed, also when
  EC control has moved to another recipe while it settled.
- **Nothing learned, a recipe starts from the larger of the start
  effect fixed in code and its reference's effect** (within a quarter to
  four times the start effect). A reference only ever makes a round
  smaller; a product truly weaker than the start effect needs a round
  more until it is learned.
- **What is learned is kept within a quarter to four times the start
  effect fixed in code**, the band the reference must lie in, whatever
  the recipe's reference; below it, the recipe's start. So a high
  reference does not raise the band's lower end, and no reference moves
  its upper end above 1.1 mS/cm per ml/L.
- **The cap of a round converts the step limit with the larger of the
  start and the learned effect**, the start already being the larger of
  the start effect fixed in code and the reference's (SD-057's first
  safeguard). Neither a small reference nor a small learned effect widens
  it; a stored value above the band counts at its top, so the cap never
  shrinks to where no round fits and nothing could be learned again.
- **An edit of a recipe** that changes the parts EC control scales or
  their ratio scales the learned effect up by what is no longer scaled
  (fixed now or removed), as RAT-085 does for a part made fixed, at most
  to the band's top, and keeps it only if it is at least the recipe's
  new start; a round on that recipe that settles across the edit learns
  nothing. A change of all amounts alike or the name keeps it. A
  reference raised above it forgets it: the same parts may hold a
  stronger product (decided by the owner on 2026-10-22, from `domain`'s
  review). An import treats a recipe the hub had and the file keeps the
  same way; a recipe it leaves out or brings new starts afresh, as one
  deleted or newly created does, also while a round on its ID settles.
  At the update, the one effect learned before goes to every recipe
  whose start it does not undercut. For these, nothing doses more than
  with either keeping or forgetting the value; an edit that keeps the
  parts, their ratio and the start keeps what was measured, which may
  lie below the start.
- **A part removed from a recipe while a round on it runs is still
  dosed** in that round: a pair leaves a recipe only whole, and dropping
  its second part after the first went in would split it (RAT-054). The
  round stays within the cap it was planned on and learns nothing.
- **Not covered:** a ratio moved towards a stronger part among those
  that stay is not bounded by the edit; the 0.8 aim absorbs a blend up to
  1.25 times stronger, and the cap bounds the rest. The volume a round
  learns over is read when it has settled; irrigation meanwhile is not
  yet counted (#301; since SD-103 the volume is read when the round
  starts, and a round watering overlaps learns nothing).

Not chosen: a recipe starting from its reference, as SD-057 had it (a
low reference raises a round, up to the cap); forgetting the learned
effect on every edit (a strong product would then overshoot on its next
round); keeping it when a reference is raised above it (a product
change in the same bottles would dose with the old, too small value).


## SD-093: No watering on water not fit for it: a wide band, the probes the hub judges, a dose mixed in first

Decided by the project owner on 2026-10-21 and 2026-10-22 in the software
session, for the third baseline rule of SD-091 (#298), after the `domain`,
`security`, `qa`, `reviewer` and `ux` reviews, each question as
recommended: the band, no override, how probes count, the mixing time,
the case of a cut mix, an EC target nobody set and when EC control's own
target counts, the wait after a start, a hand dose with the circulation
pump off, and no wait without a probe checked. The proposals PD-253 to
PD-258 and PD-300 to PD-303 on watering after a cut mix are decided by
it (SD-091).

- **The band (RAT-086):** water is not fit for watering when its pH is
  below 4.0 or above 8.5, or its EC is more than 1.0 mS/cm above the EC
  target, and never above 5.0 mS/cm. The target is the higher of this
  phase's and the last phase's, each as it was in effect (the phase's,
  else EC control's setting, else the catalog's default), so a tank mixed
  for the last phase stays fit after a step down (RAT-012). A target
  counts only when EC control is on or the phase names one: the settings
  card saves every field, so a target in EC control's settings while it
  is off may be a default nobody chose. Otherwise only the limit of 5.0
  counts, so a tank mixed by hand is not refused against it. There is
  no lower EC bound: plain water flushes. The band catches a pump that
  ran too long or dosed too much, not a recipe missed: uncorrected tap or
  RO water typically stays fit (RAT-084). The values are fixed in code
  (R7) and are assumptions; no setting changes them.
- **Readings:** only a usable reading counts. A pH or EC probe that is
  assigned but has no usable reading (offline, no data, stale, frozen,
  implausible, locked after a jump, not calibrated) blocks: a missing
  value is never fit (R5). So does a probe in a calibration: it measures
  a buffer, not the tank. A probe not assigned, or of a device class
  calibrated outside the hub, is not checked, whatever its reading
  (RAT-025): a setup may do without probes. With neither probe checked,
  nothing is checked and nothing waits.
- **A dose mixes in first:** with a probe checked, every run of a dosing
  pump holds watering while it runs and for the mixing time after it: at least 240 s and
  100 s per 20 L in the tank (RAT-052; the scaling is an assumption),
  never more than 60 min. A run that starts while the pump waters stops
  it. After every start the hub waits the same time, as a dose the
  restart cut is not known after it.
- **Where:** in the gateway (R1), on every path that switches the
  watering pump on: by hand, over the API and the test run. A running
  pump goes off as soon as the water is not fit or a dose starts, with an
  event and without a latch: it can water again once the water is fit.
  There is no override, by hand neither. Nothing switches the pump on
  again by itself, so a refusal says why and when to try again.
- **After a cut mix** the same holds as always: watering once the last
  run has mixed in and the water is inside the band. The earlier
  proposals for that case are dropped: waiting times per phase, an
  emergency dose without probes, excess counted per product, catalogue
  roles, soft water, and a mix counted as cut after the watering delay.
  SD-084's rule stays for the irrigation function, not built yet: a
  planned watering time waits until a running mix is finished.
- **Residual risks:**
  - a small overdose in a large tank stays inside the band and is
    watered (one 60 s run of A and B in 100 L adds about 0.25 mS/cm), and
    so is a half-finished mix inside the band;
  - without probes nothing is checked; a probe reading too low (dry, or
    arbitrary in RO water, RAT-046) can hide an overdose or refuse plain
    water; tap water above pH 8.5 is refused;
  - after a hand dose with the circulation pump off (in interval mode),
    the mixing time can be too short; the check goes on while the pump
    waters and stops it once the probe sees the dose;
  - a dosing pump stuck on at the board shows no running dose to the hub
    (board v0.1 has no hardware time limit), so only the band catches
    it: part A alone after about 1.4 min in 20 L;
  - every run of a dosing pump holds watering, also a calibration or
    priming run into a cup, and correction rounds after a refill hold it
    for about 20–40 min in 30–60 L;
  - the lock shows only when someone taps, and a refusal over the API
    leaves no event (#307);
  - deep water culture, RDWC and NFT have no watering role yet, so the
    rule protects pulsed watering only.

## SD-094: Testers start from the docs for newcomers (#304); "Report a problem" opens the bug form

Decided by the project owner on 2026-10-22 in the software session, after
a review by `ux` of what a first-time tester finds, and after two sessions
had written docs for testers in parallel (#304 and #316): #304 is the
base, and #316 keeps only what #304 does not bring.

- **The pages for testers are #304's:** `docs/DEMO.md` (download, start,
  a short tour, what feedback helps), `docs/FAQ.md`, `SUPPORT.md` and
  `CODE_OF_CONDUCT.md`, with the README pointing to them. Feedback goes
  to the Discussions category "Feedback" (slug `feedback`, open-ended
  format) with #304's form; the project owner creates the category.
  Ideas that are accepted become issues, as `CONTRIBUTING.md` says.
- **Every release's notes** get a line "Testers: start here" that
  `release.yml` adds, as it adds "Try it without hardware", pointing to
  `docs/DEMO.md` and `SUPPORT.md`.
- **Bugs that can be repeated** go to the bug form, which asks for the
  steps. "Report a problem" in the web app opens it
  (`template=bug.yml`) with its text fields filled in from the query:
  the title with the report ID, `what` with the user's description,
  `version`, and `report` with the report ID; the user picks where it
  happened, as a form fills only text fields from the query. The link
  carries no `labels`: GitHub answers a query parameter the visitor has
  no right to with "404 Not Found", so for anyone outside the project
  the link failed (#305); the form sets its labels itself. It attaches
  nothing. The diagnostic package stays on the user's device; when we
  ask for it in the issue, the user looks at it and attaches it there,
  as there is no private way to send it yet, and the app says so.
- **Not chosen:** the pages of #316 (`docs/GETTING_STARTED.md`,
  `docs/FEEDBACK.md` and a category "Tester feedback") next to #304's,
  as they repeat them; merging both into one pull request (more work and
  new reviews for little gain); a full manual of every page (weeks of
  work that changes with each version, and the product's manual is a
  product question); an outside survey form (a third party handles the
  data, a privacy notice is needed, a second channel next to "no
  cloud").


## SD-095: The simulator's demo speaks English

Decided by the project owner on 2026-10-22 in the software session, for
testers who do not read German (#302; #304's decision makes it a
precondition of 0.1.0-proto.5). Changes SD-083, point 1 ("the
simulator's demo scenarios stay German").

- The demo scenarios start in English, with English names for devices,
  bottles and recipes. German is chosen under Settings as before. Names
  the demo sets then stay English; a device without a name of its own is
  still named by its class in the page's language (SD-083, point 3).
- The simulator's own messages on the console and `--help` are in
  English too; `sim/README.txt` and `docs/SIMULATOR.md` say where to
  switch the language. Unchanged: the demo password `demo-passwort`, as
  the docs, the release notes and the tests name it; the data folder
  `growcontroller-daten`, so a demo saved before keeps its data and its
  language; the scenario names `demo`, `neu` and `stufe1` of the
  command line and the API.
- A hub's stored language is kept (SD-083): a demo folder from an older
  version stays German.
- **Not chosen:** a German demo with a hint where to switch (the first
  impression for testers abroad would be a language they do not read);
  two demos chosen by a start option (two to keep up and test).


## SD-096: Product requirement (from #250, F5): a prototype says it is for testing only

Recorded on 2026-10-22 from the product side's answer to #250 (F5, of
2026-10-21, marked preliminary; SD-072); where it shows in the web app
decided by the project owner on 2026-10-22 in the software session.

- **The requirement:** every `-proto` and `-beta` release and the web app
  carry a notice that the version is for testing only and does not meet
  the requirements of the EU Cyber Resilience Act (CRA).
- **Where:** the release notes of every such release, also the Home
  Assistant app's `ha/app/CHANGELOG.md`, the README's status, and in the
  web app on the sign-in page (also when the first password is set;
  behind Home Assistant's Ingress, where there is none, in the app's
  introduction, which can be skipped) and always under Settings › Display
  and info. In `ha/app/CHANGELOG.md` each pre-release's section starts
  with it.
- **When the web app shows it:** when its version has a SemVer
  pre-release part (a hyphen after `X.Y.Z`, before any `+`), which
  covers `-proto` and `-beta`, and when the version is missing or cannot
  be read. A Stable release drops it without a change of text. Builds of
  the Development channel show it while `VERSION` on `main` is a
  pre-release, as it is until the first Stable release; how a Development
  build after a Stable release is named is decided with that release.
  `VERSION` has no build part (`tools/version.test.mjs`), so a hyphen in
  it always marks a pre-release.
- **Text,** in both languages with a key: "For testing only: this
  version does not meet the requirements of the EU Cyber Resilience Act
  (CRA)." It names no channel, as a Beta is meant for real devices
  (`docs/RELEASE.md`); the README's safety note stays as it is.
- It stays until the product side changes its answer; a confirmation by
  a test lab or a lawyer comes later (#250).
- **Not chosen:** a strip on every page (it costs room on a phone and
  competes with alarms); the release notes and the README only (the
  answer names the web app).


## SD-097: Product requirement (from PD-320): an advisory only for what an attacker can trigger; the label of the other fixes

Recorded on 2026-10-22 from the product side's answer to #200 (PD-320, of
2026-10-21; SD-072); the label decided by the project owner on
2026-10-22 in the software session, from the product side's proposal.
Settles SD-079's point 1 and its open point #200.

- **PD-320:** a fixed defect gets its own GitHub Security Advisory only
  if someone without the needed rights can trigger it, for example over
  the network, through a crafted file or another input. This confirms
  SD-079's point 1 and replaces PD-220's "every security item gets an
  advisory". Fixed safety and robustness defects without an attacker
  stay in the release notes under Security, with what they are, the
  affected versions, the impact, the severity and the remedy. Timing and
  CVEs stay as SD-079 says.
- **The label of their block** in the changelog's Security section is
  "Safety and robustness fixes, not exploitable by an attacker (CRA
  Art. 3(41)); disclosed under Annex I, Part II (4)", instead of "Safety
  and robustness fixes, no vulnerability": by the product side's
  regulatory review, the CRA's notion of a vulnerability (Art. 3(40))
  is wider than "exploitable by an attacker", which is closer to an
  exploitable vulnerability (Art. 3(41)). Such an item's severity reads
  "no CVSS, as it is not exploitable by an attacker" instead of "…, as it
  is not a vulnerability". The rule goes into `SECURITY.md` and
  `docs/RELEASE.md`.
- **To check before the CVE requests for proto.5** (requested by
  2026-11-10, three working days before the advisories' date): the
  Security item
  "An abandoned probe calibration no longer keeps jump checks off" can be
  reached in the Home Assistant app through a crafted import, the case of
  point 1, and so can the fix "Settings files: whole numbers only" (#270,
  under Fixed). `security` checks both against PD-320. An item that a
  crafted file can trigger moves to the vulnerabilities, with an advisory
  of its own or within one it shares a cause with.
- **Answered without a new decision** (#200, F1 and F3, assumptions of
  the product side's regulatory review): reporting under Art. 14 starts
  with the first offer or the first device lent, as SD-079 says; until
  then we report voluntarily (`SECURITY.md`). A plan for the 24-hour
  early warning is needed anyway; who covers it is open on the product
  side.
- **Not chosen:** keeping "no vulnerability" (narrower than the CRA's
  term); a short "no attacker path" with the reasoning only in
  `SECURITY.md` (the release notes would not name the CRA's terms).


## SD-098: A fixed pattern doses at most the recipe's amounts, from the larger of the reference's and the learned effect

Decided by the project owner on 2026-10-22 in the software session, a
control detail (SD-091), for step 5 of SD-057 (#143), after `domain`
weighed the choices. A fixed pattern to a target EC (SD-057's way 2) runs
without any feedback, often with reverse-osmosis water entered as 0 and no
meter; a reference typed too low (1.2 for 2.1 passes the band, RAT-056)
would make it dose up to four times the chart amounts, which only
draining and diluting undo (RAT-055). The grow proposes each phase's
target from the same reference (RAT-066), so such a slip usually sets the
target too and no warning on the amounts would catch it. This changes
SD-057's point on way 2; the rest of SD-057 stands.

- **The effect:** the reference's over all the recipe's parts (a fixed
  part taken as strong as the rest, an assumption), and the recipe's
  learned effect where it is larger (SD-092). The reference counts only
  inside the band of a quarter to four times the start effect; a learned
  value counts from the band's bottom, one stored above it at its top,
  as for a round (RAT-056). No floor at the start effect: without
  feedback, a product weaker than it would never reach its target. With
  a learned effect, the EC to expect takes the fixed parts as strong as
  it; a fixed part weaker than that (a cleanser, for example) leaves the
  tank lower than said, on the safe side.
- **The amounts:** every scaled part by one factor, (target − water's EC
  − what the fixed parts add) over the effect, at most 1, so never more
  than the recipe's own ml/L; a fixed part per litre of fresh water, also
  on a top-up (RAT-011, RAT-085). A target above what the recipe's
  amounts reach is not met: the plan names the EC to expect and how to
  get more (higher amounts and the matching chart EC in the recipe).
- **Only with EC control off or locked (SD-089, S20):** EC control
  switched on and free raises the tank to its own target after the mix,
  over the one asked for. Until mixing to the target with the probe is
  built (way 1, step 6), the hub refuses a fixed pattern to a target EC
  then, and the mix page offers only the recipe's amounts, says why and
  how to switch EC control off. Changed by SD-102: with EC control on
  and free, way 1 is offered instead, the recipe's amounts as the second
  choice. A locked EC control that a person releases after a fixed
  pattern takes the tank to its own target then; a person acts there.
- **The water's EC** comes from a meter, is entered as 0 for
  reverse-osmosis water, or on a new fill is the probe's valid reading,
  taken over with one tap (SD-089, S20); the page asks that the fresh water is
  already in the tank, and rounds it to 0.01.
- **Refused, with the reason:** without a reference inside the band
  (also when a learned effect exists: it may stem from another recipe or
  from the start's fallback), a target outside the EC target's range,
  without the water's EC, which is never taken as 0 (R5, RAT-006), or
  with a water EC above the EC target's range (most likely entered in
  µS/cm). A request that gives a target or water EC without asking for
  the target way, or a choice of amounts the hub does not know, is
  refused too, so an API client never gets the full recipe by mistake.
  The recipe's amounts can always be mixed.
- **Said in the plan:** that it runs without measuring, the EC to expect,
  water above the 0.4 the charts are made for on a new fill, and with pH
  control on that the pH correction afterwards adds up to about 0.3
  mS/cm, or, when the EC to expect lies below pH control's EC lock
  (RAT-046), that the pH is corrected by hand. Its note for after the
  mix asks to check the EC with a meter. Water at or above the target,
  alone or with the fixed parts, gets only the fixed parts.
- The step limit (`max_ec_step`) does not bound a fixed pattern; the
  recipe's amounts do (RAT-087).
- It sets how much may be dosed without feedback, so its step waits for
  the project owner's "mergen" (SD-033).

Not chosen: the reference alone with a warning above 25 % of the chart
(misses a slip the grow carried into the target); a floor at the start
effect as in SD-092 (a weak product would never reach its target).

---


## SD-099: After irrigation a fill waits until the level stops rising (ebb and flow drains back)

Decided by the project owner on 2026-10-22 in the software session
("umsetzen" for #294), along the product side's answer to that question
(option 3, a proposal under SD-091) and the `domain` review: the calm
criterion and the fixed upper bound as recommended, and that the readings
count from 3 min after the pump stops (the settle time of RAT-038;
`domain` had proposed 2 min, the reading's lag).
Changes SD-082 for the drain-back it named as a residual risk.

- **The wait after irrigation:** a fill that falls due after the
  irrigation pump ran waits 3 min after the pump stops, as before (the
  level reading lags 1.5–2 min, RAT-038). It then also waits until the
  level has stopped rising: the median of the readings of the last 90 s
  is at most 0.3 L above the median of the 90 s before. The readings count
  from 3 min after the pump stopped, when the lagging reading has caught
  up; counted from the stop, the still-falling reading of the flood looks
  like a fall and hides the water coming back. So the first verdict comes
  6 min after the stop, also in a run-to-waste drip system, where nothing
  comes back and a fall counts as calm. The value is taken once a second
  while a fill may wait for it; both windows must be spanned, and each
  must hold at least three reports the sensor made within it, so a value
  held between rare reports is not taken as calm: a sensor that reports
  less often than every 30 s never counts as calm and waits the full
  half hour. The values are assumptions (RAT-038): with a drain-back that
  fades with a time constant of about 5 min, the fill can end up about
  0.75 L past the target (with the lag, about 0.5 L still coming back and
  the reading about 0.25 L low; up to about 0.9 L with a report every
  20–30 s), within the 1 L the target keeps below the capacity.
- **Upper bound:** 30 min, fixed in code, from when the fill began to
  wait or from the pump's stop, whichever comes first; then the fill goes
  ahead, marked as irrigated (SD-082). The product side's "upper bound
  per system type" waits for the system types (M4); there is none yet, and
  no setting for it.
- **Only after irrigation:** a Stop, maintenance, a start, an inlet latch,
  the function switched off or a valve opened by hand keep the plain
  3 min (SD-088, SD-090). The readings are not kept across a restart
  (R6): after one the plain 3 min apply.
- **The line** says that water may still run back after watering and
  that the fill starts once the level stops rising, with the time left
  at most. EC and pH wait meanwhile, as for any fill that waits to settle
  (SD-090).
- **Residual risks:**
  - a drain-back slower than assumed is filled on top of what is still
    to come: about 1.1 L at a time constant of 7 min, 1.7 L at 10 min; one
    still running after 30 min is filled on top as well. The 1 L margin
    is what is left; the inlet's capacity cut-off closes the inlet at the
    capacity, but water that drains back afterwards can still pass it;
  - a long flood counts against the 30 min, as the fill is usually due
    from the flood's start: a 15 min run leaves 15 min for the drain-back;
  - a level sensor reporting less often than every 30 s, or a hub that
    ticks less often, waits the full half hour after every watering, also
    in a drip system (`docs/HOME_ASSISTANT.md`);
  - watering switched by a timer the hub does not switch gets no wait,
    and water a circulation pump in interval mode holds outside the tank
    (NFT, RDWC) stays SD-082's residual risk;
  - a flood or drain-back of more than 20 L within 5 min locks the level
    as a jump and can cut the watering pump mid-flood (#324; closed by
    SD-101: the running watering pump explains a level change);
  - the twin's irrigation draws no water and its level reading follows at
    once and every 5 s unless a test sets a lag or a report interval, so
    the tests change the tank's volume themselves and run the
    ebb-and-flow case without a lag, with a lag of 105 s and with a report
    every 45 s.

---

## SD-100: The demo goes to a wider public only after 0.1.0-proto.5, which starts in English

Decided by the project owner on 2026-10-22 in the software session, after
the reviews of the docs for newcomers (#304) by `reviewer`, `qa`, `ux`
and `security`.

- **#302 is a precondition of 0.1.0-proto.5** (SD-055, #143): the demo,
  its sign-in page, its console window and `--help` start in English for
  an English visitor. On `main`, "English first" (SD-083) shows the hub's
  language from the sign-in page on, and the demo hub is German, so
  without #302 proto.5 would show every visitor a German demo.
- **The demo is announced to a wider public** (hobby, grow and Home
  Assistant communities) **only once proto.5 is released.** Until then
  the download is 0.1.0-proto.4: it lacks most of the polish block, and
  its Home Assistant app still has the first-password weakness fixed
  under `[Unreleased]`.
- The docs for newcomers may merge before that: they describe `main` and
  say how the download differs.
- How the announcement and the FAQ speak about the hardware is product
  question #303; signing the downloads is product question #306.

Not chosen: pulling proto.5 forward without steps 4 to 6 of SD-057
(it would change SD-055's order); announcing now with proto.4 (an older
demo in German, and a known weakness in its Home Assistant app).

## SD-101: The watering pump explains a level change (a flood and its drain-back lock nothing)

Decided by the project owner on 2026-10-23 in the software session
(#324, from the `domain` review for #294), a control detail under
SD-091, each question as recommended: first the time after the stop,
only the level and only the watering pump; after the reviews of
`reviewer`, `qa`, `domain` and `security`, that the 10 min stay with the
corrected numbers below and that the explanation is bounded upwards.
Changes RAT-042 and what SD-067's `jump` hold sees; closes the residual
risk SD-099 named for it.

- **What it fixes:** the level's jump lock (a change of more than 20 L
  within 5 min, RAT-042) knew the hub's own inlet but not the watering
  pump. In ebb and flow a flood can take more than that out of the tank
  within minutes, and the drain-back brings it back as fast (a
  120 × 120 cm table holds about 30–45 L at a shallow flood, about 70 L
  at a deep one; assumptions, not measured). The level then locked for
  15 min: the gateway cut the watering pump mid-flood as "level
  invalid" (the dry-run protection), the refill waited, the circulation
  pump could not be switched on, and a dry-run latch of the circulation
  pump set during the flood held for a person instead of releasing
  itself (SD-067).
- **The rule:** watering can only lower the level, and the drain-back
  brings it back at most to where the run started. So while the
  watering pump (`zone.irrigation_pump`) reads as on, a reading up to
  2 L above the lowest reading of the run counts as explained, so a
  fall; until 10 min after it stops, a reading up to 2 L above the
  highest level a run of the series started at. A run that starts
  within the 10 min of the one before continues the series; of its
  runs, only those started within the last 30 min count (SD-099's
  longest drain-back), so a series of drip shots does not keep the
  morning's level as its bound. A reading above the bound is judged as
  any change again: more than 20 L within 5 min of the last explained
  reading locks, so a level sensor that jumps high, the dangerous
  direction for the dry-run cut-offs, still does. Renewed on every
  tick, as for the inlet. The 2 L are a gate, not a lock threshold:
  above it the 20 L apply, so a small gate only makes the check
  stricter; it covers noise and a small recovery while the pump runs (an
  assumption). A pump whose state is not known counts as off, and a
  state that flaps counts as one run within a minute. A level not
  usable when a run started explains nothing for that run, nor after the
  stop for its series: a jump then locks, the safe side.
- **10 min, not the inlet's 5:** water still runs back after the stop,
  and the reading lags 1.5–2 min (RAT-038). Readings within the 10 min
  set the reference, so only what comes back after them counts. With a
  drain-back that fades exponentially, the 10 min keep any table up to
  about 110 L from locking, whatever the time constant (5 min: about
  58 L). With a drain-back of nearly constant flow, as from a table
  draining under gravity, about 52 L with the reading 2 min late (5 min:
  about 32 L, so an ordinary table could lock). 30 min would leave a
  jumping sensor unnoticed for half an hour after every watering. All of
  this rests on assumptions about the drain-back.
- **Only the level:** watering takes tank water out and the drain-back
  brings mostly the same water back, so EC, pH and water temperature are
  expected to stay below their jump thresholds (0.5 mS/cm, 1.0 pH,
  3 °C; an assumption, to be checked on an ebb-and-flow installation).
  Explained, they would hide probe faults. A jump of these while
  watering still locks, and a probe without a usable reading stops
  watering (SD-093): the safe side.
- **Only the watering pump:** a circulation pump holds water outside the
  tank while it runs (NFT, RDWC); how much is not known. Explained while
  it runs, one running all the time would switch the level's jump lock
  off for good. It stays SD-082's residual risk until a case or a
  measurement shows that it moves 20 L within 5 min.
- **What still checks the level meanwhile:** its age, the bound above,
  the minimum level at the gateway (the dry-run cut-offs below it, which
  read the same sensor) and, after watering, the refill's wait for a
  calm level (SD-099). Its plausible range is 0–1000 L for every tank,
  and it has no standstill check (SD-067).
- **Residual risks:**
  - a level sensor that jumps low, or high but not past the bound, while
    the pump runs or within 10 min after it stops is not locked and
    raises no alarm; a false low reading errs towards the dry-run
    cut-offs and a fill, which SD-082 and SD-099 check, and a false high
    one within the bound can release a dry-run latch (SD-067);
  - a reading past the bound is locked only as without watering: more
    than 20 L within 5 min; a sensor stuck 15 L above the run's lowest
    reading is not;
  - a pump that stops delivering while its plug reads on (air drawn in
    at a low level, a thermal or float switch in the pump, a timer after
    the plug, an overflow that siphons the table) lets the table drain
    back during the run: more than 20 L within 5 min then locks the
    level and cuts the pump as "level invalid", mostly the safe side;
    the lock's text asks about work by hand, which does not fit;
  - a deep flood with a drain-back of nearly constant flow (more than
    about 50 L) can still lock the level after the 10 min: an alarm, and
    until 15 min of calm the circulation pump not switched on (EC and pH
    wait for it, RAT-047) and a dry-run latch held for a person;
  - watering more often than every 10 min, as with drip shots, keeps
    falls explained all the time, and rises up to the highest level a
    shot of the last 30 min started at; once the system types exist
    (M4), the explanation may be limited to ebb and flow;
  - a refill already open when a flood starts adds water the bound does
    not know; after the inlet's own 5 min, the drain-back can pass the
    bound and locks only above 20 L within 5 min;
  - a series of floods lasting more than 30 min drops its first start
    from the bound, so the last drain-back can pass it; it locks only
    above 20 L within 5 min;
  - a smart plug that reports "on" falsely keeps falls explained for as
    long as it does, and 10 min after; the hub's run limit covers only
    runs it started;
  - a restart during a flood switches the pump off (R6) and forgets the
    explanation, so the drain-back after it can lock the level, and a
    dry-run latch set during the flood then holds for a person;
  - a pump the hub does not see (not bound as the watering pump, a drain
    valve or a second pump) explains nothing; one bound and switched by
    its own schedule, its button or Home Assistant does, as the hub reads
    its state;
  - a round of EC or pH control whose settle time overlaps a flood can
    learn from a level that is still coming back (#301; closed by
    SD-103: such a round learns nothing);
  - the twin's irrigation draws no water, so the test changes the tank's
    volume itself, with the reading following at once and with a lag of
    105 s.

## SD-102: Mixing to the target with the hub's probe: measure first, a capped first step, then EC control closes the rest

Decided by the project owner on 2026-10-22 in the software session, ten
control details (SD-091) for step 6 of SD-057 (#143), each presented on
its own after `domain` planned the step, and eleven follow-ups after the
reviews, on 2026-10-22 and 2026-10-23 (in D3, D4, D8, three times in D10 and the first
step below, each as recommended). Way 1 of SD-057 is EC
control's first round done as a mix: the parts go in the recipe's order
with mixing between them, fixed parts per litre of fresh water, the first
step without a partial-run limit and converted with the largest known
effect (SD-089 S4), and the mix's own mixing-in time. This changes
SD-057's point on way 1 and SD-098's "until way 1 is built"; the rest
stands.

- **Offered (D1):** with EC control switched on and free and an EC probe
  and a circulation pump assigned, the mix page offers "To target with
  probe" and preselects it while it can run: a valid EC of the tank, no
  probe calibration open, the circulation pump not blocked, the inlet
  closed, no maintenance mode (it explains every jump and holds EC
  control, so the first step would never be judged) and no round of EC
  control still settling (the first step becomes its round 1). Otherwise
  the recipe's amounts are preselected and the page says why; until the
  user taps a choice it follows what the hub can do. The recipe's amounts
  stay the second choice, with a warning when its chart EC plus the
  water's EC lands above EC control's target plus tolerance, since EC is
  never lowered (RAT-012). A fixed pattern (way 2, SD-098) stays for EC
  control off or locked; with the hardware there, the page says when way
  1 is offered.
- **The recipe (D2):** only EC control's own recipe (the phase's), so the
  rest is closed in the same ratio and what is learned goes to that
  recipe (SD-092); another recipe gets only its amounts, with the reason.
- **Measuring first (D3):** the job first circulates for 100 s per 20 L,
  at least 100 s and at least EC control's settle time (the user's own
  statement of how long the system takes to mix, follow-up), at most
  60 min, counted from when the pump runs (fixed in code, R7; the scaling
  with volume is the same assumption as the mix's mixing-in time), then
  reads the tank's EC, taken since the pump runs, plans again with it
  (every refusal and the stock checked again) and doses without a further
  confirmation; the card shows the time left, what it measured and the new
  amounts. A pump that does not come on within 2 min, or stops for 10 s
  once it ran (RAT-037, RAT-039), a probe calibration started, the inlet
  opening or a refill starting, a reading not usable, or none taken since
  the pump runs within 5 min after the measuring time end the mix with
  nothing in and the reason. Watering waits while it measures; a running
  watering pump stops as routine. EC and pH are announced while it measures, as for
  every mix (follow-up, changes the decision of D3 that a jump while
  measuring locks): fresh water not yet mixed moves the probes, which
  would end most top-ups; the step limit and the first-step lock (D6)
  bound a probe at fault to one step at the step limit, then lock
  (RAT-042).
- **The litres (D4):** a usable level reading, else the litres entered:
  the fresh water on a new fill, and on a top-up the litres in the tank
  now, fresh water included, which the page asks for and the hub then
  keeps as the tank's volume (follow-up, replaces "known plus fresh":
  what was used since the last mix would count as still there, and the
  first step would raise EC by that ratio more than the step limit). The
  fresh water is in before the start. Fixed parts on the fresh litres.
  The plan names the source; on a new fill it warns when the entered
  litres and the level differ by more than 10 % (an assumption). Litres
  above the tank's capacity are refused (a level read too high named as
  such), and litres that moved by more than 10 % while it measured end
  the mix with nothing in.
- **The aim (D5):** where EC control aims, its target minus the reserve
  for what pH− adds (RAT-053); "at the target" from the aim minus the
  tolerance. Plan and controller share one function. Water at the aim
  gets only the fixed parts, with a note; without fixed parts the mix is
  refused as at the target.
- **The first step:** 0.8 × the gap (aim − water's EC − what the fixed
  parts add, counted at the same effect), at most the step limit
  (`max_ec_step`), both converted with the largest of the start effect,
  the recipe's reference and its learned effect (SD-089 S4, RAT-056); not
  bounded by the recipe's amounts, since feedback follows, except while
  the recipe has neither a chart EC (inside the band) nor a learned
  effect: then nobody knows how strong the product is, and round 1 doses
  at most the recipe's own amounts for the fresh water (on a new fill all
  the tank's litres), with a note (follow-ups; a product up to four times
  the start effect would otherwise raise EC by up to four times the step
  limit, which only diluting undoes). The step limit binds the scaled
  parts; the fixed parts add what their amounts give. On a new fill the
  fixed parts go on the litres it doses on (all the water is fresh), on a
  top-up on the fresh water; a top-up with more fresh water than the level
  reads is refused.
- **When the EC fails (D6):** the probe's own verdict (not usable, a
  probe calibration open): while measuring the mix ends with nothing in
  and gives the reason; once a part is in, the step is finished without
  cutting a pair (RAT-054) and handed over, and EC control waits for the
  probe. It never goes on as a fixed pattern. At the hand-over the first
  step's rise is judged at once, before anything is learned: below the
  larger of 0.02 mS/cm and a quarter of the rise expected (the band's
  bottom, RAT-056), EC control locks (`ec.no_effect`) with its own text:
  is the probe in the water, do the pumps deliver, are the litres right?
  A dry or misplaced probe reads about 0 and counts as usable, so without
  this check up to two more rounds at the full step limit would follow. A
  rise above four times the expected gives a warning. A step during which
  the inlet opened, a refill ran, a probe was calibrated or maintenance
  mode was on (also while the mix waited, failed) is not judged, as its
  rise says nothing; a resumed one is.
- **Learning (D7):** the first step counts as EC control's first round:
  it learns into the recipe when clean (no inlet opened, no Stop or
  resume, no failed part, the pump ran, no probe calibration or
  maintenance, the recipe not edited meanwhile; RAT-057), from the rise
  over all the millilitres that went in, the fixed parts taken as strong
  as the rest, as the plan counts them (D5); counted over the scaled parts
  alone, a fixed part's rise would be learned as theirs. A fixed part much
  weaker than the rest (a cleanser) makes the learned effect low by its
  share, so the next round is somewhat larger; small in practice, as round
  1 is usually at the step limit and the next clean round corrects it. It
  counts for `max_doses`.
- **The end of the mix (D8):** when the first step has mixed in; the card
  says what happened: handed over ("round 1 in, EC X; EC top-up takes over
  the rest to Y", then EC control's own line), locked by D6, EC control
  off or locked meanwhile (the rest waits), or the probe not usable (EC
  control waits for it). A way-1 mix that ends with nothing in (cancelled,
  stopped, restarted, refused after measuring, or at the target once
  measured, which the card says as "nothing needed") undoes the window
  against mixing twice it opened at the start (RAT-003), when it is still
  its own; the litres it booked stay, as the fresh water is in before the
  start (follow-up; the litres from before would let EC control's next
  round raise EC by their ratio more than the step limit). One that
  failed with a pair half in holds EC and pH control, whose line says to
  resume or end it, so the part still missing goes in (follow-up,
  RAT-054; ended, the card, a Stop and the report after a restart name it
  with its millilitres, the last as an alarm, follow-up); one
  that failed with the pair complete is not resumed once EC control has
  started a round since, as the rest would go in on top of it; the card
  says so instead of offering it. A mix whose pump stood still while it
  mixed in hands nothing over: EC and pH settle first, as after every mix
  cut with nutrients in (SD-057), and EC control starts afresh, so its
  own no-effect lock applies then, not D6.
- **Temperature (D9):** built on the reading EC control uses today; the
  plan always says it is not temperature compensated and that cold fresh
  water reads low, so the tank gets more. When SD-060 is built, way 1
  follows its gate.
- **Large tanks (D10):** an EC round that would need more partial runs
  than allowed (6) is made smaller by one factor for all its scaled parts
  instead of refused (RAT-054 names partial-run limits among those a pair
  scales at); the round count and the 30-min pause bound the rest. This
  changes EC control for every round. A round made smaller that should
  raise EC by less than 0.06 mS/cm (three times the no-effect threshold)
  is not judged or learned on its own, so pumps far too slow for the
  tank do not look broken and stay usable (follow-up, PD-317). What a
  round should raise EC by is reckoned at the effect it was planned on,
  the one learned for its recipe or else the recipe's start, not at the
  largest known one, so a recipe that learned a small effect is not
  expected to do more and a working probe is not locked for it (follow-up after
  the fourth review, 2026-10-23; not chosen: the largest effect with a
  named residual risk, or 0.12 instead of 0.06). Clean rounds of one
  recipe that are too small are added up instead: once they should raise
  EC by 0.06 between them, their rises, each counted from its own
  round's start, are judged as one round against 0.02 and learned from
  the summed rise over the summed millilitres per litre, each round on
  its own litres; so a dry or misplaced probe, an empty bottle or a pump
  that delivers nothing still locks EC control with any pump, after less
  than about 0.24 mS/cm at the effect planned on (follow-up after the
  fourth review, 2026-10-23; not chosen: leaving it as a named residual
  risk, or counting small rounds singly again). A round that is not
  clean (the inlet, a refill, a hand dose, a mix) or has no valid reading
  at its settle end is left out, and the sum goes on, so a refill that
  comes back again and again cannot keep it from being judged. The sum
  starts afresh with a round of another recipe, a round large enough to
  judge alone, a calibration, the lock, a first step of a mix that is
  judged, an edit of the recipe's scaled parts or their ratio, the
  recipe deleted, left out of an import or replaced under its ID, and a
  higher reference that forgets what it learned; never only because a
  reading lies in the band, which a misplaced probe could bring about
  again and again. Remaining: a restart clears the sum, as it clears the
  count of rounds without effect (R6); a recipe whose effect lies below a
  quarter of the start effect falls back to its start (RAT-055), so its
  small rounds are expected at the start and can lock a working probe,
  as its rounds judged alone can; with slow pumps the rounds of a series
  hold watering for hours, as each run holds it until mixed in (SD-093).
- A first step that does not raise EC locks, and the rounds change at
  the run limit, so this step waits for the project owner's "mergen"
  (SD-033).

Not chosen: way 1 only, or the recipe's amounts as the default (D1); any
recipe, or EC control switching recipe for the mix (D2); the reading at
the start, or a stability check that can block (D3); the smaller of the
entered and level litres, or a refusal above 10 % (D4); the plain target
(D5); only the probe's verdict, or a stop between the parts of a pair
(D6); no learning, or learning only a larger effect (D7); keeping the job
open until EC control is done (D8); waiting for SD-060, or a tick per mix
(D9); a warning up front, or leaving large tanks short of the target
(D10).

## SD-103: A round that watering overlaps learns nothing and counts half towards "no effect"; a round learns over the volume when it started

Decided by the project owner on 2026-10-23 in the software session
(#301, from the `domain` review of SD-057 step 4), a control detail
under SD-091, each question as recommended: when a round counts as
watered and the volume it learns over; after the reviews of
`reviewer`, `qa` and `domain`, how such a round counts towards "no
effect" and that learning keeps a tail of its own; after SD-102 was
merged meanwhile and `security` and `domain` reviewed the fit, that a
sum of small rounds with a watered round in it counts half, as
recommended. Changes RAT-057 and
what RAT-041 and RAT-055 count; closes SD-092's "Not covered" point on
the volume and SD-101's residual risk on learning during a flood.

- **What it fixes:** a round of EC or pH control learned its effect
  over the volume read when it had settled, and a watering pump running
  meanwhile did not make the round unclean. A flood draws tank water
  out during the settle and the drain-back brings the table's water in,
  with its own EC and pH; the rise or fall the round sees is then not
  its dose's own. For EC the learned effect sticks to the recipe the
  round dosed until its next clean round (SD-092). Example from the
  review: 30 % of the tank drawn during the settle makes the effect come
  out about 30 % low, and the next round doses about 43 % more.
- **Watered:** a round, from its start until it has settled, counts as
  watered while the watering pump (`zone.irrigation_pump`) reads as on,
  while its state is not known (a plug offline runs its own schedule),
  and until 10 min after either, the time SD-101 gives the drain-back.
  The same 10 min run from the first tick the pump is bound, also after
  a restart, which may have cut a flood (R6). A watered round learns
  nothing, apart from the pH floor below. The 10 min were set for the level; for EC and pH they are an
  assumption, not measured (the table's water mixing in, its own EC and
  pH).
- **"No effect" counts it half:** a watered round without effect counts
  half towards the "no effect" latch (RAT-041, RAT-055), so it latches
  after 2 clean rounds without effect or 4 watered ones; a judged round
  with an effect resets the count. Not counting them at all would switch
  the latch off while watering lasts (drip shots, a plug offline), and
  counting them fully would latch on drain-back that hides a dose's
  effect (runoff from rockwool raising pH). Other disturbances (the
  inlet, a user job, a dose of the other controller, a recipe changed,
  a cut round) neither count nor reset, as before; most of them also
  hold back new rounds.
- **pH doses do not grow:** a pH dose without a measurable fall (under
  0.03), clean or watered, tells nothing about the effect; it sets the
  learned pH effect to at least its start value and never lowers a higher
  one, so from the next dose on the doses are at the start size or
  smaller up to the latch (`security`'s and `domain`'s findings, decided
  by the owner). Before, a clean dose without a measurable fall could
  learn a low effect and make the next dose several times bigger (up to
  16 times, bounded by the dose cap), an overshoot if the fault cleared
  on it. EC has the same gap; it needs
  values of its own (the start per recipe) and is a follow-up.
- **Over the volume at the start:** a clean round learns over the volume
  read when it started (EC) or when its dose started (pH): the volume
  the dose went into and the one it was planned on. With watering and
  the inlet excluded, the two differ only by what else leaves the tank,
  such as evaporation, and a constant error of the level curve cancels
  between planning and learning. When the level is not usable, the volume
  is the one entered or set by the last mix, as for planning
  (`Hub::tankVolume`), and its error cancels the same way.
- **Only learning and the count change:** the round still doses, settles
  and decides as before; watering itself waits for a dose to mix in
  (SD-093).
- **With SD-102:**
  - The first step of a mix to the target, EC control's round 1 (D7),
    learns nothing when watering overlaps it, by the same tail, from
    when its parts go in until the hand-over, and it learns over the
    litres it was planned on. It is still judged at once, at full weight
    (D6): a dry or misplaced probe reads about 0 whatever drains back,
    and the litres cannot move far while it runs (watering waits while
    it measures; more than 10 % ends it, D3, D4). Drain-back far below
    the tank's EC (a top-up after the substrate was flushed with plain
    water) can lock a working step, on the safe side; runoff above it
    can hide a part that delivered nothing, which the next rounds catch.
  - A round made smaller for slow pumps and too small to judge alone
    that watering made unclean still joins the sum, for counting only:
    the sum learns nothing and, once judged, counts half (1 point) if
    any round in it was watered, 2 if all were clean (decided by the
    owner after the reviews). Leaving such rounds out would switch the
    latch off for slow pumps while every round is watered (a plug
    offline, frequent drip shots): `security` and `domain` found that a
    dry probe could then let EC control dose without end. A round
    something else made unclean stays out, as SD-102 has it. A watered
    round too big for the sum counts half on its own and leaves the sum
    as it is.
  - Each small round's millilitres per litre are taken over the volume
    at its start, as its expected rise is.
- **Residual risks:**
  - a pump the hub does not see (not bound, a drain valve, a second
    pump, a shot shorter than the interval at which the hub reads the
    plug) makes no round watered;
  - pauses in watering shorter than about 15 min (10 min plus a round)
    leave no clean round, so nothing is learned while they last: the
    effect stays at its last value, or at the start value within the
    cap; once the system types exist (M4), the exclusion may be limited
    to systems where water drains back;
  - with every round watered (drip shots, a plug offline, someone
    switching the plug at the device or in the network), the latch needs
    4 rounds instead of 2: for EC up to twice as much goes in before a
    probe fault latches (4 × the largest EC step instead of 2, each round
    capped; with slow pumps 4 sums of small rounds instead of 2, about 8
    rounds of under 0.06 mS/cm each); for pH the first dose is as in the
    clean case and the doses after it stay at the start size or
    smaller: 2 more than clean, each
    at most 0.06 ml/L (about 0.3 pH at the start effect), so at most
    twice the clean case, and at most 0.42 ml/L instead of 0.30 ml/L at
    the lowest learned effect;
  - drain-back that fakes an effect (runoff with a higher EC, or a lower
    pH) resets the "no effect" count, so the latch may not trip while it
    does so at least every fourth watered round; this needs a probe that
    reads the tank, so it can hide an empty bottle, a blocked line or a
    failed pump, not a probe that misses the doses (watered rounds reset
    the count before too; the window grew from 2 rounds to 4);
    drain-back that hides an effect (runoff raising pH) can latch a
    working dose after 4 watered rounds, on the safe side, and a person
    releases it;
  - unbinding the watering pump lets the tail run out; binding it to
    another device starts it again;
  - a high learned pH effect gone stale (the tank became much more
    buffered, by a new mix with harder water) can no longer be learned
    lower from small doses: doses under about 0.024 ml/L fall less than
    0.03, so the "no effect" latch trips and the grower finds nothing
    wrong, until a correction starts at a larger gap; the safe side, a
    false alarm instead of an overshoot (learning after a release is a
    possible follow-up);
  - a drain-back slower than 10 min, or the start reading taken before
    returned water has mixed in (circulation in interval mode), can still
    reach a clean round, and its learned effect can come out too low: up
    to about 20 % the 0.8 aim absorbs it, beyond that the next dose
    overshoots, for pH beyond undoing (a longer tail for learning, or a
    mixing time before the start reading, is a follow-up, #337); the
    same holds for large tanks whose mixing time is longer than 10 min
    (the hub's own model gives 100 s per 20 L, about 17 min at 200 L);
  - the twin's watering draws no water, so the tests check only how
    rounds are counted; the volume test takes 8 L out at once during the
    settle, and the twin keeps the concentration (water taken out, not
    evaporated).
- **The texts:** the "no effect" event for EC, and the latch line and
  event for pH, say "after the last few rounds" (doses) instead of
  "after two", true for both counts; pH's name the pump among what to
  check (`ux`). EC's latch line keeps SD-102's "after dosing", which
  also fits a first step that locked.
- **Merging:** it changes when the "no effect" latch fires, a protective
  latch, so it needs `security` and the project owner's "mergen"
  (SD-033). Without a bound watering pump the latch behaves as before (2
  clean rounds).

---

## SD-104: After a restart cut a mix, the part that was running is named as possibly in, its amount unknown

Decided by the project owner on 2026-10-23 in the software session, for
#310 (a control detail under SD-091; "name the part, amount unknown",
as recommended). The wording, and that the report says to mix before
measuring, follow the reviews of `reviewer` and `domain`.

- **What the report says:** a mix cut by a restart while a part ran
  names that part: up to its planned amount of it may be in, how much is
  unknown; for a part resumed after a cut, between what was in already
  and the plan. The parts in before are listed as before, an ended part
  without its amount as "amount unknown", never as nothing (R5). A cut
  between two parts, or while the mix mixes in at its end, is said as
  before.
- **What to do next:** with EC or pH control on, the report says that
  they wait until the circulation pump has mixed the tank; without, it
  says to mix the tank first. Either way: measure EC and add only what is
  missing, as a second mix would dose every part again.
- **No amount is guessed and none is saved while a part runs:** the
  moment of the cut is not known, so an estimate from the run time could
  only overstate or understate what went in, and saving the progress
  during a run would write to the flash while it doses.
- **Afterwards** EC and pH settle first with the circulation pump, as
  after any mix cut with nutrients in (SD-057, SD-073, RAT-052): before,
  a restart during the first part counted as nothing in, and they dosed
  on the unmixed tank.
- **Not covered:** a dose by hand, a prime and the run of a calibration
  are not in `job.json` while they run, so a restart during one is not
  reported at all; nor is a restart in the moment after a part's pump
  started and before the mix saved it as running. Both are #329.
- **Residual risks:** "up to" includes nothing; the hub cannot tell
  whether the pump had started before the cut, and the grower measures.
  A part of a pair still to come is named as missing, as an alarm, once
  another part of the pair is in (SD-102); the running part itself is
  named with what may be in, not as missing too. Until #333, a pair
  whose only started part was running is not named as half in, and a
  part dosed in many runs has its whole planned amount as the upper
  bound.
- **Decided to follow** (by the project owner on 2026-10-23, #333): the
  restart report names each part of a pair started and not complete,
  a running one too, with what is missing, as an alarm; and the hub
  keeps the amount in `job.json` after each finished partial run of a
  mix part, one small write per minute of dosing, so the bound is at
  most one run wide.

Not chosen: an estimate from the run time; saving the amount during
the run.

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
