# Sicherheit

## Lücken melden – nicht öffentlich

Bitte **nie als öffentliches Issue**. Solange das Repo privat ist:
E-Mail an den Projektinhaber (Adresse wird beim Öffentlichschalten hier
eingetragen; bis dahin über den direkten Kontakt). Danach über GitHub
**Private Vulnerability Reporting**:
`https://github.com/OWNER/REPO/security/advisories/new`.

Hilfreich: betroffene Version, Schritte zum Nachstellen (der Simulator reicht
oft), mögliche Folgen. Besonders wichtig: alles, wodurch eine Pumpe oder ein
Ventil ungewollt schalten kann, und alles, was die Anmeldung umgeht.

## Was du von uns erwarten kannst

> **Entwurf.** Fristen und Zusagen in diesem Abschnitt gelten erst, wenn der
> Projektinhaber sie vor dem Öffentlichschalten freigibt.

| Schritt | Ziel |
|---|---|
| Eingangsbestätigung | 3 Werktage |
| Erste Einschätzung | 10 Werktage |
| Korrektur | so schnell wie möglich; Sicherheitskorrekturen kommen als eigene PATCH-Version |
| Veröffentlichung | nach der Korrektur, abgestimmt mit dir; spätestens 90 Tage nach Meldung |

Aktiv ausgenutzte Lücken melden wir nach Art. 14 CRA an die Behörden (seit
11.09.2026) und informieren betroffene Nutzer. Wer gutgläubig forscht und
meldet, wird nicht verfolgt.

## Unterstützte Versionen

| Version | Sicherheitsupdates |
|---|---|
| 0.x (Prototyp) | nein – nicht an echter Hardware einsetzen |

Der Supportzeitraum für verkaufte Geräte wird vor dem ersten Verkauf
festgelegt (mindestens 5 Jahre, CRA Art. 13).

Hintergrund: [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md),
[`docs/RELEASE.md`](docs/RELEASE.md).
