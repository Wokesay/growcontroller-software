# Rationale register

Each domain rule of this software that was learned from operating
practice has an entry here. The entry says what the rule is, why it
exists, what evidence backs it, where it is implemented and which tests
cover it. Code, tests and other docs cite only the ID (RAT-001 …).

- **IDs are stable.** An ID is never reused. If a rule changes, its entry
  is updated and says what changed; a rule that is dropped stays listed as
  dropped.
- **Reference installation** means the private installation on which the
  rules were first developed and measured. Evidence names the year only.
- **Evidence** says whether a number was measured, observed or
  calculated. "—" means there is no number behind the rule.
- **Planned** marks rules that are described here but not implemented
  yet. **Deviation** marks where this software deliberately differs from
  the reference installation.
- New rules get the next free ID, an entry here and a test
  (`docs/INVARIANTS.md`).

### RAT-001 – Fill volume is computed; the level sensor only checks and cuts off

- **Rule:** A refill opens the inlet for a time computed from the missing volume (target minus current level) and the configured inlet flow rate. The level reading can only end the fill early (at target + 1 L) and acts as the safety cut-off. Without a flow rate the refill is blocked, and the target is capped 1 L below tank capacity.
- **Why:** A fill that trusts only the level sensor inherits every sensor fault, from drift to a frozen reading, and can overflow the tank. The computed amount is an independent bound, and the sensor stays on as plausibility check and emergency stop. Each refill logs both the computed and the measured litres, so drift becomes visible.
- **Evidence:** Observed on the reference installation (2026): the fill target was configurable (default 100 L in a 150 L tank), with a hard emergency cut-off at 140 L regardless of the target. After a move to a 60 L tank (overflow at 55 L), the cut-off became 45 L and the target was capped at 40 L.
- **Implemented in:** core/src/control.cpp (`RefillController`), docs/INVARIANTS.md
- **Tests:** — (no dedicated test; related: tests/core/test_scenarios.cpp "Szenario: Zulauf – Füllstand fällt aus → Notabschaltung, kein automatischer Neuanlauf")


### RAT-002 – Harvest is recorded only by the user

- **Rule:** The hub never declares harvest on its own. Harvest is a timestamped event that the user records. Closing the cultivation run is a separate, explicit "completed" state. Whether a phase advances by itself after its planned days or only after the user confirms is set per phase (PD-025); the harvest always needs the user.
- **Why:** Ripeness is judged by inspecting the plant, not by counting days. An automatic harvest at the end of the planned flowering period would act on a guess and contradict what the user sees.
- **Evidence:** Observed on the reference installation (2026): an automation started the harvest day by itself once the planned late-flowering days were reached. It was replaced by a manual confirmation plus a daily reminder that also counts the days beyond plan.
- **Implemented in:** core/src/hub.cpp (harvest and completion handlers), core/src/api.cpp, web/src/pages/tank.tsx, docs/INVARIANTS.md
- **Tests:** —


### RAT-003 – One job at a time; a repeated mix needs confirmation; bad calibration results are rejected

- **Rule:** Only one user job, such as a mix or a pump calibration, runs at a time, and a second start is refused. Another mix within 30 minutes of the last one needs explicit confirmation, because dosing again doubles the nutrients. A pump calibration result that is missing, zero, negative or above 1000 ml is rejected, and the previous value stays.
- **Why:** A dosing sequence that can start twice overdoses. One that runs on a missing or zero rate either has no exit or underdoses, and either way it looks like success.
- **Evidence:** Observed on the reference installation (2026): the mixing sequence read its inputs from an empty trigger message. Its attempt counters became NaN, so "attempts ≥ 5" never became true and the dosing loops had no exit. A flow rate of "0" slipped past the fallback and would have caused silent underdosing. The fix added an abort on a zero rate, a 60 s cap per pump step and a guard against double starts.
- **Implemented in:** core/src/hub.cpp (`Hub::mixStart`, `Hub::calibrationResult`), core/src/mix.cpp (`planMix`, repeat check), docs/INVARIANTS.md
- **Tests:** tests/core/test_scenarios.cpp "Szenario Stufe 0: Einmessen und geführtes Mischen treffen die Mengen, A:B bleibt gekoppelt" (second start needs confirmation); tests/core/test_mix.cpp "Mischen: Nutzvolumen, Doppelstart, Vorrat". The calibration rejection has no dedicated test.


### RAT-004 – pH correction always comes last and is never part of a recipe

- **Rule:** Recipes may contain nutrients only. A pH adjuster in a recipe is rejected both when the configuration is saved and when a mix is planned. pH is corrected after all nutrients are in, either by hand after mixing or by the automatic pH control.
- **Why:** pH correction needs the finished solution. The base nutrients carry most of the EC and lower the pH themselves. Correcting before them leaves the tank at an unknown, usually too low pH, and there is no pH-up channel to undo an overshoot.
- **Evidence:** Observed on the reference installation (2026): pH-down was dosed before the two-part base nutrient, and nothing was measured afterwards, so the tank went to irrigation at an unknown pH. After the reorder, the acid raises EC by only a few hundredths of mS/cm (about 20 ml in 100 L), which is below the 0.05 EC tolerance. Precipitation was not a concern: the source water was reverse-osmosis water with almost no carbonate hardness, and a buffering additive went in before all nutrients.
- **Implemented in:** core/src/config.cpp (`validateConfig`), core/src/mix.cpp (`planMix`), core/include/gc/mix.hpp
- **Tests:** tests/core/test_catalog_config.cpp "Konfiguration: Prüfung fachlich und sicherheitlich", subcase "pH-Korrektur im Rezept ist verboten"; tests/core/test_mix.cpp "Mischen: pH im Rezept wird abgelehnt" (M1-3); tests/core/test_scenarios.cpp "Szenario Stufe 1: Regelung bringt pH und EC ins Ziel, ohne Rastung"


### RAT-005 – The start-up grace period applies to the watchdog only

- **Rule:** For 120 s after a restart, the watchdog reports target-band checks as neutral ("start-up after restart") instead of as problems. The grace period applies to the watchdog only. Controllers do not wait and act on new targets right away.
- **Why:** Right after a restart, values are still settling, so reporting them as problems would only raise false alarms. Controllers must not have a grace period: after a phase change, they should move to the new targets at once.
- **Evidence:** Observed on the reference installation (2026): a "central gate" that combined the start-up grace and maintenance mode existed only as a display. None of the 11 automations that switched actuators checked either flag, and the watchdog ignored the grace flag that had been set for it.
- **Implemented in:** core/src/watchdog.cpp (`kStartupGraceS`), docs/INVARIANTS.md
- **Tests:** tests/core/test_watchdog.cpp "Watchdog: neutral mit Grund – Pflegemodus, Anlauf, kein Ziel"


### RAT-006 – A missing value is never 0

- **Rule:** A missing number is stored as "no value": NaN or nullopt in code, null in JSON, an empty field in CSV and a gap in charts. It is never stored as 0. A missing input aborts the step without a substitute: a mix without a water volume is not planned, and a probe calibration point needs a fresh raw reading. A CI check rejects `value_or(0)` in the core.
- **Why:** 0 is a valid reading. For most controllers it is also the strongest demand: too cold, too dry, no CO2, tank empty. A failed sensor that reads 0 therefore drives actuators at full load while nothing reports an error. A calibration built on a missing value is worse than none, because the readings afterwards look plausible.
- **Evidence:** Observed on the reference installation (2026): a dead water-temperature sensor read as 0 °C kept a heater on. A dead CO2 sensor (0 ppm) kept CO2 dosing, and a dead humidity sensor (0 % RH) kept the humidifier on. A dead load cell read as 0 g ("empty") booked a whole tank as pumped although nothing flowed. A missing raw value was saved as a 0 V calibration point, and a report showed 0.0 °C instead of "–".
- **Implemented in:** core/include/gc/common.hpp, core/include/gc/truth.hpp, core/include/gc/mix.hpp, core/src/mix.cpp, core/src/hub.cpp (CSV export, probe calibration), tools/arch_check.sh (R5), docs/CONCEPT.md, docs/HISTORY.md
- **Tests:** tests/core/test_truth.cpp "Sensorwahrheit: kein Messwert ist nie 0" (M8-1); tests/core/test_mix.cpp "Mischen: Wassermenge fehlt → nichts geplant" (M1-4); tests/core/test_catalog_config.cpp "Konfiguration: Hin- und Rückweg über JSON, fehlende Zahl bleibt fehlend"; tools/arch_check.sh


### RAT-007 – After a restart everything is off; timed actuators have an independent cut-off

- **Rule:** On boot, the hub switches all outputs off. Interrupted sequences (dose, pulse, refill) are reported as interrupted and never resumed. Each pump run is capped at 60 s in software, and the dosing block has its own 90 s cut-off that works without the hub; this is modelled in the simulator, and the hardware limit is still open. A later project decision (PD-020) will let state functions such as fans, light and irrigation resume. Until that is implemented, this rule applies unchanged.
- **Why:** An "off" command that waits in a timer lives in volatile memory, so a restart or update loses it and the actuator keeps running. Every actuator whose stop depends on a wait therefore needs a second, independent stop path. That path must sit above the longest legitimate run, or it would cut valid runs.
- **Evidence:** Observed on the reference installation (2026): a scan found 21 "on → wait → off" sequences in six automations whose off command would be lost on a restart. A closed enable condition had the same effect: a heater, a dimmer and a valve simply stayed in their last state. Independent cut-offs were added, for example 90 s for all dosing pumps and 30 s for the CO2 valve.
- **Implemented in:** core/src/hub.cpp (`Hub::boot`), core/src/dosing.cpp (`Actuators::stopAll`, `Actuators::startRun`), core/include/gc/config.hpp (`kHardMaxRunS`), sim/world.cpp (90 s limit of the dosing-block twin), docs/CONCEPT.md (R6)
- **Tests:** tests/core/test_scenarios.cpp "Szenario: Stromausfall im Lauf → alles aus, nicht fortgesetzt, gemeldet"


### RAT-008 – A tolerance is never 0

