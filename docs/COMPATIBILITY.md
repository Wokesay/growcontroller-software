# Hardware compatibility

What works with growcontroller, by how it is connected (SD-049). As of
2026-10-12; each row names the version it is about. The list grows with
every report (see "Report a device" below).

> **The firmware for the ESP32-S3 is not released yet** (milestone M1,
> [`ROADMAP.md`](ROADMAP.md)). Everything on the ESP32-S3 and the carrier
> board is therefore "Not yet" for now. Today the Home Assistant app reads
> sensors you already have in Home Assistant and switches nothing.

## Status

| Status | Meaning |
|---|---|
| **Tested** | Someone ran it with the version named; the report is linked. |
| **Expected** | Should work by how the software or the board is built; nobody has reported a test yet. |
| **Not yet** | Needs something first, named in the row: a milestone, or a rule from the product repository. |
| **No** | Does not work, or is excluded on purpose; the row says why. |

"Works" always means what the software does with it in that version. The
Home Assistant app shows values and switches nothing; on the ESP32-S3 the
firmware will control and switch.

## ESP32 boards

The firmware runs on an ESP32-S3 on a DevKitC-1 board, which is plugged
into the carrier board (PD-162, SD-043).

| Board | Status | Notes |
|---|---|---|
| Espressif ESP32-S3-DevKitC-1-N32R16V (module ESP32-S3-WROOM-2, 32 MB flash, 16 MB PSRAM) | Not yet (M1) | The main board profile. |
| Espressif ESP32-S3-DevKitC-1-N8R8 (8 MB flash, 8 MB PSRAM) | Not yet (M1) | The second board profile, with a smaller web app and a shorter history. |
| Other ESP32-S3 boards and modules, for example a DevKitC-1 with N16R8 or a board of another maker | Not yet | No board profile. CI builds the firmware skeleton for an ESP32-S3-WROOM-1-N16R8 today, but that build has never run on hardware. Open an issue if you need one. |
| ESP32 chips other than the ESP32-S3 (ESP32, ESP32-S2, ESP32-C3, ESP32-C6, ESP32-H2, ESP32-P4) | No | The firmware is built for the ESP32-S3 only. |

How to flash the ESP32-S3: a step-by-step guide follows with the first
firmware release (M1, SD-049). Building and flashing your own firmware:
[`firmware/FLASHING.md`](../firmware/FLASHING.md).

## Home Assistant app (today: display only)

The app reads the sensors of Home Assistant and takes a value when its unit
fits ([`HOME_ASSISTANT.md`](HOME_ASSISTANT.md), "Units"). Whether a value
counts as a measurement depends on where it was calibrated: a value
calibrated outside the app is shown, marked "calibrated elsewhere", but the
app has not checked it (RAT-025).

### Home Assistant itself

| Installation | Status | Notes |
|---|---|---|
| Home Assistant OS or Supervised, 64-bit (amd64 or aarch64: a PC, a Raspberry Pi 4 or 5) | Expected | Installs from the app store ([`HOME_ASSISTANT.md`](HOME_ASSISTANT.md)). Home Assistant 2024.4 or later. |
| Home Assistant Container or Core | Expected | No app store. Run the server on a computer of your own instead ([`HOME_ASSISTANT.md`](HOME_ASSISTANT.md), "Run it on a computer of your own"). |
| A 32-bit system (armv7, i386) | No | The app image is built for amd64 and aarch64 only (SD-034). |

### Measurements

| Measurement | What Home Assistant delivers | What the app does with it |
|---|---|---|
| Air and water temperature | °C or °F | Measurement |
| Humidity | % | Measurement |
| CO2 | ppm | Measurement |
| pH | a pH value, calibrated in the sensor or in Home Assistant | Display only |
| pH | the voltage of a probe without an amplifier (mV or V, at most 5 V) | Calibrated in the app with pH 7 and pH 4 buffers, then a measurement (SD-041) |
| pH | the voltage of a board with an amplifier | Not yet: refused at calibration; boards with gain need a rule first (SD-041) |
| EC | mS/cm or µS/cm, calibrated in the sensor or in Home Assistant | Display only |
| EC | a conductivity compensated to 25 °C, to calibrate in the app | Not yet (SD-041, step 3) |
| EC | a voltage | Not yet: needs a rule first (SD-041) |
| Level | litres | Display only |
| Level | % | Not yet: needs the tank's shape |
| Level | the voltage of an immersion (pressure) probe | Not yet (SD-041, step 2) |
| Level | an ultrasonic distance | No: the app does not calibrate ultrasonic level sensors (SD-041). A level in litres computed in Home Assistant is shown like any other. |

A sensor that reports only when its value changes looks stale when it is
calm. On an ESPHome device, add a sign of life so the app sees the last
measurement ([`HOME_ASSISTANT.md`](HOME_ASSISTANT.md), "Sign of life").

### Products

