# Konfigurationsmodell: Katalog, Rollen, Funktionen, Resolver

Stand 06.10.2026. Entwurf `architekt`, im Prototyp umgesetzt.

## 1. Die Kette in einem Satz

Ein **Gerät** meldet **Capabilities**. Eine **Rolle** bindet eine Capability
über die **Geräte-ID** an einen Platz, etwa „pH im Tank“. Eine **Funktion**
verlangt Rollen, Kalibrierungen und Einstellungen. Der **Resolver** sagt je
Funktion, ob sie geht und was fehlt. Die UI klappt danach Einstellungen auf
oder zu.

```
Gerät (DB-7A31C0, PHEC-3F2A91 …)        ← erkannt am Port, übernommen vom Nutzer
  └─ Capability (measure.ph, dose.peristaltic, switch.12v …)
       └─ Rolle (tank.ph, tank.circulation, Kanister „Teil A“ → Pumpe)
            └─ Funktion (pH regeln) ← Voraussetzungen aus dem Katalog
                 └─ Parameter (Ziel-pH …) ← Vorgabe ⊕ Einstellung ⊕ aktive Phase
```

## 2. Katalog (`catalog/catalog.json`)

Der Katalog ist Daten. Er wird zur Bauzeit eingebettet und kommt mit der
Firmware. Inhalt:

- **capabilities**: Einheit, Nachkommastellen, Plausibilitätsband,
  Frischegrenze, Sprungschwelle.
- **deviceClasses**: Stufe, Anschluss (Hub-Port, Dosierblock-Port, Box, im
  Hub), gelieferte Capabilities, Kalibrierbedarf, Shop-Text.
- **roles**: Platz → Capability, und ob er als Messreihe in den Verlauf geht.
- **functions**: Stufe, Gruppe, Text, harte und weiche Voraussetzungen,
  Parameter mit Typ, Bereich, Vorgabe und dem Vermerk „von der Phase
  überschreibbar“.
- **templates**: Rezeptvorlagen.

**Feste Voraussetzungsarten** (neue Art = Code, neue Funktion = nur Daten):

| Art | Beispiel | prüft |
|---|---|---|
| `role` | `{"role":"tank.ph","calibrated":true}` | Hardware da → Rolle gebunden → kalibriert → Wert gültig (Laufzeit) |
| `canisters` | `{"canisters":{"min":1,"kind":"ph_down","calibrated":true}}` | Kanister des Typs mit Pumpe, eingemessen, erkannt |
| `config` | `{"config":"recipe"}` | Rezept vorhanden, Nutzvolumen gesetzt |
| `function` | `{"function":"circulation"}` | andere Funktion eingeschaltet |
| `device` | `{"device":"pump_cap"}` | Geräteklasse vorhanden |

Skriptsprachen (Lua/JS) im Katalog wurden verworfen: Sie vergrößern die
Angriffsfläche, und die Sicherheit läge im Skript (`architekt`).

## 3. Zustände je Funktion (zwei Achsen)

**Einrichtung** (Resolver):

| Zustand | Bedeutung | UI |
|---|---|---|
| `unavailable` | Gerät der nötigen Klasse fehlt | „Dafür brauchst du: Kopf pH/EC“, sichtbar unter Geräte › Erweitern |
| `needs_setup` | Gerät da; es fehlen Zuordnung, Kalibrierung oder Einstellung | Checkliste mit „Jetzt erledigen →“ |
| `limited` | alles Harte erfüllt, Weiches fehlt | „Eingeschränkt: Ohne Umwälzpumpe … umrühren“ |
| `ready` | vollständig | Schalter frei |

**Laufzeit** (Regler): `off`, `idle` (ruht), `working` (regelt), `waiting`
(wartet), `blocked` (Einschaltsperre, hebt sich selbst auf), `latched`
(gerastet, Quittierung nötig; Quelle: RAT-062).

Ein eingerichtetes Gerät, das gerade nicht antwortet, macht eine Funktion
nicht „nicht verfügbar“. Es zeigt „gesperrt: Gerät antwortet nicht“.

## 4. Konfigurationsbaum (`config.json`)

