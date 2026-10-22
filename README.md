# growcontroller-software

Free software that mixes nutrient solution for your tank, keeps pH and
EC on target and refills the water, and tells you in plain words why it
doses or why not. For hydroponics, greenhouses and indoor growing. It is
made to run on a small device of its own, the hub (built on an ESP32-S3
board, in development), with its own web app: no cloud, no account, no
Home Assistant required.

**It is a prototype.** Today it runs as a simulator on your computer:
**[try the demo](docs/DEMO.md)** on Windows, macOS or Linux, about 15
minutes, no hardware needed. On a phone? Read the
[short tour](docs/DEMO.md#5-a-short-tour) now and try it later on a
computer.

![The web app in the demo: tank with pH, EC and water temperature, the control rows with their state, room climate and switched outputs](docs/images/overview.png)

> **For testing only:** this version does not meet the requirements of
> the EU Cyber Resilience Act (CRA).

> **Status: prototype `0.1.0-proto.4`.** Core, simulator and web app run;
> the port to the ESP32-S3 follows (`firmware/README.md`). The Home
> Assistant app only reads sensors and switches nothing. Do not use it to
> control real hardware.

> **Safety.** This is experimental prototype software, not a finished or
> certified product (no CE marking). Do not use it to control real pumps,
> valves, mains-powered devices or chemicals such as pH adjusters: a fault
> can cause overdosing, overflow, water damage or fire. You use it at your
> own risk; see sections 15 and 16 of the [`LICENSE`](LICENSE).

## Try the demo

1. Download the simulator for Windows, macOS or Linux from
   [Releases](https://github.com/Wokesay/growcontroller-software/releases)
   (one zip file per system).
2. Unpack it and start `growcontroller-simulator`. Windows warns once
   (**More info › Run anyway**); macOS needs one Terminal command first
   ([start on macOS](docs/DEMO.md#2-start)).
3. Your browser opens the demo. Sign in with the password
   `demo-passwort`. The demo opens in German for now: switch to English
   under **Einstellungen › Darstellung und Info › Sprache** (Settings ›
   Display and info › Language).

A tank that has been running for two days, with pumps, probes, history
and a panel to speed up time and cause faults. Step by step, with a short
tour and help when it does not start: the
[step-by-step guide](docs/DEMO.md). Download only from the Releases page.

With Home Assistant (OS or Supervised), growcontroller also installs as
an app that reads your sensors and switches nothing:
[`docs/HOME_ASSISTANT.md`](docs/HOME_ASSISTANT.md).

## What it does

These work in the simulator (as of the code on `main`; the newest
download lags behind in places), and will on the hub from its first
firmware release:

- **Guided mixing:** recipes from your bottles, including A and B pairs,
  for a new batch or a top-up. With a circulation pump it mixes by
  itself; without one it tells you when to stir. If a mix stops halfway,
  it says what is in the tank and what is missing.
- **pH and EC:** corrects in small doses, waits for the tank to settle,
  and learns how much each dose moves the reading.
- **Refill and circulation:** refills by a calculated amount, with the
  level sensor as the emergency cut-off; circulates on a schedule or when
  needed.
- **Safety rules on the device:** every dose and every opening of the
  inlet has a time limit; a reading that jumps, freezes or stops updating blocks the
  control that depends on it; a missing reading is never treated as 0;
  after a power cut, pumps, valves and the light stay off; STOP in the
  app switches everything off at once.
- **It explains itself:** each control function has a checklist that
  answers "why is it dosing now, or why not?", and the monitoring says
  what is wrong in words.
- **Guided setup:** devices are detected, not typed in; the hub says what
  works with your hardware and what is missing.
- **History and data:** readings, doses and events, CSV export, a backup
  of your settings, and a diagnostic package for bug reports.

Planned: automatic light, climate and watering (today you switch them by
hand), notifications without a vendor cloud, and Home Assistant
integration ([roadmap](docs/ROADMAP.md)).

| Part | Status |
|---|---|
| Software in the simulator, web app in English and German | works; tested automatically on every change |
| Home Assistant app | reads sensors, switches nothing |
| Firmware for the hub (ESP32-S3) | in development, not released |
| Control of real pumps and valves | not yet |

## Feedback and help

Feedback on the demo is the most useful contribution right now.

- **Questions, ideas, feedback:** [Discussions](https://github.com/Wokesay/growcontroller-software/discussions)
- **Something does not work:** in the app under "Settings › Report a
  problem" (with a report number), or a
  [bug report](https://github.com/Wokesay/growcontroller-software/issues/new?template=bug.yml)
- **A device you use:** the
  [device form](https://github.com/Wokesay/growcontroller-software/issues/new?template=device.yml)
- **Vulnerabilities:** never in public, see [`SECURITY.md`](SECURITY.md)
- More: [FAQ](docs/FAQ.md), [getting help](SUPPORT.md),
  [contributing](CONTRIBUTING.md), [code of conduct](CODE_OF_CONDUCT.md),
  [changes](CHANGELOG.md)

## How this project is made

growcontroller is built in the open by one maintainer with an AI coding
assistant (Claude Code), which writes most of the code, tests and docs;
the commits say so. AI review agents check every change, the automated
tests and checks below must pass, and the maintainer approves every
release and every logic change to the pump gateway and the protective
cut-offs. Each rule taken from growing practice is written down with its
reason, most of them with a test, and each decision is recorded. Codes
like SD-050, PD-022 or RAT-042 point to those records
([what they mean](docs/FAQ.md#what-do-sd--pd--rat--and-m1-mean)).

## Build it yourself (no hardware needed)

Requirements: CMake ≥ 3.20, Ninja, a C++17 compiler, Node.js 22.12 or
later (CI runs the one in `.nvmrc`); for `tools/ci.sh` also
[reuse](https://reuse.software/) (`pipx install reuse==6.2.0`; CI installs
`tools/requirements-reuse.txt` with its hashes).

```bash
tools/dev.sh
# → http://127.0.0.1:8080  ·  password: demo-passwort (simulator only)
```

This starts the digital twin with a set-up tank and 48 hours of history.
`SCENARIO=neu tools/dev.sh` starts an empty hub with first-time setup. The
**Simulator** button in the app offers time-lapse, faults (pH jump, pump
blocked, power loss, wrong plug …) and scenarios. What to install on
Linux, macOS, Windows or a Raspberry Pi:
[`docs/DEMO.md`](docs/DEMO.md#8-build-it-yourself-instead).

## Layout

```
core/      C++17 core, platform-neutral: catalog, configuration, sensor truth,
           resolver, mixing, actuator gateway, controllers, watchdog, history, events, API
catalog/   device catalog as data (capabilities, device classes, roles, functions)
sim/       simulator: twin (ports, dosing block, heads, tank) and host server
ha/        read-only spike: the core next to Home Assistant; ha/app is the Home Assistant app
           (docs/HOME_ASSISTANT.md)
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

## Hardware: which way fits you

From the first firmware release (M1) on, an ESP32-S3 does all control;
Home Assistant is an option on top (SD-050).

- **ESP32-S3 on its own** (in development, M1): an Espressif
  ESP32-S3-DevKitC-1 (N32R16V; N8R8 works too, with a shorter history) on
  the growcontroller carrier board, with sensors and pumps. You flash the
  firmware yourself and use the web app on the ESP in your browser.
- **ESP32-S3 with Home Assistant** (planned, M3): the same, plus Home
  Assistant. All values and states, the watchdog included, show in Home
  Assistant, and the Home Assistant app opens the ESP's web app there.
  Control stays on the ESP.
- **Try it today:** the simulator needs no hardware. The Home Assistant app
  shows sensors you already have in Home Assistant and switches nothing
  (see "Try the demo" above).

Does my hardware work?

- The [compatibility list](docs/COMPATIBILITY.md) covers ESP32 boards,
  sensors in Home Assistant (for example DFRobot, Atlas Scientific,
  DS18B20), the carrier board's inputs and outputs, Bluetooth sensors and
  smart plugs, each with its status. Tested a device?
  [Report it](https://github.com/Wokesay/growcontroller-software/issues/new?template=device.yml).
- **Flashing the ESP32-S3:** a step-by-step guide follows with the first
  firmware release. For developers:
  [build and flash your own firmware](firmware/FLASHING.md).

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
  subscription and no dual licensing.
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

## Trademarks

Product and company names such as Athena, Atlas Scientific, DFRobot,
Espressif and Shelly are trademarks of their respective owners. They are
used only to describe compatibility or to cite a source; this project is
not affiliated with, endorsed or sponsored by them.
