# Hub firmware (ESP32-S3, ESP-IDF)

Status 2026-10-06: **skeleton.** It builds the unchanged core from `core/`
together with the embedded web app into an image for the
ESP32-S3-WROOM-1-N16R8. CI builds it on every PR (job "firmware") and so
proves that the core and the web app compile for the target chip and fit
into the image: first green run on 2026-10-06, **image 1.24 MB** (41 % of
the 3 MB app partition), static DIRAM 19 %. The image is kept for 14 days
as the artifact `firmware-esp32s3` of the run. It has not been started on
real hardware yet. No bus yet: Modbus, port enable and the dosing block
follow in milestone M1.

## Building

```bash
(cd ../web && npm ci && npm run build) && node ../tools/embed_web.mjs
idf.py set-target esp32s3
idf.py menuconfig        # growcontroller › Wi-Fi (development only), output GPIOs
idf.py build flash monitor
```

Tested with ESP-IDF v5.4 (CI: container `espressif/idf:v5.4.1`). On the
breadboard with the N8R8 (8 MB flash): shrink `partitions.csv` (history
3 MB).

## What the skeleton already does

| Part | Status |
|---|---|
| Core (`components/gc_core`) | the same sources as in the simulator; catalog and changelog embedded |
| Web app | gzipped in the app image (`tools/embed_web.mjs`), falls back to `index.html` |
| API | `esp_http_server`, all methods on `/api/*`, cookie and bearer sessions, security headers, 64 KB size limit |
| Live data | no SSE: the web app polls every 3 s (built-in fallback) |
| Storage | SPIFFS at `/data`, writes via tmp + rename |
| Outputs | two GPIOs for the 12 V outputs, off at boot |
| Clock | `esp_timer` (monotonic), SNTP |
| Updates | A/B partitions, app rollback enabled; marks the new version as valid after boot |

## Plan up to M1 (breadboard P0)

1. **Tasks** (proposal by the `firmware` agent of the product repository):

   | Task | Core | Period | Content |
   |---|---|---|---|
   | `actuator_guard` | 1, highest app priority | 50–100 ms | the only path to actuators, heartbeat to the block, task watchdog |
   | `bus` | 1 | heads 1 s, block 200 ms | port scheduler, Modbus |
   | `gc_core` | 1 | 200 ms–1 s | sensor truth, controllers, resolver |
   | `watchdog_eval` | 0 | 5–60 s | evaluation on a snapshot |
   | `persist` | 0 | on event | storage, history |
   | `httpd` | 0 | – | web server, only a queue to the core |
2. **Bus per port** [PD-012]:
   - UART with DE/RE via the decoder.
   - Check measurement of the port ID via ADC and mux; the firmware's
     consent is the AND input of the hardware enable.
   - Modbus master with retries for reads only. Writes are idempotent via
     the job ID.
3. **Dosing block protocol** (the register map comes from the product
   repository as an issue, PD-100):
   - Run in ms with job ID, actual run time, result of the current check.
   - Heartbeat from the hub; if it stops, the block stops.
   - Read and write the calibration value in the ID chip (PD-010).
4. **Storage:**
   - LittleFS for configuration and state.
   - History as a ring buffer in the partition `history` (`docs/HISTORY.md`).
5. **Live channel:** SSE via asynchronous handlers or WebSocket
   (`CONFIG_HTTPD_WS_SUPPORT`).
6. **Security before the first device goes to someone else**
   (`docs/SECURITY_MODEL.md`): HTTPS, signed OTA with self-test, setup
   access point via a button. Signed updates must still let the owner
   install their own firmware (PD-022).

Acceptance on the breadboard follows `docs/prototyp/README.md` §5 in the
product repository (P0, acceptance tests 1–8).
