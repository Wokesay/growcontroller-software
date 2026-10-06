# Fachregeln (Invarianten) – Umsetzung und Testnachweis

Stand 06.10.2026. Grundlage: Fachregeln der Referenzanlage (Module
M1–M15). „Test“ nennt die Datei unter `tests/core/` bzw. `web/e2e/`.
Status: **✓** umgesetzt und getestet · **◐** umgesetzt, ohne eigenen Test
oder mit Abweichung · **○** offen.

## M1 Mischen und Rezept

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| pH-Korrektur immer zuletzt, nie im Rezept | RAT-004 | ✓ | `validateConfig`, `planMix` · test_mix „pH im Rezept“, test_catalog_config |
| Paare an jeder Grenze gemeinsam skalieren, nie je Pumpe kappen | RAT-054 | ✓ | `planEcDose` (gemeinsamer Faktor) · test_mix M5-1; Mischen dosiert das ganze Rezept |
| Mengen auf das frische Wasser rechnen („Auffüllen“) | RAT-011 | ◐ | `planMix` mit `mode=topup` · kein eigener Test |
| Große Gabe in gleiche Teilläufe statt kürzen | RAT-055 | ✓ | `splitRuns` · test_mix „Teilläufe“. Abweichung: Mischen ohne Obergrenze der Laufzahl, Regelung höchstens 6 |
| Fehlende Größe → Abbruch, kein Ersatzwert | RAT-006, RAT-015 | ✓ | test_mix M1-4 |
| Kein Doppelstart | RAT-003, RAT-018 | ✓ | Rückfrage bei Mischung < 30 min, ein Auftrag zugleich · test_scenarios |
| Reihenfolge ist Rezeptdaten (Athena: Balance → B → A → CaMg → Cleanse) | RAT-080 | ✓ | Rezeptschritte sortierbar |

## M2 Dosierausführung

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| Nach Neustart alles aus, nichts fortsetzen | RAT-007 | ✓ | `Hub::boot`, `Actuators::stopAll` · test_scenarios „Stromausfall“. PD-020 ersetzt den Teil für Zustandsfunktionen (Lüfter, Licht, Gießen), Umsetzung offen |
| Unter 1,0 s nicht dosieren, sichtbar | RAT-050 | ✓ | `splitRuns`, Gateway · test_mix |
| Handgabe in ml begrenzt | RAT-039 | ✓ | `Limits::handDoseMaxMl` (5 ml, fest höchstens 50 ml) · test_catalog_config „Grenzen“ |
| Job-ID gegen Doppeldosierung bei Wiederholung | Vorschlag `firmware` | ✓ | Dosierblock im Simulator; jeder Versuch und jeder Start eigene ID · test_scenarios „nachholen“, „Job-IDs nach Neustart“ |
| Frist je Lauf: ohne Rückmeldung aus, als gelaufen zählen | Vorschlag `reviewer` | ✓ | `Doser::tick` · test_scenarios „Block stumm“, „Kappe abgezogen“ |
| Feste Grenzen im Code, Konfiguration verschärft nur | R7 | ✓ | `Limits::bounded`, `validateConfig` · test_catalog_config, test_api „Import“ |
| Ist-Laufzeit vom Dosierblock, nicht die angeforderte | RAT-070 | ✓ | `RunStatus::actualMs`, Buchung je Lauf |
| Zeitlimit in Hardware über dem längsten Lauf (SW 60 s / HW 90 s) | RAT-007, RAT-018 | ◐ | Gateway 60 s; Dosierblock-Zwilling 90 s. In Hardware: offen |
| Ein Kanal zugleich | PD-010, Vorschlag `firmware` | ✓ | Gateway und Zwilling lehnen ab |

