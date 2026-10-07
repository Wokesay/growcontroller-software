# Arbeitsweise: eigenes Repo, Issues, Agenten, Entscheidungen

Stand 06.10.2026. Vorschlag; Entscheidungen dazu in
`docs/DECISIONS.md` (Entwürfe).

## 1. Repo-Schnitt

| Software-Repo (dieses Paket) | bleibt im Produkt-Repo `growcontroller` |
|---|---|
| Kern, Simulator, Web-App, Firmware des Hubs; später Firmware von Dosierblock und Köpfen | Platinen (KiCad), bis die Hardware-Lizenz entschieden ist |
| Schnittstellen: Katalog, API, später Bus-Registerplan | Einkauf, Lieferanten, Preise, Margen, Business Case |
| Doku, Software-Entscheidungen (SD), CHANGELOG, SECURITY, CONTRIBUTING | Regulatorik-Akte, Prüfberichte |
| Agenten für die Software-Rollen | interne Fachquellen, Kundendaten |

Signierschlüssel liegen in keinem Repo. Quelle: `produkt`.

**Transparenz der Produktentscheidungen:** Hat eine PD Folgen für die
Software, entsteht im Software-Repo ein SD-Eintrag „Produktvorgabe (aus
PD-0xx)“. Er nennt, was gilt, und den öffentlichen Grund, ohne Zahlen und
Lieferanten. Zurück ins Produkt-Repo führt ein Einbahn-Spiegel
`ref/software/` (PD-017); schreiben
darf dorthin nur der Sync-Workflow.

## 2. Umzug in ein eigenes Repo (Schritte)

Das Paket ist so gebaut, dass es ohne Änderungen umziehen kann. Alle Pfade
sind relativ; CI, Vorlagen, Agenten und `CLAUDE.md` liegen im Paket.

1. **Organisation und Repo anlegen.** Der Projektinhaber legt eine
   GitHub-Organisation an (übertragbar, Rechte je Repo) und darin das private
   Repo (Arbeitsname `growcontroller-software`).
   - Secret Scanning und Push Protection einschalten.
   - Die Claude-GitHub-App für dieses Repo freigeben (PD-017).
2. **Inhalt übernehmen, ohne Historie.** Nur versionierte Dateien, keine
   lokalen Reste. Ab dem ersten Tag schreiben, als wäre das Repo öffentlich
   (`produkt`):
   ```bash
   mkdir growcontroller-software && cd growcontroller-software && git init -b main
   git -C ../growcontroller archive HEAD software | tar -x --strip-components=1
   git status
   git -C ../growcontroller ls-files software | sed 's|^software/||' | xargs git add --
   git commit -s -m "Start aus growcontroller/software"
   ```
   Die Dateien kommen einzeln mit Namen aus der Liste der versionierten
   Dateien, nicht über `git add .`.
3. **Verweise anpassen.** Platzhalter `OWNER/REPO` in
   `.github/ISSUE_TEMPLATE/config.yml` und `SECURITY.md` ersetzen.
   `ISSUE_URL` in `web/src/pages/settings.tsx` zeigt bis zum Umzug auf das
   Produkt-Repo und wird auf das neue Repo umgestellt.
4. **Übergangslösung entfernen.** Im Produkt-Repo `software/` und
   `.github/workflows/software.yml` löschen. Den Spiegel `ref/software/`
   einrichten (PD-017), vorher die Regeln für `ref/` in `CLAUDE.md`
   erweitern.
5. **Beim Öffentlichschalten:**
   - Private Vulnerability Reporting einschalten.
   - Discussions einschalten.
   - Lizenzdatei nach der SD-Entscheidung hinzufügen.
   - Marke prüfen.

## 3. Rollen und Agenten

Claude ist Produktverantwortlicher und Entwickler, wie im Produkt-Repo. Die
Fachrollen sind Subagenten in `.claude/agents/`. Sie schreiben nichts, außer
wo vermerkt.

| Agent | Rolle in einer Software-Firma | zuständig für | Werkzeuge |
|---|---|---|---|
| `triage` | Support / Issue-Triage | neue Issues einordnen, im Simulator nachstellen, Duplikate, Schwere, Antwortentwurf | lesen, Shell zum Bauen und Starten des Simulators, GitHub lesen |
| `reviewer` | Code-Review | Korrektheit, Architekturregeln R1–R8, Invarianten, Lesbarkeit, Testabdeckung je Diff | lesen |
| `qa` | Qualitätssicherung | Testfälle ableiten, Tests ausführen, Lücken in `INVARIANTS.md`, Abnahme vor Release | lesen, Shell zum Testen |
| `security` | Produktsicherheit | Bedrohungsmodell, EN 18031/CRA technisch, Abhängigkeiten und SBOM, Auth, OTA, Diagnosedaten | lesen, Web |
| `release` | Release- und Build-Verantwortung | Version, Changelog, Kurzfassung „Was ist neu“, Artefakte, Kanäle, Rollback-Plan | lesen, Shell zum Bauen |
| `ux` | UX/UI-Design und Texte | Informationsarchitektur, Texte nach den Grundsätzen in `UX.md`, mobil, Barrierefreiheit | lesen, Web |
| `fachlogik` | Domänenexperte Fertigation | Regel- und Dosierlogik gegen `INVARIANTS.md`, Zahlen im Simulator, neue Fachregeln einordnen | lesen, Web |

