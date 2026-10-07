# growcontroller-software

Standalone software for a hydroponic grow controller: platform-neutral C++
core with setup wizard, sensor catalog, pH/EC dosing, refill and
circulation control, watchdog and logging. Includes ESP32-S3 firmware, a
web app for browser and mobile, and a simulator (digital twin) for testing
without hardware.

---

Universal plant automation for greenhouses, indoor growing and
hydroponics: mix nutrient solution, control pH and EC, fill the tank – and
step by step light, climate and irrigation. The software runs on the hub
(ESP32-S3) and brings its own web app – no cloud, no account, no Home
Assistant required.

> **Status: prototype `0.1.0-proto.1`.** Core, simulator and web app run;
> the port to the ESP32-S3 follows (`firmware/README.md`). Do not use it
> with real hardware.

## Try it without installing

From the next release on, **Releases** offers the simulator for Windows,
macOS and Linux: one file, unpack, double-click. The browser opens the
demo (password `demo-passwort`). Notes on Windows SmartScreen and macOS
are in `README.txt` inside the package. Every CI run also stores the
packages as artifacts.

## Build it yourself (no hardware needed)

Requirements: CMake ≥ 3.20, Ninja, a C++17 compiler, Node.js 22; for
`tools/ci.sh` also [reuse](https://reuse.software/) (`pipx install reuse==6.2.0`).

```bash
tools/dev.sh
# → http://127.0.0.1:8080  ·  password: demo-passwort (simulator only)
```

This starts the digital twin with a set-up tank and 48 hours of history.
`SCENARIO=neu tools/dev.sh` starts an empty hub with first-time setup. The
**Simulator** button in the app offers time-lapse, faults (pH jump, pump
blocked, power loss, wrong plug …) and scenarios.

## Layout

```
core/      C++17 core, platform-neutral: catalog, configuration, sensor truth,
           resolver, mixing, actuator gateway, controllers, watchdog, history, events, API
catalog/   device catalog as data (capabilities, device classes, roles, functions)
sim/       simulator: twin (ports, dosing block, heads, tank) and host server
web/       web app (Preact, TypeScript, Vite) and end-to-end tests (Playwright)
tests/     C++ tests: unit, API contract, scenarios against the twin
firmware/  plan and skeleton for the ESP32-S3 (ESP-IDF)
docs/      concept, domain rules and rationale, UX, API, tests, releases, security, workflow
tools/     ci.sh (all checks), dev.sh (start the simulator), arch_check.sh (architecture rules)
```

Start reading the docs at [`docs/README.md`](docs/README.md).

## Checks

```bash
tools/ci.sh            # architecture rules, licenses, core with sanitizers, C++ tests, web build with size budget
E2E=1 tools/ci.sh      # plus the browser tests against the simulator
```

## Contributing, bugs, security

- Contributions: [`CONTRIBUTING.md`](CONTRIBUTING.md)
- Bugs: in the app under "Settings › Report a problem" (with a report ID),
  or an issue with the "Bug report" template
- Never report vulnerabilities as an issue: [`SECURITY.md`](SECURITY.md)
- Changes: [`CHANGELOG.md`](CHANGELOG.md)

## License

This software is free software under the **GNU Affero General Public
License, version 3 or later** (AGPL-3.0-or-later), see [`LICENSE`](LICENSE).
In plain words:

- **Use, study, change, share.** You may do all of this, also
  commercially.
- **Share alike.** If you pass the software on – as a download, on a
  device, or as a service over a network – you must offer the source code
  of your version under the same license. The web app links to the exact
  source code it was built from (AGPL §13).
- **No warranty.** The software comes as it is.
- **No paid edition.** The project sells no licenses and has no
  subscription and no dual licensing. It is funded by selling hardware.
- **Your device is yours.** The hub does not lock out third-party
  firmware: there is no Secure Boot against the user, and you can install
  your own firmware. How: [`firmware/FLASHING.md`](firmware/FLASHING.md).
- **One license for everything here.** Code, docs, catalog and templates
  are under the AGPL. One additional permission (AGPL §7) lets the
  firmware be combined and shared with Espressif's binary-only ESP-IDF
  libraries (Wi-Fi, PHY, coexistence), see
  [`ADDITIONAL_PERMISSION.md`](ADDITIONAL_PERMISSION.md).
- **Contributions** are accepted under the same license, including the
  additional permission (inbound = outbound); no CLA, no sign-off.
- **Third-party components** keep their own licenses:
  [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

The hardware (circuit boards) is not part of this repository.
