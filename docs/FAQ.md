# Frequently asked questions

Short answers, with links to the details. Missing a question? Ask in
[Discussions](https://github.com/Wokesay/growcontroller-software/discussions).

## The project

### What is growcontroller?

Software for a fertigation controller: it mixes nutrient solution from
bottles, keeps pH and EC in a target range, refills the tank and runs the
circulation pump. Light, climate and irrigation follow step by step. It
is meant to run on a small device of its own (an ESP32-S3) and to bring
its own web app: no cloud, no account, no Home Assistant required.

### Who is it for?

Growers with a nutrient tank, in hydroponics, a greenhouse or a grow
room, who want automation that explains itself: every dose, and every
time it holds back, comes with its reason in plain words. And makers who want to look
inside, change it and run their own version.

### What works today?

- **The simulator** (a digital twin) runs the complete software against
  simulated pumps, probes and a tank: guided mixing, pH and EC
  correction, refill, circulation, the watchdog, locks, history, Stop and
  the first-time setup. [Try the demo](DEMO.md).
- **The Home Assistant app** shows sensors you already have in Home
  Assistant and switches nothing ([`HOME_ASSISTANT.md`](HOME_ASSISTANT.md)).
- **Real hardware:** not yet. The firmware for the ESP32-S3 is in
  development and has not been released.

The full picture: [`CONCEPT.md`](CONCEPT.md) §5 and §6.

### When will it run on real hardware?

With the first firmware release, milestone M1 in the
[roadmap](ROADMAP.md). There is no date: it ships when its safety parts
and tests are done.

### Does the software cost anything?

No. It is free software under the AGPL-3.0-or-later. There is no paid
edition, no subscription, no cloud account and no license for sale, and
you may use, change and share it, also commercially, under the same
license ([README](../README.md#license)).

The hardware, a carrier board for the ESP32-S3, is developed separately
and is not part of this repository. It is in development and not
available; whether and how it will be offered, and under which license,
is not decided yet.

### Who is behind it, and how is it built?

One maintainer, in the open, with an AI coding assistant (Claude Code)
that writes most of the code, tests and docs; the commits say so. Every
change, whoever wrote it, has to pass:

- unit, API and scenario tests against the simulator, and browser tests
  of the web app ([`TESTING.md`](TESTING.md));
- CI with sanitizers, a size budget for the web app, license checks and
  machine checks of the architecture rules (`tools/arch_check.sh`);
- review by AI review agents ([`WORKFLOW.md`](WORKFLOW.md));
- the maintainer's approval for every release and every logic change to
  the pump gateway or the protective cut-offs.

Each rule from growing practice has an entry with its reason in
[`RATIONALE.md`](RATIONALE.md), most of them with a test
([`INVARIANTS.md`](INVARIANTS.md) shows which), and each decision is
written down ([`DECISIONS.md`](DECISIONS.md)). If you find a gap, please
tell us.

### What do SD-, PD-, RAT- and M1 mean?

- **SD-xxx:** a software decision, recorded in [`DECISIONS.md`](DECISIONS.md).
- **PD-xxx:** a product decision. Product decisions (hardware, costs,
  legal points) are made in a separate, private repository; when one
  affects the software, this repository records what applies as an SD.
- **RAT-xxx:** an entry in the rationale register
  [`RATIONALE.md`](RATIONALE.md): a rule from growing practice and why it
  exists.
- **M0 to M6:** the milestones in the [roadmap](ROADMAP.md).
- **R1 to R8:** the architecture rules in [`CONCEPT.md`](CONCEPT.md) §4.

## Hardware

### Which hardware will it need?

An Espressif ESP32-S3-DevKitC-1 (N32R16V, or N8R8 with a shorter history)
on the growcontroller carrier board, with probes and pumps. The
[compatibility list](COMPATIBILITY.md) shows what is planned and what
has been tested, sorted by how a device is connected.

### Can I use my own probes, pumps and smart plugs?

The compatibility list says what is planned for each kind of device, and
[a short form](https://github.com/Wokesay/growcontroller-software/issues/new?template=device.yml)
lets you report a device you use or would like to use. Reports like this
help decide what gets supported.

### Can I use it with Home Assistant?

Today the Home Assistant app shows your Home Assistant sensors and
switches nothing. From milestone M3 on, the ESP32-S3 sends all its values
and states to Home Assistant; control stays on the ESP32-S3, so it keeps
running when Home Assistant is down (SD-050).

### Why not ESPHome, or Home Assistant alone?

ESPHome devices are described in YAML and compiled: adding a pump or a
probe means editing the file and flashing again. growcontroller detects
devices and is set up in its web app. Home Assistant is a fine place to
see and combine values, but dosing pH down into a tank should not depend
on a server, a network and automations staying up. So control and its safety rules live on the
device, and Home Assistant is an option on top. More in
[`CONCEPT.md`](CONCEPT.md) §1 to §3.

## Safety

### Is it safe to let software dose pH down?

Not yet with this prototype: do not connect it to real pumps, valves,
mains-powered devices or chemicals. The design is built around that
risk:

- only one part of the code, the actuator gateway, can switch a pump or
  an output, and it enforces calibration, time limits for doses and the
  inlet, and one run at a time;
- the monitoring (watchdog) only evaluates and has no way to switch
  anything;
- a value that jumps, freezes, goes stale or is implausible blocks the
  control that depends on it, and a missing value is never treated as 0;
- after a restart pumps and valves stay off (the fans come back on), and
  an interrupted mix or manual dose is reported, not resumed;
- STOP in the app switches every pump and output off at once. It is not
  an emergency stop: it needs the app to reach the hub.

These protections are software. They have not run on real hardware yet,
and a time limit for the pumps in the hardware itself is still to come.
A single probe that stays wrong by a constant amount is trusted again
once it reads steadily, a known limit (SD-085).
[`CONCEPT.md`](CONCEPT.md) §4 has the rules, [`INVARIANTS.md`](INVARIANTS.md)
the tests that check them.

### Does it need the internet or a cloud?

No. Control runs on the device and needs neither the internet nor an
account; the demo runs offline. With internet, the device will check
GitHub for updates (you can switch that off). Access from outside your
home network is planned ([roadmap](ROADMAP.md), M6) as an option you
switch on yourself; it goes through a relay that passes on the encrypted
connection and stores no values (SD-062).

## Software

### Which languages and units?

English and German. Units are metric for now; gallons, °F and EC as
ppm (500 or 700 scale) are planned.

### Why C++?

The same core runs in the simulator on your computer and on the
ESP32-S3, with only the hardware layer replaced. ESP-IDF, Espressif's
framework, is mature in C/C++. Why not Rust or MicroPython:
[`CONCEPT.md`](CONCEPT.md) §3.

### Why the AGPL?

So that every version of it stays open, also one that runs as a service
over a network. You can always install your own firmware on your device:
there is no Secure Boot against you ([README](../README.md#license),
[`firmware/FLASHING.md`](../firmware/FLASHING.md)).

## Taking part

### How can I help?

Try the demo and tell us what you think: that helps most right now. Then
report devices, report bugs, improve texts, or send code.
[`CONTRIBUTING.md`](../CONTRIBUTING.md) says how.

### Where do I ask?

Questions and ideas in
[Discussions](https://github.com/Wokesay/growcontroller-software/discussions),
bugs as an [issue](https://github.com/Wokesay/growcontroller-software/issues/new/choose),
security problems privately ([`SECURITY.md`](../SECURITY.md)). More in
[`SUPPORT.md`](../SUPPORT.md).
