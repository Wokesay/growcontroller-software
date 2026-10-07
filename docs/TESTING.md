# Test strategy

As of 2026-10-07. Goal: every domain rule (see `docs/RATIONALE.md`) has a
test. Every visible function runs end to end in the browser at least once.

## Levels

| Level | Tool | Location | Count today | Checks |
|---|---|---|---|---|
| Architecture rules | shell/grep | `tools/arch_check.sh` | 6 rules | no sdkconfig option that burns eFuses (PD-022, SD-024), core without platform headers, actuators only through the gateway, watchdog without an actuator path, no phase names in the logic, no `value_or(0)` |
| Unit | doctest (C++) | `tests/core/test_*.cpp` | 77 cases | catalog, configuration and migration, fixed limits (R7), phase parameters, sensor truth, curve, history, events, mix planning, resolver, watchdog, SHA-256/PBKDF2, login, sockets and gateway (`test_net`), room climate and VPD (`test_climate`) |
| API contract | doctest against the core | `tests/core/test_api.cpp` | 17 cases | access, error shapes, fields the web app reads, no secrets, origin check, import validation, lost password |
| Scenario | doctest + twin | `tests/core/test_scenarios.cpp` | 19 cases | stage 0 set up by hand, calibration, amounts and A:B ±3 %, pair fault with catch-up, power cut, mis-plug, control to target, EC gate, jump lock, dry run, inlet emergency cut-off, emergency stop, abort books consumption, cap pulled off, silent block, job IDs after a restart |
| Licenses | reuse, Node | `tools/ci.sh` | REUSE 3.3, npm, 3 parser tests | SPDX information for every file, allowed licenses of all npm packages, runtime packages listed in `THIRD_PARTY_NOTICES.md` (`tools/check_licenses.test.mjs`) |
| Memory errors | AddressSanitizer + UBSan | `GC_SANITIZE=ON` | all C++ tests | overflows, use-after-free, undefined behaviour |
| Web | TypeScript strict, size budget | `npm run build` | – | types, ≤ 250 KB gzip |
| End to end | Playwright + Chromium | `web/e2e/*.spec.ts` | 15 cases | first-time setup up to the first mix run, English switch, emergency stop, jump lock visible, functions and history, mis-plug, diagnostic bundle, recipe template, tiles on sensor failure, climate areas with VPD, socket adoption and test, access protection, security headers, source code link (AGPL §13) |

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
`main` that changes code. It runs exactly these steps plus E2E, the
firmware build and the simulator packages for Windows, macOS and Linux. Docs-only changes
run only the quick checks (`.github/workflows/checks.yml`: architecture
rules, `reuse lint`, dependency licenses; PD-059).

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
