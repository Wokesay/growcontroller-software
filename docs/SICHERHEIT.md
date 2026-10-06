# Sicherheit: Bedrohungsmodell und Umsetzung

Stand 06.10.2026. Einordnung nach `regulatorik` (EN 18031-1, CRA), keine
Rechtsberatung. Zwei Arten von Sicherheit:

- **funktional:** keine Überdosis, kein Überlauf, kein Trockenlauf;
- **IT-Sicherheit:** kein fremder Zugriff auf Pumpen.

## Bedrohungen und Antworten

| Bedrohung | Antwort im Prototyp | später |
|---|---|---|
| Fremder im Heimnetz schaltet Pumpen | Pflichtpasswort vor jeder Funktion, kein Standardpasswort, PBKDF2-SHA-256 (10.000 Runden) mit Salz, Sitzung als HttpOnly-/SameSite-Cookie, Sperre nach 5 Fehlversuchen (30 s, verdoppelt bis 15 min), alles außer `/info` verlangt Anmeldung | HTTPS mit Zertifikat je Gerät, Token mit Rollen für Integrationen |
| Mitlesen im WLAN | – | HTTPS als Voreinstellung (EN 18031-1 SCM; Shelly erzwingt es) |
| Webseite eines Dritten löst Aktionen aus (CSRF, Clickjacking) | SameSite=Strict, CSP `default-src 'self'`, `X-Frame-Options: DENY` | – |
| Manipuliertes Update | – | signiertes OTA, Downgrade-Sperre, Secure Boot v2 |
| Überlast am Webserver stört die Regelung (RLM) | Regelung im eigenen Takt, Anfragen nur unter Sperre | eigene Task-Priorität und eigener Kern auf dem ESP32, Lasttest |
| Fehlerhafte Eingaben | Prüfung jeder Änderung, JSON-Grenze 1 MB, Bereichsprüfung der Parameter | Fuzzing |
| Datenabfluss beim Melden | Diagnosepaket ohne Hash, Sitzungen, WLAN, IP; Vorschau vor dem Herunterladen; Hinweis „GitHub ist öffentlich“ | Upload nur mit Einwilligung, Löschfrist |
| Firmwarefehler dosiert zu viel | Gateway mit Mindestsperren (R1, R7), Einmesswert Pflicht, Laufzeitgrenzen, Job-ID gegen Doppeldosierung | Zeitlimit und „ein Kanal“ in Hardware im Dosierblock, Freigabe in Hardware je Port [PD-012] |
| Sensor lügt | Sensorwahrheit: Frische, Stillstand, Band, Sprungsperre, Kalibrierung; EC-Gate | Messfenster mit Pumpe aus, solange die Trennung nicht abgenommen ist (RAT-044) |
| Stromausfall mitten im Lauf | nach dem Start alles aus, nichts fortsetzen, Meldung | Dosierblock stoppt ohne Lebenszeichen des Hubs |

## Protokoll (CRA Anhang I 2(l))

Ins Ereignislog kommen:

- Anmeldung und Fehlversuche;
- Passwortwechsel;
- Konfigurationsänderungen mit Revision;
- Import;
- Not-Halt und Pflegemodus;
- angeforderte Updates.

Exportierbar ist das Log über das Diagnosepaket. Abschaltbar soll es werden
(offen).

## Werksreset und Daten (CRA 2(m))

Export und Import der Einstellungen gibt es. Der Werksreset, der sicher
löscht, kommt mit `firmware/`. Telemetrie ist nicht eingebaut.

## Vor dem ersten Gerät bei Dritten (auch Beta-Tester)

Laut `regulatorik` muss vor dem 11.12.2027 jedes verkaufte oder verliehene
Gerät EN 18031-1 voll erfüllen. Danach gilt der CRA. Liste:

1. HTTPS lokal, Zertifikat je Gerät.
2. Signiertes OTA, Secure Boot v2, Flash-Verschlüsselung, eFuse-Plan.
3. Setup-Zugangspunkt nur nach Tastendruck und zeitlich begrenzt.
   WLAN-Schlüssel je Gerät auf dem Etikett.
4. Bluetooth, JTAG und Debug aus. MQTT und HA nur auf Wunsch einschalten.
5. Cyber-Risikobewertung, EN-18031-Eigenbewertung, technische Dokumentation,
   Supportzeitraum, Meldeprozess.

Wie man Schwachstellen meldet: [`../SECURITY.md`](../SECURITY.md).
