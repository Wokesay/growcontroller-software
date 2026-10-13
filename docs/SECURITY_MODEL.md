# Security: threat model and implementation

As of 2026-10-07. Assessment by `regulatorik` (EN 18031-1, CRA); not
legal advice. The rows on the Home Assistant app (SD-034) were added on
2026-10-10 by the software session after `security`'s review and are to
be confirmed by `regulatorik`. Additions for SD-036 and SD-037 to the rows
on the Home Assistant app's port and on faulty input were made on
2026-10-11 by the software session, also to be confirmed by `regulatorik`;
so are those for SD-041 (raw values calibrated in the app) and SD-042
(a sign of life per sensor). The lockout per client (SD-051) in the first
row was added on 2026-10-12 by the software session, also to be confirmed
by `regulatorik`. So is the number and text rule for Home Assistant
states in the row on faulty input (#71), added on 2026-10-13.
Two kinds of safety and security:

- **functional:** no overdose, no overflow, no dry run;
- **IT security:** no outside access to the pumps, unless the user
  chooses "View and control from outside" at setup, and then only through
  defined ways (SD-059).

## Threats and responses

| Threat | Response in the prototype | Later |
|---|---|---|
| A stranger on the home network switches pumps | Mandatory password before any function, no default password, PBKDF2-SHA-256 (10,000 rounds) with salt, session as an HttpOnly/SameSite cookie, lockout after 5 failed attempts (30 s, doubling up to 15 min) per client: an address that signed in since the hub last started (at most 16, in memory only, the one seen longest ago drops out first) has its own count, all others share one, so someone guessing from another address cannot lock out such a device, and guessing from addresses that never signed in is as slow as from one; a host that takes over a known device's address gets that device's count (accepted, SD-051); the address comes from the socket, never from a header (`tools/arch_check.sh` refuses a trusted proxy); a wrong old password when changing it counts and is logged; only counted failures are logged; everything except `/info` requires login | HTTPS with a certificate per device, tokens with roles for integrations |
| Eavesdropping on the Wi-Fi | – | HTTPS by default (EN 18031-1 SCM; Shelly enforces it) |
| A third-party website triggers actions (CSRF, clickjacking, DNS rebinding) | SameSite=Strict, origin check (`Sec-Fetch-Site`/`Origin` against `Host`), `Host` only an IP address, `localhost` or a home-network name (`.local`, `.lan`, `.home.arpa`, `.internal`, `.fritz.box`); CSP `default-src 'self'`, `X-Frame-Options: DENY` | – |
| Tampered update | Home Assistant app (SD-034): the image is built without write access, checked against its checksum and labels, pushed only by the release job for an admin-only `v*` tag, assembled by digest and attested (`docs/RELEASE.md`). Home Assistant checks no attestation; it installs whatever image carries the version in `ha/app/config.yaml` on `main`, so a version change there is a release step (a process rule; no GitHub rule enforces it). Open, accepted by the project owner for the experimental phase (SD-034): any workflow of the repository can be given `packages: write` and overwrite the image, so everyone with write access to the repository can | signed OTA checked by the firmware, downgrade protection in software, no eFuses burned except the HMAC key for encrypted NVS; the owner can always install their own firmware over USB (PD-022, PD-043, PD-047) |
| A device in the network reaches the Home Assistant app's port | plain HTTP on port 8099 of the Home Assistant host; password, lockout, `Host` and origin checks as on the hub; until the password is set, whoever comes first sets it (the app's documentation says to set it right away); clearing the port hides it from the network, not from Home Assistant's internal network; a session reads the names of the Home Assistant sensors it lists (the suggestions and, on request, the other sensors that could be picked or need a reason, at most 1000; values only of those that could be picked, SD-036; a voltage of at most 5 V, which could be a pH probe's raw signal, is among them with its value, SD-041; higher voltages are not passed on; on request, while a sign of life is picked, the sign-of-life texts and timestamp sensors with their names, channel codes and ages, at most 100 of each, and otherwise only their count and the names of those in use, SD-042), nothing else of Home Assistant | Home Assistant's ingress (its login and HTTPS), a one-time setup code in the app's log (#79) |
| A flaw in the app's web server exposes the Supervisor's token, which acts as Home Assistant's administrator | the address and path are fixed (`--app`), no request a user can steer goes to Home Assistant, the bus switches nothing, the image has no shell (`FROM scratch`), the app asks for no other right (`tools/app.test.mjs`) | run as a user without root and capabilities, an AppArmor profile (#80) |
| A known flaw in a library inside the app image | the image names musl's and the GCC runtime's exact versions (`/licenses/BUILD_PACKAGES.txt`); security fixes for them, cpp-httplib and nlohmann/json are watched by hand; scanners see nothing in a static binary | an SBOM for the image with the release (#81) |
| A malicious npm package in the web app's build (the build tools run native code in every build) | every package pinned by its sha512 in `web/package-lock.json` and resolving to its own record on the npm registry; Dependabot proposes updates only after 7 days; `npm ci --ignore-scripts`; `npm audit signatures` right after every install (SD-058) proves that the registry's record of each installed version is signed and checks provenance where a package publishes one. It adds nothing against a hijacked maintainer account: the registry signs its releases too, and a hijacked publishing pipeline even produces valid provenance | fail when a package that had provenance publishes a version without it (#128); compare each lockfile hash with the signed one |
| Load on the web server disturbs control (RLM) | control runs in its own cycle, requests only under a lock | own task priority and own core on the ESP32, load test |
| Faulty input | every change and every import is validated (limits, phases, calibrations; an imported calibration passes the same check as one measured in the app, RAT-025, and is trusted like a backup, only for a kind the device's class offers; an unknown entry in the choice per area, SD-037, is left out of an import; a sign of life only for a class whose sensors report on their own schedule (set in code), its source of `a–z`, `0–9`, `_` and `.` up to 255 characters, its channel of `a–z`, `0–9` and `_` up to 16, SD-042; a sign-of-life text at most 255 characters, 64 channels and ages up to 9 999 999 s, anything else unreadable), JSON limit 1 MB or 64 KB (exception: the Home Assistant trial reads Home Assistant's state list, at most 16 MB, read by a SAX reader that keeps only sensors (at most 20 000) and a few fields of each (a name cut to 256 bytes, any other longer field dropped) and stops at 32 levels of nesting, docs/HOME_ASSISTANT.md; a state is a value only as a plain decimal number read in full, whatever the locale, and names and faults keep only printable, well-formed UTF-8, #71), exceptions caught (400/500 instead of a crash; in the control cycle: everything off, alarm) | fuzzing |
| Password file lost (power failure while writing) | on the device reading falls back to the complete `.tmp`; `auth.json` is written before the lock marker and is on disk when the call returns (Linux: `fsync` of file and folder; macOS/Windows best effort; #68), otherwise setup answers "Not saved" and sets nothing; files are created for the owner only (0600, a new folder 0700); once a password was set, setup over the network is locked (423); the marker never comes from an import | factory reset by button |
| A slow peer blocks the web server | firmware: 2 s timeout, one retry, then 408; headers up to 2 KB | emergency stop on the device without the web, load test |
| Data leak when reporting a problem | diagnostic package without hash, sessions, Wi-Fi, IP; preview before download; note "GitHub is public" | upload only with consent, deletion period |
| A firmware bug doses too much | gateway with fixed limits in code, configuration only tightens them (R1, R7); calibration value mandatory; job ID per attempt and start against double dosing; deadline per run (without feedback: off, counted as run) | time limit and "one channel" in hardware in the dosing block, enable in hardware per port [PD-012] |
| A sensor lies | sensor truth: freshness, frozen readings, band, jump lock, calibration; EC gate. Home Assistant sensors: freshness learned per sensor, up to 30 min (SD-035), for display only until control has its own freshness check; a pH probe's voltage calibrated in the app is a valid reading, but nothing that can switch reads it (the Home Assistant bus refuses every run and switch and reports no pumps; what control needs first is in `docs/HOME_ASSISTANT.md`); a device whose reported class differs from the configured one has no value, so an import cannot turn a pH calibrated in Home Assistant into one the hub checks (SD-041); with a sign of life a sensor is stale 60 s after its last measurement, and an unavailable or unreadable sign of life, a missing channel, a time in the future, the same text dated again or one that dates the measurement before the sensor's own report makes it stale, never the longer learned limit (SD-042); which sign of life belongs to which sensor rests on the user's pick, which the app marks but cannot prove, so a wrong pick could keep a dead sensor fresh: display only, to be checked before control reads it (`docs/HOME_ASSISTANT.md`) | measurement window with the pump off until the galvanic isolation has passed acceptance (RAT-044) |
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
   `tools/arch_check.sh` rejects sdkconfig options, also in the board
   profiles, that would burn eFuses
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
