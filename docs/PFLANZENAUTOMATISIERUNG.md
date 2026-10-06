# Ganze Pflanzenautomatisierung: Bereiche, Netzaktoren, Phasen

Stand 06.10.2026. Auftrag des Projektinhabers: Die Software deckt die ganze
Anlage ab, nicht nur Mischen und Tank – Licht mit Dimmung, Bewässerung,
Klima (Abluft, Umluft, Befeuchter, Entfeuchter, Heizung) und Phasen mit
eigenen Rezepten und Einstellungen. 230-V-Geräte laufen über schaltbare
Steckdosen, zuerst Shelly (lokal). Die App ist neutral formuliert:
universelle Pflanzenautomatisierung für Gewächshaus, Indoor-Anbau und
Hydroponik. Grundlage: Entwürfe von `architekt`, `anwender` und `hardware`
vom 06.10.2026.

> **Entwurf.** Alles hier ist Vorschlag, auch wo es nach Festlegung klingt
> („fest im Code“, „nur Steckergeräte“). Entschieden wird als PD im
> Produkt-Repo (`../docs/DECISIONS.md`); offene Punkte stehen in §9.

## 1. Bereiche im Datenmodell

Bisher liegen alle Rollen in `tanks[0].roles`. Neu (Schema v2, mit Migration):

```
config
├─ tanks[]  id, Nutzvolumen, Mindestfüllstand, roles{tank.*}
└─ zones[]  id, kind (room|tent|greenhouse), name, tank, roles{zone.*}
```

- Im Datenmodell sind mehrere Zonen vorgesehen; v1 erlaubt eine Zone.
- Ein Durchgang gehört zu einer Zone.
- Die Rollen heißen neutral `zone.*` statt `tent.*`. Die Migration benennt sie um.
- Rollen akzeptieren mehrere Capabilities (`accepts[]`), dürfen bei Bedarf
  mehrfach gebunden werden (`multi`) und tragen ein Sicherheitsprofil (§3).

| Bereich | Rollen |
|---|---|
| Tank | `tank.ph`, `tank.ec`, `tank.water_temp`, `tank.level`, `tank.circulation`, `tank.inlet`, `tank.heater` |
| Licht | `zone.light` (Schalten), `zone.light_dim` (0–10 V) |
| Klima | `zone.air_temp`, `zone.humidity`, `zone.co2`, `zone.exhaust`, `zone.circulation_fan`, `zone.humidifier`, `zone.dehumidifier`, `zone.heater` |
| Bewässerung | `zone.irrigation_pump` |

**Abgeleitete Werte** (fester Code, kein Skript): `zone.vpd` aus
Lufttemperatur und Luftfeuchte. Er wird nur gerechnet, wenn beide Werte gültig
und höchstens 60 s auseinander sind; sonst entsteht eine Lücke (R5).

- Sättigungsdampfdruck: es(T) = 0,6108 · exp(17,27·T / (T + 237,3)) kPa (FAO-56, Gl. 11,
  https://www.fao.org/4/x0490e/x0490e07.htm; online noch nicht gegengeprüft,
  die Seite war am 06.10.2026 vom Proxy gesperrt).
- Luft-VPD = es(T) · (1 − rF/100).
- Blatt-VPD = es(T + Δ) − es(T) · rF/100. Der Offset Δ ist optional und
  wird als „geschätzt“ gezeigt; Standard ist Luft-VPD (Quelle: RAT-017).

## 2. Ausgänge und Netz-Bus

**Capabilities:**

| Capability | Bedeutung |
|---|---|
| `switch.12v` | 12-V-Ausgang am Hub |
| `switch.mains` | 230 V über ein Netzgerät |
| `switch.dry` | potentialfreier Kontakt |
| `dim.0_10v` | Dimmwert 0–100 % |
| `measure.power` | Leistung je Kanal in W, optional |

Fest im Code: Dosiert wird nie über Netzgeräte, nur über den Dosierblock
[PD-010, PD-011].

**Shelly (Gen2 und neuer), lokal über JSON-RPC** (API-Doku
https://shelly-api-docs.shelly.cloud/gen2/; Modelle, mDNS, Digest und
Felder wie `auto_off` sind online noch nicht gegengeprüft, die Seite war am
06.10.2026 vom Proxy gesperrt). Für Endkunden kommen nur
Steckergeräte in Frage: Plug S Gen3, Outdoor Plug S Gen3, Power Strip 4 Gen4.
Einbaugeräte (1PM, Pro 4PM, Dimmer 0/1-10V) sind für Elektrofachkräfte. Die
Software bindet sie trotzdem an.

