# Software-Entscheidungen (SD)

Verbindliche Entscheidungen für die Software. Format `## SD-XXX: Titel`,
fortlaufend, chronologisch, zwei Leerzeilen zwischen Einträgen. Kein
Status-Feld; Einträge werden nicht gelöscht, ein späteres SD ersetzt ein
früheres und verweist darauf.

Produktentscheidungen stehen im Produkt-Repo (PD). Hat eine PD Folgen für die
Software, steht hier ein SD „Produktvorgabe (aus PD-0xx)“.

**Solange das Paket als `software/` im Produkt-Repo liegt, wird hier nichts
verbindlich entschieden.** Entscheidungen werden dort PDs
(`../docs/DECISIONS.md`); dieses Log beginnt mit dem Umzug.

**Entschieden ist bisher nichts.** Die Entwürfe unten sind die Grundlage des
Prototyps. Sie werden zu SD-Einträgen, sobald der Projektinhaber
„entschieden“ sagt. Was eine PD im Produkt-Repo braucht, ist markiert.

---

## Entwürfe (warten auf „entschieden“)

| # | Entwurf | Kern | braucht PD |
|---|---|---|---|
| E1 | Eigenständige Software | Web-App auf dem Hub, ohne Cloud und Konto. Home Assistant nur optional über MQTT, dann lesend und mit wenigen Befehlen; Logik liegt nie in HA. | ja (Ergänzung VISION) |
| E2 | Plattform ESP32-S3, Option C | Regelung, Sicherheit, API, Web-UI und 1 Jahr Verlauf auf dem ESP32-S3-WROOM-1-N16R8. Kein Linux im Hub. Optionaler „Begleiter“ (Docker/NAS/HA-Add-on, später Abo) nur für Komfort. | ja (MCU in keiner PD) |
| E3 | Ein Kern, zwei Plattformen | C++17 ohne Plattform-Header; Host-Simulator als digitaler Zwilling; ESP-IDF statt Arduino; Rust später prüfen. | nein |
| E4 | Web-App | Preact + TypeScript + Vite, gzip im App-Image (Update atomar, Rollback nimmt UI mit), Budget 250 KB, Live per SSE, Hash-Routing, keine externen Ressourcen. | nein |
| E5 | Konfigurationsmodell | Katalog als Daten mit festen Voraussetzungsarten; Rollen an der Geräte-ID; zwei Zustandsachsen (Einrichtung/Laufzeit); Phasen als Parametersätze; Laufzeitzustand getrennt von der Konfiguration. | nein |
| E6 | Architekturregeln R1–R8 | Gateway einziger Aktorpfad; Watchdog nur Lesemodell; fehlender Wert nie 0; nach Neustart alles aus; Katalog kann Sicherheit nur verschärfen; eine Ereignisschleife mit injizierter Uhr. CI prüft maschinell. | nein |
| E7 | Ohne Einmesswert keine Dosierung | strenger als RAT-015; Gateway und Planung lehnen ab | **ja** (ROADMAP-Vorschlag) |
| E8 | Stufe 0 dosiert kein pH blind | pH-Korrektur nur mit abgesichertem Messweg (RAT-044); in Stufe 0 Handmessung und Hinweis | **ja** (Prototyp, Offene Frage 4) |
| E9 | Eigenes Repo | `growcontroller-software` (Arbeitsname) in einer GitHub-Organisation, privat, ohne Historie; Einbahn-Spiegel `ref/software/` im Produkt-Repo | **ja** (Ergänzung PD-002) |
| E10 | Lizenz | GPL-3.0-or-later mit DCO für Firmware und UI; Apache-2.0 für Schnittstellen; CC BY-SA 4.0 für Doku; Klonschutz über die Marke. Hängt am Secure-Boot-Konzept (Eigentümer-Modus), sonst EUPL-1.2. | **ja** (PD-005, PD-008) |
| E11 | Frei und Abo | Was das Gerät allein kann, ist frei. Was Serverkosten macht, darf Abo sein – außer es meldet oder stoppt eine Gefahr (Alarm, Status, Stopp immer frei). | **ja** (Ergänzung PD-005/PD-008) |
| E12 | Versionen und Updates | SemVer, Keep a Changelog mit Abschnitt „Sicherheit“, Kanäle stable/beta, GitHub Releases mit SBOM und Prüfsummen, OTA A/B signiert, Update nur im Ruhezustand, offline signieren | nein |
| E13 | Zugang | Pflichtpasswort vor jeder Funktion, PBKDF2, Sperre nach Fehlversuchen; HTTPS lokal vor dem ersten Gerät bei Dritten | nein (Pflicht aus EN 18031) |
| E14 | Fehler melden | technische Issues über GitHub mit Formularen; in der App „Problem melden“ mit Vorgangsnummer und Diagnosepaket, ohne Telemetrie | nein |
| E15 | Rollen und Ablauf | Agenten `triage`, `reviewer`, `qa`, `security`, `release`, `ux`, `fachlogik`; Ablauf in `ARBEITSWEISE.md`; Issue-Workflows nur lesend | nein |

Herleitung, Optionen und Quellen: `KONZEPT.md`, `KONFIGURATION.md`,
`RELEASE.md`, `SICHERHEIT.md`, `ARBEITSWEISE.md`.
