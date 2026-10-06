# Simulator (digitaler Zwilling)

Stand 06.10.2026. `sim/` ersetzt nur Bus, Geräte, Tank und Uhr. Der Kern ist
derselbe Code wie auf dem Hub.

## Starten

```bash
tools/dev.sh                  # Demo: eingerichtet, 48 h Verlauf, http://127.0.0.1:8080, Passwort „demo-passwort“
SCENARIO=neu tools/dev.sh     # leerer Hub (Stufe 0) mit Ersteinrichtung
./build/gc_sim_server --help  # alle Optionen (Port, Zeitraffer, Datenordner …)
```

Für Arbeit an der UI zusätzlich `cd web && npm run dev`: Hot-Reload auf Port
5173, die API wird an 8080 weitergereicht.

Das Demo-Passwort gibt es nur im Simulator. Ein Gerät hat nie ein
Standardpasswort (EN 18031-1).

## Szenarien

| Name | Hardware | Zustand |
|---|---|---|
| `neu` | Dosierblock an Port 1 mit 3 Kappen (A, B, CalMag) | leer, Ersteinrichtung |
| `stufe1` | dazu pH/EC-Kopf an Port 3, Kappe pH−, 40 L im Tank | leer |
| `demo` | dazu Füllstands-Kopf an Port 5, Klima-Kopf an Port 6, Umwälzpumpe und Zulaufventil an den Hub-Ausgängen, Steckdosenleiste im WLAN mit Licht, Abluft, Umluft und Befeuchter | über die echte API eingerichtet; Licht, Abluft und Umluft an; Mischlauf, Durchgang, Vorlauf (Standard 48 h) mit Nachfüllen, EC- und pH-Regelung und einer Sprungsperre |

## Modell und Zahlen

| Größe | Wert im Zwilling | Herkunft |
|---|---|---|
| EC-Wirkung Teil A, Teil B | je 0,275 mS/cm je ml/L | RAT-055 (0,275 je ml/L Paar, Osmose, 20 L) |
| EC-Wirkung CalMag | 0,217 mS/cm je ml/L | RAT-080 |
| pH− | −4,0 pH je ml/L × Pufferfaktor 1,6/(0,6+EC); +1,04 mS/cm je ml/L | RAT-050 misst −5,0 (einmal, bei EC 2,96), RAT-053; Pufferfaktor ist **Annahme** |
| pH-Absenkung durch A/B | −0,12/−0,10 pH je ml/L | **Annahme** |
| Durchmischung | t63 26 s × V/20 L mit Umwälzpumpe, 240 s ohne | RAT-052; ohne Pumpe **Annahme** |
| pH-Totzeit | 60 s | RAT-052 misst 80–99 s; gekürzt |
| Rauschen | pH σ 0,006, EC σ 0,004, Temperatur σ 0,02, Pegel 2 mV | RAT-082 (EC hier größer gewählt) |
| Messintervall | 5 s je Kopf | wie an der Referenzanlage |
| Förderrate der Kappen | 42–53 ml/min, zufällig je Kappe | RAT-054: 38–53 ml/min |
| Zulauf | 2 L/min | RAT-038: 1,44–1,54 L/min |
| Drift | pH steigt, EC sinkt leicht, Wasser verdunstet | **Annahme**, nicht gemessen |
| Sondenfehler vor Kalibrierung | pH +0,18 Offset, Steigung 0,97; EC × 1,08 | **Annahme** |
| Zeitlimit des Dosierblocks | 90 s; Wiederholung derselben Job-ID läuft nicht doppelt | Vorschlag `firmware` |
| Pegel-Kennlinie | unten nichtlinear | wie RAT-078 |

## Störknöpfe (Simulator-Panel und `POST /api/v1/sim/…`)