## M3 Einmessen

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| Ohne gültigen Einmesswert keine Dosierung | ROADMAP, RAT-015 | ✓ | Gateway, `planMix` · test_mix M3-4, test_scenarios |
| Rate 0/NaN/negativ abgelehnt, alter Wert bleibt | RAT-003 | ◐ | `Hub::calibrationResult` |
| Einmesslauf ist kein Verbrauch, nicht durch Handgrenze gekappt | RAT-070 | ✓ | Zweck `calibration` · test_scenarios „keine Dosier-Ereignisse“ |
| Wert liegt im ID-Chip der Kappe | PD-010 | ✓ | `IBus::writePumpCalibration` |

## M4 pH-Regelung

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| Gabe = 0,8 × Lücke / Wirkung × V | RAT-055 | ✓ | `planPhDose` · test_mix M4-1 |
| Deckel min(0,3 pH, 0,3 ml/L, Höchstmenge) | RAT-050 | ✓ | test_mix M4-1 |
| Startwirkung 5,0 pH je ml/L (kleinste Dosis) | RAT-050 | ✓ | `kPhStartEffect` |
| Wirkung klemmen [0,25×; 4×] | RAT-055 | ✓ | `clampEffect` · test_mix M4-5 |
| Wirkung nur aus sauberen Gaben (kein Zulauf dazwischen) | RAT-055 | ◐ | `clean_` |
| Plausibilitätsband 3–9 | RAT-020 | ✓ | Katalog · test_truth |
| < 0,03 Bewegung nach 2 Gaben → Abbruch | RAT-020, RAT-041 | ◐ | Rastung `ph.no_effect` · kein eigener Test |
| Kein pH+ nach pH− (Ping-Pong) | RAT-041 | ✓ | pH+ im Prototyp gar nicht geregelt |
| Wert älter als 10 min → keine Korrektur | RAT-043 | ◐ | `kMaxPhAge` |
| Jede Sperre sichtbar mit Grund | RAT-039, RAT-084 | ✓ | Regelzeile mit Checkliste · E2E „Sprungsperre“ |

## M5 EC-Nachdosierung, M6 EC-Gate

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| 0,8 × Lücke / Wirkung; Deckel 1,0 mS/cm je Runde, in ml/L über max(Start, Wirkung) | RAT-056 | ✓ | `planEcDose` · test_mix M5-1, M5-4 |
| Startwirkung 0,275 mS/cm je ml/L Rezept | RAT-055 | ✓ | `kEcStartEffect` |
| Vorhalt für die folgende pH−-Gabe | RAT-053 | ◐ | `EcController::tick` |
| Keine Bewegung nach 2 Runden → Abbruch | RAT-055 | ◐ | Rastung `ec.no_effect` |
| pH nur bei EC ≥ 0,5; EC ungültig sperrt; fehlende Historie sperrt nicht | RAT-046 | ✓ | test_scenarios „EC-Gate“ |
| Ruhezeit 240 s nach EC-Gabe | RAT-058 | ✓ | `kEcRestS` |
| EC lässt sich nur heben | RAT-012 | ✓ | Regelzeile „senken geht nur mit frischem Wasser“ |
| Während Zulauf und Kalibrierung nicht dosieren | RAT-055 | ✓ | `ControlEnv::refilling`, `calibrating` |

## M7 Durchmischung und Settle

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| Ohne Durchmischung keine Regel-Dosierung | RAT-047, RAT-051 | ✓ | Gateway `act.no_mixing` |
| Wartezeit ≥ 2 × Glättungsfenster; aus Messung | RAT-052, RAT-058 | ◐ | feste Parameter (pH 5 min, EC 4 min) |
| Mischzeit wächst mit Volumen / Umwälzleistung | RAT-052 | ○ | offen; im Simulator modelliert |