- **Stand 06.10.2026:** `INetBus` mit Shelly im Simulator, Profile im
  Gateway, Zuordnung mit Rücklesen, Testen und Handbetrieb sind umgesetzt
  (§8 Schritte 3–4). Offen: Finden per mDNS und Digest auf dem Gerät,
  „Aus nicht bestätigt“, Watchdog-Bewertung über die Leistung.
- **Eigener Netz-Bus** `INetBus` neben dem RS485-Bus `IBus`. Er hat kein
  `startRun`; damit ist schon über den Typ ausgeschlossen, dass übers Netz
  dosiert wird. Nur `Actuators` schaltet (R1).
- **Finden:** mDNS `_shelly._tcp` oder IP von Hand.
  - Das Gerät wird an seiner Shelly-ID erkannt; die IP ist nur ein
    Laufzeitattribut.
  - Es braucht eine Anmeldung per Digest. Der Hub erzeugt das Passwort je
    Gerät, wenn der Nutzer zustimmt.
- **Übernehmen:**
  - Der Hub zeigt die geplante Sicherheitskonfiguration, schreibt sie und
    liest sie zurück.
  - Rollen mit Sicherheitsprofil werden erst gebunden, wenn das Rücklesen
    stimmt.
- **Steckdosen heißen nach ihrem Zweck**, z. B. „Steckdose Abluft“.
  - Jede Steckdose hat einen Knopf „Testen“ (3 s an).
  - In Leisten wird ab 1 gezählt. Die Shelly-Oberfläche zählt ab 0, das wird
    einmal gesagt (Quelle: RAT-019).

## 3. Sicherheit der Netzaktoren

| Profil | Rollen | nach Stromausfall | Grenze im Shelly | Software |
|---|---|---|---|---|
| dauer | Licht, Umluft, Abluft, Umwälzpumpe | aus (Lüfter V: an) | – | – |
| puls | Befeuchter, Gießpumpe, Zulauf | aus | **Auto-Off Pflicht**, knapp über der Software-Grenze | Höchstlaufzeit, Wartezeit |
| kompressor | Entfeuchter | aus | – | Mindestlauf 10 min, Mindestpause 5 min (Quelle: RAT-034) |
| heizen | Heizung ohne eigenen Thermostat (z. B. Heizstab) | aus | **Auto-Off Pflicht** (6000 s bei 90 min Software-Grenze; Quelle: RAT-060), `power_limit` | Sperren an der Sensorwahrheit, Rastung |
| versorgen | Gerät mit eigenem Thermostat, das Relais gibt nur Strom | aus | kein Auto-Off, aber `power_limit` als Netz für einen hängenden Thermostat (Quelle: RAT-069) | Bewertung durch den Watchdog |

Fest im Code (R7):

- Befeuchter und Entfeuchter laufen nie gleichzeitig (Quelle: RAT-034).
- Einen Dimmwert unter der Einschaltschwelle hebt der Hub auf die Schwelle;
  0 heißt aus, und das Relais schaltet mit ab (Quelle: RAT-013, RAT-029).
- Nach einem Neustart sendet der Hub „aus“ an alle Kanäle. Abläufe (Gabe,
  Puls, Zulauf) werden nicht fortgesetzt. Zustandsfunktionen (Licht, Lüfter,
  Klima) rechnen erst neu, wenn Uhrzeit und Sensorwahrheit gesichert sind.
  Ohne gesicherte Uhrzeit bleibt das Licht aus (Präzisierung von R6).
- Not-Halt schaltet alles aus, auch die Lüfter. Nicht erreichbare Ausgänge
  zeigen „Aus nicht bestätigt“, und der Hub wiederholt den Befehl.

Der Watchdog bewertet zusätzlich:

- Soll ≠ Ist;
- „soll aus, zieht > 2 W“ (Runaway; Quelle: RAT-073);
- Licht in der Dunkelphase;
- Trockenlauf über die Leistung (Quelle: RAT-049).

Er schaltet nichts (R2).

**[SICHERHEIT] Für die Anleitung:**

- Netzgeräte stehen außerhalb des Pflanzraums.
- Alles im Wasser hängt hinter einem FI-Schutzschalter mit 30 mA.
- Heizgeräte mit dem Warnhinweis „nicht mit Zeitschaltuhr betreiben“ aus
  EN 60335-2-30 werden nicht angeschlossen (Norm nicht eingesehen, Angabe
  des `hardware`-Entwurfs).
