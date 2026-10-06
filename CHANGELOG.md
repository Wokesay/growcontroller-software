# Changelog

Alle wesentlichen Änderungen an growcontroller-software. Format nach
[Keep a Changelog 1.1.0](https://keepachangelog.com/de/1.1.0/), Versionen
nach [SemVer](https://semver.org/lang/de/). Sicherheitsrelevante Änderungen
stehen in jedem Release unter „Sicherheit“.

## [Unreleased]

### Neu
- Simulator als Download für Windows, macOS (Apple-Chip und Intel) und
  Linux: eine Datei mit eingebetteter Web-App; Doppelklick startet die Demo
  und öffnet den Browser, die Daten liegen neben dem Programm. Releases
  hängen die Pakete automatisch an.

### Geändert
- Eingebettete Texte (Katalog, Changelog) als Byte-Felder, damit der Kern
  auch mit MSVC übersetzt.

## [0.1.0-proto.1] – 2026-10-06

Erster Prototyp. Läuft im Simulator; die Firmware für den ESP32-S3 ist ein
Gerüst ohne Bus. Keine Zusagen zur Kompatibilität.

### Neu
- **Kern (C++17, plattformneutral):** Katalog mit Capabilities,
  Geräteklassen, Rollen, Funktionen und Vorlagen; Konfiguration mit Migration
  und Prüfung in drei Stufen; Resolver mit Einrichtungszuständen und
  Checklisten; Phasen liefern Parameter.
- **Sensorwahrheit:** Frische, Stillstand, Plausibilität, Sprungsperre
  (überlebt den Neustart), Kalibrierung pH, EC und Füllstand-Kennlinie;
  angekündigte Änderungen erklären Sprünge (Quelle: RAT-042).
- **Dosieren:** Mischen nach Rezept mit A:B als Paar, Handgabe,
  Pumpen-Einmessen und Schlauchfüllen; Teilläufe mit Ist-Laufzeit und
  Buchung je Lauf; Job-ID je Versuch; „Nachholen“ nach Blockade.
- **Regelung:** EC und pH (Quelle: RAT-053 u. a.),
  Nachfüllen über den Zulauf, Umwälzpumpe; Zustände
  ruht, regelt, wartet, gesperrt, gerastet; Checkliste je Regler.
- **Watchdog**, der nur bewertet (OK, Problem, neutral).
- **Verlauf** in drei Stufen (10 s, 1 min, 15 min), Ereignislog, CSV-Export.
- **API** `/api/v1` mit Live-Kanal (SSE) und Simulator-Endpunkten.
- **Web-App:** Übersicht, Mischen, Tank und Regelung, Verlauf, Rezepte und
  Kanister, Geräte, Funktionen, Einstellungen, Setup-Assistent in 8 Schritten;
  hell und dunkel, mobil.
- **Simulator** als digitaler Zwilling mit Szenarien `neu`, `stufe1`, `demo`,
  Fehlereinspielung, Zeitraffer und Neustart.
- **Firmware-Gerüst** für den ESP32-S3 (ESP-IDF): Kern, Web-App im Image,
  API, Ablage, 12-V-Ausgänge, A/B-Partitionen.
- **Tests:** Unit- und Szenariotests (doctest, auch mit ASan/UBSan),
  End-to-End-Tests (Playwright), Architekturprüfung `tools/arch_check.sh`,
  CI für Simulator, Web-App und Firmware.
- **Updates und Changelog** in der App (im Simulator als Attrappe).

### Sicherheit
- Pflicht-Erstpasswort ohne Standardpasswort; PBKDF2-HMAC-SHA256 mit Salz;
  Sperre nach 5 Fehlversuchen; Sitzung als HttpOnly-/SameSite-Cookie.
- Sicherheitskopfzeilen (CSP, `X-Frame-Options`, `nosniff`).
- Aktoren nur über das Aktor-Gateway mit festen Grenzen im Code; die
  Konfiguration (auch per Import oder Phase) kann sie nur verschärfen. Nach
  dem Neustart ist alles aus; ohne Einmesswert keine Dosierung.
- Doser mit Frist je Lauf; ein Abbruch bucht, was schon gelaufen ist;
  Bus-Job-IDs sind über Neustarts eindeutig.
- Herkunftsprüfung gegen CSRF und DNS-Rebinding; Hash-Vergleich in
  konstanter Zeit; ist das Passwort verloren, ist die Einrichtung über das
  Netz gesperrt.
- Kaputte Eingaben und Dateien führen zu 400/500 bzw. zu „alles aus“ mit
  Alarm, nicht zum Absturz. Simulator: Szenario-Reset nur angemeldet.
- Noch nicht enthalten: HTTPS, signiertes OTA, Secure Boot
  (`docs/SICHERHEIT.md`).
