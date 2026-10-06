# Bedienkonzept der Web-App

Stand 06.10.2026. Grundlage: `anwender` (Grower-Sicht) und `kunde` (Außensicht, App-Kritik an der Konkurrenz). Im
Prototyp umgesetzt, wo nicht anders vermerkt.

## 1. Grundsätze

- **Anzeige folgt dem Ist.** „Läuft“ erscheint erst, wenn der Dosierblock es
  meldet. Quelle: RAT-037.
- **Jede Sperre ist eine sichtbare Zeile mit Grund.** Sie verschwindet von
  selbst, wenn die Sperre endet. Quelle: RAT-037, RAT-039.
- **Klartext statt Kürzel.** Die Farbe zeigt den schlimmsten Eintrag.
  Normalfall: „Alles in Ordnung (13 Prüfungen)“; einzeln erscheint nur, was
  abweicht. Quelle: RAT-042, RAT-072.
- **Drei Zustände trennen:** „Sensor liefert nicht“, „nicht kalibriert“,
  „nicht anwendbar“ (z. B. Wassertemperatur bei leerem Tank). Quelle:
  RAT-021, RAT-048.
- **Nur zeigen, was die Hardware kann.** Was fehlt, steht an einer Stelle:
  Geräte › Erweitern. Leere Kacheln „–“ und Sondenwerbung stören (`anwender`).
- **Kein Fachjargon vorne:** kein Modbus, kein RS485. Ports heißen „Port 3“
  mit Bild, Fehlsteckungen kommen als Satz. Bastelanmutung kippt den Kauf
  (`kunde`).
- **Ehrlich zum Zustand:** „gerade eben / vor 20 s“ je Messwert; „getrennt“,
  wenn die App den Hub nicht erreicht. „Die Steuerung läuft weiter, auch wenn
  die App zu ist.“

## 2. Navigation

| Bereich | Inhalt |
|---|---|
| Übersicht | Überwachung, Tank mit Messwerten und 6-h-Trend, Regelzeilen, laufender Auftrag, Vorrat, Durchgang, letzte Ereignisse |
| Mischen | Rezept, Wasser, „Neu ansetzen“/„Auffüllen“, Vorschau in ml, geführter Ablauf; Handgabe |
| Tank & Regelung | Regelzeilen mit Checkliste, Überwachung im Detail, Rastungen quittieren, Tank, Ausgänge, Pflegemodus, Durchgang und Phasen |
| Verlauf | pH, EC, Wassertemperatur, Füllstand mit Zielband und Dosier-Markierungen; Ereignisse mit Filtern; CSV |
| Rezepte & Kanister | Kanister ↔ Pumpe, Paare, Vorrat, „Kanister gewechselt“; Rezepte, Vorlagen |
| Geräte | Ports mit Prüfmessung, Geräte, Übernehmen, Einmessen und Kalibrieren; Zuordnung; Erweitern |
| Funktionen | Konfigurationsbaum nach Stufe: Zustand, was fehlt, Schalter, Einstellungen |
| Einstellungen | System, Zugang, Updates mit „Was ist neu“, Problem melden, Daten, Darstellung |

Am Handy: Übersicht, Mischen, Tank, Verlauf, Mehr. Der **STOPP**-Knopf
(Not-Halt) steht immer oben rechts.

## 3. Übersicht je Ausbaustufe

- **Stufe 0:**
  - Tank-Kachel mit „Zuletzt gemischt …“ und Eingabe der Handmessung (pH).
  - Knopf „Mischen“, Vorrat als „4 ok“.
  - Keine leeren pH/EC-Kacheln.
- **Stufe 1:**
  - pH, EC und Wasser groß, mit Zielband, Trend und Messwert-Alter.
  - Regelzeilen für EC und pH.
- **Stufe 2:**
  - Volumen gemessen (L und Balken), Regelzeile Nachfüllen.
  - „nicht anwendbar“, wenn der Tank leer ist.
- **Stufe 3–4** (offen): nächste Gabe, Drain in %, Klima und VPD je Zone.

**Regelzeile** (Antwort auf „Warum dosiert er gerade nicht?“):

- Regelt: „pH 6,40 → 5,80 · Teilgabe 2 von 8 · wartet 2:10 auf Durchmischung“
- Ruht: „pH im Ziel (5,82)“
- Gesperrt: „EC 0,10 unter 0,50: pH so nicht messbar, erst Nährstoffe“
- Tippen öffnet die Checkliste: ✓ Sonde liefert · ✓ Ruhezeit vorbei · ✗ EC-Gate …

## 4. Setup-Assistent

