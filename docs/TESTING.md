# Test strategy

As of 2026-10-11. Goal: every domain rule (see `docs/RATIONALE.md`) has a
test. Every visible function runs end to end in the browser at least once.

## Levels

| Level | Tool | Location | Count today | Checks |
|---|---|---|---|---|
| Architecture rules | shell/grep | `tools/arch_check.sh` | 7 rules | no sdkconfig option that burns eFuses (PD-022, SD-024), core without platform headers, actuators only through the gateway, watchdog without an actuator path, no phase names in the logic, no `value_or(0)`, values calibrated elsewhere only in the history (`forHistory()`, SD-039) |
| Unit | doctest (C++) | `tests/core/test_*.cpp` | 233 cases | catalog, configuration and migration, fixed limits (R7), phase parameters, sensor truth, curve, history, events, their journals (snapshot plus journal, torn end, a torn record followed by a whole one, only known series and plausible times, never counted twice), mix planning, resolver, watchdog, SHA-256/PBKDF2, login, sockets and gateway (`test_net`), room climate and VPD (`test_climate`), hub messages with keys and arguments (`test_messages`), Home Assistant spike: states, units, timestamps, freshness learned per sensor and the history of values calibrated there, other sensors picked only in a unit that fits, a pH probe's voltage suggested (not above 5 V, not ORP), calibrated in the app with the slope band, the reason for a refusal and a point from the next report (not one from before, its limit learned, failing when the sensor goes away), the standstill check by its reports, no value after a unit change, a sign of life per sensor (the text's format, its channel ages, 60 s after the last measurement, a silent device, a channel without a measurement since the start, a timestamp sensor, nothing looser when it fails, a sign of life that does not fit the sensor, the same text dated again, a time first ahead then behind, listed only on request, the standstill check and a calibration point by the last measurement, set only from what Home Assistant offers, in the configuration and checked on import), a calibration ending by itself and with its device, an import that cannot turn a sensor calibrated elsewhere into one the hub checks, read-only, finding and picking sensors in one step, poller against a stand-in Home Assistant, also below a path as in an add-on, the address checked as a whole (host, port, path) (`test_ha`), storage on an SD card: password, configuration and STOP on disk when the call returns, bytes written while idle, history and events after a hard stop and after a day in one go, failed logins cannot grow the event journal, setup when the password file cannot be written, a failed snapshot keeps its journal (also at the end of a fast-forward), one alarm while a file fails and the change written once there is space, a failed journal append repaired by a snapshot, a failed password change keeps the session, a snapshot that does not fit leaves no temporary file and a STOP still reaches the disk, a simulator in memory only keeps everything through a fast-forward and a power cut, a clean shutdown right after a full card keeps the history, a new scenario without the old journals (`test_storage`) |
| API contract | doctest against the core | `tests/core/test_api.cpp` | 23 cases | access, error shapes, fields the web app reads, no secrets, origin check, import validation, lost password, the choice per area (SD-037; also through export and import) |
| Scenario | doctest + twin | `tests/core/test_scenarios.cpp` | 20 cases | stage 0 set up by hand, calibration, amounts and A:B ±3 %, pair fault with catch-up, power cut, mis-plug, control to target, EC gate, jump lock, dry run, inlet emergency cut-off, emergency stop, abort books consumption, cap pulled off, silent block, job IDs after a restart, calibration result and emergency stop as messages |
| CI and release scripts | Node | `tools/ci_changes.test.mjs`, `tools/workflows.test.mjs`, `tools/sbom_cpp.test.mjs`, `tools/app.test.mjs`, `tools/version.test.mjs` | 27 tests | docs-only changes skip the heavy CI jobs, everything else runs them; `ci-ok` waits for every job (SD-030); actions are GitHub's own and pinned by commit SHA; the C++ SBOM matches `cmake/deps.cmake` (#29); the Home Assistant app installs the image of this release, asks for nothing but Home Assistant's API and one port, is the only app config in the repository, matches `--app` and the staged Dockerfile; its image is pushed only by the publishing job, with the same steps the dry run rehearses; steps fail on a failing command in a pipe (SD-034); tools come pinned (`reuse` by hashes, images by digest, #29); every place of the version names `VERSION` |
| Home Assistant server | Node against the built server | `tools/ha_server.test.mjs` | 5 tests | `--app` refuses a mapping and a token file, takes the token only from `SUPERVISOR_TOKEN` and writes nothing without it, is not switched on by an option's value, honours `--data`, `--host`, `--web` and `--port`, stops cleanly, private files; values a stand-in Home Assistant reports with a `Date` header reach the readings: pH 6.2, an unavailable sensor stays missing, 5 µS/cm becomes 0.005 mS/cm |
| Home Assistant app image | Docker | `.github/workflows/app.yml` | 2 architectures | built on `amd64` and `aarch64` runners; labels and size; refused without a token or with `--config`; started like the Supervisor starts it next to a stand-in for the Supervisor reached by name: it answers, serves the web app, refuses a foreign host name, survives long paths, asks only `GET /core/api/states` with the token; stops cleanly on SIGTERM; private data folder; license texts and source code link in the image; the core tests built and run with the image's toolchain (musl, its GCC, its linker flags), and the image's own server through `tools/ha_server.test.mjs` (#112) |
| Text keys | Node | `tools/i18n_keys.test.mjs` | 7 tests | no key in two language files, every area file has the same keys and placeholders in German and English, the hub's messages have a German text with the same placeholders and every `say()` names a key of the table (SD-032), keys the reader cannot parse fail instead of being skipped (#18) |
| Licenses | reuse, Node | `tools/ci.sh` | REUSE 3.3, npm, 12 tests | SPDX information for every file, allowed licenses of all npm packages, runtime packages listed in `THIRD_PARTY_NOTICES.md`, every package the web app imports listed as a runtime dependency (`tools/check_licenses.test.mjs`) |
| Memory errors | AddressSanitizer + UBSan | `GC_SANITIZE=ON` | all C++ tests | overflows, use-after-free, undefined behaviour |
| Web | TypeScript strict, size budget, no build tool code | `npm run build` | – | types, ≤ 250 KB gzip, no code of Vite in the built app (`web/scripts/no-tool-code.mjs`) |
| End to end | Playwright + Chromium | `web/e2e/*.spec.ts` | 45 cases | first-time setup up to the first mix run, English switch, monitoring and control lines in both languages, English pages (sign-in, recipes with a decimal point, settings, overview, mixing, tank, functions, history, devices, simulator panel), numbers in the page language, emergency stop, jump lock visible, functions and history, mis-plug, diagnostic bundle with an English issue link, recipe template (also from an English page on a German hub), tiles on sensor failure, climate areas with VPD, socket adoption and test, access protection, security headers, damaged event messages leave the page and STOP usable, a part that fails to draw shows a notice, closing a failed calibration run cancels it, every case fails on an uncaught exception in the page or a part the error boundary replaced, a probe calibration closed half way ends on the hub and starts again at the first buffer, a value calibrated outside the hub shows with its trend, marked as calibrated elsewhere (tile and setup step), a read-only hub opens a short introduction instead of the setup, also by its address (name, colour scheme kept after a reload, sensors, the choice per area saved, area pages without switches or notes about switching and with the choice changed there; skipping it chooses nothing, Settings opens it again) and shows no emergency stop (a stop from before can be cleared), a hub of its own has no such introduction, the overview points to new devices, picking a Home Assistant sensor per measurement (one sensor, one measurement; a pH probe's voltage first, as a raw value calibrated in the app, calibrated from the assignment with the wait for the next reading and the message when none comes; a sign of life picked per sensor (the list only on request, the own device marked, the row on a phone), the age then by the measurement in colour and words at 30/31/60 s, "unverändert seit" without one; other sensors with a search and the reason one does not fit, also without any suggestion; only history curves that can have values; units it cannot convert; a long sensor name on a phone; assigned sensors still shown when the list does not come; "renamed?" only while Home Assistant answers), telling a refused token or no answer from "no sensors", source code link (AGPL §13) |

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
