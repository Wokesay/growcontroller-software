# Domain rules (invariants): implementation and test evidence

As of 2026-10-07. These are the domain rules documented in
[`RATIONALE.md`](RATIONALE.md), grouped by module (M1–M15), with their
implementation and test evidence. "Rationale" cites the RAT ID;
"proposal by <agent>" marks a rule that came from a review; R1–R8 are the
code rules ([`CONCEPT.md`](CONCEPT.md) §4) and PD-xxx are product
decisions. "Test" names the file under `tests/core/` or `web/e2e/`.
Status: **✓** implemented and tested · **◐** implemented, without a
dedicated test or with a deviation · **○** open.

## M1 Mixing and recipe

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| pH correction always last, never in the recipe | RAT-004 | ✓ | `validateConfig`, `planMix` · test_mix "pH im Rezept", test_catalog_config |
| Scale pairs together at every limit, never cap per pump | RAT-054 | ✓ | `planEcDose` (common factor) · test_mix M5-1; mixing doses the whole recipe |
| Compute amounts on the fresh water ("Top up") | RAT-011 | ◐ | `planMix` with `mode=topup` · no dedicated test |
| Split a large dose into equal partial runs instead of shortening it | RAT-055 | ✓ | `splitRuns` · test_mix "Teilläufe". Deviation: mixing has no upper limit on the number of runs, control at most 6 |
| Missing quantity → abort, no substitute value | RAT-006, RAT-015 | ✓ | test_mix M1-4 |
| No double start | RAT-003, RAT-018 | ✓ | confirmation if the last mix was < 30 min ago, one job at a time · test_scenarios |
| Order is recipe data (Athena: Balance → B → A → CaMg → Cleanse) | RAT-080 | ◐ | recipe steps can be reordered |