Kundensicht: höchstens 5 Schritte bis zum ersten Erfolg, ohne Konto. Geräte
werden erkannt, nicht ausgewählt.

| # | Schritt | Pflicht |
|---|---|---|
| – | Erstpasswort (vor allem anderen) | ja, kein Überspringen (EN 18031-1 AUM-5-1) |
| 1 | Name, Zeitzone (vom Browser); auf dem Hub zusätzlich WLAN | ja |
| 2 | Geräte erkennen, „Alle übernehmen“ | bis Dosierblock und Kappe da sind |
| 3 | Tank: Nutzvolumen, Wasser, Umwälzpumpe/Zulauf am Hub-Ausgang | Nutzvolumen ja |
| 4 | Kanister den Kappen zuordnen (Vorschlag Teil A/B als Paar, CalMag, pH−) | ja |
| 5 | Schlauch füllen, Pumpen einmessen (Messbecher; Ist-Laufzeit) | ohne Einmesswert dosiert die Pumpe nicht |
| 6 | Sonden kalibrieren (pH 7/4, EC 1,413, Füllstand-Kennlinie) | überspringbar |
| 7 | Rezept: eigenes oder Vorlage | ja |
| 8 | Fertig: was jetzt geht, was fehlt; „Zur ersten Mischung“ (Vorschlag: 10-L-Eimer) | – |

Während der Schritte 3–7 zeigt der Assistent live, was für „Nährlösung
mischen“ noch fehlt. Das kommt direkt aus dem Resolver.

**Noch offen** (`kunde`):

- QR-Code am Gerät.
- Rückfalladresse des Hotspots (Beispiel WLED: 4.3.2.1).
- Anzeige der neuen Adresse nach dem WLAN-Wechsel.
- Hinweis „Zum Startbildschirm hinzufügen“.

## 5. Fehlbedienungen, die die App abfängt

| Fehler | Abfang |
|---|---|
| Falsche Wassermenge (100 statt 10, Gallonen) | über dem Nutzvolumen gesperrt; ml je Kanister groß in der Vorschau |
| Zweimal gemischt | „Dieser Tank wurde vor 12 min gemischt. Noch einmal dosieren verdoppelt die Nährstoffe.“ – nur mit Bestätigung |
| Restlösung im Tank | Modus „Auffüllen: X L frisches Wasser“ |
| Sehr kleine Gabe | Warnung unter 1 ml; unter 1 s Pumpenlauf gesperrt |
| pH-Kanister im Rezept | abgelehnt („pH kommt immer zuletzt“) |
| Pumpe blockiert mitten im Paar | „Teil B nicht vollständig dosiert … Teil A ist schon drin … Teil B nachholen“ |
| Stromausfall im Lauf | alles aus; Ereignis „Mischlauf durch Neustart unterbrochen bei Schritt 2/3. Drin: …“ |
| Kappe direkt am Hub-Port | Port rot: „Pumpenkappe direkt am Hub. Bitte in den Dosierblock stecken.“ |
| Kappe nach Umstecken | Ereignis „Kappe … wieder da. Sitzt sie noch auf Teil A?“ |

## 6. Meldungen (Konzept, noch nicht umgesetzt)

| Stufe | Was | Wann |
|---|---|---|
| sofort, auch nachts | Pumpe geht nicht aus/blockiert, Zulauf-Notabschaltung, Überlauf, Sprungsperre bei laufender Regelung, Hub ohne Lebenszeichen | Wiederholung nach 5, 15, 60 min |
| tagsüber | Sperre > X min, pH/EC deutlich außerhalb (Alarmband), Vorrat reicht nicht | Nachtruhe 22–07 → Morgenbericht |
| nur in der App | knapp, Kalibrierung fällig, Hinweise | – |

Regeln:

- Titel ≤ 40 Zeichen, Text ≤ 200. Quelle: RAT-045.
- Quittieren friert nur die Wiederholung ein. Quelle: RAT-022.
- Weg ohne Herstellercloud: ntfy, E-Mail, Webhook, MQTT. App-Push erst mit
  einer App.
- Sicherheitsalarme sind kostenlos [PD-008].
- Ehrlich sagen: „Ohne Internet keine Benachrichtigungen.“

## 7. Gestaltung

- Eigene Design-Tokens, hell und dunkel, keine externen Schriften: Die App
  läuft offline aus dem Flash.
- Farben für Status: OK grün, Problem rot, Hinweis gelb, ruht grau. Farben
  für Messgrößen: pH violett, EC orange, Temperatur türkis, Pegel blau.
- Mobil zuerst bedienbar: untere Leiste, große Knöpfe, Zahlen mit Komma.