- Einbaugeräte schließt nur eine Elektrofachkraft an.

## 4. Neue Funktionen (Katalog)

| Funktion | braucht | Parameter (P = aus der Phase) |
|---|---|---|
| `light_schedule` | `zone.light`, gesicherte Uhrzeit | `on_at`, `light_hours` P, `intensity_pct` P, `ramp_min` 15 (Quelle: RAT-035), Einschaltschwelle (Quelle: RAT-013) |
| `circulation_fan` | `zone.circulation_fan` | Modus (immer, Intervall, mit Licht) |
| `climate_control` | Lufttemperatur, Luftfeuchte, mindestens ein Klimagerät | Temperatur Tag/Nacht P, rF oder VPD Tag/Nacht P, Hysterese, Mindestzeiten (Quelle: RAT-034), Heizungsgrenze (Quelle: RAT-060) |
| `vpd_watch` | Lufttemperatur, Luftfeuchte | VPD-Ziel Tag/Nacht P, Toleranz, Blatt-Offset |
| `irrigation` | `zone.irrigation_pump`, Uhrzeit | Gaben je Tag P, Faktor der ersten Gabe P, Dauer je Gabe P (Quelle: RAT-010), Mindeststand im Tank (Quelle: RAT-067) |

- „Tag“ und „Nacht“ richten sich nach dem Licht, nicht nach der Uhrzeit.
- Bei VPD-Führung wird der rF-Sollwert aus dem VPD-Ziel und der
  **Soll**-Temperatur gerechnet (Quelle: RAT-009, RAT-017).

## 5. Phasen

Phasen sind Parametersätze über alle Bereiche (R4). Das Vokabular steht
zentral im Katalog (`phaseParams`), gruppiert nach Licht, Klima, Wasser und
Bewässerung. Der Phasen-Editor zeigt nur Gruppen, für die Hardware da ist;
Einsteiger sehen die Grundwerte, alles andere steht unter „Mehr
Einstellungen“.

| Wert | Wachstum | Blüte | Quelle |
|---|---|---|---|
| Lichtstunden | 18 h | 12 h | RAT-066 |
| Dimmung | 50 % | 75 % | Annahme |
| Temperatur Tag | 26 °C | 27 °C | RAT-009 |
| rF bzw. VPD | 70 % ≈ 1,0 kPa | 62 % ≈ 1,35 kPa | RAT-009 |
| pH-Ziel | 5,9 | 5,9–6,1 | RAT-066 |
| EC-Ziel | Herstellerplan | Herstellerplan | RAT-066 |
| Gaben je Tag | 6 | 8–9 | RAT-010 (Steinwolle) |

Sinkt das EC-Ziel beim Phasenwechsel um mehr als 0,2 mS/cm, weist die App
darauf hin, den Tank ganz abzulassen (Quelle: RAT-012). Liegen Rest und
Frischwasser unter dem neuen Ziel und lässt sich aufdosieren, entfällt das
Ablassen; beim Wechsel des Produkts bleibt es (Quelle: RAT-065, K-1).
Teilweises Ablassen wäre eine eigene Annahme und ist nicht vorgesehen. Der
Hub kann nicht verdünnen.

## 6. Bedienung

- **Navigation nach Bereichen:** Übersicht · Phasen · Tank & Mischen ·
  Bewässerung · Klima · Licht · Rezepte & Nährstoffe · Verlauf · Geräte ·
  Einstellungen (mit „Funktionen“).
  - Bereiche ohne Hardware sind ausgeblendet. Den Weg zu mehr zeigt Geräte ›
    Erweitern.
  - Am Handy: Übersicht · Tank · Phasen · Verlauf · Mehr.
- **Übersicht nach Dringlichkeit:**
  1. Banner (Not-Halt, Pflegemodus, getrennt);
  2. Statuszeile;
  3. Durchgang;
  4. „Gerade läuft“;
  5. Karten für Tank, Klima, Licht, Bewässerung;
  6. Kanister;
  7. Ereignisse.

  Karten ohne Gerät fehlen ganz.
- **Ganze Bildschirmbreite:** Die Karten fließen in Spalten. Auf kleinen
  Bildschirmen ordnen sie sich untereinander an, und nichts läuft aus seiner
  Box.
