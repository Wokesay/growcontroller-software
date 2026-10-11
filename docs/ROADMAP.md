# Software roadmap

Status 2026-10-11, order decided by the project owner: the freshness fix,
then the ESP32-S3 before new Home Assistant features. The ESP32-S3 on the
carrier board runs control and safety on its own; Home Assistant and the
Home Assistant app are options (PD-145 to PD-174, #91, #94, SD-045).

| Step | Content | Depends on |
|---|---|---|
| **M0 Prototype** (done) | Core, simulator, web app, tests, CI; Home Assistant app that reads sensors and switches nothing (releases 0.1.0-proto.1 to proto.4) | – |
| **Freshness by sign of life** (next) | A sensor is fresh by its last measurement: sign-of-life text or timestamp sensor per sensor, stale after 60 s, age in colour (SD-042) | – |
| **M1 ESP foundation** | In small steps: board profiles N32R16V and N8R8 with browser flashing (ESP Web Tools manifests); safety component apart from control (fixed-end jobs, dose end by timer, one pump at a time, budgets and pauses, release only on request, sign of life to the board watchdog, actuators off before flash writes); Wi-Fi over Improv and the setup network (SD-044); LittleFS and history ring buffer; board v0.1 drivers (expanders, ADCs, pumps, refill valve, RS485/Modbus per port, contacts, light output, buzzer); "Stop" instead of "emergency stop" in all texts (#94) | SD-043, SD-045; board samples for the hardware tests |
| **M2 Secure by default** | Local HTTPS, signed OTA with own downgrade check and self-test, encrypted NVS, factory reset, update manifest from releases; the owner keeps their own firmware (PD-022, SD-021, SD-024) | SD-043 |
| **M3 Home Assistant over the ESPHome native API** | Read-only first, then commands as jobs through the gateway; the Home Assistant app stays an option (SD-043, #84) | M1 |
| **M4 Grow functions in the core** | Grow type in the setup, automatic phase change with all parameters per phase, irrigation with drain from the drain tank's level (PD-094, PD-097, PD-098), humidity control, light schedule and dimming, CO2 (#42), heater (#24) | M1 safety component; product questions on grow types, drain and RDWC |
| **M5 Notifications** | Push without a manufacturer cloud (ntfy, email, webhook), morning report, alarm levels (RAT-022, RAT-045) | – |
| **M6 Archive and remote access** | Archive and comparison of grows, remote access over paths the user chooses (PD-024, PD-150; the way is a product question) | – |
| Paused | Home Assistant app: level (SD-041 step 2) and EC (step 3) from raw values, combined history view (SD-039), stop menu (SD-040) | after M1 |
| ongoing | Languages (English default, German; PD-035), accessibility, fuzzing, long-run tests | – |

Open questions for the project owner are in `DECISIONS.md` (drafts marked
in the column "needs PD").
