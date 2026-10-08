# Concept: growcontroller as standalone software

As of 2026-10-07. **V** = proposal, **A** = assumption, [PD-xxx] = decided in
the product repository. Software decisions not decided by a PD are still
drafts (`docs/DECISIONS.md`).

## 1. Goal

Our own software on the hub, not built on a home-automation platform
[PD-013]:

- **Web app on the device**, without cloud and without account. It is
  reachable in the home network. A phone app comes later.
- **Setup instead of configuration.** Devices are detected, not
  selected. The hub says what works with the hardware at hand and what is
  missing.
- **Safety in the core.** Domain rules (see `docs/RATIONALE.md`) are code
  and tests, not dashboard discipline.
- **Run as a product.** Versions, changelog, updates with a way back, bug
  reports and traceable development.

## 2. What runs where (recommendation of `software`: option C) [PD-014]

```
Browser ──Wi-Fi── ESP32-S3 hub [control · safety · API · web UI · 1 year history]
                     │
                     ├── RS485/Modbus per port ── dosing block, sensor heads
                     └── optional: "companion" (Docker/NAS/HA add-on)
                                    for long-term archive, comparing cultivation runs, push relay
```

| Option | Cost in the hub | Assessment |
|---|---|---|
| A: everything on the ESP32-S3 | none (module N16R8 approx. $3.4–5.2, LCSC) | carries control, UI, history |
| B: Linux module (e.g. CM5) | approx. $85–98, more expensive in 2026 | boot time, file system, OS patching duty (CRA), target price at risk [PD-006] |
| **C: A + optional companion** | none | **recommended**: without the companion nothing safety-relevant is missing [PD-008]. There is no subscription; all functions are free [PD-024] |

Sources (research by `software`, retrieved 2026-10-06):

- https://www.lcsc.com/product-detail/WiFi-Modules_Espressif-Systems-ESP32-S3-WROOM-1-N16R8_C2913202.html
- https://www.raspberrypi.com/news/more-memory-driven-price-rises/
- https://www.theregister.com/2026/04/01/raspberry_pi_price_hikes/

## 3. One core, two platforms

The domain core is **platform-neutral C++17** (`core/`). The same code
runs:

- **in the simulator** (`sim/`): a host server with a digital twin.
  Development, tests and demos run on it, without hardware.
- **on the hub** (`firmware/`): ESP-IDF on the ESP32-S3. Only the HAL
  interfaces are replaced (`IBus`, `IStorage`, `IClock`, HTTP binding).

Why C++ and not Rust/MicroPython/ESPHome:

- **ESP-IDF is mature.** For Rust, esp-hal 1.0 exists since October 2025;
  the std crates have community support only. Re-evaluate later.
- **MicroPython** is out: garbage collection pauses, errors only at run
  time.
- **ESPHome** configures at compile time and has no configuration tree at
  run time. It remains a source of ideas, not the basis.

Source: `software`, retrieved 2026-10-06:
https://developer.espressif.com/blog/2025/10/esp-hal-1/

The **web app** (`web/`) is Preact + TypeScript, built with Vite. It talks
only to the REST API. Gzip-compressed it is **approx. 73 KB** today (budget
250 KB, checked in the build). It sits in the app image of the firmware, so
UI and API always match, and a rollback takes the UI along.

## 4. Layers and rules (proposal of `architekt`)

| # | Layer | Code | platform-neutral |
|---|---|---|---|
| 0 | HAL: UART/DE per port, eFuse, ADC, flash, Wi-Fi, clock | `IBus`, `IStorage`, `IClock` | no |
| 1–3 | bus, port enable, drivers, device registers | `bus.hpp`, simulator `simbus` | interface yes |
| 4 | sensor truth | `truth.*` | yes |
| 5 | configuration, roles, parameters, phases | `config.*`, `catalog.*` | yes |
| 6 | resolver ("What's missing?") | `resolver.*` | yes |
| 7 | functions and controllers | `mix.*`, `control.*` | yes |
| 8 | **actuator gateway**: the only path to actuators | `dosing.*` (`Actuators`) | yes |
| 9 | watchdog: only evaluates | `watchdog.*` | yes |
| 10 | history, event log | `history.*`, `events.*` | yes |
| 11 | API | `api.*` | yes (server binding no) |
| 12 | web app | `web/` | own build |

