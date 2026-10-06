# API v1

Stand 06.10.2026. REST mit JSON unter `/api/v1`, transportneutral im Kern
(`core/src/api.cpp`). Die Web-App nutzt nur diese API. Was die UI kann, kann
also auch eine Integration.

## Zugang

- `GET /info` ist öffentlich: Version, Plattform, ob ein Passwort gesetzt ist.
- `POST /auth/setup {password}` setzt das Erstpasswort, genau einmal (sonst
  409). Mindestens 8 Zeichen.
- `POST /auth/login {password}` setzt das Cookie `gc_session` (HttpOnly,
  SameSite=Strict). Nach 5 Fehlversuchen kommt 429 mit Wartezeit (30 s,
  verdoppelt bis 15 min).
- `POST /auth/logout`. `PUT /auth/password {old,new}` meldet alle Sitzungen
  ab.
- Alles andere verlangt eine Sitzung (Cookie oder `Authorization: Bearer`),
  sonst 401.
- Sicherheitskopfzeilen am Server: CSP `default-src 'self'`,
  `X-Frame-Options: DENY`, `nosniff`, `no-referrer`.
- **Herkunft:** Der `Host`-Kopf muss eine IP, `localhost` oder ein
  `.local`-Name sein (gegen DNS-Rebinding). Schreibende Anfragen eines
  Browsers nur von derselben Herkunft (`Sec-Fetch-Site`, sonst `Origin`
  gegen `Host`), sonst 403 `api.origin`.
- **Fehler:** falsches Format 400 `api.bad_input`, interner Fehler 500
  `api.internal`; der Server läuft weiter. Ist das Passwort verloren
  (`auth.json` fehlt, obwohl gesetzt), lehnt `/auth/setup` mit 423
  `auth.lost` ab: Werksreset am Gerät nötig.
- **Import** (`POST /config/import`) prüft wie jede Änderung, dazu Grenzen,
  Phasenparameter und Kalibrierdaten; während eines Auftrags 409.

## Lesen

| Methode | Pfad | Inhalt |
|---|---|---|
| GET | `/state` | Live-Zustand: Ports, Geräte, Messwerte mit Qualität und Grund, Tank, Regler mit Regelzeile und Checkliste, Ausgänge, Auftrag, Dosierung, Watchdog (mit `stale`), Funktionen (Resolver), Rastungen, Vorrat, Durchgang |
| GET | `/events/stream` | Server-Sent Events: alle 1 s `event: state` |
| GET | `/config`, `/config/export` | Konfiguration ohne Geheimnisse |
| GET | `/catalog` | Katalog der Firmware |
| GET | `/history?series=a,b&from&to&points` | Messreihen (`t`, `avg`, `min`, `max`, `stepS`) |
| GET | `/events?type&from&to&limit` | Ereignisse, neueste zuerst |
| GET | `/export.csv?series&from&to` | CSV |
| GET | `/diagnostics` | Diagnosepaket (ohne Hash, Sitzungen, WLAN, IP) |
| GET | `/changelog` | eingebetteter CHANGELOG (Markdown) |
| GET | `/update` | Version, verfügbare Version mit Kurzfassung |

## Einrichten

| Methode | Pfad | |
|---|---|---|
| POST | `/setup/complete` | Einrichtung abgeschlossen |
| PUT | `/system` | Name, Zeitzone, Update-Kanal und -Prüfung, Grenze je Handgabe |
| POST / PATCH / DELETE | `/devices/{id}/accept`, `/devices/{id}` | übernehmen, umbenennen, entfernen |
| PUT / DELETE | `/roles/{rolle}` `{device, channel}` | Zuordnung |
| PUT | `/tank` | Name, Nutzvolumen, Trockenlaufgrenze, Wasser, Volumen ohne Füllstandskopf |
| POST / DELETE | `/canisters`, `/canisters/{id}`, `/canisters/{id}/stock` | Kanister, Vorrat |
| POST / DELETE | `/recipes`, `/recipes/template`, `/recipes/{id}` | Rezepte, Vorlagen |
| PATCH | `/functions/{id}` `{enabled, params}` | Einschalten nur, wenn eingerichtet (sonst 409 mit „was fehlt“) |
| POST | `/config/import` | geprüft, Aktoren vorher aus |

Fehler kommen als `{"error":{"key","text"},"errors":[…]}`. Der Schlüssel
`key` ist für Übersetzungen da, `text` ist deutscher Klartext.

## Bedienen

| Methode | Pfad | |
|---|---|---|
| POST | `/mix/plan`, `/mix/start` `{recipe, waterL, mode, guided, confirmRepeat}` | Vorschau, Start |
| POST | `/jobs/{id}/continue`, `/abort`, `/resume`, `/result {ml}` | weiter nach Umrühren, abbrechen, Paar nachholen, Einmessergebnis |
| POST | `/dose {canister, ml}` | Handgabe |
| POST | `/pumps/{id}/calibrate {seconds}`, `/pumps/{id}/prime {seconds}` | Einmessen, Schlauch füllen |
| POST | `/probe {device, kind, action, reference}` | Sondenkalibrierung: start, point, commit, cancel |
| POST | `/latches/{id}/ack` | Rastung quittieren (Sprungsperre hebt sich selbst auf) |
| POST | `/stop`, `/resume`, `/maintenance {minutes}` | Not-Halt, fortsetzen, Pflegemodus |
| POST | `/measure {ph, ec}` | Handmessung |
| POST | `/grow/start`, `/grow/next`, `/grow/harvest`, `/grow/complete` | Durchgang |
| POST | `/update/check`, `/update/install`, `/update/rollback` | Install nur ohne laufende Dosierung (409) |

## Nur im Simulator

`GET /sim`, `POST /sim/{speed|plug|unplug|cap|uncap|fault|water|probe|reboot|scenario|advance}`
(siehe `SIMULATOR.md`). Diese Pfade gibt es auf dem Gerät nicht.

## Offen

- OpenAPI-Datei aus diesem Dokument erzeugen.
- Token für Integrationen mit Rollen (lesen, bedienen, admin).
- `If-Match` auf `revision` gegen gleichzeitiges Bearbeiten.
- WebSocket statt SSE prüfen (`architekt` empfiehlt WebSocket, `software`
  SSE). Der Prototyp nutzt SSE: einfacher, mit curl lesbar und mit
  automatischem Wiederverbinden.
