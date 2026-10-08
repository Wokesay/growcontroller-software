// SPDX-License-Identifier: AGPL-3.0-or-later
// English templates of the texts the hub sends (SD-032). One entry per line
// as {"key", "template"}: tools/i18n_keys.test.mjs reads this table and
// compares keys and placeholders with the German one in web/src/lang/msg.ts.
#include "gc/messages.hpp"

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
    {"ctl.stopped", "Blocked: emergency stop active"},
    {"ctl.maintenance", "Resting: maintenance mode"},
    {"ctl.calibrating", "Waiting: probe is being calibrated"},

    // EC top-up
    {"ec.off", "Off"},
    {"ec.latched", "Needs release: EC did not rise after two rounds – check pumps and bottles, then release"},
    {"ec.settling", "Controlling: EC {from:2} → {target:2} · round {round} · waiting {left} for mixing"},
    {"ec.invalid", "Blocked: EC – {reason}"},
    {"ec.dosing", "Controlling: EC {from:2} → {target:2} · round {round} · dosing"},
    {"ec.dose_failed", "Blocked: {reason}"},
    {"ec.circ_failed", "Blocked: circulation pump did not start – round cancelled"},
    {"ec.wait_circ", "Controlling: circulation pump starting"},
    {"ec.no_recipe", "Blocked: no recipe selected"},
    {"ec.above", "Resting: EC {ec:2} above target – only fresh water can lower it"},
    {"ec.ok", "Resting: EC on target ({ec:2} mS/cm)"},
    {"ec.cooldown", "Waiting: next try after a pause"},
    {"ec.wait_job", "Waiting: a job is running"},
    {"ec.wait_refill", "Waiting: refilling"},
    {"ec.wait_ph", "Waiting: pH correction still running"},
    {"ec.no_circ", "Blocked: circulation pump blocked – no dosing without mixing"},
    {"ec.plan", "Blocked: {reason}"},
    {"ec.start", "Controlling: EC {from:2} → {target:2} · round {round}"},

    // pH correction
    {"ph.off", "Off"},
    {"ph.latched", "Needs release: pH did not move after two doses – check bottles and probe, then release"},
    {"ph.settling", "Controlling: pH {from:2} → {target:2} · partial dose {n} of {max:0} · waiting {left} for mixing"},
    {"ph.invalid", "Blocked: pH – {reason}"},
    {"ph.dosing", "Controlling: pH {from:2} → {target:2} · partial dose {n} · dosing"},
    {"ph.start", "Controlling: pH {from:2} → {target:2} · partial dose {n} · {ml:1} ml"},
    {"ph.ec_invalid", "Blocked: EC invalid – pH control blocked"},
    {"ph.gate", "Blocked: EC {ec:2} below {floor:2}: pH can't be measured like this, nutrients first"},
    {"ph.below", "Resting: pH {ph:2} below target – pH+ is not provided, only lowering"},
    {"ph.ok", "Resting: pH on target ({ph:2})"},
    {"ph.cooldown", "Waiting: next try after a pause"},
    {"ph.wait_ec", "Waiting: EC first, pH is always the last step"},
    {"ph.wait_rest", "Waiting: rest time after an EC dose, {left} left"},
    {"ph.wait_job", "Waiting: a job is running"},
    {"ph.wait_refill", "Waiting: refilling"},
    {"ph.no_down", "Blocked: no calibrated pH− assigned"},
    {"ph.no_circ", "Blocked: circulation pump blocked – no dosing without mixing"},
    {"ph.wait_circ", "Controlling: circulation pump starting"},
    {"ph.plan", "Blocked: {reason}"},
    {"ph.too_small", "Resting: the correction would be smaller than an accurate pump run ({ml:2} ml)"},
    {"ph.dose_failed", "Blocked: {reason}"},

    // Refill
    {"refill.off", "Off"},
    {"refill.stopped", "Needs release: the inlet was switched off – check the events"},
    {"refill.done", "Resting: refilled"},
    {"refill.filling", "Filling: {from:1} → {target:1} L · {left} left"},
    {"refill.latched", "Needs release: inlet blocked after a fault – check, then release"},
    {"refill.level", "Blocked: level – {reason}"},
    {"refill.ok", "Resting: level {level:1} L"},
    {"refill.cooldown", "Waiting: pause after the last refill"},
    {"refill.inhibit", "Blocked: {reason}"},
    {"refill.flow", "Blocked: inlet flow rate missing"},
    {"refill.dosing", "Waiting: dosing – refill afterwards"},
    {"refill.near", "Resting: target almost reached"},
    {"refill.open", "Blocked: {reason}"},
    {"refill.start", "Filling: {litres:1} L"},

    // Circulation
    {"circ.unbound", "No circulation pump assigned"},
    {"circ.latched", "{reason}"},
    {"circ.blocked", "Blocked: {reason}"},
    {"circ.on_dosing", "Running (for dosing)"},
    {"circ.on_always", "Running (always on)"},
    {"circ.on_interval", "Running (interval)"},
    {"circ.idle", "Resting until the next interval"},
    {"circ.off", "Off – runs only for dosing"},

    // Checklists under a controller row
    {"check.ec_ok", "EC probe delivers valid values"},
    {"check.ec_bad", "EC: {reason}"},
    {"check.recipe", "Recipe: {name}"},
    {"check.no_recipe", "No recipe"},
    {"check.circ_free", "Circulation pump free"},
    {"check.circ_blocked", "Circulation pump blocked"},
    {"check.no_latch", "No latch"},
    {"check.job_running", "A job is running"},
    {"check.no_job", "No other job"},
    {"check.ph_ok", "pH probe delivers valid values"},
    {"check.ph_bad", "pH: {reason}"},
    {"check.gate_ok", "EC {ec:2} above {floor:2}: pH can be measured"},
    {"check.gate_low", "EC {ec:2} below {floor:2}: pH can't be measured like this, nutrients first"},
    {"check.rest_over", "Rest time after the EC dose is over"},
    {"check.rest_left", "Rest time after the EC dose: {left} left"},
    {"check.ec_first", "EC top-up first"},
    {"check.ec_done", "EC top-up done"},
    {"check.ph_down", "pH− calibrated: {name}"},
    {"check.no_ph_down", "No calibrated pH−"},
    {"check.level_ok", "Level valid"},
    {"check.level_bad", "Level: {reason}"},
    {"check.inlet_free", "Inlet free"},
    {"check.dry_ok", "Dry-run protection OK"},

    // Monitoring: headline, labels and assessments (watchdog.cpp, hub.cpp)
    {"watch.headline.problem", "1 problem"},
    {"watch.headline.problems", "{n} problems"},
    {"watch.headline.ok", "All fine ({n} checks)"},
    {"watch.headline.none", "Nothing to assess yet"},
    {"watch.maintenance", "Maintenance mode – values are not assessed"},
    {"watch.startup", "Starting after a restart"},
    {"watch.stop.label", "Emergency stop"},
    {"watch.stop", "Emergency stop active – all automation is paused"},
    {"watch.devices.label", "Devices"},
    {"watch.devices.none", "No devices set up yet"},
    {"watch.devices.ok", "All devices that are set up respond"},
    {"watch.devices.offline", "Not responding: {names}"},
    {"watch.reading.ok", "Valid"},
    {"watch.reading.uncalibrated", "Not calibrated"},
    {"watch.reading.failed", "Data loss: {reason}"},
    {"watch.band.ph.label", "pH in target band"},
    {"watch.band.ec.label", "EC in target band"},
    {"watch.band.off", "No target set (control off)"},
    {"watch.band.no_value", "No valid value – see the reading"},
    {"watch.band.ok", "In band {lo:2}–{hi:2}"},
    {"watch.band.out", "{value:2} outside {lo:2}–{hi:2}"},
    {"watch.water.label", "Water temperature"},
    {"watch.water.off", "Assessment off"},
    {"watch.water.empty", "Not applicable: tank empty"},
    {"watch.water.ok", "{value:1} °C in band"},
    {"watch.water.out", "{value:1} °C outside {lo:1}–{hi:1}"},
    {"watch.level.label", "Level"},
    {"watch.level.ok", "{value:1} L"},
    {"watch.level.low", "{value:1} L below {min:1} L"},
    {"watch.stock.label", "Stock {name}"},
    {"watch.stock.low", "{ml:0} ml – refill"},
    {"watch.calib.label", "Calibrate {name}"},
    {"watch.calib.missing", "Pump not calibrated – it will not dose"},
    {"watch.ctl.ec", "Top up EC"},
    {"watch.ctl.ph", "Control pH"},
    {"watch.ctl.refill", "Refill"},
    {"watch.ctl.circulation", "Circulation"},
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

std::string number(double v, int decimals) {
  if (!isNum(v)) return "–";
  char buf[64];
  if (decimals >= 0) {
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
  }
  std::snprintf(buf, sizeof buf, "%.6f", v);
  std::string s(buf);
  s.erase(s.find_last_not_of('0') + 1);
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s == "-0" ? "0" : s;
}

std::string value(const json& args, const std::string& name, int decimals) {
  if (!args.is_object()) return "–";
  auto it = args.find(name);
  if (it == args.end() || it->is_null()) return "–";
  if (it->is_number()) return number(it->get<double>(), decimals);
  if (it->is_string()) return it->get<std::string>();
  if (it->is_object()) return jstr(*it, "text", "–");
  return it->dump();
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