**Rules** (checked by machine in `tools/arch_check.sh` where possible):

- **R1:** Only the actuator gateway calls pumps and outputs. Manual doses
  and the emergency stop go through it too.
- **R2:** The watchdog sees only the read model and the configuration
  (`readmodel.hpp`). It has no path to actuators. Rationale: RAT-074.
- **R3:** Locks read the sensor truth, never the watchdog's assessment.
- **R4:** Functions get only effective parameters (`ParamView`), never
  phase names. Rationale: RAT-076.
- **R5:** A missing value is `nullopt`/`NaN`, in JSON `null`, never 0.
  Rationale: RAT-006.
- **R6:** After a restart everything is off. Sequences are not resumed but
  reported as interrupted. Latches and jump locks persist. Rationale:
  RAT-007, RAT-028, RAT-044, RAT-063. PD-020 replaces the part for state
  functions (fans, light, irrigation): fan sockets come back on after a
  power loss and keep their state (PD-050, SD-028); light and irrigation
  are open. An emergency stop survives a restart and keeps the fans off
  too until someone resumes (PD-076); an internal error keeps the fans
  running (PD-077).
- **R7:** The catalog can only tighten safety. The minimum lives in the
  gateway: calibration value, run time limits, one run at a time,
  emergency stop, dry run, emergency limit of the inlet.
- **R8:** One event loop with an injected clock. This makes tests
  deterministic and time-lapse in the simulator possible.

## 5. What the prototype can do today

- **Stage 0:**
  - Detect and adopt devices.
  - Bottles with pairs (A:B), recipes with an order, templates.
  - Calibrate pumps; the value is stored in the ID chip [PD-010].
  - Guided mixing: "new" or "top up", with stirring instructions or a
    circulation pump. Pair errors with "catch up", protection against a
    double start.
  - Manual dose with a limit, stock per bottle.
- **Stage 1:**
  - pH/EC head with calibration: pH with 2 buffers, EC with 1 reference.
  - Sensor truth: freshness, frozen values, plausibility, jump lock,
    calibration.
  - EC top-up and pH control in partial doses based on the measured
    effect, with EC gate, settling time and learning only from clean
    doses.
  - Circulation by interval or on demand.
- **Stage 2:**
  - Level with a piecewise linear curve (RAT-078).
  - Refill by a calculated volume; the sensor is the emergency cut-off.
  - Dry-run protection with latch, assessment of the water temperature.
- **Everywhere:**
  - Watchdog: OK, problem or neutral, always with a reason.
  - Control line with checklist ("Why is it (not) dosing right now?").
  - Emergency stop, maintenance mode, cultivation run with phases as
    parameter sets, harvest as an event.
  - History in 3 tiers, event log, CSV export, backup and import of the
    configuration.
  - Login with a mandatory password and lockout after failed attempts.
  - Update view with "What's new" (a mock-up in the simulator).
  - Diagnostic package for "Report a problem".

## 6. What is deliberately still missing

- **ESP-IDF port:** Modbus per port, port enable, NVS/flash ring buffer,
  OTA. Plan in `firmware/README.md`.
- **HTTPS in the home network** with a certificate per device, and signed
  OTA (`docs/SECURITY_MODEL.md`). Signed updates must still let the owner
  install their own firmware (PD-022).
- **Notifications without a vendor cloud** (ntfy, email, webhook), morning
  report.
- **MQTT with HA discovery** (read-only plus a few commands).
- **Multiple tanks:** The data model is already a list; UI and logic use
  one tank.
- **Stage 3–4:** irrigation, drain, climate. Heating only via an external
  socket with auto-off.
- **Languages and units:** The web app speaks English and German. Still
  missing: English as the default, and a unit system chosen separately
  from the language (PD-035, PD-027).