| Aktion | Wirkung | prüft |
|---|---|---|
| pH-Sprung | Sonde +2,1 pH | Sprungsperre, Regelzeile, Ereignis |
| EC 0 | Sonde trocken | EC-Gate, Rastung „ohne Wirkung“ |
| Wert friert | Rohwerte stehen | Stillstandserkennung |
| Kopf/Füllstand offline | Gerät antwortet nicht | Datenausfall, Zulauf-Notabschaltung |
| Kappe blockieren/abziehen | Lauf scheitert nach 0,3 s bzw. Kappe fehlt | Paar-Fehler, „nachholen“; Menge aus der Laufzeit geschätzt |
| Dosierblock offline | Block antwortet nicht, Hub sieht den letzten Stand | Frist je Lauf: Pumpen aus, als gelaufen gezählt |
| Kappe an Hub-Port 2 | Kennung passt nicht, Port wird nicht freigegeben | Fehlsteck-Meldung (PD-012) |
| Stromausfall | Hub startet neu, Ausgänge stromlos | R6: alles aus, Ablauf gemeldet, nicht fortgesetzt |
| Frisches Wasser | Volumen, EC und pH setzen | Mischen, EC-Gate, Trockenlauf |
| Zeitraffer 1–300× | – | Settle-Zeiten, Verlauf |
| Szenario | alles neu; nur angemeldet, ohne Anmeldung nur mit `--allow-reset` (Playwright) | Ersteinrichtung |

## Grenzen

- **Chemie:** Das Modell ist grob. Es soll Abläufe und Texte prüfbar machen,
  nicht Rezepte vorhersagen.
- **Messungen fehlen:** Die Startwirkungen in Nährlösung (RAT-081) und die
  Mischzeit nach Volumen (M-3) sind offen.
- **Bus nicht nachgebildet:** Der Modbus-Bus selbst, also Zeitverhalten,
  Timeouts und CRC, ist nicht modelliert. Das gehört in Treibertests der
  Firmware.

## Schaltbare Steckdosen (Shelly)

| Größe | Wert im Simulator | Quelle |
|---|---|---|
| Geräte | Plug S Gen3 (1 Dose), Power Strip 4 Gen4 (4 Dosen), IP 192.168.1.60 ff. | **Annahme** |
| Zustand nach Stromausfall ab Werk | „wie vorher“, bis der Hub „aus“ setzt | **Annahme** (Werkseinstellung nicht geprüft) |
| Last je Dose | Umwälzpumpe 18 W, Licht 240 W, Abluft 35 W, Umluft 15 W, Befeuchter 30 W | **Annahme** |
| Auto-Off | wirkt im Gerät, auch ohne Hub | RAT-019, RAT-060 |
| Dose ohne WLAN | Last läuft weiter; zählt für Raumklima und Umwälzung | **Annahme** (Strom fließt unabhängig vom WLAN) |
| Störungen | WLAN weg (`offline`), Einstellung abgelehnt (`readonly`), Einstellung ignoriert (`ignore`), Schaltbefehl abgelehnt, Dose bleibt im Zustand (`stuck`, z. B. Relais klemmt) | Testfälle |

## Raumklima

| Größe | Wert im Simulator | Quelle |
|---|---|---|
| Startwerte | 21 °C, 55 % rF, 450 ppm | **Annahme** |
| Außenluft | 19 °C, 50 % rF | **Annahme** |
| Wärme durch Licht / Heizung / Entfeuchter | +6 / +4 / +1 K über Außenluft | **Annahme** |
| Feuchte durch Verdunstung | +14 % rF bei Licht, +5 % ohne | **Annahme** |
| Abluft | Gewinne × 0,45, schnellere Angleichung (τ 600 s statt 1800 s) | **Annahme** |
| Angleichung Feuchte | τ 400 s mit Abluft, 1500 s ohne | **Annahme** |
| Befeuchter / Entfeuchter | +0,8 / −0,6 % rF je Minute | **Annahme** |
| Feuchte begrenzt | 15–97 % rF | **Annahme** |
| CO2 | 420 ppm mit Abluft, 380 ppm bei Licht, sonst 600 ppm; τ 900 s | **Annahme** |
| Rauschen | Luft σ 0,05 K, Feuchte σ 0,3 %, CO2 σ 8 ppm | **Annahme** |
