// SPDX-License-Identifier: AGPL-3.0-or-later
// English templates of the texts the hub sends (SD-032). One entry per line
// as {"key", "template"}: tools/i18n_keys.test.mjs reads this table and
// compares keys and placeholders with the German one in web/src/lang/msg.ts.
#include "gc/messages.hpp"

#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace gc {

namespace {

struct Entry {
  const char* key;
  const char* text;
};

// clang-format off
constexpr Entry kMessages[] = {
    // Controllers: state of every controller (control.cpp)
    {"ctl.stopped", "Blocked: emergency stop – tap Resume to continue"},
    {"ctl.maintenance", "Waiting: maintenance mode is on"},
    {"ctl.calibrating", "Waiting: probe is being calibrated"},

    // EC top-up
    {"ec.off", "Switched off in Functions"},
    {"ec.latched", "Needs release: EC didn't rise after two rounds – check pumps and bottles"},
    {"ec.settling", "Controlling: EC {from:2} → {target:2} · round {round} · waiting {left} for mixing"},
    {"ec.invalid", "Blocked: EC – {reason}"},
    {"ec.dosing", "Controlling: EC {from:2} → {target:2} · round {round} · dosing"},
    {"ec.dose_failed", "Blocked: {reason}"},
    {"ec.circ_failed", "Blocked: circulation pump did not start – round cancelled"},
    {"ec.wait_circ", "Controlling: circulation pump starting"},
    {"ec.no_recipe", "Blocked: no recipe yet – add one under Recipes & bottles"},
    {"ec.above", "Resting: EC {ec:2} mS/cm above target – add fresh water to lower it"},
    {"ec.ok", "Resting: EC on target ({ec:2} mS/cm)"},
    {"ec.cooldown", "Waiting: pausing before the next try"},
    {"ec.wait_job", "Waiting: a job is running"},
    {"ec.wait_refill", "Waiting: refilling"},
    {"ec.wait_ph", "Waiting: pH correction still running"},
    {"ec.no_circ", "Blocked: circulation pump can't run – no dosing without mixing"},
    {"ec.plan", "Blocked: {reason}"},
    {"ec.start", "Controlling: EC {from:2} → {target:2} · round {round}"},

    // pH correction
    {"ph.off", "Switched off in Functions"},
    {"ph.latched", "Needs release: pH didn't drop after two doses – check the pH− bottle and probe"},
    {"ph.settling", "Controlling: pH {from:2} → {target:2} · partial dose {n} of {max:0} · waiting {left} for mixing"},
    {"ph.invalid", "Blocked: pH – {reason}"},
    {"ph.dosing", "Controlling: pH {from:2} → {target:2} · partial dose {n} · dosing"},
    {"ph.start", "Controlling: pH {from:2} → {target:2} · partial dose {n} · {ml:1} ml"},
    {"ph.ec_invalid", "Blocked: no valid EC reading – pH correction needs one"},
    {"ph.gate", "Blocked: EC {ec:2} below {floor:2}: pH can't be measured like this, nutrients first"},
    {"ph.below", "Resting: pH {ph:2} below target – the hub only lowers pH, never raises it"},
    {"ph.ok", "Resting: pH on target ({ph:2})"},
    {"ph.cooldown", "Waiting: pausing before the next try"},
    {"ph.wait_ec", "Waiting: EC first – pH always comes last"},
    {"ph.wait_rest", "Waiting: rest after the EC dose, {left} left"},
    {"ph.wait_job", "Waiting: a job is running"},
    {"ph.wait_refill", "Waiting: refilling"},
    {"ph.no_down", "Blocked: no pH− bottle on a calibrated pump"},
    {"ph.no_circ", "Blocked: circulation pump can't run – no dosing without mixing"},
    {"ph.wait_circ", "Controlling: circulation pump starting"},
    {"ph.plan", "Blocked: {reason}"},
    {"ph.too_small", "Resting: correction too small to dose accurately ({ml:2} ml)"},
    {"ph.dose_failed", "Blocked: {reason}"},

    // Refill
    {"refill.off", "Switched off in Functions"},
    {"refill.stopped", "Needs release: the inlet was shut off – see the events for why"},
    {"refill.done", "Resting: refill done"},
    {"refill.filling", "Filling: {from:1} → {target:1} L · {left} left"},
    {"refill.latched", "Needs release: inlet shut off after a fault – check the inlet"},
    {"refill.level", "Blocked: level – {reason}"},
    {"refill.ok", "Resting: level {level:1} L"},
    {"refill.cooldown", "Waiting: pause after the last refill"},
    {"refill.inhibit", "Blocked: {reason}"},
    {"refill.flow", "Blocked: inlet flow rate not set – set it under Functions"},
    {"refill.dosing", "Waiting: refill starts after dosing and mixing"},
    {"refill.near", "Resting: target almost reached"},
    {"refill.open", "Blocked: {reason}"},
    {"refill.start", "Filling: adding {litres:1} L"},

    // Circulation
    {"circ.unbound", "No circulation pump assigned"},
    {"circ.latched", "{reason}"},
    {"circ.blocked", "Blocked: {reason}"},
    {"circ.on_dosing", "Running (while dosing)"},
    {"circ.on_always", "Running (always on)"},
    {"circ.on_interval", "Running (interval)"},
    {"circ.idle", "Resting until the next interval"},
    {"circ.off", "Off – runs only while dosing"},

    // Checklists under a controller row
    {"check.ec_ok", "EC probe gives valid readings"},
    {"check.ec_bad", "EC: {reason}"},
    {"check.recipe", "Recipe: {name}"},
    {"check.no_recipe", "No recipe yet"},
    {"check.circ_free", "Circulation pump can run"},
    {"check.circ_blocked", "Circulation pump can't run"},
    {"check.no_latch", "No release needed"},
    {"check.job_running", "A job is running"},
    {"check.no_job", "No other job running"},
    {"check.ph_ok", "pH probe gives valid readings"},
    {"check.ph_bad", "pH: {reason}"},
    {"check.gate_ok", "EC {ec:2} above {floor:2}: pH can be measured"},
    {"check.gate_low", "EC {ec:2} below {floor:2}: pH can't be measured like this, nutrients first"},
    {"check.rest_over", "Rest time after the EC dose is over"},
    {"check.rest_left", "Rest time after the EC dose: {left} left"},
    {"check.ec_first", "EC top-up still running"},
    {"check.ec_done", "EC top-up done"},
    {"check.ph_down", "pH− bottle ready: {name}"},
    {"check.no_ph_down", "No pH− bottle on a calibrated pump"},
    {"check.level_ok", "Level reading valid"},
    {"check.level_bad", "Level: {reason}"},
    {"check.inlet_free", "Inlet can open"},
    {"check.dry_ok", "Dry-run protection OK"},

    // Monitoring: headline, labels and assessments (watchdog.cpp, hub.cpp)
    {"watch.headline.problem", "1 problem"},
    {"watch.headline.problems", "{n} problems"},
    {"watch.headline.ok", "Everything OK ({n} checks)"},
    {"watch.headline.ok_one", "Everything OK (1 check)"},
    {"watch.headline.none", "Nothing to check yet"},
    {"watch.maintenance", "Maintenance mode – readings aren't assessed"},
    {"watch.startup", "Starting after a restart"},
    {"watch.stop.label", "Emergency stop"},
    {"watch.stop", "Emergency stop active – all automation is paused"},
    {"watch.devices.label", "Devices"},
    {"watch.devices.none", "No devices set up yet"},
    {"watch.devices.ok", "All devices respond"},
    {"watch.devices.offline", "Not responding: {names}"},
    {"watch.reading.ok", "Valid"},
    {"watch.reading.uncalibrated", "Not calibrated"},
    {"watch.reading.failed", "No valid reading: {reason}"},
    {"watch.band.ph.label", "pH limits"},
    {"watch.band.ec.label", "EC limits"},
    {"watch.band.off", "Not checked – control is off"},
    {"watch.band.no_value", "No valid reading – see above"},
    {"watch.band.ok", "Within {lo:2}–{hi:2}"},
    {"watch.band.out", "{value:2} outside {lo:2}–{hi:2}"},
    {"watch.water.label", "Water temperature"},
    {"watch.water.off", "Not checked – switched off"},
    {"watch.water.empty", "Not applicable: tank empty"},
    {"watch.water.ok", "{value:1} °C – within limits"},
    {"watch.water.out", "{value:1} °C – outside {lo:1}–{hi:1} °C"},
    {"watch.level.label", "Level"},
    {"watch.level.ok", "{value:1} L"},
    {"watch.level.low", "{value:1} L below {min:1} L"},
    {"watch.stock.label", "{name} stock"},
    {"watch.stock.low", "{ml:0} ml left – change the bottle soon"},
    {"watch.calib.label", "Calibrate {name}"},
    {"watch.calib.missing", "Pump not calibrated – it won't dose"},
    {"watch.ctl.ec", "EC top-up"},
    {"watch.ctl.ph", "pH correction"},
    {"watch.ctl.refill", "Tank refill"},
    {"watch.ctl.circulation", "Circulation"},
    // Building blocks
    {"ev.plain", "{text}"},
    {"amount", "{name} {ml:1} ml"},
    {"span.s", "{n} s"},
    {"span.min", "{n} min"},
    {"span.h", "{h:1} h"},
    {"where.slot", "Pump {n} on the dosing block"},
    {"where.port", "Port {n}"},
    {"where.net", "On your Wi-Fi"},
    {"where.hub", "Built into the hub"},
    {"where.ha", "In Home Assistant"},
    {"device.unnamed", "Unknown device"},
    {"net.not_confirmed", "not confirmed in the device"},
    {"reading.ph", "pH {value:2}"},
    {"reading.ec", "EC {value:2} mS/cm"},

    // Events: start, configuration, system (hub.cpp, api.cpp)
    {"ev.config_unreadable", "Settings unreadable"},
    {"ev.config_unreadable.text", "Started with factory settings. Copy saved as config.broken.json. Restore a backup under Settings › Data. Reason: {reason}"},
    {"ev.state_unreadable", "Saved state unreadable"},
    {"ev.state_unreadable.text", "Locks and stock levels were lost – check the tank and bottles. Everything stays off until you tap \"Resume\". Copy saved as state.broken.json. Reason: {reason}"},
    {"reason.not_json", "not valid JSON"},
    {"reason.not_json_object", "not a valid JSON object"},
    {"ev.credentials_lost", "Sign-in data missing"},
    {"ev.credentials_lost.text", "The password can't be read any more. Changes over the network are locked – reset the hub to factory settings on the device itself."},
    {"ev.stop_kept", "Emergency stop still active"},
    {"ev.stop_kept.text", "After the restart everything stays off, fans included, until you tap \"Resume\"."},
    {"ev.mix_reboot", "Mix interrupted by a restart"},
    {"ev.job_reboot", "Job interrupted by a restart"},
    {"ev.reboot.text", "Stopped at step {step} of {total}. In the tank: {done}. Not resumed automatically."},
    {"ev.reboot.text_none", "Stopped at step {step} of {total}. Nothing went into the tank. Not resumed automatically."},
    {"ev.started", "Hub started"},
    {"ev.started.text", "Version {version}"},
    {"ev.config.text", "Settings revision {rev}"},
    {"ev.tick_ok", "Control running again"},
    {"ev.tick_ok.text", "Automation is running without errors again."},
    {"ev.tick_fault", "Internal error – pumps and outputs off"},
    {"ev.tick_fault.text", "The hub caught an error and switched off all pumps and outputs. Fans keep running. Reason: {reason}"},
    {"ev.clock.lost", "Clock no longer synced"},
    {"ev.clock.lost.text", "The hub keeps counting from the last synced time."},
    {"ev.clock.secured", "Clock synced"},
    {"ev.clock.first", "Network time received. Earlier times in the history are wrong."},
    {"ev.clock.forward", "The clock moved forward by {span} – it was behind by that much."},
    {"ev.clock.back", "The clock moved back by {span}."},
    {"ev.clock.received", "Network time received."},
    {"ev.clock.set", "Clock set"},
    {"ev.clock.set_forward", "The clock moved forward by {span}."},
    {"ev.clock.unsecured", "Clock not synced"},
    {"ev.clock.continued", "No network time. The hub keeps counting from the last saved time, so it is behind by the length of the outage."},
    {"ev.clock.unset", "No network time and no saved time. Times in the history are only correct once the clock is synced."},
    {"ev.stop", "Emergency stop"},
    {"ev.stop.text", "All pumps and outputs off. Triggered by: {who}"},
    {"stop.app", "STOP button in the app"},
    {"ev.resumed", "Automation resumed"},
    {"ev.resumed.text", "Emergency stop lifted"},
    {"ev.maintenance", "Maintenance mode started"},
    {"ev.maintenance.text", "Automation pauses for {min:0} min. Locks and sensor checks stay active."},
    {"ev.maintenance_end", "Maintenance mode ended"},
    {"ev.auth.password_set", "Password set"},
    {"ev.auth.first_setup", "During initial setup"},
    {"ev.auth.failed", "Sign-in failed"},
    {"ev.auth.signed_in", "Signed in"},
    {"ev.auth.password_changed", "Password changed"},
    {"ev.auth.password_changed.text", "Signed out on all devices"},
    {"ev.update.requested", "Update requested"},
    {"ev.update.rollback", "Previous version requested"},

    // Events: configuration changes
    {"ev.cfg.setup_done", "Setup complete"},
    {"ev.cfg.system", "System settings saved"},
    {"ev.cfg.device_added", "Device added: {name}"},
    {"ev.cfg.device_renamed", "Device renamed: {name}"},
    {"ev.cfg.device_removed", "Device removed: {name}"},
    {"ev.cfg.role", "Assignment saved: {label}"},
    {"ev.cfg.role_removed", "Assignment removed: {role}"},
    {"ev.cfg.tank", "Tank saved: {name}"},
    {"ev.cfg.zone", "Area saved: {name}"},
    {"ev.cfg.canister", "Bottle saved: {name}"},
    {"ev.cfg.canister_removed", "Bottle removed: {name}"},
    {"ev.cfg.recipe", "Recipe saved: {name}"},
    {"ev.cfg.recipe_removed", "Recipe removed: {name}"},
    {"ev.cfg.pair_template", "Bottles paired from the template"},
    {"ev.cfg.imported", "Settings restored"},
    {"ev.cfg.calibration", "{kind} calibration saved: {name}"},
    {"ev.cfg.function_on", "{label} switched on"},
    {"ev.cfg.function_off", "{label} switched off"},
    {"ev.cfg.function_params", "{label}: settings saved"},

    // Events: devices, sockets, locks
    {"ev.device.found", "{label} detected"},
    {"ev.device.back", "{label} is back"},
    {"ev.device.where", "{where}"},
    {"ev.device.where_pump", "{where}. Is it still on the bottle \"{name}\"?"},
    {"ev.device.lost", "{name} not responding"},
    {"ev.device.lost.text", "Check its cable, power or Wi-Fi."},
    {"ev.jump", "Jump lock: {label}"},
    {"ev.jump_cleared", "Jump lock lifted: {label}"},
    {"ev.jump_cleared.text", "Steady for 15 min – the reading counts again."},
    {"ev.latch_released", "Lock released"},
    {"latch.ec.no_effect", "EC doses have no effect"},
    {"latch.ph.no_effect", "pH doses have no effect"},
    {"latch.circulation.dry", "Dry run"},
    {"latch.inlet.fault", "Inlet emergency cut-off"},
    {"ev.net.unprotected", "{name}: safety setting not saved"},
    {"ev.net.protected", "{label}: safety setting saved"},
    {"ev.net.power_on", "On after a power cut"},
    {"ev.net.power_off", "Off after a power cut"},
    {"ev.net.power_on_auto", "On after a power cut; the plug switches itself off after {min:0} min at the latest"},
    {"ev.net.power_off_auto", "Off after a power cut; the plug switches itself off after {min:0} min at the latest"},
    {"ev.net.stays_on", "{label}: plug may come back on"},
    {"ev.net.stays_on.text", "Setting it back to \"off after a power cut\" failed: {reason}. Change it in the smart plug itself."},
    {"ev.net.on_not_set", "{label}: stays off after a power cut"},
    {"ev.net.on_not_set.text", "Setting \"on after a power cut\" failed: {reason}. Assign it again under Devices › Assignment."},
    {"ev.net.off_not_set", "{label}: may come back on"},
    {"ev.net.off_not_set.text", "Setting \"off after a power cut\" failed: {reason}. It may switch on by itself after a power cut – unplug it if it must stay off."},
    {"ev.net.import_unconfirmed", "{label}: setting not confirmed"},
    {"ev.net.import_unconfirmed.text", "Its safety setting wasn't confirmed after restoring settings. The hub won't switch it on until it is. Assign it again under Devices › Assignment."},
    {"ev.net.import_unconfirmed.text_reason", "Its safety setting wasn't confirmed after restoring settings ({reason}). The hub won't switch it on until it is. Assign it again under Devices › Assignment."},
    {"ev.off_unconfirmed", "{label}: may still be on"},
    {"ev.off_unconfirmed.device_removed", "Switching off failed after removing the device: {reason}. Check it and unplug it if needed."},
    {"ev.off_unconfirmed.old_output", "Switching off the previous output failed: {reason}. Check it and unplug it if needed."},
    {"ev.off_unconfirmed.unassigned", "Switching off failed after removing the assignment: {reason}. Check it and unplug it if needed."},
    {"ev.manual_on", "{label} on"},
    {"ev.manual_off", "{label} off"},
    {"ev.manual.text", "By hand"},
    {"ev.bottle_changed", "Bottle changed"},
    {"ev.bottle_changed.text", "{name}: {ml:0} ml"},

    // Events: control (control.cpp)
    {"ev.ec.no_effect", "EC doses have no effect"},
    {"ev.ec.no_effect.text", "EC didn't rise after two rounds. Check pumps and bottles, then tap \"Release\" under Tank & control."},
    {"ev.ec.done", "EC topped up"},
    {"ev.ec.max", "EC top-up: round limit reached"},
    {"ev.ec.rounds_one", "EC {from:2} → {to:2} mS/cm in {n} round"},
    {"ev.ec.rounds", "EC {from:2} → {to:2} mS/cm in {n} rounds"},
    {"ev.ec.round_cancelled", "EC round cancelled"},
    {"ev.ec.round_cancelled.text", "The circulation pump didn't start. No dosing without mixing."},
    {"ev.ec.aborted", "EC top-up interrupted"},
    {"ev.ph.no_effect", "pH doses have no effect"},
    {"ev.ph.no_effect.text", "pH didn't drop after two doses. Check the pH− bottle and probe, then tap \"Release\" under Tank & control."},
    {"ev.ph.done", "pH corrected"},
    {"ev.ph.max", "pH correction: dose limit reached"},
    {"ev.ph.doses_one", "pH {from:2} → {to:2} in {n} dose"},
    {"ev.ph.doses", "pH {from:2} → {to:2} in {n} doses"},
    {"ev.ph.aborted", "pH correction interrupted"},
    {"ev.refilled", "Refilled"},
    {"ev.refilled.text", "Added {measured:1} L (planned {planned:1} L)"},

    // Events and messages of jobs: mixing, manual doses, calibration
    {"ev.mix.started", "Mix started"},
    {"ev.mix.started.topup", "{recipe} · top up with {water:1} L fresh water"},
    {"ev.mix.started.new", "{recipe} · new batch, {water:1} L"},
    {"ev.mix.interrupted", "Mix interrupted"},
    {"ev.mix.aborted", "Mix cancelled"},
    {"ev.job.failed", "Job failed"},
    {"ev.mix.done", "Mix finished"},
    {"ev.mix.done.text", "{water:1} L \"{recipe}\": {summary}. {after}"},
    {"ev.mix.resumed", "Mix resumed"},
    {"ev.mix.resumed.text", "Completing {name}"},
    {"ev.job.aborted", "Job cancelled"},
    {"ev.contents", "In the tank: {done}"},
    {"ev.contents_none", "Nothing went into the tank"},
    {"ev.manual_pair", "Manual dose from a pair"},
    {"ev.manual_pair.text", "{name} is part of pair {pair}. Dose its partner in the same ratio too, or the mix will be off."},
    {"ev.pump_calibrated", "Pump calibrated"},
    {"ev.pump_calibrated.text", "{name}: {flow:1} ml/min, saved in the pump."},
    {"ev.pump_calibrated.text_changed", "{name}: {flow:1} ml/min, saved in the pump. Clearly different from before ({prev:1} ml/min) – check the tubing."},
    {"ev.probe_calibrated", "Sensor calibrated"},
    {"ev.probe_calibrated.text", "{name} ({kind})"},
    {"kind.ph", "pH"},
    {"kind.ec", "EC"},
    {"kind.tank_curve", "Level"},
    {"ev.manual_reading", "Manual reading saved"},
    {"ev.manual_reading.text", "{values}"},
    {"ev.grow.started", "Grow cycle started"},
    {"ev.grow.started.text", "{name} · phase \"{phase}\" (day 1)"},
    {"ev.grow.phase", "Phase changed"},
    {"ev.grow.phase.text", "Now in phase \"{phase}\""},
    {"ev.grow.harvest", "Harvest recorded"},
    {"ev.grow.finished", "Grow cycle finished"},
    {"job.reboot", "Interrupted by a restart – not resumed"},
    {"job.internal", "Stopped by an internal error. In the tank: {done}"},
    {"job.internal_none", "Stopped by an internal error. Nothing went into the tank."},
    {"job.start_failed", "{name}: {reason}"},
    {"job.dosing_step", "Step {n} of {total}: {name} · {ml:1} ml"},
    {"job.dose_failed", "{name} was not fully dosed ({reason})."},
    {"job.pair_failed", "{name} was not fully dosed ({reason}). {partner} is already in the tank. Fix the cause, then tap \"Complete {name}\" – otherwise the mix will be off."},
    {"job.done", "{name}: {ml:1} ml"},
    {"job.aborted", "Cancelled. In the tank: {done}"},
    {"job.aborted_none", "Cancelled. Nothing went into the tank."},
    {"job.emergency_stop", "Cancelled by the emergency stop"},
    {"mix.done", "Done: {water:1} L \"{recipe}\". {after}"},
    {"mix.after_auto", "pH comes last: pH correction takes over after mixing."},
    {"mix.after_manual", "pH comes last: measure pH by hand now and enter it."},
    {"mix.circulate", "Mixing with the circulation pump (1 min)"},
    {"mix.stir", "Added: {name} {ml:1} ml. Now stir for 1 minute, then tap \"Continue\"."},
    {"cal.running", "Pump is running into the measuring cup for {s:0} s …"},
    {"cal.measure", "How much is in the measuring cup? Enter the amount in ml."},
    {"cal.done", "Saved in the pump: {flow:1} ml/min. The value stays when you replug the pump."},
    {"cal.done_changed", "Saved in the pump: {flow:1} ml/min – clearly different from before ({prev:1} ml/min). Check the tubing for kinks or air; if you find any, fix it and calibrate again."},

    // Actuator gateway: why an output or pump may not run (dosing.cpp)
    {"act.stopped", "Emergency stop is on – tap Resume first"},
    {"act.busy", "A pump is already running"},
    {"act.offline", "Pump not responding – check that it's plugged into the dosing block"},
    {"act.fault", "Pump reports a fault: {fault}"},
    {"act.uncalibrated", "Pump not calibrated – calibrate it under Devices first"},
    {"act.too_short", "Pump run under {s:1} s is too inaccurate – choose a larger amount"},
    {"act.too_long", "Pump run over {s:0} s is not allowed – dose a smaller amount"},
    {"act.no_mixing", "No dosing without mixing – the circulation pump isn't confirmed running"},
    {"act.bus", "Dosing block refuses: {error}"},
    {"act.output_refused", "Device didn't switch ({error})"},
    {"act.circ.latched", "Needs release: circulation pump off after a dry run – fill the tank"},
    {"act.circ.level_invalid", "No valid level reading – circulation pump stays off"},
    {"act.circ.low", "Level {level:1} L – the circulation pump only runs from {min:1} L"},
    {"act.inlet.latched", "Needs release: inlet shut off after a fault – check the inlet"},
    {"act.inlet.level", "No valid level reading – inlet stays closed so the tank can't overflow"},
    {"act.inlet.full", "Tank is full"},
    {"act.inlet.capacity", "Usable volume not set – set it under Tank & control"},
    {"act.climate.pair_unknown", "{label} not responding – it might still be running, so this one stays off"},
    {"act.climate.pair", "{label} is running – switch it off first; the two never run together"},
    {"act.humidifier.rh", "No valid humidity reading – humidifier stays off"},
    {"act.humidifier.rh_high", "Humidity {rh:0}% – the humidifier only runs below {max:0}%"},
    {"act.irrigation.level", "No valid level reading – watering pump stays off so it can't run dry"},
    {"act.irrigation.min_missing", "Minimum level not set – set it under Tank & control"},
    {"act.irrigation.low", "Level {level:1} L too low for the watering pump"},
    {"act.compressor.pause", "{label}: minimum pause of {min:0} min after switching off"},
    {"act.unbound", "Not assigned yet – assign it under Devices › Assignment"},
    {"act.net.unreachable", "Smart plug not responding – check its power and your Wi-Fi"},
    {"act.net.safety", "The plug's safety setting is missing or differs – assign it again under Devices › Assignment"},

    // Doser: running doses and why they ended
    {"dose.busy", "A dose is already running"},
    {"dose.empty", "Nothing to dose"},
    {"dose.no_response", "{name}: dosing block not responding – pump stopped, amount counted as dosed"},
    {"dose.lost", "{name}: {reason} – amount estimated from the run time"},
    {"dose.disconnected", "Pump disconnected"},
    {"dose.failed", "{name}: {reason}"},
    {"dose.run_cancelled", "Pump stopped early"},
    {"dose.aborted", "{name}: {reason}"},
    {"abort.auto_off", "Automation paused"},
    {"abort.calibrating", "Probe is being calibrated"},
    {"abort.internal", "Internal error"},
    {"abort.job", "Job cancelled"},
    {"abort.stop", "Emergency stop"},
    {"purpose.mix", "Mix"},
    {"purpose.manual", "Manual dose"},
    {"purpose.ec", "EC top-up"},
    {"purpose.ph", "pH correction"},
    {"purpose.calibration", "Calibration"},
    {"purpose.prime", "Prime tubing"},
    {"purpose.other", "Dosing"},

    // Events of the gateway: outputs switched, protective cut-offs
    {"ev.dose", "{name} · {ml:1} ml"},
    {"ev.dose_incomplete", "{name} · {ml:1} ml (incomplete)"},
    {"ev.inlet.open", "Inlet opened"},
    {"ev.inlet.closed", "Inlet closed"},
    {"who.refill", "Tank refill"},
    {"who.refill_done", "Tank refill done"},
    {"who.refill_off", "Tank refill switched off in Functions"},
    {"who.circulation", "Circulation"},
    {"who.manual", "By hand"},
    {"who.test", "Test button"},
    {"who.device_removed", "Device removed"},
    {"who.role_changed", "Assignment changed"},
    {"who.role_removed", "Assignment removed"},
    {"ev.off_failed.text", "Switching off failed: {error}. Check it and unplug it if needed. Reason: {reason}"},
    {"ev.off_confirmed", "{label}: now off"},
    {"ev.off_confirmed.text", "Switching off is confirmed."},
    {"ev.circ.dry", "Circulation pump off: dry run"},
    {"ev.circ.dry.low", "Level {level:1} L below {min:1} L. Fill the tank, then tap \"Release\" under Tank & control."},
    {"ev.circ.dry.latched", "Still locked after a dry run. Check the tank, then tap \"Release\" under Tank & control."},
    {"ev.circ.off", "Circulation pump off"},
    {"ev.circ.off.text", "No valid level reading: {reason}"},
    {"why.level_invalid", "No valid level reading"},
    {"why.capacity", "Emergency limit reached: tank full at {level:1} L"},
    {"why.open_too_long", "Inlet open for more than {min:0} min"},
    {"why.irrigation_low", "Level {level:1} L below the minimum level"},
    {"ev.inlet.cutoff", "Inlet emergency cut-off"},
    {"ev.inlet.cutoff.text", "{why}. Check the inlet and level sensor, then tap \"Release\" under Tank & control."},
    {"ev.inlet.cutoff.latched", "Still locked: {why}. Check the inlet, then tap \"Release\" under Tank & control."},
    {"ev.irrigation.off", "Watering pump off: dry-run protection"},
    {"ev.irrigation.off.text", "{why}."},
    {"ev.max_on", "{label} off: maximum run time"},
    {"ev.max_on.text", "Maximum run time of {min:0} min reached."},
};
// clang-format on

const std::unordered_map<std::string_view, std::string_view>& table() {
  static const std::unordered_map<std::string_view, std::string_view> t = [] {
    std::unordered_map<std::string_view, std::string_view> m;
    for (const auto& e : kMessages) m.emplace(e.key, e.text);
    return m;
  }();
  return t;
}

// Rounds the exact value half away from zero, like toFixed in the web app,
// so both languages show the same digits (20.25 → 20.3, 20.65 → 20.6).
std::string number(double v, int decimals) {
  if (!isNum(v)) return "–";
  char buf[352];
  if (decimals >= 0) {
    // printf rounds the exact value; only an exact tie (20.25 at one
    // decimal) goes to even there, so a tie is moved a quarter unit away
    // from zero first.
    std::snprintf(buf, sizeof buf, "%.60f", std::fabs(v));
    const std::string exact(buf);
    const size_t next = exact.find('.') + 1 + static_cast<size_t>(decimals);
    const bool tie = next < exact.size() && exact[next] == '5' && exact.find_first_not_of('0', next + 1) == std::string::npos;
    const double a = tie ? std::fabs(v) + 0.25 * std::pow(10.0, -decimals) : std::fabs(v);
    std::snprintf(buf, sizeof buf, "%.*f", decimals, a);
    std::string s(buf);
    const bool zero = s.find_first_not_of("0.") == std::string::npos;
    return v < 0 && !zero ? "-" + s : s;
  }
  std::snprintf(buf, sizeof buf, "%.6f", v);
  std::string s(buf);
  if (s.find('.') != std::string::npos) {
    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.') s.pop_back();
  }
  return s == "-0" ? "0" : s;
}

std::string value(const json& args, const std::string& name, int decimals) {
  if (!args.is_object()) return "–";
  auto it = args.find(name);
  if (it == args.end() || it->is_null()) return "–";
  if (it->is_number()) return number(it->get<double>(), decimals);
  if (it->is_string()) return it->get<std::string>();
  if (it->is_object()) {  // a nested message; an empty one is missing, as in the web app
    const std::string text = jstr(*it, "text");
    return text.empty() ? "–" : text;
  }
  if (it->is_array()) {  // a list, e.g. amounts: "Part A 12.0 ml, Part B 8.0 ml"
    std::string out;
    for (size_t i = 0; i < it->size(); ++i) {
      if (i) out += ", ";
      out += value(json{{"v", (*it)[i]}}, "v", decimals);
    }
    return out.empty() ? "–" : out;
  }
  return "–";
}

bool nameChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

}  // namespace

std::string render(std::string_view tmpl, const json& args) {
  std::string out;
  size_t i = 0;
  while (i < tmpl.size()) {
    if (tmpl[i] == '{') {
      size_t j = i + 1;
      while (j < tmpl.size() && nameChar(tmpl[j])) ++j;
      int decimals = -1;
      size_t end = j;
      if (end < tmpl.size() && tmpl[end] == ':' && end + 1 < tmpl.size() && tmpl[end + 1] >= '0' && tmpl[end + 1] <= '9') {
        decimals = tmpl[end + 1] - '0';
        end += 2;
      }
      if (j > i + 1 && end < tmpl.size() && tmpl[end] == '}') {
        out += value(args, std::string(tmpl.substr(i + 1, j - i - 1)), decimals);
        i = end + 1;
        continue;
      }
    }
    out += tmpl[i++];
  }
  return out;
}

bool knownMessage(std::string_view key) { return table().count(key) > 0; }

Msg say(std::string_view key, json args) {
  auto it = table().find(key);
  std::string text = it == table().end() ? std::string(key) : render(it->second, args);
  return {std::string(key), std::move(text), std::move(args)};
}

}  // namespace gc