| Product | Way into Home Assistant | What the app does with it | Status | Report |
|---|---|---|---|---|
| DFRobot Gravity: Analog pH Sensor/Meter Kit V2 (SEN0161-V2) | ESPHome, ADC | The board amplifies the probe's voltage: 1500 mV at pH 7 and about 177 mV per pH step with the maker's default values [1]. A pH value computed on the node: display only. Its voltage: the app refuses to calibrate it, as it accepts 47–74 mV per pH step. | Not yet: boards with gain need a rule (SD-041) | – |
| DFRobot Gravity: Analog Electrical Conductivity Sensor V2, K=1 (DFR0300) | ESPHome, ADC | The board gives a voltage; the maker's library computes EC and compensates the temperature on the microcontroller [2]. EC in mS/cm or µS/cm computed on the node: display only. Its voltage: no value. | Not yet: EC from a voltage needs a rule (SD-041) | – |
| Atlas Scientific EZO-pH, EZO-EC | ESPHome (`ezo`) | The circuit is calibrated in itself: display only. | Expected | – |
| DS18B20 water temperature (1-Wire) | ESPHome (`dallas_temp`) | Measurement. Add a sign of life; [`HOME_ASSISTANT.md`](HOME_ASSISTANT.md) has an example. | Expected | – |
| Shelly BLU H&T (BTHome) | Home Assistant's BTHome integration | Air temperature and humidity: measurement. | Expected | – |

## Carrier board with the ESP32-S3 (from M1)

What the board offers is public (PD-157, PD-170, issue #94); the board
itself is not part of this repository. The firmware drivers follow with
M1 and are tested on board samples. Values marked "calculated" or
"assumed" are checked on the first samples.

| Connection | What fits | Status | Notes |
|---|---|---|---|
| 8 sensor ports: RS485 (Modbus RTU, 9600 Bd) and 12 V, one bus per port | growcontroller sensor heads, detected automatically; third-party Modbus sensors once you pick their type | Not yet (M1) | Which third-party types the catalog will know is open; report yours. A value counts only after a plausibility check. |
| pH input and EC input, galvanically isolated; water temperature on the EC side | a pH electrode; an EC cell (calculated for cell constant 1: about 20 µS/cm to 20 mS/cm); a 10 kΩ NTC (B 3950, assumed) | Not yet (M1) | The inputs take the electrode directly. Signal boards with their own amplifier, such as the DFRobot Gravity boards above, do not belong there. |
| 3 contact inputs | dry contacts, for example a leak sensor or a float switch | Not yet (M1) | A probe plugged in by mistake reads "implausible". |
| 10 dosing pump outputs, 12 V | 12 V DC dosing pumps | Not yet (M1) | One pump runs at a time. The group switches off above about 1.8 A (calculated). |
| Refill valve output, 12 V | a 12 V solenoid valve, with an overflow float switch in series | Not yet (M1) | Hardware time limit 31–38 min, current limit about 1.8 A (both calculated). |
| Light dimming output, isolated | the 0–10 V or 1–10 V dimming input of a light | Not yet (M1) | When the ESP fails, the output goes to about 50 % (with a jumper: 100 %). |

## Wireless sensors (Bluetooth, on the ESP32-S3)

| What | Status | Notes |
|---|---|---|
| BTHome sensors with a fixed send interval, for example Shelly BLU H&T: air temperature and humidity | Not yet | May steer humidifier, dehumidifier, exhaust fan and air heater. A stale value switches the device off; the device's own controller stays the outer limit (PD-153). |
| Other wireless values | Not yet | Display only (PD-153). |
| Level, overflow, leak, drain, pH, EC, water temperature, substrate, anything that drives the CO2 valve | No | Never over radio (PD-153): a lost message must not hide a full tank or a wrong pH. |

## Smart plugs (local network, on the ESP32-S3)

| What | Status | Notes |
|---|---|---|
| Shelly smart plugs, local, without the maker's cloud | Not yet | Shelly first (SD-007). The simulator has a Shelly plug and power strip. Never for dosing pumps; a pump on a plug gets an auto-off for every run (#39). Which Shelly generations: open (#85). |
| Smart plugs of other makers | Not yet | The hub sets the protection in the device and reads it back (SD-007), which needs a driver per make. |

## Report a device

- Use the issue form **"Device or sensor"**: the device, how it is
  connected, what you tried, what the app showed, and the software
  version. Never post access tokens, passwords or Wi-Fi details: issues
  are public.
- An entry is added by a pull request with review. A "Tested" entry names
  the version and date and links the report; a test of our own counts
  too.
- A row names the version it was tested with: a later version can change
  what works.

## Sources

[1] DFRobot_PH library, `DFRobot_PH.cpp`: defaults 1500.0 mV at pH 7 and
2032.44 mV at pH 4; the pH is computed from (voltage − 1500 mV) / 3.
https://github.com/DFRobot/DFRobot_PH (retrieved 2026-10-12).

[2] DFRobot_EC library, `DFRobot_EC.cpp`: EC from the voltage, temperature
compensation of 1.85 % per °C in the library.
https://github.com/DFRobot/DFRobot_EC (retrieved 2026-10-12).

## Trademarks

Product and company names such as DFRobot, Atlas Scientific, Espressif and
Shelly are trademarks of their respective owners. They are used here only
to describe compatibility; this project is not affiliated with, endorsed or
sponsored by them.