## M2 Dosing execution

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| After a restart everything is off, nothing resumes; fan sockets come back on and keep their state (PD-050), except during an emergency stop (PD-076) | RAT-007 | ◐ | `Hub::boot`, `Actuators::stopAll` · test_scenarios "Stromausfall", test_net "fans come back on after a power loss", "Restart without a power loss". Light and irrigation (PD-020) open; interrupted controller rounds and refills are not reported yet (PD-070, issue #20) |
| A socket bound to the exhaust or circulation fan is "on after power loss", every other socket "off"; a role change, removing the device or an import resets it, a failure is reported | RAT-019, PD-050 | ✓ | `safetyForRole`, `Hub::releaseSocket`, catalog allowlist · test_net "a socket that loses its fan role", "removing the device or importing a configuration releases fan sockets", "a fan socket that keeps", "Catalog: only the fans may come back on" |
| Without a secured time the clock continues from the saved time, the outage counts as 0; intervals within one start use operating time, across a restart wall time only if both moments were secured, otherwise operating time | PD-069, PD-073 | ◐ | `HubClock`, `elapsedS` · test_clock. Used by the dosing intervals that follow (PD-063, PD-070) |
| A clock step (time secured later, set before the secured flag, network time corrected, secured time lost) keeps the remaining time of jump locks and maintenance; a jump lock never holds longer than 15 min after a step | RAT-044, PD-069 | ✓ | `Hub::watchClock`, `Hub::shiftDeadlines` · test_clock "Clock jump: jump locks and maintenance keep their remaining time (RAT-044)", "a jump lock loaded before the time was known", "a platform time set before the secured flag follows", "a secured time lost while running continues without a jump", "a network time step while secured keeps deadlines and is reported" |
| A clock step keeps the remaining time of controller pauses (EC/pH cooldown, rest after an EC dose, refill) | RAT-058, PD-069 | ◐ | `EcController::shiftClock` and siblings, called by `Hub::shiftDeadlines`; no test yet |
| The emergency stop survives a power loss or restart; fan sockets stay off until resume, also when assigned during the stop; an unreadable run-time state starts stopped | RAT-036, PD-076 | ✓ | `RuntimeState::stopped`, `Hub::setFanSockets`, `Hub::bindRole` · test_net "Not-Halt: survives a power loss, fans stay off until resume (PD-076)", "a fan socket assigned during the stop"; test_clock "an unreadable run-time state keeps everything stopped" |
| An internal error switches everything off except the fans | RAT-036, PD-077 | ✓ | `Hub::tick` · test_net "Internal error: everything goes off except the fans (PD-077)" |
| No dosing below 1.0 s, and this is visible | RAT-050 | ✓ | `splitRuns`, gateway · test_mix |
| Manual dose capped in ml | RAT-039 | ✓ | `Limits::handDoseMaxMl` (5 ml, fixed maximum 50 ml) · test_catalog_config "Grenzen" |
| Job ID against double dosing on retry | proposal by `firmware` | ✓ | dosing block in the simulator; every attempt and every start has its own ID · test_scenarios "nachholen", "Szenario: Bus-Job-IDs der Regler sind nach einem Neustart neu" |
| Deadline per run: without feedback, off and counted as run | proposal by `reviewer` | ✓ | `Doser::tick` · test_scenarios "Szenario: Dosierblock antwortet im Lauf nicht", "Kappe im Lauf abgezogen" |
| Fixed limits in code, configuration only tightens them | R7 | ✓ | `Limits::bounded`, `validateConfig` · test_catalog_config, test_api "Import" |
| Actual run time from the dosing block, not the requested one | RAT-070 | ◐ | `RunStatus::actualMs`, booking per run |
| Hardware time limit above the longest run (SW 60 s / HW 90 s) | RAT-007, RAT-018 | ◐ | gateway 60 s; dosing-block twin 90 s. In hardware: open |
| One channel at a time | PD-010, proposal by `firmware` | ◐ | gateway and twin refuse |

## M3 Calibration

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| No dosing without a valid calibration value | RAT-015, PD-015 | ✓ | gateway, `planMix` · test_mix M3-4, test_scenarios |
| Rate 0/NaN/negative rejected, the old value stays | RAT-003 | ◐ | `Hub::calibrationResult` |
| A calibration run is not consumption and is not capped by the manual-dose limit | RAT-070 | ✓ | purpose `calibration` · test_scenarios "Szenario Stufe 0: Einmessen und geführtes Mischen treffen die Mengen, A:B bleibt gekoppelt" (no dose events) |
| The value is stored in the cap's ID chip | PD-010 | ◐ | `IBus::writePumpCalibration` |

## M4 pH control

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| Dose = 0.8 × gap / effect × V | RAT-055 | ✓ | `planPhDose` · test_mix M4-1 |
| Cap min(0.3 pH, 0.3 ml/L, maximum amount) | RAT-050 | ✓ | test_mix M4-1 |
| Initial effect 5.0 pH per ml/L (smallest dose) | RAT-050 | ✓ | `kPhStartEffect` |
| Clamp the effect to [0.25×; 4×] | RAT-055 | ✓ | `clampEffect` · test_mix M4-5 |
| Learn the effect only from clean doses (no inlet in between) | RAT-057 | ◐ | `clean_` |
| Plausibility band 3–9 | RAT-020 | ✓ | catalog · test_truth |
| < 0.03 movement after 2 doses → abort | RAT-020, RAT-041 | ◐ | latch `ph.no_effect` · no dedicated test |
| No pH+ after pH− (ping-pong) | RAT-041 | ✓ | the prototype does not control pH+ at all |
| Value older than 10 min → no correction | RAT-043 | ◐ | `kMaxPhAge` |
| Every block is visible with its reason | RAT-039, RAT-084 | ✓ | control line with checklist · E2E "Sprungsperre" |

## M5 EC top-up dosing, M6 EC gate

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| 0.8 × gap / effect; cap 1.0 mS/cm per round, in ml/L via max(initial effect, effect) | RAT-056 | ✓ | `planEcDose` · test_mix M5-1, M5-4 |
| Initial effect 0.275 mS/cm per ml/L of recipe | RAT-055 | ✓ | `kEcStartEffect` |
| Reserve for the following pH− dose | RAT-053 | ◐ | `EcController::tick` |
| No movement after 2 rounds → abort | RAT-055 | ◐ | latch `ec.no_effect` |
| pH only at EC ≥ 0.5; invalid EC blocks; missing history does not block | RAT-046 | ✓ | test_scenarios "EC-Gate" |
| Rest time 240 s after an EC dose | RAT-058 | ◐ | `kEcRestS` |
| EC can only be raised | RAT-012 | ◐ | control line "lowering only works with fresh water" |
| No dosing during inlet and calibration | RAT-055 | ◐ | `ControlEnv::refilling`, `calibrating` |

## M7 Circulation and settling

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| No control dosing without circulation | RAT-047, RAT-051 | ◐ | gateway `act.no_mixing` |
| Wait time ≥ 2 × smoothing window; derived from measurement | RAT-052, RAT-058 | ◐ | fixed parameters (pH 5 min, EC 4 min) |
| Mixing time grows with volume / circulation flow | RAT-052 | ○ | open; modelled in the simulator |

## M8 Sensor truth

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| Always runs, also without a cultivation run and in maintenance mode | RAT-023, RAT-075 | ◐ | `SensorTruth::update` every cycle |
| A missing value is never 0 | RAT-006 | ✓ | test_truth M8-1; `arch_check.sh` |
| Freshness and frozen readings are checked separately | RAT-023, RAT-059 | ✓ | test_truth |
| Invalid calibration → no control value | RAT-025, RAT-026 | ✓ | test_truth |
| Jump lock (pH > 1.0 / EC > 0.5 in 5 min), released after 15 min steady, survives a restart | RAT-039, RAT-044 | ✓ | test_truth M8-2, test_scenarios, E2E |
| An announced intervention explains a jump | RAT-042 | ✓ | own doses, mix run, inlet, calibration, maintenance mode · test_truth M8-3 |
| Failure only after n missed readings | RAT-027 | ○ | job of the bus driver (firmware) |
| VPD is air VPD without leaf offset; if an input is missing, no value | RAT-017 | ✓ | `SensorTruth::updateDerived` (FAO-56 eq. 11) · test_climate "Formel", "bei Ausfall eine Lücke". Own rule: both inputs at most 60 s apart |

## M9 Watchdog

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| No path to actuators | RAT-074 | ✓ | `arch_check.sh` R2 |
| Invalid value = problem, never neutral | RAT-015 | ✓ | test_watchdog M9-1 |
| Neutral in maintenance mode, during the start-up grace period, without a target | RAT-073, RAT-005 | ✓ | test_watchdog |
| Assessment older than 3 min → red | RAT-073 | ◐ | `stale` in the API, overview |
| Narrow control band, wide alarm band | RAT-033 | ✓ | test_watchdog |
| Tolerance never 0 | RAT-008 | ✓ | catalog lower limit · test_catalog_config |

## M10 Inlet and level, M11 Circulation

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| Fill volume is computed; the sensor only checks and cuts off in an emergency | RAT-001, RAT-038 | ◐ | `RefillController` |
| The emergency limit follows the valve, for every inlet path | RAT-032 | ✓ | gateway `enforce` |
| No automatic restart after a fault | RAT-031 | ✓ | latch `inlet.fault` · test_scenarios |
| Level curve piecewise linear, strictly increasing | RAT-078 | ✓ | `Curve` · test_truth M10-4/5 |
| Blind zone, rate check, follow-up correction | RAT-030, RAT-038 | ○ | open |
| Dry-run protection acts whoever switched the pump on; latch only if the pump was running | RAT-047, RAT-062 | ✓ | test_scenarios M11-1 |
| Latch survives a restart | RAT-063 | ◐ | `RuntimeState::latches` · test_net "Inlet latch: a reason saved before SD-032" (inlet latch kept and enforced after a restart, also with a reason saved by an older version) |
| No-load detection via power measurement (< 2.5 W) | RAT-049 | ○ | needs current measurement at the output (dry run via current draw: PD-031) |

## M12–M15

| Rule | Rationale | Status | Implementation / test |
|---|---|---|---|
| Heating only external, with a latching emergency cut-off | RAT-060, RAT-061 | ○ | not in the prototype; heating roles stay out of the catalog until then · test_net "Heizung" |
| Network sockets off after a power failure, auto-off in the device, read back before binding | RAT-019, RAT-060 | ✓ | `Hub::acceptDevice`, `Hub::bindRole`, `safetyForRole` · test_net "übernehmen", "Rücklesen"; factor 1.11 for `puls` (pulse profile) is an assumption |
| Check the protection setting in the device before every switch-on; the inventory is not the device | RAT-019 | ✓ | `Actuators::setRole` · test_net "Gateway: Schutzeinstellung im Gerät verloren → kein Einschalten" |
| Humidifier and dehumidifier never together; an unknown state blocks | RAT-034, R5 | ✓ | `Actuators::inhibit` · test_net "nie zugleich", "unbekannter Zustand". Deviation: the 10-min counter-lock is missing (follows with the climate function) |
| Dehumidifier: minimum pause 5 min, also after emergency stop and restart | RAT-034 | ✓ | `kCompressorPause`, `Actuators::stopAll` · test_net "Kompressor-Pause"; the 10-min minimum run follows with the climate function |
| Irrigation pump only above the minimum level; if the level drops below it during a run → off | RAT-068, RAT-062 | ◐ | `Actuators::inhibit`, `Actuators::enforce` · test_net "Gießpumpe". With an assigned but unreadable level sensor, irrigation is blocked and reported (PD-031). Open (PD-031): a limited emergency dose when the plants are too dry, irrigation without a level sensor (the prototype still requires a level) and dry-run detection via current draw. Deviation: RAT-068 describes a predictive check (level minus cycle volume); here the current level is checked |
| Maximum run time per role; the device switches itself off shortly after | RAT-060 | ✓ | `Actuators::enforce` · test_net "Höchstlaufzeit", "Auto-Off im Gerät" |
| Emergency stop also switches network sockets off | RAT-036 | ◐ | `Actuators::stopAll` · test_net "Not-Halt". Deviation (see RAT-036): everything goes off at once, with no 5-min run-on for circulation fan and exhaust and no devices that are left unswitched. "Off not confirmed" with repetition is open |
| A protective cut-off reports honestly: if switching off fails, "Off not confirmed" (alarm) once per reason; repeated while the reason or latch persists; "Off confirmed" as soon as the output reads as off; no all-clear after reassigning | proposal by the reviewer (product repository, PR #18) | ✓ | `Actuators::cut`, `enforce` · test_net "Aus nicht bestätigt" (Gießpumpe, Umwälzpumpe, Zulauf, Höchstlaufzeit), "Gerät schaltet selbst ab", "nach Umzuordnen", "Lösen", "Zwei Schutzgründe", "späterer Trockenlauf", "gleich wieder eingeschaltet". Open: the emergency stop clears the run times and so ends the repetition for the maximum run time (fallback: auto-off in the device). Alarm rather than warning, because the output keeps running despite a protection reason; on reassigning it stays a warning |
| A latch is enforced: if the circulation pump or the inlet runs despite an unacknowledged latch, it is switched off again; reason and time are frozen when the latch is set | RAT-051 (cut-off on the switch-on edge, also when switched by hand), RAT-062 (reason frozen), RAT-031 (no restart) | ◐ | `Actuators::enforce` · test_net "späterer Trockenlauf", "Zulauf: Grund weg, Rastung steht", "Zulauf klemmt: nach Pegelausfall meldet die Notgrenze neu", "neuer Trockenlauf nach Quittierung". Deviation (see RAT-063): the pump's level latch does not release itself at the release level yet; SD-022 decides that a pure level latch releases itself, implementation open |
| Irrigation by the number of shots; drain % leads | RAT-010, RAT-014 | ○ | stage 3 |
| Consumption is only what goes into the tank; book per run, also on abort | RAT-070, RAT-040 | ✓ | `Doser::book`, `Doser::abort` · test_scenarios "Abbruch bucht" |
| A stock of unknown size is not booked | RAT-015 | ◐ | `Doser::book` |
| Minimum stock 150 ml, pH− 20 ml | RAT-071 | ◐ | watchdog |
| Phases provide parameters, never names | RAT-076 | ✓ | `effectiveParams` · test_catalog_config M15-1; `arch_check.sh` R4 |
| Harvest is an event; "completed" is a state of its own | RAT-077, RAT-002 | ◐ | `growHarvest`, `growComplete` |
| Stop is an active, idempotent cascade | RAT-036 | ✓ | `Hub::stop` · test_scenarios; second stage after 30 s open |