```
config {schemaVersion, revision}
├─ system      Name, Zeitzone, Sprache, Update-Kanal, Update-Prüfung
├─ limits      Grenze je Handgabe, Laufzeitgrenzen
├─ devices[]   Geräte-ID → Klasse, Name   (flaches Inventar)
├─ tanks[]     Nutzvolumen, Mindestfüllstand, Wasser, roles{tank.* → Gerät/Kanal}
├─ zones[]     Anbaubereich: Name, Art (Raum/Zelt/Gewächshaus), Tank, roles{zone.* → Gerät/Kanal}
├─ canisters[] Name, Typ (Nährstoff/pH−/pH+), Pumpe, Paar, Farbe, Größe
├─ recipes[]   Schritte in Dosierreihenfolge (Kanister, ml/L)
├─ functions{} eingeschaltet, Parameter
├─ calibrations{Gerät → ph | ec | tank_curve}
└─ grow        none | running | completed; Phasen = Parametersätze
```

Getrennt davon liegen:

- `state.json`: Vorrat, bekanntes Volumen, gelernte Wirkungen, Rastungen,
  Sprungsperren, Handmessungen. Er ändert sich laufend; dafür braucht es keine
  neue Konfigurations-Revision.
- `auth.json`: nur Hash und Salz.
- `events.json`, `history.bin`.

**Versionierung:**

- `schemaVersion` ist eine ganze Zahl. Migrationen sind reine Funktionen
  vN → vN+1 (`migrateConfig`) und getestet.
  - v1 → v2: Rollen `tent.*` am Tank werden `zone.*` an der ersten Zone.
    Messreihen und Sprungsperren unter den alten Namen werden nicht
    umbenannt; der Klimaverlauf vor dem Update bleibt unter `tent.*`.
- Ist die Datei unlesbar, startet der Hub mit Werkseinstellung, alle Aktoren
  aus. Er meldet das laut und sichert die defekte Datei als
  `config.broken.json`.
- `revision` zählt jede Änderung. Vorgesehen ist sie als ETag gegen
  gleichzeitiges Bearbeiten.

**Prüfung in drei Stufen:**

1. Struktur beim Lesen.
2. Verweise: Rolle → Gerät mit passender Capability; Rezept → Kanister.
3. Fachlich und sicherheitlich:
   - pH nie im Rezept;
   - Paare vollständig;
   - eine Pumpe nur an einem Kanister;
   - ein Kanister nur einmal je Rezept;
   - Rollen am richtigen Ort (`tank.*` am Tank, `zone.*` an der Zone), höchstens eine Zone mit Namen;
   - ein Schaltausgang nur für eine Rolle, Kanal im Bereich des Geräts;
   - Parameter im Bereich, Toleranz nie 0.

**Phasen:** Wirksame Parameter = Katalog-Vorgabe ⊕ Einstellung ⊕ aktive Phase
(nur Parameter mit `phase: true`). Regler sehen nur diese Sicht. Ein
umbenannte Phase ändert nichts (Test M15-1).

## 5. Bindung an die Geräte-ID

- **Der Port ist ein Laufzeitattribut** („zuletzt an Port 3“), keine
  Konfiguration. Umstecken ändert nichts.
- **Beim Übernehmen** wird eine Messrolle automatisch gebunden, wenn es genau
  einen Kandidaten gibt. Dosier- und Schaltrollen bindet der Hub nie
  automatisch.
- **Kommt eine Kappe nach dem Umstecken zurück**, fragt der Hub im
  Ereignislog: „Sitzt sie noch auf Teil A?“. Elektrisch kann er nicht
  erkennen, auf welchem Kanister sie sitzt.

## 6. Offene Punkte

- **Mehrere Tanks:** Das Datenmodell ist bereit, UI und Logik nutzen einen
  Tank.
- **Lasten an Schaltausgängen deklarieren** (Art, „stromlos zu“, Leistung)
  und daran Regeln knüpfen. Beispiel: Heizung nur an `switch.mains` mit
  Auto-Off.
- **Tausch-Assistent:** Altes Gerät fehlt, neues der gleichen Klasse ist da.
- **Kalibrieralter:** Hinweis nach N Tagen; ob weich oder hart, ist offen.
- **JSON-Schema für die Konfiguration**, damit die UI vorab prüfen kann.
