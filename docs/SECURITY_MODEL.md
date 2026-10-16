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
states in the row on faulty input (#71), added on 2026-10-13. The
changes for SD-076 (pre-flashed devices, the setup network, the first
password, the board's USB port, Bluetooth, the factory reset, the
support period) were made on 2026-10-13 by the software session after
`security`'s review, also to be confirmed by `regulatorik`. Those for
SD-080 (the factory reset after BOOT 15 s and a confirmation, the plugs
switched off first, the access reset) were made on 2026-10-15 by the
software session after `security`'s review, also to be confirmed by
`regulatorik`.
Two kinds of safety and security:

- **functional:** no overdose, no overflow, no dry run;
- **IT security:** no outside access to the pumps, unless the user
  chooses "View and control from outside" at setup, and then only through
  defined ways (SD-059).

## Threats and responses

| Threat | Response in the prototype | Later |
|---|---|---|
| A stranger on the home network switches pumps | Mandatory password before any function, no default password, PBKDF2-SHA-256 (10,000 rounds) with salt, session as an HttpOnly/SameSite cookie (at most 16; a 17th sign-in ends the session used longest ago; it ends after 12 h without a request, on a restart and on a password change; a page in use renews it, no fixed longest lifetime; a Stop from the app needs it too, SD-064), lockout after 5 failed attempts (30 s, doubling up to 15 min) per client: an address that signed in since the hub last started (at most 16, in memory only, the one seen longest ago drops out first) has its own count, all others share one, so someone guessing from another address cannot lock out such a device, and guessing from addresses that never signed in is as slow as from one; a host that takes over a known device's address gets that device's count (accepted, SD-051); the address comes from the socket, never from a header (`tools/arch_check.sh` refuses a trusted proxy); a wrong old password when changing it counts and is logged; only counted failures are logged; everything except `/info` requires login (`/info` gives the version, platform, hub name and language, whether a password is set and whether a setup code is needed, docs/API.md) | HTTPS with a certificate per device, tokens with roles for integrations; on the ESP the first password with SD-044's setup token (until it is built, whoever comes first sets it) |
| Eavesdropping on the Wi-Fi | – | HTTPS by default (EN 18031-1 SCM; Shelly enforces it) |
| A third-party website triggers actions (CSRF, clickjacking, DNS rebinding) | SameSite=Strict, origin check (`Sec-Fetch-Site`/`Origin` against `Host`), `Host` only an IP address, `localhost` or a home-network name (`.local`, `.lan`, `.home.arpa`, `.internal`, `.fritz.box`); CSP `default-src 'self'`, `X-Frame-Options: DENY` | – |
| Tampered update | Home Assistant app (SD-034): the image is built without write access, checked against its checksum and labels, pushed only by the release job for an admin-only `v*` tag, assembled by digest and attested (`docs/RELEASE.md`). Home Assistant checks no attestation; it installs whatever image carries the version in `ha/app/config.yaml` on `main`, so a version change there is a release step (a process rule; no GitHub rule enforces it). Open, accepted by the project owner for the experimental phase (SD-034): any workflow of the repository can be given `packages: write` and overwrite the image, so everyone with write access to the repository can | signed OTA checked by the firmware, downgrade protection in software, no eFuses burned except the HMAC key for encrypted NVS; the owner can always install their own firmware over USB (PD-022, PD-043, PD-047) |
| A device in the network reaches the Home Assistant app's port | plain HTTP on port 8099 of the Home Assistant host; password, lockout, `Host` and origin checks as on the hub; the first password needs a one-time setup code of 80 bits that the app prints to its log, which only Home Assistant's administrators read (and apps they gave that right) (#79, SD-068): kept in memory only, used up by the password, a new one at every start; wrong codes are counted, never locked out (a lockout shared by every new device would let anyone block the owner's setup), and logged at most once per 15 min; the server takes its random numbers from `getentropy()`, and `tools/arch_check.sh` refuses `std::random_device`, RDSEED and RDRAND outside the Windows branch of `sim::randomBytes` (#234); clearing the port hides it from the network, not from Home Assistant's internal network; a session reads the names of the Home Assistant sensors it lists (the suggestions and, on request, the other sensors that could be picked or need a reason, at most 1000; values only of those that could be picked, SD-036; a voltage of at most 5 V, which could be a pH probe's raw signal, is among them with its value, SD-041; higher voltages are not passed on; on request, while a sign of life is picked, the sign-of-life texts and timestamp sensors with their names, channel codes and ages, at most 100 of each, and otherwise only their count and the names of those in use, SD-042), nothing else of Home Assistant | Home Assistant's ingress (its login and HTTPS) |
| A flaw in the app's web server exposes the Supervisor's token, which acts as Home Assistant's administrator | the address and path are fixed (`--app`), no request a user can steer goes to Home Assistant, the bus switches nothing, the image has no shell (`FROM scratch`), the app asks for no other right (`tools/app.test.mjs`); before it opens its port the server gives its data folder to an account of its own (65532) and becomes it for good: no root, no capabilities, no new privileges, checked by trying `setuid(0)` (#80); an AppArmor profile lets it read, besides the basic system files, only its web app and `/etc/hosts`, write only its data folder and use only TCP (`ha/app/apparmor.txt`, tested in the app job, which fails on any refusal). Still open: the Supervisor starts every app without a seccomp filter, and its docker-init stays root next to the server with the profile's capabilities (chown, dac_override, kill, setgid, setuid); it runs nothing but the server, which can no longer signal it; a host without AppArmor runs the app without the profile | a seccomp filter of the server's own |
| A known flaw in a library inside the app image | the image names musl's and the GCC runtime's exact versions (`/licenses/BUILD_PACKAGES.txt`); security fixes for them, cpp-httplib and nlohmann/json are watched by hand; scanners see nothing in a static binary | an SBOM for the image with the release (#81) |
| A malicious npm package in the web app's build (the build tools run native code in every build) | every package pinned by its sha512 in `web/package-lock.json` and resolving to its own record on the npm registry; Dependabot proposes updates only after 7 days; `npm ci --ignore-scripts`; `npm audit signatures` right after every install (SD-058), run by the Node and npm pinned in `.nvmrc` (in the app image from the `node` image pinned by digest, which the build checks against `.nvmrc`; SD-070, SD-071), proves that the registry's record of each installed version is signed and checks provenance where a package publishes one. It adds nothing against a hijacked maintainer account: the registry signs its releases too, and a hijacked publishing pipeline even produces valid provenance; `tools/check_provenance.mjs` fails when a package loses the provenance a lower version of it had, or gets it from another source repository or repository id, the typical sign of a release published by hand from a stolen token (SD-063; accepted cases with their reason in `tools/provenance-exceptions.json`) | not caught yet: a package that never had provenance, a release by hand on a version line that never had provenance, a release by a hijacked CI or trusted publisher of the same repository; compare each lockfile hash with the signed one |
| Load on the web server disturbs control (RLM) | control runs in its own cycle, requests only under a lock | own task priority and own core on the ESP32, load test |
| Faulty input | every change and every import is validated (limits, phases, calibrations; whole numbers in range for the running phase, days, channels and the schema version, a time the hub cannot have written read as not set; an imported calibration passes the same check as one measured in the app, RAT-025, and is trusted like a backup, only for a kind the device's class offers; an unknown entry in the choice per area, SD-037, is left out of an import; a sign of life only for a class whose sensors report on their own schedule (set in code), its source of `a–z`, `0–9`, `_` and `.` up to 255 characters, its channel of `a–z`, `0–9` and `_` up to 16, SD-042; a series id in the history and the CSV export of `a–z`, `0–9`, `.` and `_` up to 64 characters, at most 16 series and none twice per request, anything else refused before a line is read or written; a sign-of-life text at most 255 characters, 64 channels and ages up to 9 999 999 s, anything else unreadable), JSON limit 1 MB or 64 KB (exception: the Home Assistant trial reads Home Assistant's state list, at most 16 MB, read by a SAX reader that keeps only sensors (at most 20 000) and a few fields of each (a name cut to 256 bytes, any other longer field dropped) and stops at 32 levels of nesting, docs/HOME_ASSISTANT.md; a state is a value only as a plain decimal number read in full, whatever the locale, and names and faults keep only printable, well-formed UTF-8, #71), exceptions caught (400/500 instead of a crash; in the control cycle: everything off, alarm; in a Stop: everything off and the Stop in force and saved first, a dose that cannot be booked is dropped and reported) | fuzzing |
| Password file lost (power failure while writing) or unreadable | on the device reading falls back to the complete `.tmp`, only for a file that is not there; a password file that is there but cannot be read (no permission, a read error) is kept and never written over: no first password over it (423 `auth.kept`), no setup code, no advice to reset (SD-081); `auth.json` is written before the lock marker and is on disk when the call returns (Linux: `fsync` of file and folder; macOS/Windows best effort; #68), otherwise setup answers "Not saved" and sets nothing; files are created for the owner only (0600, a new folder 0700); once a password was set, setup over the network is locked (423); the marker never comes from an import | password hash, lock and "window used" in encrypted NVS and a "set up" marker in `storage`: losing one of the two reports lost credentials (423), never a new device; the firmware will no longer erase NVS or format `storage` by itself (today it does both); residual risk: losing both at once; factory reset by holding BOOT for 10 s (SD-076) |
| A slow peer blocks the web server | firmware: 2 s timeout, one retry, then 408; headers up to 2 KB | the Stop on the device without the web, load test |
| Data leak when reporting a problem | diagnostic package without hash, sessions, Wi-Fi, IP; preview before download; note "GitHub is public" | upload only with consent, deletion period |
| A firmware bug doses too much | gateway with fixed limits in code, configuration only tightens them (R1, R7); calibration value mandatory; job ID per attempt and start against double dosing; deadline per run (without feedback: off, counted as run) | time limit and "one channel" in hardware in the dosing block, enable in hardware per port [PD-012] |
| A sensor lies | sensor truth: freshness, frozen readings, band, jump lock, calibration; EC gate. Home Assistant sensors: freshness learned per sensor, up to 30 min (SD-035), for display only until control has its own freshness check; a pH probe's voltage calibrated in the app is a valid reading, but nothing that can switch reads it (the Home Assistant bus refuses every run and switch and reports no pumps; what control needs first is in `docs/HOME_ASSISTANT.md`); a device whose reported class differs from the configured one has no value, so an import cannot turn a pH calibrated in Home Assistant into one the hub checks (SD-041); with a sign of life a sensor is stale 60 s after its last measurement, and an unavailable or unreadable sign of life, a missing channel, a time in the future, the same text dated again or one that dates the measurement before the sensor's own report makes it stale, never the longer learned limit (SD-042); which sign of life belongs to which sensor rests on the user's pick, which the app marks but cannot prove, so a wrong pick could keep a dead sensor fresh: display only, to be checked before control reads it (`docs/HOME_ASSISTANT.md`) | measurement window with the pump off until the galvanic isolation has passed acceptance (RAT-044) |
| Power failure in the middle of a run | after start-up everything is off, nothing resumes, a message; exception: fan sockets come back on (PD-050), not during a Stop (PD-076) | the dosing block stops without a sign of life from the hub |
| A socket switches on by itself when mains returns | only roles on a fixed allowlist in code (exhaust, circulation fan) may be "on after power loss", only continuous loads without a maximum run time; a socket that loses the role, its device or its binding in an import goes back to "off", read back, failure reported | the setting is stored in the socket; the user can change it there |
| Someone else in the home network sets the first password | prototype: whoever comes first sets it, as the Home Assistant app's documentation says | self-flashed devices: a one-time token over USB (SD-044); pre-flashed devices: within 15 min from the first join to the home network, reopened only by holding BOOT for 3 s, never by a restart (whether a restart also ends an open window is open); never through the relay (SD-062); the open window shown by the setup network's last page, an LED and a countdown; for 60 min the login page shows when it was set, and after login the web app and the event log show from which address; residual risks: anyone in the home network can be first, and the way back is the factory reset, which also undoes what the stranger changed, or the access reset with BOOT, which keeps the data and those changes (SD-080); the certificate per device is trusted on first use, so an active attacker in the home network can read the first password (SD-076) |
| Someone replaces a password that is set | only with the old password (`PUT /auth/password`) | with the old password, or without it only by the access reset: BOOT held long at the device opens a 15-min window in the home network, and the new password counts only after a short BOOT press, the old one until then; a second request or a restart ends the window; never by Home Assistant, an API token, the setup network or the relay; afterwards every session, token and the Home Assistant key (the native API key) are invalid and their live connections end, the lockout forgets its clients, and the level of outside access and the relay address go back to their defaults; the Home Assistant app's own password (SD-077) is not touched; the request comes only directly from the home network, decided by the interface it arrives on; an "access reset" entry stays until a factory reset, shown after login and on the LEDs; residual risks: whoever is at the device and in the home network takes over access, and anyone in the home network can end the window with a second request (SD-080) |
| A computer on a USB port of the DevKit flashes other firmware or restarts the ESP | – | on the ESP32-S3 only an eFuse stops a host on the board's port from starting download mode for good, which PD-022 and SD-024 exclude, and the DevKit's UART port starts it through its auto-program circuit (to be confirmed on the schematic): the docs say to unplug USB in operation, the firmware shows a message while a USB host is connected, a restart by the USB host counts as a power loss (everything off except the fans, reported, R6, PD-050); JTAG cut at run time by the bridge bit, serial kept for Improv (to be proven on a sample); residual risk: other firmware can feed the board watchdog and use the HMAC key; open product question: the port inside the enclosure, a switch for its data lines, or USB-OTG in operation (SD-076) |
| Someone in radio range joins the setup network | – | WPA2 with a random key per device of at least 80 bits (pre-flashed: made at production, on the label; self-flashed: made at the first start, shown after login); on a pre-flashed device open while no Wi-Fi is configured (a time limit is open), otherwise only for 10 min after BOOT is held for 3 s, one client; sets Wi-Fi only, never a password; no fallback access point; residual risks: a photo of the label (the key can be changed in the web app, until a factory reset brings the label's key back), the key passing the production PC and the printer, a seller who knows the key of a used device (SD-076) |
| Someone in radio range sends forged Bluetooth advertisements | – | passive observer only for sensors the user added, off without one; no advertising, no connections, no pairing; their values pass sensor truth like any sensor, encrypted where the sensor can (`COMPATIBILITY.md`); residual risk: a forged value inside the band from an unencrypted sensor (SD-076) |
| Wrong or spoofed time | plausibility bounds for the saved clock and events; continued clock without a secured time; clock steps keep the remaining time of locks and pauses and are logged; intervals across restarts use operating time unless both moments were secured | plausibility checks of network time and the app's device time (PD-073) are open |

## Logging (CRA Annex I 2(l))

The event log records:

- login and failed attempts;
- refused setup codes before the first password (the first one, then at
  most one entry per 15 min with the count, and the count at the setup);
- password changes;
- configuration changes with revision;
- import;
- the Stop and maintenance mode;
- a file of the hub that cannot be read and is kept (SD-081; the Home
  Assistant server also prints it at the start, with the reason, since no
  one can sign in while the password file is kept). While the events
  themselves are kept, new entries, sign-ins and failed logins included,
  stay in memory until a restart can read them again;
- requested updates;
- planned (SD-080): factory resets started, not confirmed or done, and
  access resets requested, ended by a second request or applied; an
  applied access reset stays visible until a factory reset.

The log can be exported with the diagnostic package. Users should be able
to switch it off (open).

## Factory reset and data (CRA 2(m))

Export and import of the settings exist. The factory reset that erases
securely comes with `firmware/` (SD-076, SD-080):

- **Triggers:** BOOT held for 15 s; the web app from the home network,
  after asking for the password again; our flash page over USB. Home
  Assistant and API tokens cannot start it.
- **Switch-off first:** the board's dosing pumps and refill valve and
  the plugs with a water or CO2 role go off through `Actuators`; light, fans, room heater and
  air pump keep their state, and the drain with its own pump stays on.
- **Confirmation:** a short BOOT press within 30 s (in the web app: its
  confirmation), also when a plug did not report "off"; without it
  nothing is erased.
- **Erased:** it marks the reset, erases `history` and `storage`, then
  the user NVS last, and makes a new random value for the storage keys
  (where it is kept: open, #176).
  This removes Wi-Fi, the password, settings, calibrations, history and
  the passwords of adopted plugs; the web app offers an export without
  passwords first.
- **Kept:** the app slots, `otadata`, the floor of the downgrade
  protection (proposed, SD-080) and the read-only factory data (`fctry`:
  serial number, the setup network's key from the label).
- **Interrupted:** a reset cut by a power loss is finished at the next
  start.
- **Whole-chip erase:** over USB it also loses the factory data and
  switches no plug off; the device then behaves like a self-flashed one.
- **Access reset** (BOOT held long, SD-080): replaces only the password,
  after a short BOOT press, and keeps the data; it ends every session and
  token and the Home Assistant key, and leaves an entry until a factory
  reset.
- **Passing a device on:** the owner uses the factory reset.

There is no telemetry.

## Before the first device goes to third parties (beta testers included)

According to `regulatorik`, before 2027-12-11 every device that is sold or
lent must fully meet EN 18031-1. After that date the CRA applies. With
pre-flashed devices (PD-179, SD-076) the project is the manufacturer of a
radio device: an EN 18031-1 and -2 self-assessment for every firmware
version shipped to them, and factory provisioning of the label key.
List:

1. HTTPS locally, certificate per device.
2. Signed OTA (EN 18031-1 SUM-2) checked by the firmware, without
   hardware Secure Boot; downgrade protection in software; no eFuses
   burned except the HMAC key for encrypted NVS (PD-043, PD-047, SD-021,
   SD-024). Secrets (Wi-Fi credentials, password hash, TLS key) in
   encrypted NVS, sessions only as hashes; JTAG cut at run time by the
   USB-Serial-JTAG bridge bit, without a further eFuse, the serial port
   kept for Improv (to be proven on a sample; JTAG is on from each reset
   until the app starts). Over
   USB the owner can always install their own firmware (PD-022).
   `tools/arch_check.sh` rejects sdkconfig options, also in the board
   profiles, that would burn eFuses
   for Secure Boot, flash encryption, a disabled download mode or
   anti-rollback. Residual risk, documented: someone with the device in
   hand can load own code and use the HMAC key. On the DevKit's ports
   no button is needed: any connected computer can do this (SD-076).
   Open: whether a test lab accepts the USB path without a button.
3. Setup network (PD-181, PD-186, SD-076): WPA2 with a key per device of
   at least 80 bits (pre-flashed: made at production and printed on the
   label; self-flashed: made at the first start, shown after login,
   SD-044); on a pre-flashed device open without a button press while no
   Wi-Fi is configured (a time limit is open), otherwise only for 10 min
   after BOOT is held for 3 s, one client; no fallback access point; it
   sets Wi-Fi only. The first password is mandatory: self-flashed
   devices need the one-time token from Improv over USB (SD-044),
   pre-flashed devices take it within 15 min after joining the home
   network (PD-191, PD-192). It fails closed when one of its two stores
   is lost. Without the old password, a password that is set is replaced
   only by the access reset at the device (SD-080).
4. Bluetooth only as a passive observer for sensors the user adds
   (PD-153): no advertising, no connections, no pairing, no setup over
   Bluetooth (PD-186), off while no such sensor is set; JTAG and debug
   off in a way that keeps installing own firmware possible (PD-022).
   MQTT and Home Assistant only when the user switches them on.
5. Cyber risk assessment, EN 18031 self-assessment, technical
   documentation, reporting process; support period of 7 years from the
   delivery of each board, ending in December, shown in the web app
   (PD-182 to PD-185, `SECURITY.md`).

How to report vulnerabilities: [`../SECURITY.md`](../SECURITY.md).
