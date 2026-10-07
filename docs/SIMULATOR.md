# Simulator (digital twin)

As of 2026-10-06. `sim/` replaces only the bus, devices, tank and clock.
The core is the same code as on the hub.

## Starting

```bash
tools/dev.sh                  # demo: set up, 48 h history, http://127.0.0.1:8080, password "demo-passwort"
SCENARIO=neu tools/dev.sh     # empty hub (stage 0) with first-time setup
./build/gc_sim_server --help  # all options (port, time-lapse, data folder …)
```

For UI work, also run `cd web && npm run dev`: hot reload on port 5173;
the API is passed on to 8080.

The demo password exists only in the simulator. A device never has a
default password (EN 18031-1).

## Scenarios

| Name | Hardware | State |
|---|---|---|
| `neu` | dosing block on port 1 with 3 caps (A, B, CalMag) | not set up, first-time setup |
| `stufe1` | plus a pH/EC head on port 3, a pH− cap, 40 L in the tank | not set up |
| `demo` | plus a level head on port 5, a climate head on port 6, circulation pump and inlet valve on the hub outputs, a Wi-Fi power strip with light, exhaust fan, circulating fan and humidifier | set up through the real API; light, exhaust fan and circulating fan on; mix run, cultivation run, lead-in (default 48 h) with refill, EC and pH control and one jump lock |

## Model and numbers

| Quantity | Value in the twin | Origin |
|---|---|---|
| EC effect part A, part B | 0.275 mS/cm per ml/L each | RAT-055 (0.275 per ml/L of the pair, RO water, 20 L) |
| EC effect CalMag | 0.217 mS/cm per ml/L | RAT-080 |
| pH− | −4.0 pH per ml/L × buffer factor 1.6/(0.6+EC); +1.04 mS/cm per ml/L | RAT-050 measured −5.0 (once, at EC 2.96), RAT-053; the buffer factor is an **assumption** |
| pH drop from A/B | −0.12/−0.10 pH per ml/L | **Assumption** (based on an observation on the reference installation) |
| Mixing | t63 26 s × V/20 L with circulation pump, 240 s without | RAT-052; without pump an **assumption** |
| pH dead time | 60 s | RAT-052 measured 80–99 s; shortened |
| Noise | pH σ 0.006, EC σ 0.004, temperature σ 0.02, level 2 mV | RAT-082 (EC set larger here) |
| Measurement interval | 5 s per head | as on the reference installation |
| Flow rate of the caps | 42–53 ml/min, random per cap | RAT-054: 38–53 ml/min |
| Inlet | 2 L/min | RAT-038: 1.44–1.54 L/min |
| Drift | pH rises, EC falls slightly, water evaporates | **Assumption**, not measured |
| Probe error before calibration | pH +0.18 offset, slope 0.97; EC × 1.08 | **Assumption** |
| Time limit of the dosing block | 90 s; repeating the same job ID does not run twice | proposal by `firmware` |
| Level curve | non-linear at the bottom | as in RAT-078 |

## Fault buttons (simulator panel and `POST /api/v1/sim/…`)

| Action | Effect | Checks |
|---|---|---|
| pH jump | probe +2.1 pH | jump lock, control line, event |
| EC 0 | probe dry | EC gate, latch "no effect" |
| Value freezes | raw values stand still | standstill detection |
| Head/level offline | device does not respond | data loss, inlet emergency cut-off |
| Block/pull off a cap | run fails after 0.3 s, or the cap is missing | pair fault, "catch up"; amount estimated from the run time |
| Dosing block offline | block does not respond, hub sees the last state | deadline per run: pumps off, counted as run |
| Cap on hub port 2 | identification does not match, port is not enabled | mis-plug message (PD-012) |
| Power cut | hub restarts, outputs de-energised; optionally lasting `outageMin` (the world runs on, everything off) and without a secured time afterwards (`timeSecured: false`); `mainsLost: false` restarts only the hub, the sockets keep power. The action `time` secures the clock later (`secured: true`), takes the secured time away again (`secured: false`) or steps the network time (`stepS`) | R6: everything off except the fan sockets (PD-050), during an emergency stop the fans too (PD-076); sequence reported, not resumed; without a secured time the clock continues from the saved time (PD-069); a clock step keeps the remaining time of locks and pauses. A `world.json` from before the power-on setting reads outlets as off or "as before"; "as before" starts off in the simulator |
| Fresh water | set volume, EC and pH | mixing, EC gate, dry run |
| Time-lapse 1–300× | – | settle times, history |
| Scenario | everything reset; only when logged in, without login only with `--allow-reset` (Playwright) | first-time setup |

## Limits

- **Chemistry:** the model is rough. It is meant to make sequences and
  texts testable, not to predict recipes.
- **Measurements missing:** the initial effects in nutrient solution
  (RAT-081) and the mixing time by volume (M-3) are open.
- **Bus not modelled:** the Modbus bus itself, i.e. timing, timeouts and
  CRC, is not modelled. That belongs in the firmware's driver tests.

## Switchable sockets (Shelly)

| Quantity | Value in the simulator | Source |
|---|---|---|
| Devices | Plug S Gen3 (1 outlet), Power Strip 4 Gen4 (4 outlets), IP 192.168.1.60 onwards | **Assumption** |
| Factory state after a power cut | "as before", until the hub sets "off" (or "on" for exhaust and circulation fan, PD-050) | **Assumption** (factory setting not checked) |
| Load per outlet | circulation pump 18 W, light 240 W, exhaust fan 35 W, circulating fan 15 W, humidifier 30 W | **Assumption** |
| Auto-off | works in the device, even without the hub | RAT-019, RAT-060 |
| Outlet without Wi-Fi | load keeps running; counts for room climate and circulation | **Assumption** (power flows independently of Wi-Fi) |
| Faults | Wi-Fi gone (`offline`), setting rejected (`readonly`), setting ignored (`ignore`), switch command rejected, outlet stays in its state (`stuck`; auto-off in the device and a power cut still switch it off), the hub's tick fails with an internal error while set (`crash`) | test cases |

## Room climate

| Quantity | Value in the simulator | Source |
|---|---|---|
| Start values | 21 °C, 55 % RH, 450 ppm | **Assumption** |
| Outside air | 19 °C, 50 % RH | **Assumption** |
| Heat from light / heater / dehumidifier | +6 / +4 / +1 K above outside air | **Assumption** |
| Humidity from evaporation | +14 % RH with light, +5 % without | **Assumption** |
| Exhaust fan | gains × 0.45, faster equalisation (τ 600 s instead of 1800 s) | **Assumption** |
| Humidity equalisation | τ 400 s with exhaust fan, 1500 s without | **Assumption** |
| Humidifier / dehumidifier | +0.8 / −0.6 % RH per minute | **Assumption** |
| Humidity limits | 15–97 % RH | **Assumption** |
| CO2 | 420 ppm with exhaust fan, 380 ppm with light, otherwise 600 ppm; τ 900 s | **Assumption** |
| Noise | air σ 0.05 K, humidity σ 0.3 %, CO2 σ 8 ppm | **Assumption** |
