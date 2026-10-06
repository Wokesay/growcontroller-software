# growcontroller – Software

Fertigations-Controller für den Homegrow: Nährlösung mischen, pH und EC
regeln, Tank füllen. Die Software läuft auf dem Hub (ESP32-S3) und bringt
ihre eigene Web-App mit – ohne Cloud, ohne Konto, ohne Home Assistant.

> **Status: Prototyp `0.1.0-proto.1`.** Kern, Simulator und Web-App laufen;
> die Portierung auf den ESP32-S3 folgt (`firmware/README.md`). Nicht an
> echter Hardware einsetzen.

## Ausprobieren in 2 Minuten (ohne Hardware)

Voraussetzungen: CMake ≥ 3.20, Ninja, C++17-Compiler, Node.js 22.

```bash
tools/dev.sh
# → http://127.0.0.1:8080  ·  Passwort: demo-passwort (nur Simulator)
```

Startet den digitalen Zwilling mit eingerichtetem Tank und 48 Stunden
Verlauf. `SCENARIO=neu tools/dev.sh` startet einen leeren Hub mit
Ersteinrichtung. Über den Knopf **Simulator** in der App: Zeitraffer,
Störungen (pH-Sprung, Pumpe blockiert, Stromausfall, Fehlsteckung …),
Szenarien.

## Aufbau

```
core/      Kern in C++17, plattformneutral: Katalog, Konfiguration, Sensorwahrheit,
           Resolver, Mischen, Aktor-Gateway, Regler, Watchdog, Verlauf, Ereignisse, API
catalog/   Gerätekatalog als Daten (Capabilities, Geräteklassen, Rollen, Funktionen)
sim/       Simulator: Zwilling (Ports, Dosierblock, Köpfe, Tank) und Host-Server
web/       Web-App (Preact, TypeScript, Vite) und Ende-zu-Ende-Tests (Playwright)
tests/     C++-Tests: Unit, API-Vertrag, Szenarien gegen den Zwilling
firmware/  Plan und Gerüst für den ESP32-S3 (ESP-IDF)
docs/      Konzept, Fachregeln, Bedienung, API, Tests, Releases, Sicherheit, Arbeitsweise
tools/     ci.sh (alle Prüfungen), dev.sh (Simulator starten), arch_check.sh (Architekturregeln)
```

Einstieg in die Doku: [`docs/README.md`](docs/README.md).

## Prüfen

```bash
tools/ci.sh            # Architekturregeln, Kern mit Sanitizern, 57 C++-Tests, Web-Build mit Größenbudget
E2E=1 tools/ci.sh      # zusätzlich 9 Browser-Tests gegen den Simulator
```

## Mitmachen, Fehler melden, Sicherheit

- Beiträge: [`CONTRIBUTING.md`](CONTRIBUTING.md)
- Fehler: in der App „Einstellungen › Problem melden“ (Vorgangsnummer), oder
  ein Issue mit der Vorlage „Fehler melden“
- Sicherheitslücken nie als Issue: [`SECURITY.md`](SECURITY.md)
- Änderungen: [`CHANGELOG.md`](CHANGELOG.md)

## Lizenz

Noch nicht festgelegt (Entwurf E10 in `docs/ENTSCHEIDUNGEN.md`: GPL-3.0-or-later
mit DCO). Bis zur Lizenzentscheidung alle Rechte vorbehalten; Ziel ist Open
Source (PD-005, PD-008). Fremdbibliotheken: nlohmann/json,
cpp-httplib, doctest (MIT); Preact, @preact/signals, uPlot (MIT), lucide (ISC).
