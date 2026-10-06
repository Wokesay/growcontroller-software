# Versionen, Changelog, Releases, Updates

Stand 06.10.2026. Grundlage: `produkt`, `regulatorik`, `software`, `kunde`.
Einordnung, keine Rechtsberatung.

## Versionen

- **SemVer** `MAJOR.MINOR.PATCH[-beta.N]`. Die Version steht in `VERSION`,
  `web/package.json` und im Tag `vX.Y.Z`.
- **MAJOR:** Bruch in Konfiguration, API oder Bus-Protokoll. Integratoren und
  Busgeräte brauchen dieses Signal.
- **PATCH:** nur Fehler- und Sicherheitskorrekturen. Damit kommen
  Sicherheitsupdates, wo machbar, getrennt von Funktionsupdates (CRA Anhang I
  Teil II).
- **Prototyp-Phase:** `0.x.y-proto.N`. Keine Zusagen zur Kompatibilität.

## Changelog

[Keep a Changelog 1.1.0](https://keepachangelog.com/de/1.1.0/) mit den
Abschnitten **Neu, Geändert, Behoben, Entfernt, Sicherheit**. Jeder PR trägt
einen Eintrag unter `[Unreleased]` ein.

- **„Sicherheit“** nennt betroffene Versionen, Schwere, Fix-Version und
  GHSA- oder CVE-Kennung.
- **Für die App** kommt aus jedem Release eine Kurzfassung in drei Zeilen:
  „Neu / Behoben / Bitte beachten“ (`kunde`). Der vollständige Changelog ist
  in der Firmware eingebettet (`GET /api/v1/changelog`) und in der App
  lesbar.

## Release-Ablauf

1. **Vorbereiten:** `[Unreleased]` → `[X.Y.Z] – Datum`, `VERSION` und
   `web/package.json` anheben, PR, Review (`reviewer`, `release`,
   bei Sicherheitsthemen `security`), CI grün.
2. **Taggen:** Tag `vX.Y.Z` auf `main`. Der Workflow `release.yml` baut
   Simulator und Web-App, erzeugt SBOM (CycloneDX) und `SHA256SUMS` und
   legt das GitHub-Release mit dem Changelog-Abschnitt an. Vorabversionen
   (`-beta`, `-proto`) sind als Vorabversion markiert.
3. **Firmware** (sobald `firmware/` baut):
   - Die CI baut das ESP-IDF-Image und die SBOM (`idf.py sbom-create`).
   - **Signiert wird offline** nach manueller Freigabe durch den
     Projektinhaber. Der Schlüssel liegt nie im Repo und nicht ungeschützt
     in der CI.
   - Das Manifest (Version, Kanal, Größe, Hash, Signatur, Kurzfassung) kommt
     ins Release.
4. **Kanäle:** `beta` sofort, `stable` nach mindestens 7 Tagen ohne
   Befund (V).

## Updates auf dem Hub (Konzept, im Simulator als Attrappe)

- **Zwei App-Partitionen** (A/B) mit `otadata`. Die neue Version startet als
  „pending verify“.
- **Selbsttest** (Bus, Ports, Speicher, Webserver) bestätigt die neue
  Version. Sonst kehrt der Hub automatisch zur alten zurück.
- **Signatur vor der Installation:** mindestens Signaturprüfung ohne
  Hardware-Secure-Boot (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`, auf dem
  S3 testen). Vor dem ersten verkauften Gerät: Secure Boot v2,
  Flash-Verschlüsselung, Downgrade-Sperre über die Security-Version.
- **Web-UI im App-Image:** Ein Update ist atomar; ein Rollback nimmt die UI
  mit.
- **Nur im sicheren Zustand:** kein Auftrag, keine Dosierung (API: 409).
  Rezepte, Einmesswerte und Einstellungen bleiben erhalten (Kundensicht).
- **Wege:**
  - Aus der App gegen das Manifest der GitHub-Releases.
  - Offline per Datei-Upload einer signierten Datei.
  - Für Selbstbauer ein Web-Installer (ESP Web Tools, Chrome/Edge am
    Desktop).
- **Update-Prüfung:**
  - Sie ist abschaltbar und sendet keine Gerätekennung.
  - Ein Datenschutzhinweis ist nötig: Die IP geht an GitHub (§ 25 TDDDG).
- **Sicherheitsupdates:**
  - Laut CRA Anhang I 2(c) „wo zutreffend“ automatisch als Voreinstellung,
    mit einfachem Opt-out, Hinweis und Aufschieben.
  - Bei einem Dosiergerät nur im Ruhezustand.
  - Ob das für dieses Gerät „zutreffend“ ist, entscheidet der
    Projektinhaber (offen).

## Support und Pflichten (CRA, ab 11.12.2027; Meldepflicht seit 11.09.2026)

- **Supportzeitraum:** mindestens 5 Jahre (Untergrenze). Er steht beim Kauf
  als Monat und Jahr. Jedes Sicherheitsupdate bleibt mindestens 10 Jahre
  verfügbar (Art. 13).
- **Meldung aktiv ausgenutzter Schwachstellen:** 24 h / 72 h / 14 Tage über
  die ENISA-Plattform (Art. 14). Dafür braucht es eine Vertretung, die die
  24-h-Frist hält.
- **SBOM** gehört in die technische Dokumentation, nicht zwingend
  veröffentlicht.
- **Exit-Plan bei Projektende:** Schlüssel freigeben oder
  Eigentümer-Freischaltung, Marktüberwachung und Nutzer vorher informieren
  (Art. 13(23), PD-005).

Quellen: Bericht `regulatorik` vom 06.10.2026, nur Suchtreffer, abgerufen
06.10.2026:

- CRA, Verordnung (EU) 2024/2847:
  https://eur-lex.europa.eu/legal-content/EN/TXT/HTML/?uri=OJ%3AL_202402847
- Kommissions-Leitlinien C(2026) 5252, Zusammenfassung:
  https://www.cyberresilienceact.eu/commission-guidance.html
- ENISA Single Reporting Platform: im Bericht ohne URL, vor Gebrauch
  nachschlagen.