- **Setup-Assistent in 5 Schritten:**
  1. Start;
  2. Geräte;
  3. Tank;
  4. Nährstoffe: Vorlage zuerst, mit Vorschau;
  5. Einmessen.

  Licht, Klima, Bewässerung und Sonden sind überspringbare Mini-Assistenten
  auf ihren Seiten. Die feste Fußleiste hat „Zurück“ (auch auf „Fertig“),
  „Überspringen“ und „Weiter“, wobei „Weiter“ speichert.
- **Vorlagen ordnen über Rollen zu** (Teil A, Teil B, CalMag, Zusatz), nicht
  über exakte Namen.
- **Begriffe** stehen mit Erklärung im Glossar (Deutsch und Englisch,
  `web/src/lang`). Umbenannt werden:
  - Trockenlaufgrenze → Mindestfüllstand;
  - Rastung → „Gesperrt bis Freigabe“;
  - EC-Gate → „pH-Sperre bei wenig Nährstoff“;
  - Dosierblock-Port → „Pumpe 1–6“;
  - Hub-Port → „Anschluss 1–8“.
- **Geräte** zeigen ein Schaubild:
  - Hub mit Anschlüssen und Ausgängen;
  - darunter Dosierblock, Sammelbox und Köpfe;
  - Netzgeräte je Kanal;
  - „Erweitern“ mit dem, was ein Gerät freischaltet.

  Die Daten dafür liefert `GET /topology`.

## 7. pH und EC: eine oder zwei Sonden

Vorschlag (beantwortet die Roadmap-Frage aus Stufe 1, braucht eine PD). Es
gibt drei Geräteklassen:

- `head_ph_ec` (pH, EC, Wassertemperatur);
- `head_ph`;
- `head_ec` (EC, Wassertemperatur).

Die Rollen bleiben gleich. Der Hub ordnet eine Messrolle nur zu, wenn genau
ein Gerät sie liefert; liefern zwei Köpfe dasselbe, wählt der Nutzer unter
Geräte › Zuordnung. Umgesetzt im Simulator (Schema v2). Zwei Köpfe brauchen zwei Anschlüsse und je eine eigene galvanische
Trennung.

## 8. Reihenfolge der Umsetzung

Jeder Schritt ist im Simulator testbar.

1. Oberfläche:
   - volle Breite und Umsortieren;
   - Überlauf beheben;
   - neutrale Wortwahl;
   - Deutsch/Englisch (Grundgerüst);
   - Assistent mit 5 Schritten, Zurück, Erklärungen;
   - Vorlagen mit Vorschau;
   - Pumpen-Benennung.
2. Schema v2:
   - `zones[]`, Rollen je Instanz, `accepts`/`multi`;
   - Migration;
   - `phaseParams`;
   - Kopf-Klassen pH/EC einzeln.
3. Gateway für Schaltrollen allgemein:
   - Profile, Konfliktpaar, Mindestzeiten;
   - Dimmen mit Schwelle;
   - `stopAll` über alle Instanzen.
4. `INetBus` mit Shelly im Simulator (Auto-Off, Leistung, offline, Taster);
   Übernehmen mit Rücklesen.
5. Funktionen in dieser Folge: `light_schedule`, `circulation_fan`,
   `climate_control` mit `vpd_watch` und Reihe `zone.vpd`, `irrigation`.
6. Navigation nach Bereichen, Übersichtskarten, `/topology` mit dem
   Geräte-Schaubild.
7. Firmware: mDNS und HTTP-RPC-Client mit Digest.

## 9. Offene Fragen an den Projektinhaber

1. **Phasenwechsel:** automatisch nach Tagen oder erst nach Bestätigung?
2. **Licht, wenn der Hub ausfällt:**
   - Auto-Off: Der nächste Tag bleibt dunkel.
   - Lokaler Zeitplan im Shelly.
3. **Pflegemodus:** Was ruht außer dem Dosieren, auch das Gießen?
4. **Einheiten im Englischen:** Liter/°C fest, oder auch Gallonen/°F und EC als ppm?
5. **Dimmen:** Über einen eigenen Dimm-Kopf als Busteilnehmer (Empfehlung
   `hardware`, Stufe 4) und vorerst über Shelly-Dimmer? Kein 0–10 V am Hub
   (berührt PD-006).
6. **Herstellertabellen als Vorlage** (z. B. Athena Blended): Dürfen sie
   ins Produkt? Wer pflegt den Stand?
