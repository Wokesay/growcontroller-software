# Konzept: growcontroller als eigenständige Software

Stand 06.10.2026. **V** = Vorschlag, **A** = Annahme, [PD-xxx] = im
Produkt-Repo entschieden. Software-Entscheidungen sind noch Entwürfe
(`docs/ENTSCHEIDUNGEN.md`).

## 1. Ziel

Weg von Home Assistant und Node-RED als Plattform, hin zu einer eigenen
Software auf dem Hub:

- **Web-App auf dem Gerät**, ohne Cloud und ohne Konto. Sie ist erreichbar im
  Heimnetz. Eine Handy-App kommt später.
- **Einrichtung statt Konfiguration.** Geräte werden erkannt, nicht
  ausgewählt. Der Hub sagt, was mit der vorhandenen Hardware geht und was
  fehlt.
- **Sicherheit im Kern.** Die Fachregeln sind Code und Test, nicht
  Dashboard-Disziplin.
- **Produktbetrieb.** Versionen, Changelog, Updates mit Rückweg, Fehler melden
  und eine nachvollziehbare Entwicklung.

## 2. Wo läuft was (Empfehlung `software`: Option C)

```
Browser ──WLAN── ESP32-S3-Hub [Regelung · Sicherheit · API · Web-UI · Verlauf 1 Jahr]
                     │
                     ├── RS485/Modbus je Port ── Dosierblock, Köpfe, Sammelbox
                     └── optional: „Begleiter“ (Docker/NAS/HA-Add-on, später Abo)
                                    für Langzeitarchiv, Grow-Vergleich, Push-Relay
```

| Option | Kosten im Hub | Bewertung |
|---|---|---|
| A: alles auf dem ESP32-S3 | keine (Modul N16R8 ca. 3,4–5,2 $, LCSC) | trägt Regelung, UI, Verlauf |
| B: Linux-Modul (z. B. CM5) | ca. 85–98 $, 2026 teurer geworden | Bootzeit, Dateisystem, OS-Patchpflicht (CRA), Zielpreis gefährdet [PD-006] |
| **C: A + optionaler Begleiter** | keine | **empfohlen**: Ohne Begleiter fehlt nichts Sicherheitsrelevantes [PD-008]. Komfort darf Abo sein [PD-005] |

Quellen (Recherche `software`, abgerufen 06.10.2026):

- https://www.lcsc.com/product-detail/WiFi-Modules_Espressif-Systems-ESP32-S3-WROOM-1-N16R8_C2913202.html
- https://www.raspberrypi.com/news/more-memory-driven-price-rises/
- https://www.theregister.com/2026/04/01/raspberry_pi_price_hikes/

## 3. Ein Kern, zwei Plattformen

Der fachliche Kern ist **plattformneutrales C++17** (`core/`). Derselbe Code
läuft:

- **im Simulator** (`sim/`): Host-Server mit digitalem Zwilling. Darauf
  laufen Entwicklung, Tests und Vorführung, ohne Hardware.
- **auf dem Hub** (`firmware/`): ESP-IDF auf dem ESP32-S3. Ersetzt werden
  nur die HAL-Schnittstellen (`IBus`, `IStorage`, `IClock`, HTTP-Bindung).

Warum C++ und nicht Rust/MicroPython/ESPHome:

- **ESP-IDF ist ausgereift.** Für Rust gibt es esp-hal 1.0 seit Oktober
  2025; die std-Crates haben nur Community-Support. Später prüfen.
- **MicroPython** fällt aus: Pausen der Garbage Collection, Fehler erst zur
  Laufzeit.
- **ESPHome** konfiguriert zur Kompilierzeit und hat keinen
  Konfigurationsbaum zur Laufzeit. Es bleibt Muster, nicht Basis.

Quelle: `software`, abgerufen 06.10.2026:
https://developer.espressif.com/blog/2025/10/esp-hal-1/

Die **Web-App** (`web/`) ist Preact + TypeScript, gebaut mit Vite. Sie
spricht nur die REST-API. Gzip-komprimiert sind es heute **ca. 73 KB**
(Budget 250 KB, Prüfung im Build). Sie liegt im App-Image der Firmware;
damit passen UI und API immer zusammen, und ein Rollback nimmt die UI mit.

## 4. Schichten und Regeln (Vorschlag `architekt`)

| # | Schicht | Code | plattformneutral |
|---|---|---|---|
| 0 | HAL: UART/DE je Port, eFuse, ADC, Flash, WLAN, Uhr | `IBus`, `IStorage`, `IClock` | nein |
| 1–3 | Bus, Port-Freigabe, Treiber, Geräte-Register | `bus.hpp`, Simulator `simbus` | Schnittstelle ja |
| 4 | Sensorwahrheit | `truth.*` | ja |
| 5 | Konfiguration, Rollen, Parameter, Phasen | `config.*`, `catalog.*` | ja |
| 6 | Resolver („Was fehlt dir?“) | `resolver.*` | ja |
| 7 | Funktionen und Regler | `mix.*`, `control.*` | ja |
| 8 | **Aktor-Gateway**: einziger Weg zu Aktoren | `dosing.*` (`Actuators`) | ja |
| 9 | Watchdog: bewertet nur | `watchdog.*` | ja |
| 10 | Verlauf, Ereignislog | `history.*`, `events.*` | ja |
| 11 | API | `api.*` | ja (Server-Bindung nein) |
| 12 | Web-App | `web/` | eigener Build |

