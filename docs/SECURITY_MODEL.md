# Security: threat model and implementation

As of 2026-10-07. Assessment by `regulatorik` (EN 18031-1, CRA); not
legal advice. Two kinds of safety and security:

- **functional:** no overdose, no overflow, no dry run;
- **IT security:** no outside access to the pumps.

## Threats and responses

| Threat | Response in the prototype | Later |
|---|---|---|
| A stranger on the home network switches pumps | Mandatory password before any function, no default password, PBKDF2-SHA-256 (10,000 rounds) with salt, session as an HttpOnly/SameSite cookie, lockout after 5 failed attempts (30 s, doubling up to 15 min), everything except `/info` requires login | HTTPS with a certificate per device, tokens with roles for integrations |
| Eavesdropping on the Wi-Fi | – | HTTPS by default (EN 18031-1 SCM; Shelly enforces it) |
| A third-party website triggers actions (CSRF, clickjacking, DNS rebinding) | SameSite=Strict, origin check (`Sec-Fetch-Site`/`Origin` against `Host`), `Host` only an IP address, `localhost` or a home-network name (`.local`, `.lan`, `.home.arpa`, `.internal`, `.fritz.box`); CSP `default-src 'self'`, `X-Frame-Options: DENY` | – |
| Tampered update | – | signed OTA checked by the firmware, downgrade protection in software, no eFuses burned except the HMAC key for encrypted NVS; the owner can always install their own firmware over USB (PD-022, PD-043, PD-047) |
| Load on the web server disturbs control (RLM) | control runs in its own cycle, requests only under a lock | own task priority and own core on the ESP32, load test |
| Faulty input | every change and every import is validated (limits, phases, calibrations), JSON limit 1 MB or 64 KB (exception: the Home Assistant trial reads Home Assistant's state list, at most 16 MB, read by a SAX reader that keeps only sensors (at most 20 000) and a few fields of each (a name cut to 256 bytes, any other longer field dropped) and stops at 32 levels of nesting, docs/HOME_ASSISTANT.md), exceptions caught (400/500 instead of a crash; in the control cycle: everything off, alarm) | fuzzing |
| Password file lost (power failure while writing) | reading falls back to the complete `.tmp`; `auth.json` is written before the lock marker and is on disk when the call returns (on a computer with `fsync`, #68); once a password was set, setup over the network is locked (423); the marker never comes from an import | factory reset by button |
| A slow peer blocks the web server | firmware: 2 s timeout, one retry, then 408; headers up to 2 KB | emergency stop on the device without the web, load test |
| Data leak when reporting a problem | diagnostic package without hash, sessions, Wi-Fi, IP; preview before download; note "GitHub is public" | upload only with consent, deletion period |
| A firmware bug doses too much | gateway with fixed limits in code, configuration only tightens them (R1, R7); calibration value mandatory; job ID per attempt and start against double dosing; deadline per run (without feedback: off, counted as run) | time limit and "one channel" in hardware in the dosing block, enable in hardware per port [PD-012] |
| A sensor lies | sensor truth: freshness, frozen readings, band, jump lock, calibration; EC gate | measurement window with the pump off until the galvanic isolation has passed acceptance (RAT-044) |
| Power failure in the middle of a run | after start-up everything is off, nothing resumes, a message; exception: fan sockets come back on (PD-050), not during an emergency stop (PD-076) | the dosing block stops without a sign of life from the hub |
| A socket switches on by itself when mains returns | only roles on a fixed allowlist in code (exhaust, circulation fan) may be "on after power loss", only continuous loads without a maximum run time; a socket that loses the role, its device or its binding in an import goes back to "off", read back, failure reported | the setting is stored in the socket; the user can change it there |
| Wrong or spoofed time | plausibility bounds for the saved clock and events; continued clock without a secured time; clock steps keep the remaining time of locks and pauses and are logged; intervals across restarts use operating time unless both moments were secured | plausibility checks of network time and the app's device time (PD-073) are open |

## Logging (CRA Annex I 2(l))

The event log records:

- login and failed attempts;
- password changes;
- configuration changes with revision;
- import;
- emergency stop and maintenance mode;
- requested updates.

The log can be exported with the diagnostic package. Users should be able
to switch it off (open).

## Factory reset and data (CRA 2(m))

Export and import of the settings exist. The factory reset that erases
securely comes with `firmware/`. There is no telemetry.

## Before the first device goes to third parties (beta testers included)

According to `regulatorik`, before 2027-12-11 every device that is sold or
lent must fully meet EN 18031-1. After that date the CRA applies. List:

1. HTTPS locally, certificate per device.
2. Signed OTA (EN 18031-1 SUM-2) checked by the firmware, without
   hardware Secure Boot; downgrade protection in software; no eFuses
   burned except the HMAC key for encrypted NVS (PD-043, PD-047, SD-021,
   SD-024). Secrets (Wi-Fi credentials, password hash, TLS key) in
   encrypted NVS, sessions only as hashes, USB-JTAG off at run time
   without a further eFuse (feasibility open). Over
   USB the owner can always install their own firmware (PD-022).
   `tools/arch_check.sh` rejects sdkconfig options that would burn eFuses
   for Secure Boot, flash encryption, a disabled download mode or
   anti-rollback. Residual risk, documented: someone with the device in
   hand can load own code and use the HMAC key. Open: whether a test lab
   accepts the USB path with the BOOT button.
3. Setup access point only after a button press and for a limited time.
   Wi-Fi key per device on the label.
4. Bluetooth off; JTAG and debug off in a way that keeps installing own
   firmware possible (PD-022). MQTT and Home Assistant only when the user
   switches them on.
5. Cyber risk assessment, EN 18031 self-assessment, technical
   documentation, support period, reporting process.

How to report vulnerabilities: [`../SECURITY.md`](../SECURITY.md).