## M8 Sensorwahrheit

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| Läuft immer, auch ohne Grow und im Pflegemodus | RAT-023, RAT-075 | ✓ | `SensorTruth::update` jeden Takt |
| Fehlender Wert nie 0 | RAT-006 | ✓ | test_truth M8-1; `arch_check.sh` |
| Frische und Stillstand getrennt | RAT-023, RAT-059 | ✓ | test_truth |
| Kalibrierung ungültig → kein Regelwert | RAT-025, RAT-026 | ✓ | test_truth |
| Sprungsperre (pH > 1,0 / EC > 0,5 in 5 min), frei nach 15 min Ruhe, überlebt Neustart | RAT-039, RAT-044 | ✓ | test_truth M8-2, test_scenarios, E2E |
| Angekündigter Handgriff erklärt einen Sprung | RAT-042 | ✓ | eigene Gaben, Mischlauf, Zulauf, Kalibrierung, Pflegemodus · test_truth M8-3 |
| Ausfall erst nach n Fehlversuchen | RAT-027 | ○ | Sache des Bus-Treibers (firmware) |
| VPD ist Luft-VPD ohne Blatt-Offset; fehlt ein Quellwert, keinen Wert | RAT-017 | ✓ | `SensorTruth::updateDerived` (FAO-56 Gl. 11) · test_climate „Formel“, „bei Ausfall eine Lücke“. Eigene Regel: beide Werte höchstens 60 s auseinander |

## M9 Watchdog

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| Kein Pfad zu Aktoren | RAT-074 | ✓ | `arch_check.sh` R2 |
| Ungültiger Wert = Problem, nie neutral | RAT-015 | ✓ | test_watchdog M9-1 |
| Neutral im Pflegemodus, in der Anlaufschonfrist, ohne Ziel | RAT-073, RAT-005 | ✓ | test_watchdog |
| Bewertung älter als 3 min → rot | RAT-073 Nachtrag 3 | ✓ | `stale` in der API, Übersicht |
| Regelband eng, Alarmband weit | RAT-033 | ✓ | test_watchdog |
| Toleranz nie 0 | RAT-008 | ✓ | Katalog-Untergrenze · test_catalog_config |

## M10 Zulauf und Füllstand, M11 Umwälzung

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| Füllmenge gerechnet, Sensor nur Prüfung und Notabschaltung | RAT-001, RAT-038 | ✓ | `RefillController` |
| Notgrenze folgt dem Ventil, für jeden Zulaufweg | RAT-032 | ✓ | Gateway `enforce` |
| Nach Fehler kein automatischer Neuanlauf | RAT-031 | ✓ | Rastung `inlet.fault` · test_scenarios |
| Kennlinie stückweise linear, streng steigend | RAT-078 | ✓ | `Curve` · test_truth M10-4/5 |
| Blindzone, Ratenprüfung, Nachsteuern | RAT-030, RAT-038 | ○ | offen |
| Trockenlaufschutz unabhängig vom Halter; Rastung nur, wenn die Pumpe lief | RAT-047, RAT-062 | ✓ | test_scenarios M11-1 |
| Rastung überlebt Neustart | RAT-063 | ✓ | `RuntimeState::latches` |
| Leerlauf über Leistungsmessung (< 2,5 W) | RAT-049 | ○ | braucht Strommessung am Ausgang |

## M12–M15

