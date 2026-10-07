## Was ändert sich?

<!-- Kurz, aus Sicht des Nutzers. Verweis auf Issue oder Entscheidung (SD-xxx). -->

## Sicherheit und Invarianten

- [ ] Aktoren nur über das Gateway, Watchdog ohne Aktorpfad (`tools/arch_check.sh`)
- [ ] Ein fehlender Wert bleibt fehlend, nie 0
- [ ] Neue Fachregel? Quelle „RAT-xxx“ und Testfall genannt
- [ ] Sicherheitsrelevant (Pumpen, Zulauf, Anmeldung, Updates)? Dann Abschnitt „Sicherheit“ im CHANGELOG

## Tests

- [ ] `tools/ci.sh` grün, bei UI-Änderungen auch `E2E=1 tools/ci.sh`
- [ ] Neue Logik hat Unit- oder Szenario-Test

## Changelog

- [ ] Eintrag unter `[Unreleased]` (Neu / Geändert / Behoben / Sicherheit)

Signed-off-by: <!-- DCO: git commit -s -->
