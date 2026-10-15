# Software roadmap

Status 2026-10-13, order decided by the project owner on 2026-10-12: the
freshness fix, then a polish block and the prototype 0.1.0-proto.5
(SD-055), then the ESP32-S3 before new Home Assistant features. The
ESP32-S3 on the carrier board runs control and safety on its own; Home
Assistant and the Home Assistant app are options (PD-145 to PD-174, #91,
#94, SD-045).

| Step | Content | Depends on |
|---|---|---|
| **M0 Prototype** (done) | Core, simulator, web app, tests, CI; Home Assistant app that reads sensors and switches nothing (releases 0.1.0-proto.1 to proto.4) | – |
| **Freshness by sign of life** (done) | A sensor is fresh by its last measurement: sign-of-life text or timestamp sensor per sensor, stale after 60 s, age in colour (SD-042, SD-047); open before control: jump lock for calm sensors from Home Assistant | – |
| **Polish and 0.1.0-proto.5** (next) | What the review of 2026-10-12 found visible: phone fixes, a message when Stop or a switch fails, developer words out, "Stopp" instead of "Not-Halt", chart axis and setup fixes, English complete (#18, #73), the menu grouped by topic with "manual" pages (SD-056), mixing to the phase's target EC in three ways (SD-057); then the release after the project owner's test on a real Home Assistant (SD-055) | – |
| **M1 ESP foundation** (board profiles done; the rest after proto.5) | In small steps: board profiles N32R16V and N8R8 with browser flashing (ESP Web Tools manifests); safety component apart from control (fixed-end jobs, dose end by timer, one pump at a time, budgets and pauses, release only on request, sign of life to the board watchdog, actuators off before flash writes); Wi-Fi over Improv, also on the board's USB port, and the setup network; the BOOT button for the setup network (3 s) and reading the factory data; a message while a computer is connected over USB, a restart by it counted as a power loss, JTAG cut at run time (SD-044, SD-076); LittleFS and history ring buffer; board v0.1 drivers (expanders, ADCs, pumps, refill valve, RS485/Modbus per port, contacts, light output, buzzer); EC compensated by the hub from the water temperature as the user chooses, and the pH zero-point check (SD-054, SD-060); the dosing pump type with speed stages, feedback and an ID chip in eight steps, simulator first (SD-066, #132); a step-by-step guide for flashing for end users (SD-049) | SD-043, SD-045; board samples for the hardware tests; for the pump type the product side's answers to SD-066's questions and pump samples |
| **M2 Secure by default** | Local HTTPS, signed OTA with own downgrade check and self-test, encrypted NVS, factory reset (BOOT held 10 s, or the web app) that keeps the factory data, a first password that fails closed after a lost store, update manifest from releases; the owner keeps their own firmware (PD-022, SD-021, SD-024) | SD-043 |
| **Pre-flashed devices** (with the series, PD-179) | Provisioning tool for the factory data and a label template, the flash page without a whole-chip erase (or only with a warning), the first password in a window, the support end in the web app, an EN 18031-1 and -2 self-assessment per firmware version (SD-076) | M2; product questions in SD-076 |
| **M3 Home Assistant over the ESPHome native API** | Read-only first: all values and states, the watchdog included; then commands as jobs through the gateway; the Home Assistant app opens the ESP's web app in Home Assistant's sidebar, after a `security` review of the design; access from outside in three levels chosen at setup (SD-059); the Home Assistant app stays an option (SD-043, SD-050, #84) | M1 |
| **M4 Grow functions in the core** | Grow type in the setup, automatic phase change with all parameters per phase, irrigation with drain from the drain tank's level (PD-094, PD-097, PD-098), humidity control, light schedule and dimming, CO2 (#42), heater (#24) | M1 safety component; product questions on grow types, drain and RDWC |
| **M5 Notifications** | Push without a manufacturer cloud (ntfy, email, webhook), morning report, alarm levels (RAT-022, RAT-045) | – |
| **M6 Archive and remote access** | Archive and comparison of grows; remote access through a relay the project runs and anyone can run themselves, free of charge, no port forwarding (SD-062), at the level the user chooses at setup (SD-059); a store app is decided later (SD-062) | M2 (local HTTPS); its own: the ESP's key and certificate through the relay, device cookies (SD-062) |
| Paused | Home Assistant app: level (SD-041 step 2) and EC (step 3) from raw values, combined history view (SD-039), stop menu (SD-040) | after M1 |
| ongoing | Languages (English default, German; PD-035), accessibility, fuzzing, long-run tests | – |

Open questions for the project owner are in `DECISIONS.md` (drafts marked
in the column "needs PD").
