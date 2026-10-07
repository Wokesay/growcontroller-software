# Firmware des Hubs (ESP32-S3, ESP-IDF)

Stand 06.10.2026: **Gerüst.** Es baut den unveränderten Kern aus `core/`
zusammen mit der eingebetteten Web-App zu einem Image für den
ESP32-S3-WROOM-1-N16R8. Die CI baut es bei jedem PR (Job „firmware“) und
weist damit nach, dass Kern und Web-App auf dem Zielchip übersetzen und ins
Image passen: erster grüner Lauf am 06.10.2026, **Image 1,24 MB** (41 % der
3-MB-App-Partition), DIRAM statisch 19 %. Das Image liegt 14 Tage als
Artefakt `firmware-esp32s3` am Lauf. Auf echter Hardware gestartet ist es
noch nicht. Noch ohne Bus: Modbus, Port-Freigabe und Dosierblock folgen in
Meilenstein M1.

## Bauen

```bash
(cd ../web && npm ci && npm run build) && node ../tools/embed_web.mjs
idf.py set-target esp32s3
idf.py menuconfig        # growcontroller › WLAN (nur Entwicklung), GPIOs der Ausgänge
idf.py build flash monitor
```

Getestet mit ESP-IDF v5.4 (CI: Container `espressif/idf:v5.4.1`). Auf dem
Steckbrett mit N8R8 (8 MB Flash): `partitions.csv` verkleinern (Verlauf 3 MB).

## Was das Gerüst schon tut

| Teil | Stand |
|---|---|
| Kern (`components/gc_core`) | dieselben Quellen wie im Simulator; Katalog und Changelog eingebettet |
| Web-App | gzip im App-Image (`tools/embed_web.mjs`), Rückfall auf `index.html` |
| API | `esp_http_server`, alle Methoden auf `/api/*`, Cookie- und Bearer-Sitzung, Sicherheitskopfzeilen, Größengrenze 64 KB |
| Live-Daten | ohne SSE: Die Web-App fragt alle 3 s ab (eingebauter Rückfall) |
| Ablage | SPIFFS unter `/data`, Schreiben über tmp + rename |
| Ausgänge | zwei GPIOs für die 12-V-Ausgänge, beim Start aus |
| Uhr | `esp_timer` (monoton), SNTP |
| Updates | Partitionen A/B, App-Rollback aktiv; bestätigt die neue Version nach dem Start |

## Plan bis M1 (Steckbrett P0)

1. **Tasks** (Vorschlag `firmware`):

   | Task | Kern | Takt | Inhalt |
   |---|---|---|---|
   | `actuator_guard` | 1, höchste App-Priorität | 50–100 ms | einziger Pfad zu Aktoren, Lebenszeichen an den Block, Task-Watchdog |
   | `bus` | 1 | Köpfe 1 s, Block 200 ms | Port-Scheduler, Modbus |
   | `gc_core` | 1 | 200 ms–1 s | Sensorwahrheit, Regler, Resolver |
   | `watchdog_eval` | 0 | 5–60 s | Bewertung auf einem Schnappschuss |
   | `persist` | 0 | bei Ereignis | Ablage, Verlauf |
   | `httpd` | 0 | – | Webserver, nur Warteschlange zum Kern |
2. **Bus je Port** [PD-012]:
   - UART mit DE/RE über den Decoder.
   - Prüfmessung der Kennung über ADC und Mux; die Zustimmung der Firmware
     ist das UND-Glied der Hardware-Freigabe.
   - Modbus-Master mit Wiederholung nur beim Lesen. Schreiben ist idempotent
     über die Job-ID.
3. **Dosierblock-Protokoll** (Registerplan mit `hardware`):
   - Lauf in ms mit Job-ID, Ist-Laufzeit, Strombefund.
   - Lebenszeichen des Hubs; fällt es aus, stoppt der Block.
   - Einmesswert im ID-Chip lesen und schreiben (PD-010).
4. **Ablage:**
   - LittleFS für Konfiguration und Zustand.
   - Verlauf als Ringpuffer in der Partition `history` (`docs/HISTORY.md`).
5. **Live-Kanal:** SSE über asynchrone Handler oder WebSocket
   (`CONFIG_HTTPD_WS_SUPPORT`).
6. **Sicherheit vor dem ersten Gerät bei Dritten** (`docs/SECURITY_MODEL.md`):
   HTTPS, signiertes OTA mit Selbsttest, Setup-Zugangspunkt per Taste.

Abnahme auf dem Steckbrett nach `docs/prototyp/README.md` §5 im Produkt-Repo
(P0, Abnahme 1–8).
