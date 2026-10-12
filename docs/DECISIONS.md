# Software decisions (SD)

Binding decisions for the software. Format `## SD-XXX: Title`, sequential,
chronological, two blank lines between entries. No status field; entries
are not deleted, a later SD replaces an earlier one and refers to it.

Product decisions are in the product repository (PD). If a PD affects the
software, an SD "Product requirement (from PD-xxx)" stands here.

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
  written, so PD numbers do not collide.
- Product requirements arrive as English issues in this repository. A
  requirement that comes from a PD is recorded here as "Product
  requirement (from PD-xxx)".
- A product question met here is named in the issue and to the project
  owner, not decided here; it is decided and recorded in the product
  repository's session.
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
  a follow-up.
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
  message per device in the watchdog is a follow-up.
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


## SD-059: Access from outside the home network in three levels, chosen at setup

Recorded as SD-051 in #123; renumbered on 2026-10-12, because #122 recorded
the sign-in lockout as SD-051 at the same time and its code and tests cite
that number.

Decided by the project owner on 2026-10-12 in the software session, as a
question the software solves; the rules for building it come from the
`security` review of this entry. PD-150 left the way of remote access
open; if the product session decides otherwise, its decision applies and
a later SD records it (as with SD-033).

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
  network; they count separately (when this was decided the lockout was
  one counter; since SD-051 it counts per client, `core/src/auth.cpp`).
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
     nothing, with a note.
  2. **A fixed pattern** (no valid EC in the water being mixed: no probe,
     or no circulation): the user enters the litres and either the
     recipe's amounts per litre, or a target EC within the catalog's range
     together with the EC of the water, taken from a meter or entered as
     0 for reverse-osmosis water. Without the water's EC only the recipe's
     amounts are offered; it is never taken as 0 (R5, RAT-006, review).
     The hub works out the amounts from the recipe's ratio and reference,
     runs the pattern without feedback and says so.
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
    its reference.
  - A part marked as a fixed addition (a cleanser, for example) is dosed
    per litre of fresh water, also when the water is already at the
    target, and never scaled by an EC correction; its expected EC counts
    in the first step's gap.
  - Water above the charts' 0.4 mS/cm gets a note on a new fill.
  - The mix plan says when EC runs without temperature compensation
    (SD-054); cold tap water then reads low and the tank gets too much.
- **Open:** mixing outside the tank (a bucket for soaking) and whether the
  pumps then dose into it, given that consumption goes only into the tank
  (RAT-040, RAT-070) and rounds stay clean (RAT-057); large tanks, where
  a correction round may need more partial runs than allowed (RAT-054);
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
  soon not read.
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
  and fails closed. Later, if npm offers it: fail when a package that had
  provenance publishes a version without it (`docs/SECURITY_MODEL.md`).
- The check needs the npm registry and Sigstore's TUF repository
  (`tuf-repo-cdn.sigstore.dev`), also for a local `tools/ci.sh`.


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
