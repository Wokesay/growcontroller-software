# Hardware compatibility

What works with growcontroller, sorted by how it is connected (SD-049).
As of 2026-10-12. The list describes the code on `main`; what the latest
release (`0.1.0-proto.4`) does not have yet is marked "from the next
release". The list grows with every report
([report a device](#report-a-device)).

> **The firmware for the ESP32-S3 is not released yet** (milestone M1,
> the first firmware release, see the [roadmap](ROADMAP.md)). Everything on
> the ESP32-S3 and the carrier board is therefore "Not yet". Today the Home
> Assistant app shows sensors you already have in Home Assistant and
> switches nothing.

## In short

- **Sensors you already have in Home Assistant:** the Home Assistant app
  shows them today and switches nothing. The ESP32-S3 measures with probes
  on its carrier board and, later, with Bluetooth sensors such as the
  Shelly BLU H&T (#91). It does not read sensors from Home Assistant; it
  sends its own values there to show them (SD-052).
- **Probes you already have:** the carrier board's pH and EC inputs take
  the probe itself, not a signal board (see
  [Carrier board](#carrier-board-with-the-esp32-s3-from-m1)).
- **Not listed?** Find your measurement and unit under
  [Measurements and units](#measurements-and-units): the unit Home
  Assistant shows decides.

## Status

| Status | Meaning |
|---|---|
| **Tested** | Someone ran it; the entry names the version, the date and the report. |
| **Expected** | Should work by how the software or the board is built; nobody has reported a test yet. |
| **Not yet** | Something comes first, named in the entry: a milestone (M1 is the first firmware release), a feature, or a rule that is not written yet. |
| **No** | Does not work, or is excluded on purpose; the entry says why. |

Two words from the app appear below:

- **Valid:** the app trusts the value. It needs no calibration
  (temperature, humidity, CO2), or you calibrated it in the app.
- **Calibrated elsewhere:** calibrated in the sensor or in Home Assistant.
  The app shows and records it but has not checked the calibration, so it
  would never be used for control (RAT-025).

The Home Assistant app switches nothing, whatever the value; its top bar
says "Display only". On the ESP32-S3 the firmware will control and switch.

Codes such as SD-041 point to [DECISIONS.md](DECISIONS.md), RAT- codes to
[RATIONALE.md](RATIONALE.md); PD- codes are product decisions, published
here as issues (for the ESP32-S3 and the carrier board: #91 and #94). You
do not need them to use this list.

## ESP32 boards

The firmware runs on an ESP32-S3 on an Espressif DevKitC-1 board, which is
plugged into the carrier board (#91).

| Board | Status | Notes |
|---|---|---|
| ESP32-S3-DevKitC-1-N32R16V (module ESP32-S3-WROOM-2, 32 MB flash, 16 MB PSRAM) | Not yet (M1) | First choice: the firmware is built for it. |
| ESP32-S3-DevKitC-1-N8R8 (8 MB flash, 8 MB PSRAM) | Not yet (M1) | Also supported, with less room: a shorter history and a smaller web app (#91). |
| Other ESP32-S3 boards, for example a DevKitC-1 with N16R8 or a board of another maker | Not yet: no board profile | The firmware is built for the two boards above. Open an issue if you need another one. |
| Other ESP32 chips (ESP32, ESP32-S2, ESP32-C3, ESP32-C6, ESP32-H2, ESP32-P4) | No | The firmware is built for the ESP32-S3 only. |

Flashing: a step-by-step guide follows with the first firmware release
(SD-049). For developers:
[build and flash your own firmware](../firmware/FLASHING.md).

## Home Assistant app (today: shows values, switches nothing)

### Which Home Assistant installation

The app needs Home Assistant 2024.4 or later (an assumption, see
[What the sensors in Home Assistant need](HOME_ASSISTANT.md#what-the-sensors-in-home-assistant-need)).

| Installation | Status | Notes |
|---|---|---|
| Home Assistant with the app store (formerly the add-on store) on a Raspberry Pi, 64-bit | **Tested:** 0.1.0-proto.2, 2026-10-11, the project owner's test; its fixes are in 0.1.0-proto.3 ([changelog](../CHANGELOG.md)) | [Install it as an app](HOME_ASSISTANT.md#as-a-home-assistant-app). |
| Home Assistant OS on a PC (amd64) | Expected | The same app, built for amd64 too (SD-034). |
| Home Assistant Supervised | Expected | Installs like Home Assistant OS, but Home Assistant no longer supports this way of installing since 2025.12 [3]. |
| Home Assistant Container | No | No app store, so the app does not install. If you build the software yourself, its server can run on a computer next to Home Assistant ([run it on a computer of your own](HOME_ASSISTANT.md#run-it-on-a-computer-of-your-own)). |
| A 32-bit system (armv7, i386) | No | The app is built for amd64 and aarch64 only (SD-034); Home Assistant itself ended 32-bit support with 2025.12 [3]. |

### Measurements and units

The app reads your sensors from Home Assistant. It takes a value only when
the unit fits ([units](HOME_ASSISTANT.md#units)).

| Measurement | Home Assistant delivers | In the app |
|---|---|---|
| Air and water temperature | °C or °F | Valid |
| Humidity | % | Valid |
| CO2 | ppm | Valid |
| pH | pH, or no unit; calibrated in the sensor or in Home Assistant | Calibrated elsewhere |
| pH | the voltage of a pH probe from an input circuit without an amplifier, which passes the glass electrode's voltage on unchanged, so the voltage rises as the pH falls (mV or V, at most 5 V) | From the next release: you calibrate it in the app with pH 7 and pH 4 buffer solutions; then it is valid (SD-041). |
| pH | the voltage of a board with an amplifier (with a healthy probe, more than 74 mV per pH step) | Not yet: the app refuses to calibrate it; boards with an amplifier need a rule first (SD-041). Do not pick a board's voltage: behind an amplifier a worn probe can pass the check. |
| EC | mS/cm or µS/cm, calibrated in the sensor or in Home Assistant | Calibrated elsewhere |
| EC | a conductivity compensated to 25 °C, to calibrate in the app | Not yet: planned (SD-041) |
| EC | a voltage | Not yet: needs a rule first (SD-041) |
| EC | TDS in ppm | No: the factor from ppm to EC differs by meter, and a guessed one would give a wrong value (RAT-015) |
| Level | litres | Calibrated elsewhere |
| Level | % | Not yet: needs the tank's shape |
| Level | the voltage of an immersion (pressure) probe | Not yet: planned (SD-041) |
| Level | an ultrasonic distance | No: the app does not calibrate ultrasonic level sensors (SD-041). A level in litres computed in Home Assistant is shown like any other. |

A sensor that reports only when its value changes shows as "Outdated" once
its value stays the same for a while (5 to 30 min, learned per sensor).
From the next release, an ESPHome device can send a sign of life: a small
extra sensor that tells the app when each sensor last measured
([sign of life](HOME_ASSISTANT.md#sign-of-life-sd-042-sd-047)).

### Products

> **pH and EC probes in the same tank disturb each other** unless each is
> galvanically isolated, and a disturbed pH still looks plausible
> (RAT-039, RAT-044). Check that the pH does not move when you switch off
> the circulation pump or the EC sensor
> ([point 5](HOME_ASSISTANT.md#before-any-of-this-may-feed-control)). This
> matters also while the app only shows values: you dose by hand on what
> it shows.

A test report goes into the entry, for example: *Tested* (0.1.0-proto.5,
2026-11-02, #123).

**DFRobot Gravity analog pH kit V2 (SEN0161-V2)**, through ESPHome (analog
input)

- pH computed on the ESPHome device: Calibrated elsewhere. *Expected.*
- The board's voltage, calibrated in the app: *Not yet.* The board
  amplifies the probe: 1500 mV at pH 7 and about 177 mV per pH step with
  the maker's default values [1]. The app takes at most 74 mV per pH step,
  so it refuses to calibrate it. Boards with an amplifier need a rule
  first (SD-041). Do not pick the board's voltage at all: behind the
  amplifier a worn probe can pass the check.

**DFRobot Gravity analog EC sensor V2, K=1 (DFR0300)**, through ESPHome
(analog input)

- EC in mS/cm or µS/cm computed on the ESPHome device: Calibrated
  elsewhere. *Expected.* The maker's Arduino library compensates the
  temperature [2], but it does not run on ESPHome: your configuration
  decides whether EC is compensated (uncompensated EC is off by roughly 2 %
  per °C).
- The board's voltage: *Not yet.* EC from a voltage needs a rule first
  (SD-041). The app offers any voltage as a possible pH signal; do not pick
  this one for pH.

**Atlas Scientific EZO-pH and EZO-EC**, through ESPHome (`ezo`)

- pH and EC: Calibrated elsewhere, because you calibrate the circuit
  itself. *Expected,* once you set the unit in ESPHome
  (`unit_of_measurement`: `pH`, `µS/cm`): ESPHome's `ezo` sensor has none,
  and without a unit the app takes no EC value.
- EZO-EC: send it the water temperature. Otherwise it assumes 25 °C, and
  EC is off by roughly 2 % per °C (a typical value,
  [HOME_ASSISTANT.md](HOME_ASSISTANT.md#before-any-of-this-may-feed-control)).

**DS18B20 water temperature (1-Wire)**, through ESPHome (`dallas_temp`)

- Valid. *Expected.*
- Add a sign of life (from the next release). Without it, a steady
  temperature shows as "Outdated" after 5 to 30 min.

**Shelly BLU H&T**, through Home Assistant's BTHome integration (Bluetooth)

- Air temperature and humidity: Valid. *Expected.*

## Carrier board with the ESP32-S3 (from M1)

The ESP32-S3 way needs the carrier board; its design is not part of this
repository. What it offers here comes from the public issues #91 and #94.
The firmware's drivers follow with M1 and are tested on board samples;
values marked "calculated" or "assumed" are checked on the first samples.

| Connection | What fits | Status | Notes |
|---|---|---|---|
| 8 sensor ports | growcontroller sensor heads, found automatically; wired sensors of other makers that use Modbus over RS485 (9600 baud), once you pick their type in the app | Not yet (M1) | Each port is its own bus with 12 V. Which sensors of other makers the app will know is open; report yours. The app uses a value only after a plausibility check, and pH and EC only with a calibration made in growcontroller: a value calibrated in the sensor shows as "Calibrated elsewhere" and is never used for control (RAT-025). |
| pH and EC inputs | a pH electrode; an EC probe with K=1 (calculated for about 20 µS/cm to 20 mS/cm); a 10 kΩ NTC for the water temperature (B 3950, assumed) | Not yet (M1) | Each input is isolated on its own, so the probes do not disturb each other. They take the probe itself: a signal board with its own amplifier (DFRobot Gravity) or an Atlas EZO circuit does not belong there. The pin assignment in #94 has no 1-Wire input for a DS18B20. At setup you choose whether the ESP32-S3 compensates EC for the water temperature (needs a water temperature sensor, for example the NTC) or not (about 2 % off per °C away from the calibration temperature; calibrate at the usual tank temperature). With compensation, EC and pH are not corrected while the water temperature sensor fails (SD-054, SD-056). A pH probe whose zero point is more than 30 mV off is refused at calibration: try a fresh pH 7 buffer and clean the probe, otherwise replace it (SD-056). |
| 3 contact inputs | switch contacts, for example a float switch, and leak sensors | Not yet (M1) | |
| 10 dosing pump outputs, 12 V | 12 V DC dosing pumps | Not yet (M1) | One pump runs at a time. All 10 outputs together switch off above about 1.8 A (calculated). There is no time limit in the hardware; the firmware limits run time, volume and pause per pump (SD-045). For pH− in a tank under about 30 L, choose a pump of about 20–30 ml/min or less: a first pH− dose is capped at about 0.06 ml per litre of tank (0.6 ml in 10 L), and a pump runs at least 1 s (RAT-050; calculated, pH− products differ in strength). |
| Refill valve output, 12 V | a 12 V solenoid valve, with an overflow float switch in series | Not yet (M1) | The float switch is required: it is the overflow guard, as a small tank overflows long before any time limit ends (RAT-032, RAT-079). Test it before the first fill. The output switches off above about 1.8 A (calculated). |
| Light dimming output, isolated | the 0–10 V or 1–10 V dimming input of a light | Not yet (M1) | |

## Wireless sensors (Bluetooth, on the ESP32-S3)

| What | Status | Notes |
|---|---|---|
| Bluetooth sensors in the BTHome format (an open format, used by Shelly BLU and others) that send at a fixed interval of 60 s or less: air temperature and humidity | Not yet: no milestone yet | Can control humidifier, dehumidifier, exhaust fan and air heater. Encrypted where the sensor can. Outdated after 120 s without a report (SD-056). If its value is outdated or invalid, fans are switched on, a dehumidifier with its own hygrostat keeps power, humidifier and other dehumidifiers switch off, and an air heater switches off and latches (SD-053). An air heater only with an emergency cut-off that latches (RAT-060). Keep the device's own setting (for example its humidistat) as the outer limit (PD-153, #91). |
| Other wireless values | Not yet: no milestone yet | Shown, never used for control (PD-153). |
| Level, overflow, leak, drain, pH, EC, water temperature, substrate, anything that drives the CO2 valve | No | Never over radio (PD-153): a lost message must not hide a full tank or a wrong pH. |

## Smart plugs (local network, on the ESP32-S3)

| What | Status | Notes |
|---|---|---|
| Exhaust and circulation fans on a plug | Not yet: no milestone yet | The ESP32-S3 only switches a fan on and off; its speed or its own thermostat stays in the fan. The fan must start again in its last setting when power returns; the ESP32-S3 sets its plug to come back on after a power loss (PD-050, SD-053). |
| A dehumidifier on a plug | Not yet: no milestone yet | Choose one with its own hygrostat and a full-tank cut-off (PD-066): it keeps regulating when a climate sensor fails (SD-053). |
| Shelly smart plugs, local, without the maker's cloud | Not yet: no milestone yet | Shelly comes first (SD-007); which Shelly generations is open (#85). Never connect a dosing pump to a plug (#91). An inlet valve or irrigation pump on a plug gets an automatic switch-off in the plug, just above the software's longest run (RAT-019); one for every run is planned (#39). The circulation pump runs for hours and gets none. You can try a Shelly Plug S Gen3 and a Power Strip 4 Gen4 in the simulator. |
| A heater on a plug | Not yet (#24) | Only with an emergency cut-off that latches (RAT-060). |
| Smart plugs of other makers | Not yet: no milestone yet | The ESP32-S3 sets the protection in the device and reads it back (SD-007), which needs its own code for each make. |

## Report a device

- [Report a device](https://github.com/Wokesay/growcontroller-software/issues/new?template=device.yml)
  with the issue form "Device or sensor": the device, how it is connected,
  whether it works, what the app showed, and the software version. You do
  not need to open a pull request; the issue is enough.
- Never post passwords, access tokens or keys (also not in ESPHome YAML:
  the `api` encryption key, the `ota` password, Wi-Fi details), or the
  address of your Home Assistant from outside: issues are public.
- We add an entry by a pull request with review. A "Tested" entry names
  the version and the date and links the report; a test of our own counts
  too. A later version can change what works.

## Sources

[1] DFRobot_PH library, `DFRobot_PH.cpp`: defaults 1500.0 mV at pH 7 and
2032.44 mV at pH 4. (voltage − 1500 mV) / 3 is the electrode's own
voltage: the board has a gain of 3 around 1500 mV; the pH is a straight
line through the two stored buffer voltages.
https://github.com/DFRobot/DFRobot_PH (retrieved 2026-10-12).

[2] DFRobot_EC library, `DFRobot_EC.cpp`: EC from the voltage, temperature
compensation of 1.85 % per °C in the library.
https://github.com/DFRobot/DFRobot_EC (retrieved 2026-10-12).

[3] Home Assistant blog, 2025-05-22, "Deprecating Core and Supervised
installation methods, and 32-bit systems": support ends with 2025.12.
https://raw.githubusercontent.com/home-assistant/home-assistant.io/current/source/_posts/2025-05-22-deprecating-core-and-supervised-installation-methods-and-32-bit-systems.markdown
(retrieved 2026-10-12 by the `qa` review).

## Trademarks

Product and company names such as DFRobot, Atlas Scientific, Espressif and
Shelly are trademarks of their respective owners. They are used here only
to describe compatibility; this project is not affiliated with, endorsed or
sponsored by them.
