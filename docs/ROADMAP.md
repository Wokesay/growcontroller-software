# Software roadmap

Status 2026-10-06. The order is a proposal; effort is a rough estimate by
`software` (full-time weeks; part-time about ×2–3).

| Milestone | Content | Depends on |
|---|---|---|
| **M0 Prototype** (this state) | Core, simulator, web app, tests, CI, docs | – |
| **M1 Firmware on breadboard P0** | ESP-IDF project: Modbus per port with DE/RE, port enable, dosing block protocol with job ID and heartbeat, NVS/LittleFS, flash ring buffer, esp_http_server with SSE; acceptance according to prototype package P0 | Register map (`hardware`, `firmware`), breadboard |
| **M2 Secure by default** | Local HTTPS, signed OTA A/B with self-test, factory reset, setup access point by button, update manifest from releases; signing never locks out the owner's own firmware (PD-022) | E12, E13 |
| **M3 Notifications** | Notifications without a manufacturer cloud (ntfy, email, webhook), morning report, alarm hygiene (Rationale: RAT-022, RAT-045) | – (draft E11 dropped, PD-024) |
| **M4 Integration** | MQTT with Home Assistant discovery (read-only + stop), tokens for integrations, OpenAPI | E1 |
| **M5 Stage 3–4** | Irrigation by number of doses, drain, climate assessment, heating only external with auto-off | Sensor heads, load model |
| **M6 Companion** | Long-term archive, comparison of cultivation runs, push and remote access over paths the user chooses, no subscription (PD-024) | E2, PD-024 |
| ongoing | Languages (English default, German; PD-035), accessibility, fuzzing, long-run tests | – |

Open questions for the project owner are in `DECISIONS.md` (drafts marked
in the column "needs PD").