- **Rule:** Every tolerance parameter has a catalog minimum above zero (pH and EC tolerance at least 0.05). A configuration or phase parameter set with a tolerance of 0 is rejected.
- **Why:** With zero tolerance, any deviation from the exact setpoint counts as a violation. The result is a permanent alarm that buries the real ones, which is worse than no alarm. A zero tolerance is always a configuration error, never an intended setting.
- **Evidence:** Observed on the reference installation (2026): the humidity, pH and EC tolerances stood at 0, so the watchdog was permanently red. The values chosen afterwards were 1.0 K for air temperature (climate correction only at twice that, so it does not fight the air conditioner's own hysteresis) and 5 % for RH. CO2 got 100 ppm, because 50 ppm would lie within the sensor's stated accuracy of ±50 ppm ±5 % and make the valve chatter. pH and EC alarms got 0.2 each, while the mixing loop stopped at the tighter 0.1 pH and 0.05 EC.
- **Implemented in:** catalog/catalog.json (parameter minimums), core/src/config.cpp (`validateConfig`)
- **Tests:** tests/core/test_catalog_config.cpp "Konfiguration: Prüfung fachlich und sicherheitlich", subcase "Parameter außerhalb des Bereichs, Toleranz 0 abgelehnt"; "Konfiguration: Phasenparameter im Katalogbereich"


### RAT-009 – Climate targets keep VPD in a corridor

- **Rule:** The default phase values are a day temperature of 26 °C in the vegetative phase and 27 °C in flowering, with 70 % RH (≈ 1.0 kPa VPD) and 62 % RH (≈ 1.35 kPa) respectively. When the climate is led by VPD, the humidity setpoint is computed from the VPD target and the target temperature, not the measured temperature. This is planned for the climate function; automatic climate control does not exist yet.
- **Why:** Plants use more light and CO2 only if temperature and humidity follow. The photosynthesis optimum shifts upward, and at too high a VPD the stomata close and uptake stops. A VPD corridor of about 1.0–1.4 kPa avoids both stress and wasted enrichment.
- **Evidence:** Observed on the reference installation (2026): CO2 targets rose with light intensity: off for cuttings, 800–1000 ppm in the vegetative and stretch phases, 1200 ppm in early and mid flowering, and 800 ppm late. Day temperatures went up to 26–27.5 °C in the enriched phases, and humidity was set to keep VPD at 1.0–1.4 kPa. Late flowering broke the corridor on purpose (50 % RH, ≈ 1.63 kPa), because mould risk in dense buds outweighed a few percent of yield.
- **Deviation:** The reference also coupled CO2 enrichment to light intensity. The software only measures CO2 and has no CO2 dosing, so it takes over only the temperature and humidity targets.
- **Implemented in:** docs/PLANT_AUTOMATION.md (§4 climate function, §5 phase defaults)
- **Tests:** —


### RAT-010 – Irrigation is controlled by the number of shots, not the shot size

- **Rule:** This is planned for stage 3 and not implemented yet. Each shot stays small, and the daily amount is set by shots per day as a phase parameter: 6 in the vegetative phase and 8–9 in flowering on rockwool. Further parameters are a factor for the first shot and a duration per shot. Drain percentage remains the leading feedback.
- **Why:** Rockwool cannot absorb a large shot at once. The water channels through and drains out while the block stays unevenly wet, so the drain looks right while the root zone is not. A few large shots also create long dry spells in which the remaining solution concentrates. The number of shots shapes the moisture profile and the duration sets the amount, so a drain deviation is corrected through the duration.
- **Evidence:** Observed on the reference installation (2026): the original shots of 120–200 ml on a 650 ml block (18–31 % of the substrate volume) channelled through. Shots were then fixed at about 130 ml (≈ 20 %, 120 s), with 6–9 shots per day in flowering. Fresh cuttings deliberately got the opposite: 4 short shots of 36 s, so the block dries between them and roots extend downward, because constant wetness is a root-rot risk.
- **Implemented in:** docs/PLANT_AUTOMATION.md (§4 `irrigation`, §5), docs/INVARIANTS.md (open, stage 3)
- **Tests:** —


### RAT-011 – Top-up mixing doses on the fresh water only

- **Rule:** In "top up" mode, the recipe amounts in ml/L are multiplied by the fresh water added, not by the total tank volume. The tank volume is used only to check capacity, and without a water amount nothing is planned.
- **Why:** The residual solution already contains its share. Dosing on the full volume overdoses every day. A step driven by EC can also wrongly conclude that its target is met, because the residual raises the starting EC.
- **Evidence:** Observed on the reference installation (2026): with 20–25 L of residual solution kept in the tank, dosing on the total volume overdosed the fixed-rate additives by 25–50 % every day. A calcium/magnesium step controlled to a base EC of 0.4 mS/cm saw a starting EC of about 1.3 (20 L at 3.0 plus 27 L of fresh water). It reported its target as reached and skipped itself, so the fresh water would never have received Ca/Mg.
- **Deviation:** When the refill volume was unknown, the reference dosed on the total volume, preferring slightly too much to no disinfection. The software refuses to plan without a water amount, in line with RAT-006. In the reference, base nutrients and pH were regulated against the measured value of the whole tank. In the software, every recipe nutrient is a per-litre amount on the fresh water: recipe mixing is the probe-free base stage, and EC and pH control are a separate stage.
- **Implemented in:** core/src/mix.cpp (`planMix`, mode `topup`), web/src/pages/mix.tsx
- **Tests:** — (no dedicated test)


### RAT-012 – EC can only be raised

- **Rule:** The hub cannot dilute. If EC is above target, EC control rests and states that lowering it needs fresh water. A planned addition: when a phase change lowers the EC target by more than 0.2 mS/cm, the app advises draining the tank completely. Partial draining is not planned.
- **Why:** A loop that can only add nutrients cannot reach a lower target. Without the hint, the residual solution keeps the tank too strong in exactly the phase where a lower EC is wanted, while mixing reports the target as reached.
- **Evidence:** Observed on the reference installation (2026): at a phase change from an EC target of 3.0 to 2.0 mS/cm, the residual solution was already above the new target. The mix still reported "target EC reached", and the tank went to irrigation too strong. A later lesson: a drain must be followed directly by a refill, or the day's irrigations run without water.
- **Implemented in:** core/src/control.cpp (EC control, "above target" state), docs/PLANT_AUTOMATION.md (§5)
- **Tests:** —


### RAT-013 – Dimming never goes below the lamp's switch-on threshold

- **Rule:** This is a fixed rule that comes with the planned light function. A dimming value below the configured switch-on threshold is raised to the threshold. 0 means off, and the relay switches off as well. The threshold is a parameter of the light schedule.
- **Why:** LED drivers ignite only above a certain dimming level and go out a little lower. Values in between give no light, while relay and dimmer report "on" and "setpoint reached", so the room stays dark without any alarm. The threshold depends on the driver and the dimming path and must be measured again whenever either changes.
- **Evidence:** Measured on the reference installation: the lamps seemed to ignite at about 10 %, but the dimming line was miswired and delivered only about 7 V instead of 10 V. After the fix, they ignited at 9 % and went out at 6 %, with hysteresis in between.
- **Implemented in:** docs/PLANT_AUTOMATION.md (§3 fixed rules, §4 `light_schedule`)
- **Tests:** —


### RAT-014 – Drain percentage leads irrigation; substrate readings inform only

- **Rule:** This is planned for stage 3. Drain percentage, the run-off relative to the amount given, is the leading metric for irrigation. Substrate readings do not replace it.
- **Why:** Drain percentage is a direct and established measure of irrigation intensity. Substrate EC from a single probe is a point measurement. Using it for control first needs a data basis, and until then it should not trigger actions or alarms.
- **Evidence:** Observed on the reference installation (2026): the drain had no EC measurement, and the weighed drain tank remained the control input. A substrate probe (moisture, temperature, bulk and pore EC) went into one block. It sat at the brightest spot on purpose, with the fastest dry-back and the strongest concentration, so that any later control based on it would rather over-supply than under-supply the other plants. Its readings were only logged and displayed, with no thresholds or alarms.
- **Implemented in:** docs/INVARIANTS.md (open, stage 3)
- **Tests:** —


### RAT-015 – No substitute values for quantities; invalid data is a problem

- **Rule:** No pump doses without a measured calibration (flow rate), and both planning and the actuator gateway refuse. A missing quantity aborts the step instead of falling back to a default. For the watchdog, an invalid reading is a problem labelled "data loss", never neutral and never skipped, and it is reported separately from out-of-tolerance values. A canister stock of unknown size is not booked rather than rebuilt from 0.
- **Why:** A default for a value from which a run time or an amount follows turns a fault into silent dosing. A watchdog that stays quiet when data is missing is no watchdog. NaN passes every comparison unnoticed (NaN < 1 and NaN ≤ 0 are both false), so guards need explicit finiteness checks. A counter restarted from 0 can later not be told apart from a real one.
- **Evidence:** Observed on the reference installation (2026): a fallback pattern in 16 automation steps never applied when a sensor reported "unavailable", and many of those fallbacks were 0 anyway. Fixing only the bracket would have turned a visible NaN into a silent 0. In one place, a check for "volume ≤ 0" let a NaN volume through.
- **Deviation:** The reference still allowed a plausible operating default where one existed, for example a pump flow rate of 100 ml/min. The software is stricter and doses nothing without a measured calibration (product decision PD-015).
- **Implemented in:** core/src/mix.cpp (`planMix`), core/src/dosing.cpp (`Actuators::startRun`, `Doser::book`), core/src/watchdog.cpp, web/src/pages/setup.tsx (calibration is mandatory during setup), docs/DECISIONS.md (E7)
- **Tests:** tests/core/test_mix.cpp "Mischen: ohne Einmesswert kein Auftrag" (M3-4), "Mischen: Wassermenge fehlt → nichts geplant" (M1-4); tests/core/test_scenarios.cpp "Szenario Stufe 0: ohne Einmesswert wird nichts dosiert"; tests/core/test_watchdog.cpp "Watchdog: ungültiger Wert ist ein Problem, nie neutral" (M9-1)


### RAT-016 – Exported data leaves gaps empty

- **Rule:** CSV exports of the history use a semicolon separator and a decimal comma, and missing values stay as empty fields, never 0. A planned addition is an end-of-run summary (water, nutrient ml and cost, phase durations, yield entered by hand) as a lasting basis for comparing runs.
- **Why:** The data of a finished run is the basis for comparing runs and costs. If gaps turned into zeros, averages and totals would be silently wrong and could not be told apart from real measurements.
- **Evidence:** Observed on the reference installation (2026): the platform dropped detailed history after a retention period, so a one-time export at the end of each run became the lasting record. A check over 33 days exported 136,634 state rows and 51 statistics series with 0 unexplained gaps.
- **Implemented in:** core/src/hub.cpp (`Hub::exportCsv`), docs/HISTORY.md
- **Tests:** tests/core/test_history.cpp "Verlauf: feinste Stufe, Lücken bleiben Lücken" (gaps stay gaps); no dedicated CSV test.


### RAT-017 – VPD is air VPD, with no value when an input is missing

- **Rule:** The hub derives VPD from air temperature and humidity with the FAO-56 saturation vapour pressure (eq. 11): air VPD = es(T) · (1 − RH/100), with no leaf offset. If either input is missing or invalid, or the two are more than 60 s apart, there is no VPD value (a gap), never 0.
- **Why:** Under LED light the leaf is usually somewhat cooler than the air, but by how much depends on light intensity, air movement and leaf angle. A fixed offset would be an invented measurement, the same class of error as a default that pretends to be a reading. Air VPD is also the quantity the target values refer to.
- **Evidence:** Observed on the reference installation (2026): a cross-check of 27 °C at 62 % RH gave 1.352 kPa, against a hand-calculated reference value of 1.36 kPa. A dry run over seven scenarios (normal operation, all inputs unknown, all unavailable, light off, RH 105 %, edge phases, missing attribute) produced no 0 in any failure case. Leaf VPD under LED was put at roughly 0.1–0.2 kPa below air VPD, but varying.
- **Deviation:** The software plans an optional leaf offset whose result is shown as "estimated" (not yet implemented), with air VPD as the default. The 60 s limit between the two inputs is the software's own rule (an assumption).
- **Implemented in:** core/src/truth.cpp (`SensorTruth::updateDerived`), core/include/gc/truth.hpp, catalog/catalog.json, docs/PLANT_AUTOMATION.md, CHANGELOG.md
- **Tests:** tests/core/test_climate.cpp "VPD: Formel nach FAO-56", "VPD: aus Klima-Kopf abgeleitet, im Verlauf, bei Ausfall eine Lücke"


### RAT-018 – No parallel dosing sequence; time limits sit above the longest legitimate run

- **Rule:** The job lock is released only when a job actually ends (done, aborted, emergency stop or restart), never because it simply takes long. A slow mix therefore cannot be overtaken by a second one. Pump runs are capped at 60 s in software, with an independent 90 s cut-off in the dosing block; the simulator models it, and the hardware is still pending. The run cap depends on the dose size, not on the settling time.
- **Why:** Between two doses of the same loop, at least two sensor smoothing windows plus the mixing time must pass, which makes sequences long. A "stuck job" timeout set below that length would free the lock during a legitimate run. It would then allow a second, parallel dosing sequence, the most dangerous outcome a protective check could have.
- **Evidence:** Observed on the reference installation (2026): pH and EC readings were 5-sample moving averages at 15 s intervals, a 75 s window. Settle times of 90–120 s caused overshoot and were raised to 240–260 s. The worst-case sequence grew from about 35 to about 95 minutes, so the limit for a stale sequence had to rise from 90 to 150 minutes to keep the lock. The 60 s run cap and the 90 s device cut-off stayed unchanged.
- **Implemented in:** core/src/hub.cpp (`Hub::mixStart`, job lock), core/src/mix.cpp (repeat-mix confirmation), core/src/dosing.cpp (`Actuators::startRun`), core/include/gc/config.hpp (`kHardMaxRunS`), sim/world.cpp (90 s twin limit)
- **Tests:** tests/core/test_scenarios.cpp "Szenario Stufe 0: Einmessen und geführtes Mischen treffen die Mengen, A:B bleibt gekoppelt"; tests/core/test_mix.cpp "Mischen: Nutzvolumen, Doppelstart, Vorrat". The hardware limit has no test yet.


### RAT-019 – Network sockets fail safe on their own

- **Rule:** When the hub takes over a network socket, it sets the socket to "off after power loss". For pulse and heating roles it also writes an auto-off timer into the device, set just above the software's maximum run time (≈ 1.11 ×, an assumption for pulse roles). The hub binds a role only after reading the settings back successfully, and it checks them again before every switch-on. Loads that run for hours (light, fans, circulation) get no auto-off. Strip outlets are numbered from 1; the app notes once that the device's own interface counts from 0.
- **Why:** If the controller fails while a valve or pump is on, the device keeps its last state indefinitely, and only a timer inside the device still works. Device settings can also be lost or differ from the plan, for example after a factory reset or a change in the vendor app, so they must be read, not assumed.
- **Evidence:** Observed on the reference installation (2026): none of the 19 smart relays had a fail-safe in the device. Timers were added, for example 90 s for the dosing pumps (above the 60 s software cap), 300 s for the CO2 valve and 1800 s for the inlet valve and the irrigation pump. Light, fans and climate devices were deliberately left without a timer, which would otherwise cut the light mid-day. A later read-back of all devices showed that one power strip and the dimmer still restored their last state after power loss, and that an auto-off listed in the inventory had never been active.
- **Implemented in:** core/src/dosing.cpp (`safetyForRole`, `Actuators::setRole`), core/include/gc/dosing.hpp, core/src/hub.cpp (`Hub::acceptDevice`, `Hub::bindRole`), docs/PLANT_AUTOMATION.md (§2, §3), docs/SIMULATOR.md
- **Tests:** tests/core/test_net.cpp "Steckdose: übernehmen setzt „nach Stromausfall aus“, Umwälzpumpe schaltet über die Dose", "Steckdose: Profil puls setzt Auto-Off im Gerät, ohne bestätigtes Rücklesen keine Zuordnung", "Steckdose: Auto-Off im Gerät greift, wenn der Hub ausfällt", "Gateway: Schutzeinstellung im Gerät verloren → kein Einschalten"


### RAT-020 – pH dosing only on plausible readings, and only while it has an effect

- **Rule:** pH readings outside 3–9 are marked implausible and are not used for control. If the pH falls by less than 0.03 twice in a row after clean pH-down doses (no refill or other dosing in between), pH control latches with an alarm. It stays off until the user checks canister, pump and probe and acknowledges.
- **Why:** A finite number is not automatically a valid measurement. A damaged probe can report a stable but meaningless value, and a loop that trusts it keeps adding acid. A dose without a measurable effect points to an empty canister, a blocked hose, a failed pump or a faulty probe, and dosing more fixes none of these.
- **Evidence:** Observed on the reference installation (2026): a probe that had been stored in distilled water reported pH 10.08. The old check only tested for a finite number, so the loop would have dosed 8 × 20 ml = 160 ml of pH-down against a meaningless reading. Lesson: store pH probes in KCl storage solution, never in distilled water.
- **Implemented in:** catalog/catalog.json (`measure.ph` plausible range), core/src/truth.cpp (sensor truth), core/src/control.cpp (pH control, latch `ph.no_effect`)
- **Tests:** tests/core/test_truth.cpp "Sensorwahrheit: veraltet und unplausibel" (pH ≈ 9.7 rejected); no dedicated test for the no-effect latch.


### RAT-021 – Sensor failure and missing calibration are separate findings

- **Rule:** A sensor that delivers nothing (offline, no data, stale) and a sensor that delivers but is not calibrated are different reading states with different messages. The watchdog counts "not delivering" as a problem; "not calibrated" is a problem only when an enabled function needs that value, otherwise it is neutral.
- **Why:** If both share one status, a known setup gap becomes a permanent alarm that buries real failures, and a real outage can hide behind a mere configuration hint.
- **Evidence:** Observed on the reference installation (2026): the working but never calibrated water-level sensor of the humidifier would have kept the sensor check red from the first switch-on, i.e. a constant push alarm; a scale with a zero calibration factor was dropped silently and appeared in neither list.
- **Implemented in:** docs/UX.md, core/include/gc/readmodel.hpp, core/src/watchdog.cpp
- **Tests:** tests/core/test_truth.cpp "Sensorwahrheit: kein Messwert ist nie 0 (M8-1, RAT-006)", "Sensorwahrheit: ohne gültige Kalibrierung kein Wert für die Regelung (RAT-025)"


### RAT-022 – Acknowledging an alarm only pauses its repetition

- **Rule:** (Concept, not yet implemented.) Acknowledging an alarm pauses only its repeated notification; the status stays red. Only critical alarms are pushed at night (22:00–07:00) and repeated after 5, 15 and 60 min; lesser findings go to the morning report or stay in the app.
- **Why:** An acknowledgement that cleared the status would be a hidden way to switch monitoring off. The pause therefore ends after a time limit (12 h on the reference installation), when all checks are OK again, and immediately when a new, different problem appears. Repeating non-critical findings every hour turns observations into disturbances and buries the real alarms.
- **Evidence:** —
- **Implemented in:** docs/UX.md (§6, concept), docs/ROADMAP.md (milestone M3)
- **Tests:** —


### RAT-023 – Sensor truth always runs; freshness and frozen raw values are checked separately

- **Rule:** The sensor-truth layer evaluates every bound sensor on every cycle, also without an active cultivation run and in maintenance mode, because dosing locks depend on it. Freshness (does anything still arrive?) and standstill (does the raw value still move?) are separate checks; a pH or EC raw value unchanged for 15 min is marked "frozen".
- **Why:** Monitoring that runs only during a cultivation run misses failures exactly when they are cheapest to fix, and they surface on the start day. Standstill must be judged on the raw signal: calibrated, rounded values can legitimately stay constant for hours, while the raw signal of a live probe always jitters.
- **Evidence:** Observed on the reference installation (2026): the recorded update time changed only when the value changed, so a single timestamp could not tell a frozen sensor from a fresh one.
- **Implemented in:** core/include/gc/truth.hpp, core/src/truth.cpp, core/include/gc/readmodel.hpp, docs/INVARIANTS.md
- **Tests:** tests/core/test_truth.cpp "Sensorwahrheit: stillstehender Rohwert wird erkannt (RAT-023)"


### RAT-024 – "Not present" is a finding, not a resting state

- **Rule:** A measurement role without an assigned device has its own state "not assigned": it never carries a value and is never usable for control. Once a role is assigned, a device that disappears or stops answering is reported as "offline", which the watchdog counts as a problem.
- **Why:** If "missing" is treated as harmless, a sensor that vanishes (renamed, removed, replaced) leaves the monitoring green while a sensor is gone.
- **Evidence:** Observed on the reference installation (2026): the "missing" state was absent from the list of bad states, so the health indicator stayed green for any sensor that disappeared; an exception meant for one placeholder entry applied to all 19 entries.
- **Implemented in:** core/include/gc/readmodel.hpp, core/src/truth.cpp, core/src/watchdog.cpp
- **Tests:** tests/core/test_truth.cpp "Sensorwahrheit: Rolle ohne Gerät ist ein Befund, kein Wert", "Sensorwahrheit: kein Messwert ist nie 0 (M8-1, RAT-006)" (offline case)


### RAT-025 – Control reads only validated values; no valid calibration, no control value

- **Rule:** Control functions read only the validity state of the sensor-truth layer. Without a valid calibration (pH: two buffer points with a plausible slope; EC: factor between 0.5 and 2) a reading is "uncalibrated": the raw value may be shown, but nothing regulates on it. Out-of-range results are reported as invalid, never clamped to 0.
- **Why:** Clamping turns a defect into a plausible-looking number, and a frozen or invalid value that is still numeric passes every simple guard. Control must stop on its existing safe path instead of acting on such values.
- **Evidence:** Observed on the reference installation (2026): clamping turned a 3.5 V raw signal, i.e. a defect, into a clean-looking pH 0, and four control loops (water heater, humidity, CO2, nutrients) had no check against frozen or stale readings.
- **Implemented in:** core/include/gc/readmodel.hpp, core/include/gc/truth.hpp, core/src/truth.cpp, docs/INVARIANTS.md
- **Tests:** tests/core/test_truth.cpp "Sensorwahrheit: ohne gültige Kalibrierung kein Wert für die Regelung (RAT-025)"


### RAT-026 – Validity limits come from the calibration, not from a guess

- **Rule:** What counts as a valid reading is derived from the stored calibration. A reading outside what the calibration can produce is treated as a defect, not as a valid extreme, and yields no control value.
- **Why:** A guessed tolerance lets a real probe defect pass as "pure water", the more dangerous error. A false alarm only stops dosing and is the cheaper failure; when one occurs, the probe and its zero point are checked instead of widening the limit. Limits change only when a new calibration gives a new number.
- **Evidence:** Measured on the reference installation: with a three-point EC calibration, the calibrated value at the probe's zero point in distilled water was −0.0159 mS/cm, so the lower limit became −0.02 mS/cm instead of an estimated −0.05 mS/cm; the remaining margin was about 0.6 mV of raw signal.
- **Implemented in:** core/src/truth.cpp, docs/INVARIANTS.md
- **Tests:** tests/core/test_truth.cpp "Sensorwahrheit: ohne gültige Kalibrierung kein Wert für die Regelung (RAT-025)", "Sensorwahrheit: veraltet und unplausibel"


### RAT-027 – A sensor counts as failed only after several missed readings

- **Rule:** (Planned for the bus driver in the firmware, not yet implemented.) A sensor is declared failed only after several consecutive failed reads, not after a single one.
- **Why:** A single missed reading is usually a short communication glitch; alarming on it produces false alarms. Requiring several misses still detects a real defect within minutes.
- **Evidence:** Observed on the reference installation (2026): with sensors reporting every 30 s, a freshness limit of 5 min (ten missed reports) separated real defects from wireless dropouts; six missed reports were judged too close to ordinary dropouts.
- **Implemented in:** docs/INVARIANTS.md (open)
- **Tests:** —


### RAT-028 – After a restart, outputs fall back to off but recorded facts survive

- **Rule:** After a restart every output is switched off, and interrupted sequences (dose, mixing run, refill) are reported as interrupted, not resumed. Safety latches, jump locks and stock levels are restored from storage; if that state cannot be read, the hub raises an alarm instead of starting silently with a clean slate. (State functions such as fans and light will get their own restart rule.)
- **Why:** For an actuator, "off" is the safe direction after an unexplained reset, and switching it back on automatically is dangerous. State that records a fact (a lock is set, a calibration is valid, a run is active) must not vanish silently, or a lock is released or a run ends without anyone noticing.
- **Evidence:** Observed on the reference installation (2026): a routine configuration reload reset every status flag that was on to off; across 31 logged reloads in one week a calibration-valid flag dropped seven times, and the flag for an active cultivation run would have ended that run without any error. Numeric and text settings were not affected.
- **Implemented in:** docs/CONCEPT.md (rule R6), core/src/hub.cpp (Hub::boot), core/src/dosing.cpp (Actuators::stopAll)
- **Tests:** tests/core/test_scenarios.cpp "Szenario: Stromausfall im Lauf → alles aus, nicht fortgesetzt, gemeldet (R6, RAT-007)"; tests/core/test_truth.cpp "Sensorwahrheit: Sprungsperre, überlebt Neustart, frei nach 15 min Ruhe (M8-2)"


### RAT-029 – Dimming to zero also switches the light relay off

- **Rule:** (Draft for light control, not yet implemented.) A dimming value below the lamp's switch-on threshold is raised to that threshold; 0 means off, and the hub then switches the light relay off too, so relay state and actual light agree.
- **Why:** A relay left on with the dimmer at zero makes "relay on" and "lamp lit" disagree: relay-based checks then raise a false critical alarm for light in the dark period, while only power measurement can tell whether a lamp really shines.
- **Evidence:** Observed on the reference installation (2026): in a test night both lamp relays were on with the dimmer at 0 V; the lamps drew 0–1.3 W and stayed dark, yet the relay-based check reported a lit lamp for the whole dark period. The power-based check (threshold 10 W) correctly stayed silent.
- **Implemented in:** docs/PLANT_AUTOMATION.md
- **Tests:** —


### RAT-030 – Refill supervision respects the probe's blind zone and judges the rate over a window

- **Rule:** (Open, not yet implemented.) Near the tank bottom the level probe has a blind zone where its reading is not a measurement; rate and standstill checks of a refill are not armed there, and a time limit catches a dead valve instead. Above it, the fill rate is judged over a window of at least 5 min against a tolerance, not reading against reading.
- **Why:** Comparing successive readings of a smoothed, rounded level signal counts every smoothing dip as a fault and aborts valid fills; inside the blind zone a rate check cannot work at all.
- **Evidence:** Observed on the reference installation (2026): about 4 L had flowed in while the level still read 0.0 L; a check comparing readings 60 s apart aborted a fill at 2 L after three smoothing dips, although the rate was correct from 5 L on. The blind-zone time limit was twice the calculated crossing time plus 2 min (about 9 min for 5 L at 1.44 L/min); in a 60 L barrel the blind zone was later found to be 1 L.
- **Implemented in:** docs/INVARIANTS.md (open)
- **Tests:** —


### RAT-031 – No automatic restart after a refill fault

- **Rule:** When the inlet is shut off for a fault (level invalid, capacity limit, valve open too long), a latch is set with reason and time frozen. Refilling stays blocked until a person has checked and acknowledged it; if the valve is found open while the latch stands (failed switch-off, button on the device), the hub closes it again.
- **Why:** Unattended water inflow after a fault nobody has understood risks flooding. At the moment of an automatic restart the cause is by definition not understood, otherwise it would have been fixed; a supervised manual start is the intended path.
- **Evidence:** —
- **Implemented in:** core/src/dosing.cpp (Actuators::inhibit, Actuators::enforce), core/src/control.cpp (RefillController), docs/INVARIANTS.md
- **Tests:** tests/core/test_scenarios.cpp "Szenario: Zulauf – Füllstand fällt aus → Notabschaltung, kein automatischer Neuanlauf (RAT-031)"; tests/core/test_net.cpp "Zulauf: Grund weg, Rastung steht, Ventil klemmt offen → Hub schaltet weiter aus"


### RAT-032 – The inlet's emergency limit follows the valve, not the job

- **Rule:** Whenever the inlet valve is open – opened by the automatic refill, by hand or any other way – the actuator gateway closes it at the tank's capacity, when the level reading becomes invalid, or when it has been open longer than allowed. Without a valid level the inlet cannot be opened at all.
- **Why:** If the overflow guard lives only inside the automatic refill routine, a valve opened any other way runs unsupervised, and the switching device's own auto-off may come too late to prevent an overflow.
- **Evidence:** Observed on the reference installation (2026): a manual fill ran for 14 min without supervision; the only safeguard was a 30-min auto-off in the switching device, which from a starting level of 120 L would not have prevented overflowing the 150 L tank (120 L + 43 L).
- **Implemented in:** core/src/dosing.cpp (Actuators::enforce, Actuators::inhibit), docs/INVARIANTS.md
- **Tests:** tests/core/test_net.cpp "Zulauf klemmt: nach Pegelausfall meldet die Notgrenze neu", "Zulauf: Grund weg, Rastung steht, Ventil klemmt offen → Hub schaltet weiter aus"


### RAT-033 – Narrow control band, wider alarm band

- **Rule:** Control works close to the target; the watchdog reports only when a value is clearly outside the control band. For pH and EC the alarm band is target ± (tolerance + 0.2), in pH units or mS/cm.
- **Why:** When control and alarm share one threshold, every normal control action also raises an alarm, and constant alarms get ignored. An alarm should mean "control is failing", not "control is working".
- **Evidence:** Observed on the reference installation (2026): air-temperature control and the watchdog both acted at ±1 K, so in a test night every control step was also a watchdog message. The revised setting there was ±1.0 K control / ±2.0 K alarm for air temperature and ±3 % / ±7 % for relative humidity.
- **Implemented in:** core/src/watchdog.cpp (kAlarmMargin), docs/INVARIANTS.md
- **Tests:** tests/core/test_watchdog.cpp "Watchdog: Alarmband weiter als Regelband (RAT-033)"


### RAT-034 – Humidifier and dehumidifier never together; compressor protection

- **Rule:** The actuator gateway never lets humidifier and dehumidifier run at the same time; if the other device's state is unknown (e.g. its outlet is unreachable), the device stays off. A compressor dehumidifier may be switched on again only 5 min after it was switched off, also after an emergency stop or restart; switching off is always allowed.
- **Why:** The two devices work against each other: running both wastes energy, and alternating too fast ends in ping-pong. Compressors must not be short-cycled because they need time to equalise pressure; only quick re-starting harms them, so safety shut-offs stay immediate.
- **Evidence:** Observed on the reference installation (2026): four identical ping-pong cycles in one test night, relative humidity swinging between 62 % and 85 % at a target of 70 ± 5 %, because a lock also blocked switching off and acted as a forced minimum run. The power signature identified the dehumidifier as a compressor unit (about 3 min fan only at 32 W, then compressor start rising from 315 W to 420 W).
- **Deviation:** The reference installation also locked the opposite device for 10 min after each switch-off, kept the dehumidifier running for at least 10 min and pulsed the humidifier in 30-s steps. The software so far enforces only "never together" and the 5-min pause, with a provisional 5-min maximum per humidifier pulse (assumption), because these timings belong to the climate function, which is not built yet.
- **Implemented in:** core/src/dosing.cpp (Actuators::inhibit, Actuators::stopAll), core/include/gc/dosing.hpp (kCompressorPause), catalog/catalog.json, docs/PLANT_AUTOMATION.md, CHANGELOG.md
- **Tests:** tests/core/test_net.cpp "Gateway: Befeuchter und Entfeuchter nie zugleich, Kompressor-Pause, Höchstlaufzeit", "Gateway: unbekannter Zustand des Gegengeräts sperrt (R5)", "Gateway: Kompressor-Pause gilt auch nach Not-Halt"


### RAT-035 – Light ramps of 15 minutes inside the light period

- **Rule:** (Planned light schedule, not yet implemented.) Sunrise and sunset ramps default to 15 min (`ramp_min`) and lie inside the light period.
- **Why:** Ramps outside the light period would lengthen the photoperiod, since even dim light counts for flowering. Dropping ramps would hit the climate control with the lamps' full heat load at once. Short ramps inside the window cost little light. A ramp computed from the time of day keeps its configured length and survives a restart; stepping it minute by minute did not.
- **Evidence:** Observed on the reference installation (2026): a sunrise configured for 30 min finished in 9 min because per-minute steps were rounded to whole percent. Calculated there: 15/15-min ramps inside the window lose at most 1.7 % of the daily light integral in flowering (about 0.7 mol/m²/d), versus about 5 % (about 2.1 mol/m²/d) with 30/60-min ramps; the lamp heat load was about 500 W.
- **Implemented in:** docs/PLANT_AUTOMATION.md
- **Tests:** —


### RAT-036 – Stop is an active, idempotent shutdown

- **Rule:** An emergency stop actively switches off all pumps and all switched outputs, mains outlets included and even when their state is unknown; it aborts the running job (booking what has already run), resets the controllers and blocks automation and hand doses until resume. Triggering it again is harmless.
- **Why:** If control loops merely stop issuing new commands, a stop freezes the last state and devices keep running unnoticed.
- **Evidence:** Observed on the reference installation (2026): a stop switched almost nothing off – light, circulation fan and circulation pump ran on for 4.5 h (the pump 9.3 h in one stretch), the climate unit stayed in automatic mode for 6.8 h and a fill request stayed set for 13.3 h; three manual rounds were needed to clean up.
- **Deviation:** The reference installation staged its end-of-run stop: running sequences were ended first, all loads switched off hard after 30 s, and circulation fan and exhaust ran on for 5 min to avoid stagnant humid air around warm plants; a few devices were deliberately never switched at their outlets. The software's stop is an emergency stop and switches everything off at once, fans included; a confirming second stage after 30 s and repeating unconfirmed "off" commands are still open.
- **Implemented in:** core/src/hub.cpp (Hub::stop), core/include/gc/dosing.hpp and core/src/dosing.cpp (Actuators::stopAll), CHANGELOG.md, docs/INVARIANTS.md
- **Tests:** tests/core/test_scenarios.cpp "Szenario: Not-Halt stoppt alles und sperrt Automatik bis Fortsetzen"; tests/core/test_net.cpp "Steckdose: Not-Halt und Stromausfall schalten aus, offline gibt Klartext"; web/e2e/betrieb.spec.ts "Not-Halt stoppt alles und lässt sich fortsetzen"


### RAT-037 – Display follows the actual state; every block is a visible line with its reason

- **Rule:** The app shows an output as running only when the device reports it, not when it was requested. Each controller has a status line with one of a few states and a checklist of its conditions ("why is it not dosing?"); every block appears as its own line with a reason and disappears by itself when the block ends.
- **Why:** A display driven by requests hides devices switched by other means, and the in-between state "requested but not running" is exactly the one worth seeing. Block and control states need a visible line, and a line that is not cleared when the block ends becomes a stale message.
- **Evidence:** Observed on the reference installation (2026): five dashboard tiles took their colour from request flags, so a pump switched by other means showed "off" while running; a scheduled light ramp coloured a whole tile as a warning; block and control states had no visible line.
- **Implemented in:** docs/UX.md, core/include/gc/control.hpp (CtlStatus with checklist)
- **Tests:** web/e2e/betrieb.spec.ts "Sprungsperre der pH-Sonde wird sichtbar mit Grund", "Übersicht zeigt Überwachung, Messwerte und Regelzeilen"


### RAT-038 – Refill volume is calculated; the level sensor only verifies and cuts off

- **Rule:** An automatic refill computes its run time from the missing volume and the configured inlet rate and closes the valve when that time is up. The level sensor serves as verification and emergency stop; it ends the fill early only if the level passes the target by more than 1 L. Without a valid inlet rate the refill is blocked, with no substitute value. A re-check after the run with limited corrective top-ups is still open.
- **Why:** The level reading lags behind the real level because of smoothing and reporting intervals; stopping on it overfills, and stopping early to compensate cuts fills short. The inflow itself has no overrun once the valve closes.
- **Evidence:** Measured on the reference installation: inlet rate 1.44 L/min (later set to 1.54 L/min) with no overrun after closing, only a lagging level display; level-based stopping had overrun by 2–3 L per fill, and anticipatory stops ended fills too early. The reference re-checked the level 120 s after closing and allowed at most two corrective runs. The simulator uses 2 L/min.
- **Implemented in:** core/src/control.cpp (RefillController), docs/INVARIANTS.md, docs/SIMULATOR.md
- **Tests:** —


### RAT-039 – pH/EC safety: jump lock, hand-dose cap, no silent blocks

- **Rule:** If pH changes by more than 1.0 or EC by more than 0.5 mS/cm within 5 min without an explanation such as the hub's own dose or a refill, the reading is locked for control; the lock lifts after 15 min of calm and survives a restart. Each hand dose is capped (default 5 ml, configurable, hard ceiling 50 ml). Every blocked dosing shows its reason in the controller's status line.
- **Why:** A disturbed probe can deliver wrong values that still lie inside the plausible range, and dosing on them is dangerous. An unbounded manual pump run can add a large amount of acid in one go. A skipped dosing that is only logged looks like a working system.
- **Evidence:** Observed on the reference installation (2026): without galvanic isolation the pH and EC probes interfered, showing pH 10.4–10.9 while the real value was about 2, and no existing check caught it; a dosing run skipped at pH 9.4 left its reason only in a log; manual pH-down runs added up to about 93 s ≈ 74 ml on 23 L, limited only by a 90-s auto-off (≈ 70 ml in one run).
- **Deviation:** The reference installation limited a manual pump run by time (default 5 s, adjustable 1–60 s) and cut it off. The software rejects hand doses above a volume cap, because the user enters a hand dose in ml and the cap guards against typing errors.
- **Implemented in:** core/src/truth.cpp, core/include/gc/readmodel.hpp, core/include/gc/config.hpp, core/include/gc/control.hpp, web/src/pages/settings.tsx, docs/UX.md, docs/INVARIANTS.md
- **Tests:** tests/core/test_truth.cpp "Sensorwahrheit: Sprungsperre, überlebt Neustart, frei nach 15 min Ruhe (M8-2)"; tests/core/test_scenarios.cpp "Szenario: Sprungsperre – während der Sperre keine pH-Gabe, Ereignis sichtbar"; tests/core/test_catalog_config.cpp "Konfiguration: Grenzen dürfen nur verschärfen (R7)"; web/e2e/betrieb.spec.ts "Sprungsperre der pH-Sonde wird sichtbar mit Grund"


### RAT-040 – Book consumption per run, hand doses included; test runs are not consumption

- **Rule:** Canister stock is reduced by what actually went into the tank, booked per pump run as it happens, including hand doses and aborted runs. Calibration and priming runs (into a measuring cup or to fill the tubing) are not booked as consumption.
- **Why:** If only automatic sequences are booked, hand doses vanish from consumption and cost figures. Test runs, on the other hand, usually go back into the canister and would overstate consumption.
- **Evidence:** Observed on the reference installation (2026): the cost display showed 0 ml pH-down although about 74 ml had been dosed by hand; manual runs were booked for none of the eight pumps.
- **Implemented in:** core/src/dosing.cpp (Doser::book, Doser::abort), docs/INVARIANTS.md
- **Tests:** tests/core/test_scenarios.cpp "Szenario: Abbruch bucht, was schon gelaufen ist (RAT-070)", "Szenario Stufe 0: Einmessen und geführtes Mischen treffen die Mengen, A:B bleibt gekoppelt" (calibration runs are not consumption)


### RAT-041 – Latch pH lowering that has no effect; no pH counter-correction

- **Rule:** If two clean pH-lowering doses in a row each move pH by less than 0.03, pH control latches until the user has checked canister and probe and acknowledged. The controller only lowers pH; if pH ends below the target band it rests and says so instead of dosing the other way.
- **Why:** A dose without effect points to an empty canister, a blocked line or a faulty probe; repeating it blindly risks a large overshoot once the fault clears. Alternating between lowering and raising ("ping-pong") wastes product, adds ions and can oscillate.
- **Evidence:** —
- **Deviation:** The reference installation also raised pH automatically, with the same step limit and effect check, plus a rule that a run which has already lowered pH never raises it again. The prototype does not regulate pH upward at all, so a counter-correction cannot occur.
- **Implemented in:** `core/src/control.cpp` (`PhController::tick`: latch `ph.no_effect`, status `ph.below`)
- **Tests:** — (no dedicated test for the no-effect latch; marked partial in `docs/INVARIANTS.md`)


### RAT-042 – Announced interventions explain sensor jumps

- **Rule:** Changes the system causes or has been told about count as expected, so the jump lock does not trip on them: mix runs and hand doses, EC control rounds, an open inlet valve, a running probe calibration and maintenance mode (the user's way to announce work at the tank). Own pH control doses are not announced, because their cap (0.3 pH) is below the pH jump threshold (1.0). Status is shown in plain language rather than codes; the overall colour reflects the worst entry.
- **Why:** The jump lock exists to catch a misbehaving sensor. A step that follows a known action is not a fault; treating it as one blocks dosing and trains users to ignore alarms.
- **Evidence:** Observed on the reference installation (2026): during a probe buffer test, EC dropped from 1.19 to 0.0 mS/cm when the probe was taken out and came back at 1.21 about 2.5 minutes later. The jump guard locked and released correctly, yet the alarm was noise, because the handling had been planned.
- **Implemented in:** `core/src/hub.cpp` (`Hub::tickImpl`), `core/src/truth.cpp`, `core/include/gc/truth.hpp` (`SensorTruth::expectChange`), `docs/UX.md` §1
- **Tests:** `tests/core/test_truth.cpp` – "Sensorwahrheit: eigene Gabe erklärt die Änderung (M8-3)"


### RAT-043 – No pH correction on an old reading

- **Rule:** The pH controller refuses to dose when the pH reading is older than 10 minutes. This backstop comes on top of the general freshness check, which already marks a pH reading older than 60 s as stale and unusable.
- **Why:** A displayed value can keep looking plausible long after it stopped describing the tank. Dosing acid against it can push the real pH far off, so an outdated value must be visible and must have consequences.
- **Evidence:** Observed on the reference installation (2026): after a probe buffer test, no new tank reading was taken for about five hours. The display kept the buffer value 7.01, the target check stayed green and the overall status said "all fine". At that time only the pH-raising path checked freshness; lowering would have dosed on the old value.
- **Implemented in:** `core/src/control.cpp` (`kMaxPhAge`, `PhController::tick`)
- **Tests:** — (general staleness: `tests/core/test_truth.cpp` – "Sensorwahrheit: veraltet und unplausibel")


### RAT-044 – Jump locks survive a restart; no pH dosing on an unsecured measurement

- **Rule:** A jump lock (pH change above 1.0 or EC change above 0.5 mS/cm within 5 min, without explanation) blocks dosing until the value has been quiet for 15 min. Jump locks and latches live in the stored runtime state and survive a restart. Without a pH probe the software never doses pH automatically; it asks for a hand measurement.
- **Why:** A safety lock held only in volatile memory vanishes exactly when the system restarts, possibly while the fault persists. The plausibility band alone is no protection: a disturbed reading can still look plausible.
- **Evidence:** Observed on the reference installation (2026): with the circulation pump running, electrical interference shifted the pH reading into about 7.9–12.1, and the part from 7.9 to 9.0 lies inside the 3–9 plausibility band. After a restart had cleared the jump guard's memory, the next automatic run would have taken a disturbed value of about 8.5 as real and dosed acid into a tank that was actually near pH 4.
- **Deviation:** The reference installation also allowed pH dosing only with a pump-off measurement window or after the galvanic isolation had passed an acceptance test (raw value differing by less than 0.05 V between pump on and off). Here that measurement window is listed as a later hardware measure and is not part of the prototype.
- **Implemented in:** `core/src/truth.cpp` (`kJumpHoldS`, jump detection), `core/include/gc/config.hpp` (`RuntimeState::jumpLocks`, `latches`), `core/include/gc/readmodel.hpp` (`Quality::Jump`), `docs/CONCEPT.md` (R6), `docs/SECURITY_MODEL.md`, `docs/DECISIONS.md` (E8)
- **Tests:** `tests/core/test_truth.cpp` – "Sensorwahrheit: Sprungsperre, überlebt Neustart, frei nach 15 min Ruhe (M8-2)"; `tests/core/test_scenarios.cpp` – "Szenario: Sprungsperre – während der Sperre keine pH-Gabe, Ereignis sichtbar"; `web/e2e/betrieb.spec.ts` – "Sprungsperre der pH-Sonde wird sichtbar mit Grund"


### RAT-045 – Short notifications

- **Rule:** Planned: push notifications stay short – a title of about 40 characters without abbreviations and a body of at most 200 characters saying what is wrong, with the relevant values and the time. Explanations and troubleshooting steps belong in the app. A daily summary may be longer.
- **Why:** Lock screens cut long messages off, so the essential part was lost; a notification has to be readable at a glance.
- **Evidence:** Observed on the reference installation (2026): single notifications reached 355 characters and were truncated on the phone's lock screen; 11 test scenarios exceeded 200 characters. The daily report there was allowed up to 1,000 characters.
- **Implemented in:** not yet; planned for milestone M3 in `docs/ROADMAP.md`
- **Tests:** —


### RAT-046 – EC gate before pH correction

- **Rule:** pH is corrected only while EC is valid and at least 0.5 mS/cm (configurable floor). An invalid EC blocks; EC below its own target does not. After an EC dose, pH waits a 240 s rest; if no EC dose is on record (e.g. after a restart), that does not block. Each condition appears as a checklist line with its reason.
- **Why:** In nearly ion-free water a glass electrode has no reliable reference, so acid dosed on its reading is guesswork. A frozen EC value from the previous batch must not pass the gate. The rest keeps the smoothed EC from still containing samples from before the dose. Missing history must not block pH control forever.
- **Evidence:** Observed on the reference installation (2026): two freshly calibrated pH probes read 5.06 and 7.3 in reverse-osmosis water (EC about 0.04 mS/cm) but 7.02 and 7.2 in tap water – a 2.2 pH spread caused by the medium, not the calibration. The 240 s equalled twice the 120 s smoothing window of the EC measurement there.
- **Implemented in:** `core/src/control.cpp` (`PhController::tick`, `kEcRestS`), `core/include/gc/control.hpp` (checklist), `catalog/catalog.json` (`ec_floor`)
- **Tests:** `tests/core/test_scenarios.cpp` – "Szenario: EC-Gate – in Osmosewasser kein pH−"


### RAT-047 – Circulation dry-run protection with its own thresholds

- **Rule:** The circulation pump's dry-run protection acts whoever switched the pump on and only ever switches it off. It cuts the pump below the tank minimum and allows it again only 0.5 L above, so it never fights the circulation controller; it latches only if the pump was actually running. pH and EC control doses run only while the circulation pump runs.
- **Why:** The level below which the sensor cannot measure and the level at which the pump may run are different properties; coupling them either cuts a pump that still primes or claims a blind zone that does not exist. Aligned on/off thresholds leave no zone where one function wants the pump on and the other blocks it. Without mixing, a dose cannot be measured.
- **Evidence:** Observed on the reference installation (2026): a level test showed the probe reading correctly below 4 L and the pump priming cleanly at 3 L. Tying the pump threshold to the sensor's blind-zone setting would also have changed unrelated behaviour, e.g. a failed inlet valve would have been detected about 2.6 min later.
- **Implemented in:** `core/src/dosing.cpp` (`Actuators::inhibit`, `Actuators::enforce`, `Actuators::startRun` → `act.no_mixing`)
- **Tests:** `tests/core/test_scenarios.cpp` – "Szenario: Trockenlauf – Umwälzpumpe aus, Rastung, Quittierung" (M11-1); `tests/core/test_net.cpp` – "Umwälzpumpe: späterer Trockenlauf meldet neu, Rastung hält die Pumpe aus"


### RAT-048 – Water temperature is "not applicable" in an empty tank

- **Rule:** When the tank level is below the configured minimum, the watchdog rates water temperature as neutral ("not applicable: tank empty") instead of checking its band. Whether the sensor delivers at all is still shown. The UI keeps three states apart: sensor not delivering, not calibrated, not applicable.
- **Why:** In an empty tank the probe measures air. Rating that against water targets produces findings without meaning, such as "too cold" alarms whenever the room is cool.
- **Evidence:** Observed on the reference installation (2026): with the tank empty, water was reported "below target" whenever the room air was cooler than the target. Over 14 days, all five temperature jumps of 0.5 °C or more (out of 6,953 changes) happened at 0 L while filling or draining.
- **Implemented in:** `core/src/watchdog.cpp` (water temperature check), `docs/UX.md` §1
- **Tests:** —


### RAT-049 – Dry-run detection from pump power (planned)

- **Rule:** Planned: detect a circulation pump that runs without pumping from its power draw – pump on, start-up grace over, power below 2.5 W for more than 20 s. This needs power measurement on the output, which the prototype does not have yet.
- **Why:** The level only tells whether water is in the tank, not whether the pump delivers. Air in the pump head, a clogged intake or a wrong valve position are invisible at full level.
- **Evidence:** Measured on the reference installation (pump of about 3.4 W, 21 days, 1,050 power readings): running with water p10 3.30 W and median 3.40 W; running dry median 1.70 W, so 2.5 W separates them. Applied retrospectively, the rule found two episodes, both real, with no false trips; one was a 27-minute dry run nobody had noticed. A pump that first had to clear air from its head needed about 125 s to exceed 2.5 W, so the start-up grace must be generous.
- **Deviation:** There this criterion switched the pump off and latched until acknowledged. Here the watchdog only evaluates; switching off on a dry run belongs to the actuator gateway. PD-031 decides that the hub detects from the current draw whether a pump starts or draws air and switches off and reports on a dry run (sockets with power metering, 12 V outputs with current measurement per output). Thresholds per pump are open.
- **Implemented in:** not yet (open in `docs/INVARIANTS.md`; planned in `docs/PLANT_AUTOMATION.md` §3)
- **Tests:** —


### RAT-050 – Effect-based cap and minimum run time for pH-lowering doses

- **Rule:** A pH-lowering dose is capped at the smallest of: the amount expected to move pH by 0.3, 0.3 ml per litre, and the configured maximum per dose (default 20 ml). Until an effect has been learned, 5.0 pH per ml/L is assumed – the strongest measured effect, hence the smallest dose. A pump run under 1.0 s is not dosed; the controller rests visibly with the reason.
- **Why:** Underestimating the acid means overdosing, and a pH overshoot cannot be undone; overestimating only costs extra rounds. Very short runs are too imprecise.
- **Evidence:** Measured on the reference installation: 4.9 ml of pH− in 20.4 L of reverse-osmosis-based solution at EC 2.96 (0.24 ml/L) lowered pH by about 1.2, i.e. roughly 5.0 pH per ml/L – 7.5 times what the earlier dose formula assumed. A fixed 20 ml cap would have allowed a drop of about 4.9 pH for a 1.0 pH gap; the effect-based cap gives 1.22 ml (about 1.5 s of pumping) at 20.4 L.
- **Implemented in:** `core/src/mix.cpp` (`planPhDose`, `splitRuns`), `core/include/gc/mix.hpp`, `core/include/gc/config.hpp` (`kHardMinRunS`), `core/src/control.cpp` (`kPhStartEffect`, rest `ph.too_small`), `core/src/dosing.cpp` (gateway rejects runs below the minimum)
- **Tests:** `tests/core/test_mix.cpp` – "pH-Gabe: Deckel und Startwirkung" (M4-1, M4-5), "Teilläufe: gleich groß, Untergrenze 1 s" (M2-6)


### RAT-051 – A dry-run lock is binding for every switching path

- **Rule:** A circulation dry-run latch (likewise an inlet fault latch) is enforced in one place, the actuator gateway: if the output is found running while the latch is unacknowledged – whoever switched it on, including by hand at the device – the hub switches it off again. Controllers read the same lock and do not request the pump. pH and EC control doses only run with circulation on; if circulation is cut, a running control dose stops.
- **Why:** If only the protection function knows the lock, every other path keeps restarting the pump and the protection keeps cutting it – an endless on/off cycle. Dosing without mixing doses against a reading that cannot see the dose.
- **Evidence:** Observed on the reference installation (2026): when the dry-run protection first went live, the circulation control and a periodic routine restarted the pump every minute and the protection switched it off within 10 s each time, because the lock was invisible to them.
- **Deviation:** There a lock caused by low level released itself once the tank was refilled; here it holds until acknowledged (a formal decision on this is pending).
- **Implemented in:** `core/src/dosing.cpp` (`Actuators::enforce`, `Actuators::inhibit`, `Actuators::startRun` → `act.no_mixing`), `core/src/control.cpp` (circulation checks in the EC and pH controllers)
- **Tests:** `tests/core/test_net.cpp` – "Umwälzpumpe: späterer Trockenlauf meldet neu, Rastung hält die Pumpe aus", "Umwälzpumpe klemmt: neuer Trockenlauf nach Quittierung meldet neu", "Zulauf: Grund weg, Rastung steht, Ventil klemmt offen → Hub schaltet weiter aus", "Zulauf klemmt: nach Pegelausfall meldet die Notgrenze neu"; `tests/core/test_scenarios.cpp` – "Szenario: Trockenlauf – Umwälzpumpe aus, Rastung, Quittierung" (M11-1)


### RAT-052 – Mixing and settling: pH needs much longer than EC

- **Rule:** After each dose the controller waits before judging the result: by default 4 min for EC and 5 min for pH (configurable 2–60 min). Mixing time increases with tank volume relative to circulation flow; deriving the waiting time from volume is still open.
- **Why:** Dosing again before the previous dose is visible stacks doses and overshoots – irreversibly for pH. EC and pH respond very differently, so one shared waiting time does not fit.
- **Evidence:** Measured on the reference installation (20.4 L, circulation running): after nutrient doses EC was within tolerance after a median of 32 s (t63 median 26 s). After a pH-lowering dose nothing moved for 99 s (about 80 s counted from the pump restart), then pH fell steeply; it was within tolerance only after 194 s. Shortening the pH wait in proportion to a faster sensor filter (to about 45 s) would have allowed four doses before the first became visible.
- **Deviation:** The simulator deliberately uses a shorter pH dead time (60 s) than measured (80–99 s). Its mixing model (t63 = 26 s × V/20 L with circulation) follows the measurement; the 240 s without circulation is an assumption.
- **Implemented in:** `catalog/catalog.json` (`settle_min` defaults), `core/src/control.cpp` (settling phases), `sim/world.cpp` (`kPhDeadMs`, mixing model), `docs/SIMULATOR.md`
- **Tests:** —


### RAT-053 – EC keeps a reserve for the pH-lowering dose that follows

- **Rule:** When pH control is active and pH is above its target, EC control aims below the EC target by the EC the coming pH-lowering dose is expected to add: pH gap ÷ pH effect × 1.04 mS/cm per ml/L, capped at 0.3 mS/cm. The reserve does not depend on tank volume.
- **Why:** pH is always corrected last, and every acid adds ions. Dosing EC exactly to target and then lowering pH ends above the EC target. Ending slightly low is fixed by the next dose; too much fertiliser can only be fixed by dilution.
- **Evidence:** Measured on the reference installation: 4.9 ml of pH− in 20.4 L (0.24 ml/L) raised EC from 2.96 to 3.21 mS/cm, i.e. 1.04 mS/cm per ml/L – almost four times the effect of the nutrient pair itself (0.275). That run ended 7 % above the EC target. For a 1.0 pH gap the reserve is 1.0 ÷ 5.0 × 1.04 ≈ 0.21 mS/cm.
- **Implemented in:** `core/src/control.cpp` (`kEcFromPhDown`, `EcController::tick`); simulator model in `docs/SIMULATOR.md`
- **Tests:** — (no dedicated test)


### RAT-054 – Paired nutrients keep their ratio at every limit

- **Rule:** Parts of a pair (e.g. A and B) are always scaled by one common factor at every limit – round cap, maximum run time, partial-run limits – and never capped or rounded per pump. If one part fails during a mix run, the message says the partner is already in and offers to catch up the missing part.
- **Why:** Each pump has its own flow rate; capping both at the same run time delivers the ratio of the flow rates, not the recipe. EC does not reveal the error, because it measures total ions, not proportions.
- **Evidence:** Measured on the reference installation: the two pumps delivered about 52.8 and 39.6 ml/min. Capping each at 60 s gave A:B = 1.33:1 instead of 1:1 for larger doses in a 100–150 L tank; rounding A to whole millilitres turned a 9 ml dose into 5:4 (1.25:1).
- **Implemented in:** `core/src/mix.cpp` (`planEcDose`, joint handling of `splitRuns` errors), `core/include/gc/mix.hpp`, `core/src/hub.cpp` (`Hub::onJobDose`); simulator pump spread in `sim/world.cpp`
- **Tests:** `tests/core/test_mix.cpp` – "Mischen: Paar 2:1 ohne Rundung (M1-2)", "EC-Gabe: 0,8 × Lücke, gemeinsam skaliert" (M5-1); `tests/core/test_scenarios.cpp` – "Szenario Stufe 0: Einmessen und geführtes Mischen treffen die Mengen, A:B bleibt gekoppelt", "Szenario: Pumpe blockiert im Mischlauf → Paar-Fehler, nachholen nach Behebung"


### RAT-055 – Dose from the measured effect; large doses in equal partial runs

- **Rule:** Each EC or pH control round doses 0.8 × gap ÷ effect × volume. The effect starts at a fixed value (EC: 0.275 mS/cm per ml/L of recipe) and is then learned from clean rounds (RAT-057), clamped asymmetrically: below 0.25 × start → start value, above 4 × start → 4 × start. Doses longer than 60 s of pumping are split into equal partial runs with 3 s pauses instead of being cut short. If EC does not move after two rounds, EC control latches. Nothing is dosed while the inlet is open or a probe is being calibrated; refilling waits until a running dose has settled.
- **Why:** Aiming at 80 % of the gap leaves room for incomplete mixing at measurement time, and an EC overshoot can only be undone by dilution. Both clamp directions yield the smaller dose. Silently shortened doses falsify the recipe. Dilution or calibration in between makes the effect unmeasurable.
- **Evidence:** Measured on the reference installation: the nutrient pair raised EC by 0.275 mS/cm per ml/L in reverse-osmosis water (about 20 L). In model runs from EC 0.55 to 3.0 with 30 % of the last dose not yet mixed, aiming at the full gap ended at 3.30 and aiming at 80 % at 3.15. Before splitting, base doses cut to 60 s delivered only 64–80 % of the recipe amount in a 100 L tank.
- **Deviation:** The reference allowed at most 4 partial runs per EC round and 6 for base doses; here control doses are limited to 6 partial runs and mix runs have no upper limit.
- **Implemented in:** `core/src/mix.cpp` (`planEcDose`, `planPhDose`, `clampEffect`, `splitRuns`), `core/include/gc/mix.hpp`, `core/include/gc/config.hpp` (`kHardMaxRunS`), `core/src/control.cpp` (`kEcStartEffect`, `ec.no_effect`, refill waits), `core/include/gc/control.hpp`, `core/src/dosing.cpp` (pause between runs), `core/src/hub.cpp` (refill before control rounds)
- **Tests:** `tests/core/test_mix.cpp` – "Teilläufe: gleich groß, Untergrenze 1 s" (M2-6), "EC-Gabe: 0,8 × Lücke, gemeinsam skaliert" (M5-1), "pH-Gabe: Deckel und Startwirkung" (M4-1, M4-5)


### RAT-056 – A small learned effect never widens the EC cap

- **Rule:** The EC raise per round is capped (default 1.0 mS/cm), converted to ml/L with the larger of start effect and learned effect. A missing cap value never means "no cap".
- **Why:** A learned effect that is too small – for example measured before a dose was fully mixed – would otherwise inflate both the dose and its limit. Using the larger effect keeps the cap at most where the start value puts it, and the cap is expressed in effect rather than in millilitres that ignore how strong the product is.
- **Evidence:** —
- **Implemented in:** `core/src/mix.cpp` (`planEcDose`), `core/include/gc/mix.hpp`, `catalog/catalog.json` (`max_ec_step`)
- **Tests:** `tests/core/test_mix.cpp` – "EC-Deckel: kleine gelernte Wirkung weitet ihn nicht auf" (M5-4), "EC-Gabe: 0,8 × Lücke, gemeinsam skaliert" (M5-1)


### RAT-057 – Learn dose effects only from clean rounds

- **Rule:** A round's observed effect updates the learned effect – and counts towards the "no effect" latch – only if nothing else changed the tank in between: no inflow from the inlet, no mix run, hand dose or calibration, no dose from the other controller. While pH is dosing or settling, a new EC round waits.
- **Why:** Dilution from refilling or a second dose falsifies the before/after comparison, and a wrong learned effect mis-sizes every later dose.
- **Evidence:** —
- **Implemented in:** `core/src/control.cpp` (`clean_` in `EcController::tick` and `PhController::tick`), `core/include/gc/control.hpp` (`ControlEnv::phBusy`)
- **Tests:** — (marked partial in `docs/INVARIANTS.md`)


### RAT-058 – Waiting times come from measured time-to-tolerance

- **Rule:** Waiting after a dose should cover the measured time until the reading the controller uses stays within control tolerance, with margin (that time × 2.2, and at least twice the sensor's smoothing window). After an EC dose, pH correction also waits a 240 s rest (RAT-046).
- **Why:** What matters is when the next dosing decision would no longer change, i.e. when the remaining error is within tolerance (EC 0.05 mS/cm, pH 0.1). The usual t95 is misleading because display resolution hides the tail of the curve.
- **Evidence:** Measured on the reference installation: one EC step gave t95 = 127 s although it was within 0.03 mS/cm after 51 s. At about 20 L, EC was within tolerance after at most 47 s, which the rule turns into about 100 s.
- **Deviation:** The prototype uses fixed defaults (EC 4 min, pH 5 min, rest 240 s) instead of values derived from measurements on the actual tank.
- **Implemented in:** `core/src/control.cpp` (`kEcRestS`), `catalog/catalog.json` (`settle_min`)
- **Tests:** —


### RAT-059 – Freshness and standstill are separate checks

- **Rule:** The software checks separately whether a sensor still delivers (age of the last sample → "stale") and whether its raw value has stopped moving ("frozen"). The standstill check applies only where change is expected: pH and EC raw values, which normally show noise, are flagged after more than 15 min without change. Calm or coarsely rounded channels such as water temperature get no standstill check.
- **Why:** A rounded or very quiet signal that does not change is not a fault; flagging it produces false alarms that teach users to ignore warnings. A sensor that stops reporting is still caught by the freshness check.
- **Evidence:** Observed on the reference installation (2026): a water temperature reported in 0.1 °C steps stayed on one value for more than 60 min twenty times in 14 days (up to 211 min) with a full tank and a working sensor; an air temperature sensor with 0.1 °C resolution stood still for up to 182 min. None of them had a gap in delivery.
- **Implemented in:** `core/src/truth.cpp` (`SensorTruth::update`, `expectsNoise`, `kFrozenAfter`), `core/include/gc/readmodel.hpp` (`Quality::Stale`, `Quality::Frozen`)
- **Tests:** `tests/core/test_truth.cpp` – "Sensorwahrheit: veraltet und unplausibel", "Sensorwahrheit: stillstehender Rohwert wird erkannt"


### RAT-060 – Mains outlets protect themselves; no heater without an emergency shutdown

- **Rule:** When a smart outlet is taken over, the hub sets "off after power loss" and, for pulse and heating roles, an auto-off timer inside the device at about 1.11 × the role's maximum run time (rounded up to a full minute); the role is bound only after these settings have been read back. The hub itself switches off at the maximum run time; the device timer is the fallback if the hub fails. Heater roles are not offered until a latching emergency shutdown exists (level, water temperature, overtemperature, missing heating effect, failed sensor evaluation, run time).
- **Why:** A heater without water or with a failed sensor is a fire and scalding risk, and its protection must not depend on the hub, the network or an active cultivation cycle. A timer inside the device still works when the controller is gone.
- **Evidence:** Calculated for the reference installation: a 295 W immersion heater in 8 L of water raises it by about 47 K in 90 min. There the device timer was set to 6,000 s against a 90-min software limit (ratio 1.11).
- **Deviation:** The reference used this ratio only for the heater; applying it to pulse roles (e.g. humidifier, irrigation pump, inlet) is an assumption.
- **Implemented in:** `core/include/gc/bus.hpp` (`SwitchSafety`), `core/include/gc/dosing.hpp` and `core/src/dosing.cpp` (`safetyForRole`, maximum run time in `Actuators::enforce`), `core/src/hub.cpp` (`Hub::acceptDevice`, `Hub::bindRole`), `docs/PLANT_AUTOMATION.md` §3
- **Tests:** `tests/core/test_net.cpp` – "Heizung: keine Heizrolle, bis die Notabschaltung … steht", "Steckdose: Auto-Off im Gerät greift, wenn der Hub ausfällt", "Steckdose: Profil puls setzt Auto-Off im Gerät, ohne bestätigtes Rücklesen keine Zuordnung", "Steckdose: übernehmen setzt „nach Stromausfall aus“, Umwälzpumpe schaltet über die Dose", "Gateway: Befeuchter und Entfeuchter nie zugleich, Kompressor-Pause, Höchstlaufzeit"


### RAT-061 – Heater: judge the heating effect from power and water volume

- **Rule:** Heating is only supported through an external switched outlet with a latching emergency shutdown; until that shutdown exists, heater roles are refused. Its "no heating effect" check is to compare the measured temperature rise with the rise expected from heater power and water volume (minus passive losses), not with a fixed waiting time.
- **Why:** A fixed "rise X within Y minutes" rule hardly tests anything at realistic volumes: a partly failed heater is never noticed and a total failure takes the full waiting time. With the expectation derived from physics, a weak heater is a finding, not a pass.
- **Evidence:** Observed on the reference installation (2026): with a 295 W heater in 20 L, the old rule (0.125 K within 20 min) asked for only about 3 % of the expected ~4 K. Measured passive loss: 4.73 W/K × (water − ambient) + 5.2 W evaporation from the open tank (R² 0.95 over 56 hourly windows); the heating rate itself was not yet validated. In simulated tests the new rule caught a total failure and a 10 % partial output at 20 L after 5 min, while 70 % output did not trip. A separate check ("switched on, below 50 W after 60 s") caught the most common hardware faults within a minute.
- **Implemented in:** docs/INVARIANTS.md (open, not in the prototype); heater roles refused via the catalog.
- **Tests:** tests/core/test_net.cpp – „Heizung: keine Heizrolle, bis die Notabschaltung … steht“


### RAT-062 – Switch-on lock vs. latch; latch only after an intervention, reason frozen

- **Rule:** A present condition (e.g. low level) only blocks switching on; this lock lifts itself and is not an alarm. A latch is set only when protection has to switch off an output that was running; it raises an alarm and holds until acknowledged. Reason and time are frozen when the latch is set. Acknowledging clears the latch even if the cause persists (the switch-on lock still applies); an output found running despite an unacknowledged latch is switched off again.
- **Why:** Latching while the device is already off (e.g. an empty tank between runs) produces permanent red states and acknowledgements that seem to do nothing; a reason rewritten every cycle no longer tells what triggered the shutdown.
- **Evidence:** Observed on the reference installation (2026): after the tank was pumped empty with pump and heater already off, both protections went into alarm and stayed there; the reason text was rewritten every cycle (4.4 → 3.2 → 1.6 → 0.1 → 0 L) and acknowledging had no effect because it was only accepted once the cause was gone. After the change, switch-on attempts at 0 L were cut within 84–138 ms and latched, and acknowledgements took effect within about 50 ms.
- **Implemented in:** core/src/dosing.cpp (`Actuators::enforce`), docs/CONFIGURATION.md (states `blocked` / `latched`), docs/INVARIANTS.md
- **Tests:** tests/core/test_scenarios.cpp – „Szenario: Trockenlauf – Umwälzpumpe aus, Rastung, Quittierung …“; tests/core/test_net.cpp – „Zulauf klemmt: nach Pegelausfall meldet die Notgrenze neu“ (frozen reason), „Umwälzpumpe: späterer Trockenlauf meldet neu, Rastung hält die Pumpe aus“, „Zulauf: Grund weg, Rastung steht, Ventil klemmt offen → Hub schaltet weiter aus“


### RAT-063 – Latches survive a restart

- **Rule:** Latches (with reason and time) and jump locks are kept in the runtime state, which is stored separately and survives a restart. After a restart all outputs are off, but an unacknowledged latch still blocks.
- **Why:** A restart (power failure, update) must not silently clear a protective shutdown nobody has looked at; otherwise, e.g., a pump that ran dry could be started again unattended.
- **Evidence:** Observed on the reference installation (2026): a latch of the circulation pump could be lost when the control logic restarted, while the heater latch was restored; restoring the heater latch reused the whole display text as the reason, so each further restart would have nested it one level deeper. A restored latch of unknown kind was treated as the stricter kind (released only by acknowledgement). Restoration was verified in simulated tests, not live.
- **Deviation:** On the reference installation a level-type pump latch released itself once the level was back above the release threshold; in the software every latch holds until acknowledged (formal decision pending).
- **Implemented in:** core/include/gc/config.hpp (`RuntimeState::latches`), docs/CONCEPT.md (R6), docs/INVARIANTS.md
- **Tests:** — (no dedicated test; persistence of jump locks in the same runtime state: tests/core/test_truth.cpp – „Sensorwahrheit: Sprungsperre, überlebt Neustart, frei nach 15 min Ruhe …“)


### RAT-064 – End-of-run summary is stored with the run

- **Rule:** Planned: at the end of a run, a summary (water, nutrient ml and cost, phase durations, yield entered by hand) is kept as part of that run's record; the yield can be added later.
- **Why:** Key figures must reflect the state at the end of the run, and data added later must not be lost when the next run starts.
- **Evidence:** Observed on the reference installation (2026): export figures were taken from the latest value in the whole database instead of the value at the end of the run, and the yield was kept in a setting that was reset when the next run started. The fix: read figures at the end of the run's window, and keep an immutable per-run record with later additions such as the yield stored separately.
- **Implemented in:** docs/HISTORY.md (section "Später", planned)
- **Tests:** —


### RAT-065 – Drain on EC step-down only when needed; always on product change

- **Rule:** If the EC target drops by more than 0.2 mS/cm at a phase change, the app suggests draining the tank completely. The drain is skipped when the remaining solution plus fresh water lies below the new target and can be dosed up; on a change of nutrient product the drain stays. Partial draining is not planned; the hub cannot dilute.
- **Why:** An unconditional drain discards usable solution and nutrients; mixing a new base product onto the old one is not wanted.
- **Evidence:** Calculated from the reference installation's figures (2026): at a step from EC 3.0 to 2.0, 13.4 L of residue plus 13.7 L of fresh water gives about EC 1.55, below the new target, so no drain is needed; the drain into late flowering stays because the diluted residue (0.84) is above the 0.8 target. One avoidable drain would have discarded 34.8 L of mixed solution.
- **Implemented in:** docs/PLANT_AUTOMATION.md (§5, planned)
- **Tests:** —


### RAT-066 – Phase defaults and recipe templates follow the manufacturer chart

- **Rule:** Phase defaults: 18 h light in the vegetative phase and 12 h in flowering; pH target 5.9 (vegetative) and 5.9–6.1 (flowering); EC from the manufacturer chart. Two recipe templates come from the Athena Blended Feed Program, Metric, A01.004: vegetative weeks 1–4 (parts A/B 2.9 ml/L each, CalMag 1.0 ml/L, EC 2.1, pH 5.8–6.2) and flowering weeks 1–2 (parts A/B 3.2 ml/L each, CalMag 1.0 ml/L, EC 2.3), to be checked against the current chart.
- **Why:** Self-chosen EC targets drift far from what the product is designed for; pH targets on the edge of the band end up outside it because control stops about ±0.1 around the target.
- **Evidence:** Measured on the reference installation (2026): the chart implies 0.615 mS/cm per ml/L per part, equal to the mixing-tank measurement (0.62) and 12 % above a soak-water measurement (0.55). Before alignment, EC targets up to 3.0 exceeded the chart maximum (2.6 with PK, 2.3 without), so from the start of flowering 1.3–3 times the manufacturer amount was dosed. Flowering week 1 starts with the switch to 12/12 light.
- **Implemented in:** catalog/catalog.json (templates `athena_blended_veg`, `athena_blended_bloom`), docs/PLANT_AUTOMATION.md (§5)
- **Tests:** tests/core/test_api.cpp – „Vorlagen: Zuordnung über Rollen, sonst über Namen, sonst Liste der Fehlenden“, „Vorlagen: Paarname schon vergeben → freier Name, A/B skalieren gemeinsam“; web/e2e/betrieb.spec.ts – „Rezept-Vorlage: Vorschau, Kanister zuordnen, Rezept anlegen“ (template handling, not the values)


### RAT-067 – Irrigation needs a minimum tank level

- **Rule:** The irrigation function carries a parameter "minimum level in the tank"; irrigation does not run below it.
- **Why:** Irrigating the tank down past the point where pumps, probes and the irrigation intake are covered leads to dry running and false readings.
- **Evidence:** Observed on the reference installation (2026): the limit equalled the circulation pump's dry-run threshold — 5 L in the original tank, 2.5 L in a 60-L drum where all submerged parts are covered from 2.5 L. A cycle that would have gone below it was skipped (not shortened, not made up), reported at most once a day, and an extra refill was requested.
- **Implemented in:** docs/PLANT_AUTOMATION.md (§4, function `irrigation`, planned); the output interlock already exists, see RAT-068
- **Tests:** —


### RAT-068 – Irrigation pump only with a valid level above the minimum

- **Rule:** The actuator gateway switches the irrigation pump on only with a valid level reading at least 0.5 L above the tank minimum. A running pump is switched off, with a message, if the level drops below the minimum or becomes invalid.
- **Why:** Otherwise the pump can run dry; an unreadable level cannot show that water is there.
- **Evidence:** Observed on the reference installation (2026): irrigation was blocked when the level minus the planned cycle volume fell below the minimum; with an unreadable level it irrigated and reported, judging a silent irrigation stop the greater risk for the plants.
- **Deviation:** The reference irrigated with an unreadable level; the software blocks. PD-031 confirms the block for an assigned but unreadable level sensor and adds a limited emergency dose when the plants are too dry, irrigation without any level sensor, and dry-run detection from the current draw (RAT-049); not implemented yet. The reference checked predictively (level minus cycle volume); the software checks the current level.
- **Implemented in:** core/src/dosing.cpp (`Actuators::inhibit`, `Actuators::enforce`), docs/INVARIANTS.md, docs/PLANT_AUTOMATION.md, CHANGELOG.md
- **Tests:** tests/core/test_net.cpp – „Gateway: Gießpumpe nur mit gültigem Füllstand, Befeuchter nur mit gültiger Feuchte“, „Gießpumpe: fällt der Füllstand im Lauf unter den Mindestfüllstand, geht sie aus“, „Gießpumpe: Füllstand wird im Lauf ungültig → aus, einmal gemeldet“


### RAT-069 – "Supply" profile for devices with their own thermostat

- **Rule:** Planned outlet profile "supply": the relay only powers a device that regulates itself (e.g. a heater with built-in thermostat). Off after a power failure, no device auto-off, but a power limit in the outlet as a net for a stuck thermostat; assessment by the watchdog.
- **Why:** An auto-off of X seconds would permanently cut such a device X seconds after every switch-on, although it is meant to stay powered; a stuck thermostat shows up as excessive power, not as excessive on-time of the relay.
- **Evidence:** Observed on the reference installation (2026): such a heater's relay stayed on for weeks while its thermostat cycled — 19 heating phases above 50 W in about four weeks, longest 6.8 min, median 0.3 min, at most 369 W. A power limit of about 450 W was proposed instead of the outlet's default 3,360 W.
- **Implemented in:** docs/PLANT_AUTOMATION.md (§2 open items, §3 profile table; not implemented)
- **Tests:** —


### RAT-070 – Calibration is not consumption; book actual run time per run

- **Rule:** Calibration and priming runs are not consumption and are not capped by the manual-dose limit. Flow rates are computed from the actual run time reported by the dosing block, not the requested time. Stock is booked per completed run, so an aborted order still books what has already run.
- **Why:** Treating a calibration run as a capped manual dose shortens it and books it as consumption; dividing the measured volume by the requested time then overstates the flow rate, and every later dose is too small. Booking only at the end loses what ran before an abort.
- **Evidence:** Observed on the reference installation (2026): a nominal 10-s calibration run was cut by the 5-ml manual limit after 5.7–7.8 s depending on the pump and booked as consumption; dividing the cup volume by 10 s would have overstated the rate by 1.3–1.8×. Consumption was booked only at the end of a mixing sequence, so amounts pumped before an external stop would have stayed unbooked (a gap found in review, not observed).
- **Implemented in:** core/include/gc/bus.hpp (`RunStatus::actualMs`), core/include/gc/dosing.hpp, core/src/dosing.cpp (`Doser::book`, `Doser::abort`), core/src/hub.cpp (`Hub::calibrationResult`), docs/INVARIANTS.md
- **Tests:** tests/core/test_scenarios.cpp – „Szenario Stufe 0: Einmessen und geführtes Mischen treffen die Mengen, A:B bleibt gekoppelt“ (no dose events from calibration), „Szenario: Abbruch bucht, was schon gelaufen ist …“


### RAT-071 – Minimum stock per canister: 150 ml, pH− 20 ml

- **Rule:** The watchdog reports a canister as a problem ("refill") when its known stock falls below 150 ml for nutrients and 20 ml for other liquids such as pH−; unknown stock is not judged. A forecast against the remaining demand of the run is not implemented yet.
- **Why:** The aim is not to refill during a run; a canister running empty silently breaks dosing. pH− is used in tiny amounts, so it needs its own, lower threshold.
- **Evidence:** Calculated from the reference installation's figures (2026): a full run needs about 30 ml of pH− (range 2–114 ml) against roughly 0.7–3.3 L per nutrient product.
- **Implemented in:** core/src/watchdog.cpp, docs/INVARIANTS.md
- **Tests:** —


### RAT-072 – Normal states are counted, only deviations are listed

- **Rule:** When nothing is wrong, the overview shows one line, "Alles in Ordnung (n Prüfungen)"; problems appear individually, and checks that rest for a reason are counted, not listed one by one.
- **Why:** Expected states shown as permanent individual lines bury the real problems and train users to ignore the overview.
- **Evidence:** Observed on the reference installation (2026): expected states such as "pump off – tank empty" appeared as permanent grey single lines, and 17 identical "sensor waiting" lines filled the view; folding normal states into one green count made deviations stand out.
- **Implemented in:** core/src/watchdog.cpp (headline), web/src/pages/overview.tsx, docs/UX.md (§1)
- **Tests:** —


### RAT-073 – Stale assessment is red; neutral when nothing can be judged

- **Rule:** A watchdog assessment older than 3 minutes counts as missing and is shown red ("no assessment"). Checks are neutral, with a reason, in maintenance mode, during the start-up grace period and when no target is set. Planned: "should be off but draws > 2 W" (runaway) on metered outlets, report only. Humidifier pulses are capped at 5 min on-time (assumption) until the climate function sets its own limits.
- **Why:** A crashed or hung assessment must not look like "all OK". A stuck relay cannot be switched off through itself, so a runaway can only be reported.
- **Evidence:** Observed on the reference installation (2026): over about 5,570 h of off-time on 10 outlets, no minute exceeded 0.5 W, and at 179 switch-offs under load the power fell to 0 within the same second — so 2 W separates "off" from "current flows" safely. In pulse mode the humidifier ran 89 times, the longest run 1.5 min.
- **Implemented in:** core/include/gc/watchdog.hpp (`kWatchStaleS`), core/src/hub.cpp, catalog/catalog.json (humidifier `maxOnS`), docs/INVARIANTS.md, docs/PLANT_AUTOMATION.md
- **Tests:** tests/core/test_watchdog.cpp – „Watchdog: neutral mit Grund – Pflegemodus, Anlauf, kein Ziel“; tests/core/test_api.cpp – „API: Zustand hat die Felder, die die Web-App liest (Vertrag)“ (`stale` field)


### RAT-074 – The watchdog only assesses; no path to actuators

- **Rule:** The watchdog rates conditions as OK, problem or neutral, always with a reason, and never switches anything. It sees only the read model and the configuration; a CI check fails if it includes bus, gateway, controller, sensor-truth or API headers.
- **Why:** Protection lives in the actuator gateway and reads sensor truth; keeping assessment separate means a monitoring bug or a disabled check can neither switch nor unblock an output.
- **Evidence:** — (design principle; the reference installation already separated assessment, which only wrote status and sent notifications, from switching)
- **Implemented in:** core/include/gc/watchdog.hpp, docs/CONCEPT.md (R2), tools/arch_check.sh
- **Tests:** tools/arch_check.sh, rule R2 (run in CI)


### RAT-075 – Sensor truth always runs

- **Rule:** Sensor truth (raw value → calibrated value → validity: freshness, frozen value, plausibility, jump lock, calibration) is updated every cycle, also with no run active and in maintenance mode. Only the watchdog's assessment goes neutral in those situations.
- **Why:** Dosing and switching interlocks read sensor truth; if validation paused, interlocks would act on unchecked or stale values exactly when the tank is often empty or being worked on.
- **Evidence:** Observed on the reference installation (2026): the tank typically stands empty between runs — precisely when dry-run and heater interlocks matter.
- **Implemented in:** core/include/gc/truth.hpp (`SensorTruth::update` every cycle), docs/INVARIANTS.md
- **Tests:** — (no dedicated test)


### RAT-076 – Phases provide parameters, never names

- **Rule:** A phase is a freely named set of parameters. Control, planning and assessment read only the effective parameters, never the phase name; a CI check rejects phase-name literals in the core logic.
- **Why:** Logic keyed to names breaks silently when users rename, add or translate phases; parameter-driven phases make behaviour explicit and configurable.
- **Evidence:** Observed on the reference installation (2026): several control and monitoring paths branched on specific phase names (e.g. the drying phase), so behaviour depended on naming; replacing this needed a rebuild.
- **Implemented in:** core/include/gc/config.hpp (`PhaseCfg`), `effectiveParams`, docs/CONCEPT.md (R4), tools/arch_check.sh (R4)
- **Tests:** tests/core/test_catalog_config.cpp – „Phasen liefern Parameter; der Name ändert nichts …“, „Konfiguration: Phasenparameter im Katalogbereich …“; tools/arch_check.sh, rule R4


### RAT-077 – Harvest is an event; "completed" is its own state

- **Rule:** Harvest is recorded as an event (timestamp and log entry) of a running run. Completing the run is a separate, explicit transition to the state "completed".
- **Why:** Treating harvest as a multi-day state mixes "plants cut" with "run still going" and leaves no clear end state for reports and for starting the next run.
- **Evidence:** Observed on the reference installation (2026): harvest was a multi-day state flag, and "run completed" had no state of its own — only a timestamp set at the end of the drying phase.
- **Implemented in:** core/include/gc/config.hpp (`harvestedAt`, run state `none | running | completed`), core/src/hub.cpp (harvest and complete handlers), docs/INVARIANTS.md
- **Tests:** —


### RAT-078 – Tank level: piecewise-linear curve and switch-on hysteresis

- **Rule:** The level is derived from the raw value through a piecewise-linear curve: valid from 2 points, raw values strictly rising by at least 3 mV, volumes not falling; below the first point the first value, above the last point extrapolated with the last slope. Outputs protected by the minimum level switch on again only 0.5 L above the switch-off threshold. The simulator models a level sensor that is non-linear at the bottom.
- **Why:** A level sensor can be non-linear near the bottom of a vessel; a two-point straight line misreads the volume exactly where the dry-run thresholds sit. Points too close together make the slope noise-dominated.
- **Evidence:** Measured on the reference installation (60-L drum, 2026): 1.9607 V = 0 L, 1.9871 V = 1.0 L, 2.0114 V = 2.0 L, 2.0198 V = 2.5 L, 2.1291 V = 10 L — segment slopes 37.9 / 41.2 / 59.5 / 68.6 L/V. A two-point line read 3.5 L at a real 2.5 L and 32.6 L at a real 36 L. Thresholds were set to 2.5 L off / 3 L on, since all submerged parts are covered from 2.5 L. A check at a real 10 L still read 0.4–0.5 L high (cause unresolved), so points should be re-checked.
- **Implemented in:** core/include/gc/truth.hpp (`Curve`), core/src/truth.cpp, core/src/dosing.cpp (`kInletHysteresisL`), sim/world.cpp, docs/CONCEPT.md, docs/SIMULATOR.md, docs/INVARIANTS.md
- **Tests:** tests/core/test_truth.cpp – „Kennlinie: stückweise linear, streng steigend …“


### RAT-079 – Inlet valve: 25 min maximum, emergency limit by level

- **Rule:** The inlet valve may stay open at most 25 min; the refill function limits more tightly (default 15 min). The outlet's own auto-off sits just above the software limit (28 min). A level-based emergency limit is still required, because the auto-off alone does not prevent overflow.
- **Why:** If the hub fails, the device must still close the valve; but within the auto-off time the valve can deliver more water than the tank holds.
- **Evidence:** Observed on the reference installation (2026): after switching to a 60-L drum (overflow at 55 L), the old emergency limit of 140 L was physically unreachable, and the inlet was bounded only by timers and a 30-min device auto-off — about 46 L per run. Limits were then set to 45 L, 25 min software cap and 25 min device auto-off.
- **Deviation:** The reference set the device auto-off equal to the software cap (25 min); the software places it slightly above (28 min, factor 1.11 rounded up to whole minutes), so the device only acts when the hub fails.
- **Implemented in:** catalog/catalog.json (`tank.inlet`, `maxOnS` 1500; refill `max_open_min`), core/src/dosing.cpp, CHANGELOG.md
- **Tests:** tests/core/test_net.cpp – „Steckdose: Profil puls setzt Auto-Off im Gerät, ohne bestätigtes Rücklesen keine Zuordnung“, „Steckdose: Auto-Off im Gerät greift, wenn der Hub ausfällt“, „Zulauf klemmt: nach Pegelausfall meldet die Notgrenze neu“ (mechanism, not the inlet values)


### RAT-080 – Mixing order is recipe data; CalMag EC effect

- **Rule:** The order of the mixing steps is part of the recipe and can be rearranged; the manufacturer templates put part B before part A, then CalMag. The simulator uses an EC effect of 0.217 mS/cm per ml/L for CalMag.
- **Why:** Manufacturers specify their own order (here part B before A, CalMag after the base, cleaner last); a fixed order in code cannot follow different products.
- **Evidence:** The Athena Blended Feed Program, Metric, A01.004 lists the order: Balance → base B → base A → PK → CaMg → Cleanse. Measured on the reference installation (2026): CalMag added 0.217 mS/cm per ml/L; the chart implies about 0.22.
- **Deviation:** The reference installation kept its own order (cleaner and pH buffer first, then CalMag, A, B) for its run, judging it uncritical with circulation and at least 240 s between additions, because changing it meant rewiring; the software follows the manufacturer order in its templates and makes the order editable.
- **Implemented in:** docs/INVARIANTS.md (M1), docs/SIMULATOR.md, catalog/catalog.json (template step order), sim/world.cpp
- **Tests:** — (no dedicated test for step order)


### RAT-081 – Start effects in nutrient solution are still unmeasured

- **Rule:** Until measured, pH and EC control start from conservative effect values (the largest measured pH− effect, i.e. the smallest dose) and learn the real effect from clean doses. The simulator lists the start effects in nutrient solution as an open measurement.
- **Why:** Overestimating an effect only makes the dose too small and costs extra rounds; underestimating it overdoses. The mixing-tank solution is buffered and expected to react more weakly than the soak water in which the effects were measured, so carrying those values over errs on the safe side.
- **Evidence:** Measured on the reference installation (2026): in soak water, pH− raised EC by 1.04 mS/cm per ml/L; for the nutrient solution in the mixing tank this and the other start effects were carried over unmeasured.
- **Implemented in:** docs/SIMULATOR.md (section "Limits"); start values used in core/src/control.cpp
- **Tests:** —


### RAT-082 – Simulator sensor noise from measured noise

- **Rule:** The simulator adds Gaussian noise to raw values: pH σ 0.006, EC σ 0.004 mS/cm, water temperature σ 0.02 °C, level 2 mV.
- **Why:** Simulator numbers must be measured or marked as assumptions; realistic noise shows that filters, jump locks and frozen-value detection do not trip on normal fluctuation.
- **Evidence:** Measured on the reference installation (2026; 7 days, 10-min windows, medians): pH high-frequency σ ≈ 0.006 (≈ 0.014 over 10 min); EC σ ≈ 0.0003–0.0004 mS/cm; water temperature ≈ 0.02 °C; tank level ≈ 0.1 L in a 150-L tank. Noise was computed on a fixed time grid; a log that only stores changes had overstated it by a factor of 1.6–4.
- **Deviation:** EC noise in the simulator is about ten times the measured value, a deliberately harsher test condition.
- **Implemented in:** docs/SIMULATOR.md, sim/world.cpp
- **Tests:** —


### RAT-083 – Light hours are a phase parameter, and phase guidance shows live values

- **Rule:** The light hours per day belong to each phase's parameters (typical defaults: 18 h in the vegetative phase, 12 h in flowering). Any guidance that explains a phase shows its duration and targets from these parameters, never as fixed numbers in the text. The planned values of a phase are kept apart from the active targets of the running phase.
- **Why:** Fixed numbers in help texts go stale as soon as the user changes a phase, and the guidance then contradicts what the controller does.
- **Evidence:** Observed on the reference installation (2026): the phase guide read duration and targets of all phases live from the phase profiles; while test durations of one day were set, it showed "1 day", which was intended to stay visible.
- **Implemented in:** docs/PLANT_AUTOMATION.md (`light_schedule` with `light_hours` as a phase parameter; planned)
- **Tests:** —


### RAT-084 – A skipped pH correction does not stop irrigation, but stays visible

- **Rule:** When a mix ends without pH correction (skipped, guard against over-correction, maximum attempts reached), irrigation is not blocked. The result of the mix names "pH not corrected" and the reason first and is shown as a warning, not as success. Every exit without correction carries its reason. Implemented today: every lock and skip is visible with its reason; the irrigation part is planned with the irrigation function.
- **Why:** For plants in substrate, drying out is the bigger risk than one batch with uncorrected pH; stopping irrigation would itself cause damage, and it would happen exactly when nobody is there. A skip that is not shown claims a clean run.
- **Evidence:** Observed on the reference installation (2026): the final step of the mix overwrote the reason of a skipped pH correction, so the status claimed a clean run; four different exits ended a mix without pH correction, and only one of them carried a reason.
- **Implemented in:** control line with checklist in the web app (`web/src/widgets.tsx`), events of `core/src/control.cpp`
- **Tests:** tests/core/test_scenarios.cpp "Szenario: Sprungsperre – während der Sperre keine pH-Gabe, Ereignis sichtbar"; web/e2e/betrieb.spec.ts "Sprungsperre der pH-Sonde wird sichtbar mit Grund"
