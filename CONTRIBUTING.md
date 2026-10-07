# Mitmachen

Danke für dein Interesse. Kurzfassung: kleine PRs, zuerst ein Test, DCO,
alles auf Deutsch.

## Entwickeln

```bash
tools/dev.sh                    # Simulator + Web-App auf :8080
cd web && npm run dev           # UI mit Hot-Reload auf :5173 (API → :8080)
tools/ci.sh                     # alle Prüfungen vor dem PR
```

## Regeln für Code

- **Kern bleibt plattformneutral:** keine Plattform-Header in `core/`.
- **Aktoren nur über das Gateway** (`Actuators` in `core/src/dosing.cpp`).
- **Der Watchdog bewertet nur.** Er bindet nur `readmodel.hpp` und
  `config.hpp` ein.
- **Ein fehlender Wert bleibt fehlend** (`nullopt`/`NaN`, JSON `null`), nie 0.
- **Logik liest Parameter, nie Phasennamen.**

`tools/arch_check.sh` prüft diese Regeln; die CI bricht bei Verstoß ab.
Hintergrund: `docs/CONCEPT.md` §4.

## Regeln für Fachlogik

Jede Fachregel trägt ihre Quelle („Quelle: RAT-xxx“) und einen Test.
Neue Regeln gehören in `docs/INVARIANTS.md`. Simulator-Zahlen sind entweder
gemessen (mit Quelle) oder als Annahme markiert.

## Pull Requests

1. **Branch** von `main`, ein Thema je PR.
2. **Test zuerst:** Ein Fehler bekommt einen Test, der ihn zeigt.
3. **CHANGELOG:** Eintrag unter `[Unreleased]` (Neu / Geändert / Behoben /
   Sicherheit).
4. **DCO:** Jeder Commit mit `git commit -s`. Damit bestätigst du, dass du den
   Beitrag einreichen darfst ([developercertificate.org](https://developercertificate.org/)).
5. **Freigabe:** CI grün, Review durch `reviewer` und `qa`; bei Gateway,
   Sensorwahrheit, Anmeldung oder Updates zusätzlich `security`.

## Ideen und Fragen

Bitte in die Discussions, nicht als Issue. Ein Issue entsteht, wenn eine Idee
angenommen ist.
