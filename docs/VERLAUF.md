# Verlauf, Ereignisse, Export

Stand 06.10.2026. Vorschlag `software`, im Prototyp umgesetzt (`history.*`,
`events.*`).

## Messreihen in drei Stufen

| Stufe | Raster | Dauer | Werte | Speicher je Reihe (float) |
|---|---|---|---|---|
| L0 | 10 s | 24 h | Mittelwert | 34 KB |
| L1 | 1 min | 7 Tage | min/Ø/max | 121 KB |
| L2 | 15 min | 1 Jahr | min/Ø/max | 420 KB |

- **Abfrage:** Die Abfrage nimmt die feinste Stufe, die den Zeitraum noch
  abdeckt. Sie dünnt auf höchstens N Punkte aus und behält dabei Minimum und
  Maximum.
- **Lücken bleiben Lücken:** NaN, in der API `null`, im Diagramm eine
  Unterbrechung, im CSV ein leeres Feld. Quelle: RAT-006, RAT-016.
- **Was gespeichert wird:** Nur gültige Werte der Sensorwahrheit kommen in
  den Verlauf. Ungültige Werte werden zur Lücke, nicht zu einem falschen
  Punkt.
- **Reihen:** jede Rolle mit `series: true` (pH, EC, Wassertemperatur,
  Füllstand, Klima) und das bekannte Tankvolumen.

**Auf dem Hub** (Vorschlag `software`, noch umzusetzen):

- Die Reihen kommen in eine eigene Flash-Partition als Ringpuffer: anhängen
  an gelöschte Sektoren, gelöscht wird nur beim Umlauf.
- Werte als int16 skaliert plus 4 Byte Zeitstempel. Für ca. 12 Kanäle ergibt
  das rund 4,7 MB inklusive Ereignislog.
- Partitionen auf 16 MB: 2 × 3 MB App (A/B), 1 MB LittleFS für die
  Konfiguration, ca. 9 MB Verlauf.
- Der Verschleiß ist unkritisch: NOR-Flash hält ca. 100.000 Zyklen, L0
  rechnerisch über 250 Jahre.

**Uhrzeit ohne Batterie-RTC:** SNTP, sonst die Uhrzeit des Browsers bei der
Anmeldung. Sätze ohne gesicherte Zeit werden markiert. Ob ein RTC-Baustein
auf die Platine kommt, entscheidet `hardware`.

## Ereignislog

Ein Ringpuffer von 5.000 Einträgen. Jeder Eintrag hat Zeitpunkt, Typ,
Schwere, Titel und Text, dazu Daten.

| Typ | Beispiele |
|---|---|
| `dose` | je Auftrag: Kanister, Ist-ml, Soll-ml, Ist-Laufzeit, Zweck (mix, manual, ec, ph) |
| `mix` | gestartet, fertig mit Mengen, unterbrochen (auch durch Neustart), fortgesetzt |
| `control` | „pH korrigiert 6,29 → 5,88 mit 2 Gaben“, „EC nachdosiert …“ |
| `block` / `unblock` | Sprungsperre mit Werten, aufgehoben |
| `alarm` | Trockenlauf, Zulauf-Notabschaltung, Regelung ohne Wirkung |
| `tank` | Zulauf auf/zu, nachgefüllt (berechnet/gemessen) |
| `calibration` | Pumpe eingemessen, Sonde kalibriert |
| `device` | erkannt, getrennt, wieder da („Sitzt sie noch auf Teil A?“) |
| `config` | jede Änderung mit Revision |
| `auth` | Anmeldung, Fehlversuch, Passwortwechsel (sicherheitsrelevant protokolliert, CRA Anhang I 2(l)) |
| `system`, `grow`, `measure` | Start, Not-Halt, Pflegemodus, Updates; Durchgang, Phase, Ernte; Handmessung |

Damit ersetzt das Log das Nachtragen in einem Tagebuch.

## Export

- **CSV** je Zeitraum: Semikolon, Dezimalkomma, leere Felder statt 0.
- **Einstellungen** als JSON sichern und laden. Das Laden wird geprüft und
  schaltet vorher alle Aktoren aus.
- **Diagnosepaket** für „Problem melden“, siehe `SICHERHEIT.md`.

## Später

- **Begleiter** (Docker/NAS/HA-Add-on) für Langzeitarchiv und Vergleich von
  Durchgängen nach Durchgangstag; optional MQTT oder Influx Line Protocol.
- **Zusammenfassung am Ende eines Durchgangs:** Wasser, ml und € Nährstoff,
  Phasendauern, Ertrag von Hand. Quelle: RAT-016, RAT-064.