Fachsichten aus dem Produkt-Repo (`kunde`, `anwender`, `architekt`,
`firmware`, `hardware`, `regulatorik`, `produkt`) bleiben dort. Das
Software-Repo fragt sie über den Projektinhaber oder über eine gemeinsame
Sitzung an.

**Ablauf einer Änderung:**

```
Issue/Idee ─► triage ─► Projektinhaber entscheidet (Priorität, ob überhaupt)
   ─► Claude: Branch, zuerst Test, dann Code ─► tools/ci.sh grün
   ─► reviewer + qa (+ security bei Gateway/Sensorwahrheit/Auth/Update, + ux bei UI)
   ─► PR mit CHANGELOG-Eintrag ─► CI grün ─► Freigabe ─► Merge ─► release sammelt
```

## 4. Issues in Claude Code sichtbar machen und abarbeiten

Drei Wege, kombinierbar (Quelle: Claude-Code-Doku, abgerufen 06.10.2026:
code.claude.com/docs/en/github-actions, …/routines):

1. **In jeder Sitzung über GitHub-MCP:**
   - Beim Start listet Claude die offenen Issues mit Label `triage` und
     bewertet sie mit `triage`.
   - Der Abgleich steht in `CLAUDE.md` des Pakets unter „Bei Sessionstart“.
2. **Routine:** eine geplante Sitzung in Claude Code im Web, z. B. werktags
   morgens.
   - Sie liest neue Issues, stellt sie im Simulator nach und schreibt einen
     Kommentar-Entwurf an den Projektinhaber.
   - Sie postet nichts öffentlich, ohne dass er es freigibt.
3. **GitHub Action mit `@claude`:**
   - Vorlage in `docs/templates/claude-triage.yml`.
   - Braucht die Claude-GitHub-App und ein Secret (`ANTHROPIC_API_KEY` oder
     `CLAUDE_CODE_OAUTH_TOKEN`).

**Schutz** (Issue-Texte sind fremde Eingaben). Im Juni 2026 verschaffte ein
präpariertes Issue über die Claude-Code-Action Schreibzugriff; behoben ab
v1.0.94. Quellen (abgerufen 06.10.2026):
https://thehackernews.com/2026/06/claude-code-github-action-flaw-let-one.html,
https://flatt.tech/research/posts/poisoning-claude-code-one-github-issue-to-break-the-supply-chain/.
Deshalb:

- Triage-Workflows laufen nur lesend, ohne Geheimnisse außer dem API-Schlüssel
  und mit Rechten `issues: write`, `contents: read`.
- Aus Issue-Workflows gibt es keinen Merge und kein Release.
- Signiert wird nur offline nach manueller Freigabe.
- Sicherheitsmeldungen kommen nie als Issue (`SECURITY.md`).

**Für Hobby-Kunden ohne GitHub:**

- Die App bietet „Problem melden“ mit Vorgangsnummer und Diagnosepaket, das
  der Kunde vorher sieht.
- Später gibt es zusätzlich ein Formular oder eine E-Mail. Claude anonymisiert
  die Meldung und legt das Issue an.

**Aufwand (Annahme `produkt`):** 2–8 Meldungen pro Woche bei 50–300 Geräten,
also 2–5 h pro Woche mit Claude-Triage. Regeln:

- Antwort binnen 7 Tagen;
- Stale-Markierung nach 30/60 Tagen;
- FAQ aus häufigen Issues;
- Feature-Wünsche in Discussions „Ideen“.

## 5. Branches, Commits, Reviews

- **Branches:** `main` ist immer releasefähig. Jede Änderung auf einem
  eigenen Branch mit PR. Squash-Merge.
- **Commits:** auf Deutsch, mit DCO-Sign-off (`git commit -s`, Vorschlag
  `produkt`); kein CLA.
- **PR:** Vorlage mit Sicherheitspunkten (Gateway, fehlender Wert, Quelle der
  Fachregel, Changelog).
- **Merge** erst nach grüner CI und den Reviews oben. Wer mergt, legt der
  Projektinhaber fest (SD-Entwurf).

## 6. Entscheidungen

- **SD-Log:** `docs/DECISIONS.md`, Format `## SD-XXX: Titel`,
  fortlaufend, chronologisch. Kein Status-Feld; ein späteres SD ersetzt ein
  früheres.
- **Entwürfe** stehen getrennt und gelten erst nach „entschieden“ des
  Projektinhabers.
- **Produktentscheidungen** bleiben im Produkt-Repo (PD). Im Software-Repo
  erscheinen sie als „Produktvorgabe (aus PD-0xx)“.
