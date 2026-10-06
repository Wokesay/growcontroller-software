# Teststrategie

Stand 06.10.2026. Ziel: Jede Fachregel ist ein Test. Jede sichtbare
Funktion läuft einmal Ende zu Ende im Browser.

## Ebenen

| Ebene | Werkzeug | Ort | Zahl heute | prüft |
|---|---|---|---|---|
| Architekturregeln | Shell/grep | `tools/arch_check.sh` | 5 Regeln | Kern ohne Plattform-Header, Aktoren nur über das Gateway, Watchdog ohne Aktorpfad, keine Phasennamen in der Logik, kein `value_or(0)` |
| Unit | doctest (C++) | `tests/core/test_*.cpp` | 41 Fälle | Katalog, Konfiguration und Migration, Sensorwahrheit, Kennlinie, Verlauf, Ereignisse, Mischplanung, Resolver, Watchdog, SHA-256/PBKDF2, Anmeldung |
| API-Vertrag | doctest gegen den Kern | `tests/core/test_api.cpp` | 5 Fälle | Zugang, Fehlerformen, Felder, die die Web-App liest, keine Geheimnisse |
| Szenario | doctest + Zwilling | `tests/core/test_scenarios.cpp` | 11 Fälle | Stufe 0 von Hand eingerichtet, Einmessen, Mengen und A:B ±3 %, Paar-Fehler mit Nachholen, Stromausfall, Fehlsteckung, Regelung ins Ziel, EC-Gate, Sprungsperre, Trockenlauf, Zulauf-Notabschaltung, Not-Halt |
| Speicherfehler | AddressSanitizer + UBSan | `GC_SANITIZE=ON` | alle C++-Tests | Überläufe, Use-after-free, undefiniertes Verhalten |
| Web | TypeScript strict, Größenbudget | `npm run build` | – | Typen, ≤ 250 KB gzip |
| Ende zu Ende | Playwright + Chromium | `web/e2e/*.spec.ts` | 9 Fälle | Ersteinrichtung bis zum ersten Mischlauf, Not-Halt, Sprungsperre sichtbar, Funktionen und Verlauf, Fehlsteckung, Diagnosepaket, Zugangsschutz, Sicherheitskopfzeilen |

Die Testfälle der Regellogik folgen der Liste von `firmware` (M1-1 …
M15-3). Welche Regel welcher Test abdeckt und was offen ist, steht in
`INVARIANTEN.md`.

## Ausführen

```bash
tools/ci.sh          # Architektur, Kern mit Sanitizern, alle C++-Tests, Web-Build
E2E=1 tools/ci.sh    # zusätzlich Playwright (Browser: npx playwright install chromium)
./build/gc_tests -tc="*Sprungsperre*"   # einzelne Fälle
```

Die CI (`.github/workflows/ci.yml`) läuft bei jedem PR und auf `main` und
führt genau diese Schritte aus.

## Regeln

- **Ein gefundener Fehler bekommt zuerst einen Test**, der ihn zeigt. Erst
  dann kommt die Korrektur.

  So entstanden bereits am ersten Tag:
  - Passwort wurde nicht gesichert;
  - Einmesswert galt erst einen Takt später;
  - eigene Mischläufe lösten die Sprungsperre aus;
  - ein nachgeholter Lauf trug dieselbe Job-ID.
- **Kein Test wird übersprungen oder abgeschaltet**, um grün zu werden.
- **Zeit ist injiziert:** Szenarien laufen in Simulationszeit (Stunden in
  Sekunden), deterministisch.
- **Sicherheitsrelevante Änderungen** (Gateway, Sensorwahrheit, Anmeldung,
  Updates) brauchen einen Szenario-Test und eine Prüfung durch `security`.

## Noch offen

- Fuzzing der API-Eingaben (JSON, Pfade) und der Konfigurationsdatei.
- Langlauf: 30 Tage Simulationszeit, Speicher und Ereignisgrenzen.
- Hardware-in-the-Loop auf dem Steckbrett P0, sobald `firmware/` läuft:
  Abnahme nach `docs/prototyp/README.md` im Produkt-Repo.
- Barrierefreiheit (axe) und Darstellung mobil als E2E.
- Lasttest des Webservers auf dem ESP32: Die Regelung darf nicht leiden (RLM,
  EN 18031).
