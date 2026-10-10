# Test strategy

As of 2026-10-07. Goal: every domain rule (see `docs/RATIONALE.md`) has a
test. Every visible function runs end to end in the browser at least once.

## Levels

| Level | Tool | Location | Count today | Checks |
|---|---|---|---|---|
| Architecture rules | shell/grep | `tools/arch_check.sh` | 6 rules | no sdkconfig option that burns eFuses (PD-022, SD-024), core without platform headers, actuators only through the gateway, watchdog without an actuator path, no phase names in the logic, no `value_or(0)` |
| Unit | doctest (C++) | `tests/core/test_*.cpp` | 194 cases | catalog, configuration and migration, fixed limits (R7), phase parameters, sensor truth, curve, history, events, their journals (snapshot plus journal, torn end, a torn record followed by a whole one, only known series and plausible times, never counted twice), mix planning, resolver, watchdog, SHA-256/PBKDF2, login, sockets and gateway (`test_net`), room climate and VPD (`test_climate`), hub messages with keys and arguments (`test_messages`), Home Assistant spike: states, units, timestamps, read-only, finding and picking sensors in one step, poller against a stand-in Home Assistant, also below a path as in an add-on, the address checked as a whole (host, port, path) (`test_ha`), storage on an SD card: password, configuration and STOP on disk when the call returns, bytes written while idle, history and events after a hard stop and after a day in one go, failed logins cannot grow the event journal, setup when the password file cannot be written, a failed snapshot keeps its journal (also at the end of a fast-forward), one alarm while a file fails and the change written once there is space, a failed journal append repaired by a snapshot, a failed password change keeps the session, a snapshot that does not fit leaves no temporary file and a STOP still reaches the disk, a simulator in memory only keeps everything through a fast-forward and a power cut, a clean shutdown right after a full card keeps the history, a new scenario without the old journals (`test_storage`) |
| API contract | doctest against the core | `tests/core/test_api.cpp` | 21 cases | access, error shapes, fields the web app reads, no secrets, origin check, import validation, lost password |
| Scenario | doctest + twin | `tests/core/test_scenarios.cpp` | 20 cases | stage 0 set up by hand, calibration, amounts and A:B ±3 %, pair fault with catch-up, power cut, mis-plug, control to target, EC gate, jump lock, dry run, inlet emergency cut-off, emergency stop, abort books consumption, cap pulled off, silent block, job IDs after a restart, calibration result and emergency stop as messages |
| CI and release scripts | Node | `tools/ci_changes.test.mjs`, `tools/workflows.test.mjs`, `tools/sbom_cpp.test.mjs`, `tools/app.test.mjs` | 20 tests | docs-only changes skip the heavy CI jobs, everything else runs them; `ci-ok` waits for every job (SD-030); actions are GitHub's own and pinned by commit SHA; the C++ SBOM matches `cmake/deps.cmake` (#29); the Home Assistant app installs the image of this release, asks for no right beyond reading Home Assistant and one port, matches `--app` and the Dockerfile, and its image is pushed only by the publishing job (SD-034) |
| Home Assistant app image | Docker | `.github/workflows/app.yml` | 2 architectures | built on `amd64` and `aarch64` runners; labels the Supervisor reads; started as the Supervisor starts it, it answers, serves the web app and refuses a foreign host name; stops cleanly on SIGTERM; its data folder is private |
| Text keys | Node | `tools/i18n_keys.test.mjs` | 7 tests | no key in two language files, every area file has the same keys and placeholders in German and English, the hub's messages have a German text with the same placeholders and every `say()` names a key of the table (SD-032), keys the reader cannot parse fail instead of being skipped (#18) |
| Licenses | reuse, Node | `tools/ci.sh` | REUSE 3.3, npm, 3 parser tests | SPDX information for every file, allowed licenses of all npm packages, runtime packages listed in `THIRD_PARTY_NOTICES.md` (`tools/check_licenses.test.mjs`) |
| Memory errors | AddressSanitizer + UBSan | `GC_SANITIZE=ON` | all C++ tests | overflows, use-after-free, undefined behaviour |
| Web | TypeScript strict, size budget | `npm run build` | – | types, ≤ 250 KB gzip |
| End to end | Playwright + Chromium | `web/e2e/*.spec.ts` | 35 cases | first-time setup up to the first mix run, English switch, monitoring and control lines in both languages, English pages (sign-in, recipes with a decimal point, settings, overview, mixing, tank, functions, history, devices, simulator panel), numbers in the page language, emergency stop, jump lock visible, functions and history, mis-plug, diagnostic bundle with an English issue link, recipe template (also from an English page on a German hub), tiles on sensor failure, climate areas with VPD, socket adoption and test, access protection, security headers, damaged event messages leave the page and STOP usable, a part that fails to draw shows a notice, closing a failed calibration run cancels it, a value calibrated outside the hub shows for display only (tile and setup step), a read-only hub does not force the setup, the overview points to new devices, picking a Home Assistant sensor per measurement (one sensor, one measurement; units it cannot convert; a long sensor name on a phone; assigned sensors still shown when the list does not come; "renamed?" only while Home Assistant answers), telling a refused token or no answer from "no sensors", source code link (AGPL §13) |

The test cases for the control logic follow the list by `firmware`
(M1-1 … M15-3). `INVARIANTS.md` shows which test covers which rule and
what is still open.

## Running

```bash
tools/ci.sh          # architecture, licenses (needs reuse), core with sanitizers, all C++ tests, web build
E2E=1 tools/ci.sh    # plus Playwright (browser: npx playwright install chromium)
./build/gc_tests -tc="*Sprungsperre*"   # single cases
```

The CI (`.github/workflows/ci.yml`) runs on every PR and every push to
`main`. For a change to code it runs exactly these steps plus E2E, the
firmware build, the simulator packages for Windows, macOS and Linux and
the Home Assistant app image.
For a docs-only change its first job `changes` (`tools/ci_changes.mjs`)
skips these heavy jobs, and only the quick checks run
(`.github/workflows/checks.yml`: architecture rules, the CI and release
script tests including action pins and the C++ SBOM, `reuse lint`,
dependency licenses; PD-059). The last job `ci-ok` fails if
any job failed, was cancelled or was skipped for a change to code. The
ruleset on `main` requires `quick` and `ci-ok` (SD-027, SD-030).

## Rules

- **A bug that is found gets a test first** that shows it. Only then
  comes the fix.

  This already caught, on the first day:
  - the password was not saved;
  - a calibration value took effect only one tick later;
  - the hub's own mix runs triggered the jump lock;
  - a caught-up run carried the same job ID.
- **No test is skipped or disabled** to get green.
- **Time is injected:** scenarios run in simulated time (hours in
  seconds), deterministically.
- **Safety-relevant changes** (gateway, sensor truth, login, updates) need
  a scenario test and a review by `security`.

## Still open

- Fuzzing of the API inputs (JSON, paths) and of the configuration file.
- Long run: 30 days of simulated time, memory and event limits.
- Hardware-in-the-loop on breadboard P0 once `firmware/` runs: acceptance
  according to `docs/prototyp/README.md` in the product repository.
- Accessibility (axe) and mobile layout as E2E tests.
- Load test of the web server on the ESP32: control must not suffer (RLM,
  EN 18031).
