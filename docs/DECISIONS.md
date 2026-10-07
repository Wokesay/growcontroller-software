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
  (PD-036). Versions are self-describing (PD-037). SemVer and OTA stay a
  draft.

They are recorded below as SD-001 to SD-024. The other drafts are the
basis of the prototype and become SD entries once the project owner says
"entschieden" (decided). What needs a PD is marked.

## Log

All entries below were recorded on 2026-10-07 when the log started. Each
names what applies to the software and the public reason; the full
decision is in the product repository.


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
  everything stays off after a restart. Limits are open.


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

- Updates over the network are installed only with a valid signature,
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
- Together with it: USB-JTAG off at run time, a recessed BOOT button,
  sessions stored only as hashes.

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
| E12 | Versions and updates | SemVer, Keep a Changelog with a "Security" section, self-describing versions without code names (PD-037), channels Stable, Beta and Development (PD-036, instead of stable/beta), GitHub Releases with SBOM and checksums, OTA A/B signed (the owner can still install their own firmware, PD-022), update only when idle, sign offline | channels: decided differently by PD-036; rest: no |
| E13 | Access | Mandatory password before any function, PBKDF2, lockout after failed attempts; local HTTPS before the first device at third parties | no (required by EN 18031) |
| E14 | Bug reports | technical issues via GitHub with forms; in the app "Report a problem" with a case number and diagnostic package, without telemetry | no |
| E15 | Roles and flow | agents `triage`, `reviewer`, `qa`, `security`, `release`, `ux`, `domain`; flow in `WORKFLOW.md`; issue workflows read-only | no |

Reasoning, options and sources: `CONCEPT.md`, `CONFIGURATION.md`,
`RELEASE.md`, `SECURITY_MODEL.md`, `WORKFLOW.md`.
