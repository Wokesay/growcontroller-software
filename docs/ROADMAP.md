# Roadmap der Software

Stand 06.10.2026. Reihenfolge als Vorschlag, Aufwand grob nach `software`
(Vollzeitwochen; nebenberuflich etwa ×2–3).

| Meilenstein | Inhalt | Abhängig von |
|---|---|---|
| **M0 Prototyp** (dieser Stand) | Kern, Simulator, Web-App, Tests, CI, Doku | – |
| **M1 Firmware auf dem Steckbrett P0** | ESP-IDF-Projekt: Modbus je Port mit DE/RE, Port-Freigabe, Dosierblock-Protokoll mit Job-ID und Lebenszeichen, NVS/LittleFS, Flash-Ringpuffer, esp_http_server mit SSE; Abnahme nach Prototyp-Paket P0 | Registerplan (`hardware`, `firmware`), Steckbrett |
| **M2 Sicher ab Werk** | HTTPS lokal, signiertes OTA A/B mit Selbsttest, Werksreset, Setup-Zugangspunkt per Taste, Update-Manifest aus Releases | E12, E13 |
| **M3 Melden** | Benachrichtigungen ohne Herstellercloud (ntfy, E-Mail, Webhook), Morgenbericht, Alarmhygiene nach RAT-022/RAT-045 | E11 |
| **M4 Integration** | MQTT mit HA-Discovery (lesend + Stopp), Token für Integrationen, OpenAPI | E1 |
| **M5 Stufe 3–4** | Gießen über Zahl der Gaben, Drain, Klima bewerten, Heizen nur extern mit Auto-Off | Köpfe, Lasten-Modell |
| **M6 Begleiter** | Langzeitarchiv, Vergleich von Durchgängen, Push-Relay, Fernzugriff (Abo) | E2, E11 |
| laufend | Sprachen (DE/EN), Barrierefreiheit, Fuzzing, Langlauftests | – |

Offene Fragen an den Projektinhaber stehen in `DECISIONS.md` (Entwürfe
mit „braucht PD“).