**Regeln** (in `tools/arch_check.sh` maschinell geprüft, soweit möglich):

- **R1:** Nur das Aktor-Gateway ruft Pumpen und Ausgänge. Auch Handgaben und
  der Not-Halt laufen dort durch.
- **R2:** Der Watchdog sieht nur Lesemodell und Konfiguration
  (`readmodel.hpp`). Er hat keinen Pfad zu Aktoren. Quelle: RAT-074.
- **R3:** Sperren lesen die Sensorwahrheit, nie die Bewertung des Watchdogs.
- **R4:** Funktionen bekommen nur wirksame Parameter (`ParamView`), nie
  Phasennamen. Quelle: RAT-076.
- **R5:** Ein fehlender Wert ist `nullopt`/`NaN`, in JSON `null`, nie 0.
  Quelle: RAT-006.
- **R6:** Nach einem Neustart ist alles aus. Abläufe werden nicht fortgesetzt,
  sondern als unterbrochen gemeldet. Rastungen und Sprungsperren bleiben
  erhalten. Quelle: RAT-007, RAT-028, RAT-044, RAT-063. PD-020 ersetzt
  den Teil für Zustandsfunktionen (Lüfter, Licht, Gießen); Umsetzung offen.
- **R7:** Der Katalog kann Sicherheit nur verschärfen. Das Minimum steht im
  Gateway: Einmesswert, Laufzeitgrenzen, ein Lauf zugleich, Not-Halt,
  Trockenlauf, Notgrenze des Zulaufs.
- **R8:** Eine Ereignisschleife mit injizierter Uhr. Das macht Tests
  deterministisch und den Zeitraffer im Simulator möglich.

## 5. Was der Prototyp heute kann

- **Stufe 0:**
  - Geräte erkennen und übernehmen.
  - Kanister mit Paaren (A:B), Rezepte mit Reihenfolge, Vorlagen.
  - Pumpen einmessen; der Wert landet im ID-Chip [PD-010].
  - Geführtes Mischen: „neu“ oder „auffüllen“, mit Rühranweisung oder
    Umwälzpumpe. Paar-Fehler mit „nachholen“, Doppelstart-Schutz.
  - Handgabe mit Grenze, Vorrat je Kanister.
- **Stufe 1:**
  - pH/EC-Kopf mit Kalibrierung: pH mit 2 Puffern, EC mit 1 Referenz.
  - Sensorwahrheit: Frische, Stillstand, Plausibilität, Sprungsperre,
    Kalibrierung.
  - EC-Nachdosierung und pH-Regelung in Teilgaben aus der gemessenen Wirkung,
    mit EC-Gate, Ruhezeit und Lernen nur aus sauberen Gaben.
  - Umwälzen nach Intervall oder bei Bedarf.
- **Stufe 2:**
  - Füllstand mit stückweise linearer Kennlinie (RAT-078).
  - Nachfüllen über eine berechnete Menge; der Sensor ist Notabschaltung.
  - Trockenlaufschutz mit Rastung, Bewertung der Wassertemperatur.
- **Überall:**
  - Watchdog: OK, Problem oder neutral, immer mit Grund.
  - Regelzeile mit Checkliste („Warum dosiert er gerade nicht?“).
  - Not-Halt, Pflegemodus, Durchgang mit Phasen als Parametersätze, Ernte als
    Ereignis.
  - Verlauf in 3 Stufen, Ereignislog, CSV-Export, Sicherung und Import der
    Konfiguration.
  - Anmeldung mit Pflichtpasswort und Sperre nach Fehlversuchen.
  - Update-Ansicht mit „Was ist neu“ (im Simulator als Attrappe).
  - Diagnosepaket für „Problem melden“.

## 6. Was bewusst noch fehlt

- **ESP-IDF-Portierung:** Modbus je Port, Port-Freigabe, NVS/Flash-Ringpuffer,
  OTA. Plan in `firmware/README.md`.
- **HTTPS im Heimnetz** mit Zertifikat je Gerät und signiertes OTA
  (`docs/SICHERHEIT.md`).
- **Benachrichtigungen ohne Herstellercloud** (ntfy, E-Mail, Webhook),
  Morgenbericht.
- **MQTT mit HA-Discovery** (nur lesend und wenige Befehle).
- **Mehrere Tanks:** Das Datenmodell ist schon eine Liste; UI und Logik nutzen
  einen Tank.
- **Stufe 3–4:** Gießen, Drain, Klima. Heizen nur über eine externe Steckdose
  mit Auto-Off.
- **Sprachen:** Texte laufen schon über Schlüssel; Übersetzungen fehlen noch.