| Regel | Quelle | Status | Umsetzung / Test |
|---|---|---|---|
| Heizung nur extern, rastende Notabschaltung | RAT-060, RAT-061 | ○ | nicht im Prototyp; Heizrollen sind bis dahin nicht im Katalog · test_net „Heizung“ |
| Netzsteckdosen nach Stromausfall aus, Auto-Off im Gerät, Rücklesen vor dem Binden | RAT-019, RAT-060 | ✓ | `Hub::acceptDevice`, `Hub::bindRole`, `safetyForRole` · test_net „übernehmen“, „Rücklesen“; Faktor 1,11 für puls ist Annahme |
| Schutzeinstellung vor jedem Einschalten prüfen | RAT-019 (Nachträge: Inventar ≠ Gerät) | ✓ | `Actuators::setRole` · test_net „Schutzeinstellung verloren“ |
| Befeuchter und Entfeuchter nie zugleich; unbekannter Zustand sperrt | RAT-034, R5 | ✓ | `Actuators::inhibit` · test_net „nie zugleich“, „unbekannter Zustand“. Abweichung: Gegensperre 10 min fehlt (folgt mit der Klimafunktion) |
| Entfeuchter: Mindestpause 5 min, auch nach Not-Halt und Neustart | RAT-034 | ✓ | `kCompressorPause`, `Actuators::stopAll` · test_net „Kompressor-Pause“; Mindestlauf 10 min folgt mit der Klimafunktion |
| Gießpumpe nur über dem Mindestfüllstand; im Lauf darunter → aus | RAT-068 (Sperre), eigene Regel analog RAT-062 (Abschaltung im Lauf) | ◐ | `Actuators::inhibit`, `Actuators::enforce` · test_net „Gießpumpe“. Abweichungen: bei unlesbarem Pegel gesperrt statt „gießen und melden“ (Annahme, Empfehlung hardware; PD folgt); es zählt der aktuelle Pegel |
| Höchstlaufzeit je Rolle; Gerät schaltet knapp danach selbst ab | RAT-060 | ✓ | `Actuators::enforce` · test_net „Höchstlaufzeit“, „Auto-Off im Gerät“ |
| Not-Halt schaltet auch Netzsteckdosen aus | RAT-036 | ◐ | `Actuators::stopAll` · test_net „Not-Halt“. Abweichung (RAT-036): hier sofort alles aus. „Aus nicht bestätigt“ mit Wiederholung offen |
| Schutzabschaltung meldet ehrlich: scheitert das Ausschalten, „Aus nicht bestätigt“ (Alarm) einmal je Grund; Wiederholung, solange Grund oder Rastung besteht; „Aus bestätigt“, sobald als aus gelesen; nach Umzuordnen keine Entwarnung | eigene Regel (Hinweis `pruefer`, PR #18) | ✓ | `Actuators::cut`, `enforce` · test_net „Aus nicht bestätigt“ (Gießpumpe, Umwälzpumpe, Zulauf, Höchstlaufzeit), „Gerät schaltet selbst ab“, „nach Umzuordnen“, „Lösen“, „Zwei Schutzgründe“, „späterer Trockenlauf“, „gleich wieder eingeschaltet“. Offen: Not-Halt leert die Laufzeiten und beendet damit die Wiederholung bei der Höchstlaufzeit (Rückfall: Auto-Off im Gerät). Alarm statt Warnung, weil der Ausgang trotz Schutzgrund weiterläuft; beim Umzuordnen bleibt es eine Warnung |
| Rastung wird durchgesetzt: läuft Umwälzpumpe oder Zulauf trotz nicht quittierter Rastung, erneut aus; Grund und Zeitpunkt beim Rasten eingefroren | RAT-051 (Abschalter an der Einschaltflanke, auch von Hand), RAT-062 (Grund eingefroren), RAT-031 (kein Neuanlauf) | ◐ | `Actuators::enforce` · test_net „späterer Trockenlauf“, „Zulauf: Grund weg, Rastung steht“, „Notgrenze nach Pegelausfall“, „neuer Trockenlauf nach Quittierung“. Abweichung (RAT-063): Die Pegel-Rastung der Pumpe gilt bis zur Quittierung (bestand schon vorher, PD folgt) |
| Gießen über Zahl der Gaben; Drain% führt | RAT-010, RAT-014 | ○ | Stufe 3 |
| Verbrauch nur, was in den Tank geht; je Lauf buchen, auch beim Abbruch | RAT-070, RAT-040 | ✓ | `Doser::book`, `Doser::abort` · test_scenarios „Abbruch bucht“ |
| Unbekannter Vorrat wird nicht gebucht | RAT-015 | ✓ | `Doser::book` |
| Mindeststand 150 ml, pH− 20 ml | RAT-071 | ✓ | Watchdog |
| Phasen liefern Parameter, nie Namen | RAT-076 | ✓ | `effectiveParams` · test_catalog_config M15-1; `arch_check.sh` R4 |
| Ernte ist ein Ereignis; „abgeschlossen“ eigener Zustand | RAT-077, RAT-002 | ✓ | `growHarvest`, `growComplete` |
| Stopp ist aktive Kaskade, idempotent | RAT-036 | ✓ | `Hub::stop` · test_scenarios; zweite Stufe nach 30 s offen |
