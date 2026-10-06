# Changelog

Alle wesentlichen Änderungen an growcontroller-software. Format nach
[Keep a Changelog 1.1.0](https://keepachangelog.com/de/1.1.0/), Versionen
nach [SemVer](https://semver.org/lang/de/). Sicherheitsrelevante Änderungen
stehen in jedem Release unter „Sicherheit“.

## [Unreleased]

### Neu
- Simulator als Download für Windows, macOS (Apple-Chip und Intel) und
  Linux: eine Datei mit eingebetteter Web-App; Doppelklick startet die Demo
  und öffnet den Browser, die Daten liegen neben dem Programm. Ist der Port
  belegt, nimmt er den nächsten freien; ist der Ordner nicht beschreibbar,
  läuft die Demo nur im Speicher. Releases hängen die Pakete automatisch an,
  mit Lizenzhinweisen der enthaltenen Bibliotheken
  (`THIRD_PARTY_LICENSES.txt`, auch im Web-Paket; mit den Fremdteilen in
  nlohmann/json und dem Apache-2.0-Wortlaut). Ein Firmware-Image kommt erst
  ins Release, wenn es offline signiert wird (`docs/RELEASE.md`); die CI
  baut die Firmware weiterhin bei jedem Lauf.
- **Einrichtung in fünf Schritten** (Start, Geräte, Tank, Nährstoffe,
  Einmessen): feste Leiste mit Zurück, Überspringen und Weiter; erledigte
  Schritte anklickbar; Erklärungen (ⓘ) zu Nutzvolumen, Mindestfüllstand,
  Kanister, Paar und Einmessen; Nährstoffe beginnen mit einer Vorlage samt
  Vorschau, vorhandene Rezepte sind sichtbar.
- **Rezept-Vorlagen** mit Vorschau und Zuordnung zu den eigenen Kanistern
  statt Namensabgleich: Zweikomponenten-Dünger, Athena Blended Wachstum und
  Blüte (nach Feed Program A01.004). API: `POST /recipes/template` nimmt `map`
  (Teil → Kanister), meldet fehlende Teile in `missing` und lehnt einen
  Kanister für zwei Teile ab; ein Paar der Vorlage geht auf Kanister ohne
  eigenes Paar über, bei schon vergebenem Namen als „AB2“ usw. Die
  Herstellerquelle steht auch auf Englisch (`sourceEn`). Die bisherigen Vorlagen `athena_pro_veg` und
  `ab_basic` entfallen.
- **Anbaubereich (Schema v2):** Die Einrichtung fragt, wo die Pflanzen
  stehen (Raum, Zelt, Gewächshaus), mit eigenem Namen. Im Datenmodell ist das
  eine Zone mit eigenen Rollen (`zone.*`, vorher `tent.*` am Tank); die
  Konfiguration wird beim Start migriert. API: `PUT /zone`. Für fremde
  API-Clients: `/roles/tent.*` heißt jetzt `/roles/zone.*`; Messreihen vor
  dem Update bleiben unter dem alten Namen.
- **pH und EC als ein Kopf oder zwei:** neue Geräteklassen „Sensorkopf pH“
  und „Sensorkopf EC“ (mit Wassertemperatur) neben dem gemeinsamen
  pH/EC-Kopf. Messrollen werden nur zugeordnet, wenn genau ein Gerät passt.
  Einmessen und Geräte zeigen die Kalibrierungen je Kopf aus dem Katalog; im
  Simulator lassen sich beide Varianten stecken, Störungen treffen den
  passenden Kopf. Welche Hardware-Variante es geben wird, ist offen (PD
  folgt); die Software trägt beide.
- **Schaltbare Steckdosen (Shelly, lokal), im Simulator:** Steckdosen
  (Plug S Gen3, Power Strip 4 Gen4) erscheinen in der Einrichtung und unter
  Geräte. Je Dose sagt man, was eingesteckt ist (Umwälzpumpe, Licht, Abluft,
  Umluft, Befeuchter, Entfeuchter, Gießpumpe, Zulauf), und testet sie (3 s
  an). Beim Übernehmen setzt der Hub jede Dose auf „nach Stromausfall aus“;
  beim Zuordnen schreibt er die Schutzeinstellung des Profils ins Gerät
  (Auto-Off bei Befeuchter, Gießpumpe und Zulauf) und ordnet erst zu, wenn
  das Rücklesen stimmt; vor jedem Einschalten prüft er sie erneut. Auf dem
  Gerät folgen Finden per mDNS, Anmeldung und RPC. Welche Steckdosen der
  Shop führt, ist offen (PD folgt). API: `POST /roles/{rolle}/test`,
  `POST /roles/{rolle}/switch {on}` (Handbetrieb).
- **Prüfung:** ein Schaltausgang nur für eine Rolle, Kanal im Bereich des
  Geräts; „Ventil höchstens offen“ höchstens 25 min.
- **Bereiche Klima, Licht, Bewässerung:** eigene Seiten in der
  Navigation mit Messwerten und Schaltausgängen samt Handbetrieb; die
  Übersicht zeigt Raumklima und alle zugeordneten Schaltausgänge.
  Automatische Regelung (Lichtplan, Klima, Gießplan) gibt es noch nicht.
- **VPD:** Luft-VPD ohne Blatt-Offset; fehlt ein Quellwert, entsteht
  eine Lücke statt 0 (Quelle: RAT-017). Formel FAO-56 Gl. 11. Beide
  Werte dürfen höchstens 60 s auseinander liegen (eigene Regel, Annahme).
  Im Verlauf mit Lufttemperatur, Feuchte und CO2.
- **Simulator:** einfaches Raumklima (Licht und Heizung wärmen, Abluft
  tauscht Luft, Befeuchter und Entfeuchter, Verdunstung bei Licht;
  Annahmen); Klima- und CO2-Kopf liefern Werte. Die Demo hat einen
  Klima-Kopf und eine Steckdosenleiste mit Licht, Abluft, Umluft und
  Befeuchter. Eine Dose ohne WLAN versorgt ihre Last weiter (Raumklima,
  Umwälzung). Neue Störung `stuck`: Schaltbefehl abgelehnt, die Dose
  bleibt im Zustand.
- **Deutsch und Englisch:** Einrichtung, Navigation, Rahmen, Zahlen und
  Datum; weitere Seiten folgen. Die Sprache wird am Hub gespeichert und ist
  je Browser wählbar.

### Sicherheit
- **Sicherheitsprofile im Aktor-Gateway** für Steckdosen und 12-V-Ausgänge:
  dauer, puls, kompressor.
  - Befeuchter und Entfeuchter laufen nie zugleich; ist der Zustand des
    Gegengeräts unbekannt, bleibt das andere aus (Quelle: RAT-034, R5).
  - Entfeuchter: 5 min Pause nach dem Ausschalten, auch nach Not-Halt und
    Neustart (Quelle: RAT-034).
  - Höchstlaufzeit für Befeuchter (5 min, Annahme), Gießpumpe (10 min,
    Annahme) und Zulauf (25 min, Quelle: RAT-079); das Gerät
    schaltet knapp danach selbst ab.
  - Gießpumpe nur über dem Mindestfüllstand, im Lauf darunter aus; bei
    unlesbarem Pegel gesperrt (Abweichung von RAT-068, PD folgt).
  - Befeuchter: Ist ein Feuchtesensor zugeordnet, nur mit gültigem Wert
    unter 85 % (Annahme).
  - Umzuordnen oder Entfernen schaltet den alten Ausgang erst aus; klappt
    das nicht, steht „Aus nicht bestätigt“ im Ereignisprotokoll.
  - Not-Halt und Neustart schalten alle Schaltrollen aus;
    laufen Umluft und Abluft nicht nach (Quelle: RAT-036).
  - Heizungen gibt es noch nicht als Rolle: erst mit der rastenden
    Notabschaltung nach RAT-060.

### Geändert
- Eingebettete Texte (Katalog, Changelog) als Byte-Felder, damit der Kern
  auch mit MSVC übersetzt.
- Katalog Version 2 (Rollen `zone.*`, Köpfe pH und EC einzeln),
  Konfiguration Schema 2.
- Benennung: „Anschluss 1–8“ am Hub, „Pumpe 1–6“ am Dosierblock,
  „Sensorkopf“, „Raum“ statt „Zelt“, Untertitel „Pflanzenautomatisierung“.
  Anschlüsse zeigen das Symbol des Geräts.
- Die App nutzt die ganze Bildschirmbreite; Kacheln brechen um, Tabellen
  der Einrichtung werden auf dem Handy zu Karten.
- Meldungen sagen „Mindestfüllstand“, „Anschluss n“ und „Pumpe n am
  Dosierblock“.

### Behoben
- Messwert-Kacheln: Bei Sensorausfall ragten Hinweis und Kurve aus der
  Kachel.
- Demo: Die Phase „Blüte“ verwies auf ein fehlendes Rezept. IDs schreiben
  Umlaute jetzt um („Blüte“ → „bluete“).
- Rezepte mit demselben Kanister zweimal werden abgelehnt.
- Namen (Hub, Bereich, Tank, Gerät, Kanister, Rezept, Durchgang) werden
  auf 40 Byte gekürzt, ohne ein Zeichen zu zerschneiden; vorher machte ein
  Umlaut an der Grenze die Konfiguration unlesbar.
- Übernimmt man ein Gerät, ordnet der Hub nur dessen Messrollen zu; eine
  bewusst gelöste Rolle bleibt gelöst.
- Simulator: „Wert friert“ hält den letzten Wert fest statt „kein Wert“.
- `PUT /system`: Eine abgelehnte Angabe ändert auch die übrigen nicht.
- Simulator: macOS bindet ohne `SO_REUSEADDR`; ein nicht beschreibbarer
  Datenordner wird gemerkt („nur im Speicher“), statt vergessen; das
  Paket-Skript erkennt Windows auch lokal.

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
