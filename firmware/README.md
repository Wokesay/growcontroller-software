# Hub firmware (ESP32-S3, ESP-IDF)

Status 2026-10-12: **skeleton.** It builds the unchanged core from `core/`
together with the embedded web app into an image for two boards
(SD-043, PD-162):

| Board | Module | Flash | PSRAM | Profile |
|---|---|---|---|---|
| ESP32-S3-DevKitC-1-N32R16V (main board) | ESP32-S3-WROOM-2-N32R16V | 32 MB octal | 16 MB octal | `boards/n32r16v.*` |
| ESP32-S3-DevKitC-1-N8R8 (fallback) | ESP32-S3-WROOM-1-N8R8 | 8 MB quad | 8 MB octal | `boards/n8r8.*` |

CI builds both on every PR (job "firmware") and so proves that the core
and the web app compile for the target chip and fit into the image (first
green run on 2026-10-06: image 1.24 MB, static DIRAM 19 %). Per board it
keeps for 14 days the artifact `firmware-<board>`
(`tools/flash_manifest.mjs`): the images ESP-IDF flashes (bootloader,
partition table, OTA data, app), `flash_args` for esptool, the manifest
for flashing from the browser with ESP Web Tools, and their SHA-256
checksums. Each image goes to its own offset, so NVS and the data
partitions stay as they are. It has not been started on real hardware yet.
No bus yet: Modbus, port enable and the board v0.1 drivers follow in
milestone M1 (#94).

To check on the first N32R16V: ESP-IDF v5.4 recognises only octal flash
chips of MXIC; another chip stops at boot with "No detected Flash chip".
Open before outputs get pins: an image for the other board may start on a
board (ESP-IDF detects octal or quad flash by itself), then not find its
storage partition and format it. The firmware is to compare the board's
flash size with the build at boot and, on a mismatch, format nothing,
keep the outputs locked and report it.
GPIO 33–37 belong to flash and PSRAM on the N32R16V, GPIO 35–37 on the
N8R8; on the N32R16V GPIO 47 and 48 run at 1.8 V.

## Building

```bash
(cd ../web && npm ci && npm run build) && node ../tools/embed_web.mjs
idf.py -B build-n32r16v -D GC_BOARD=n32r16v -D IDF_TARGET=esp32s3 build   # or n8r8
idf.py -B build-n32r16v menuconfig    # growcontroller › Wi-Fi (development only), output GPIOs
idf.py -B build-n32r16v flash monitor
```

Each board gets a build folder of its own, with its own `sdkconfig` in it:
`GC_BOARD` picks `sdkconfig.defaults` plus `boards/<board>.defaults`
(flash, PSRAM, partition table) when the folder is configured the first
time; a folder of the other board stops the build. Without `GC_BOARD` the
N32R16V is built. OTA cannot change the partition table, so it is fixed
before the first device ships.

Partitions (`boards/<board>.csv`):

| | N32R16V | N8R8 |
|---|---|---|
| `nvs` (Wi-Fi, keys; encrypted later, SD-024) | 64 KB | 64 KB |
| `fctry` (factory data: serial number, the label's setup key; read-only, SD-063) | 16 KB | 16 KB |
| App A and B (`ota_0`, `ota_1`) | 4 MB each | 2.5 MB each |
| `storage` (configuration) | 2 MB | 1 MB |
| `history` | 21.9 MB | 1.9 MB |

Tested with ESP-IDF v5.4 (CI: container `espressif/idf:v5.4.1`).

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
   install their own firmware (PD-022). The update key and the lowest
   allowed version belong to the running app, not to NVS, so erasing NVS
   does not open a downgrade.

Acceptance on the breadboard follows `docs/prototyp/README.md` §5 in the
product repository (P0, acceptance tests 1–8).
