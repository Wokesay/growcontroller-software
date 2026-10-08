// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/control.hpp"

#include <algorithm>
#include <cmath>

#include "gc/messages.hpp"

namespace gc {

void to_json(json& j, const CtlStatus& s) {
  json checks = json::array();
  for (const auto& c : s.checks) {
    json one = c.msg;
    one["ok"] = c.ok;
    checks.push_back(std::move(one));
  }
  j = {{"state", s.state}, {"line", s.line}, {"checks", checks}, {"info", s.info}};
}

namespace {

// Parameter als Anzahl bzw. Dauer: fehlend oder unsinnig → sichere Vorgabe, nie
// undefiniertes Verhalten beim Umwandeln.
int countParam(double v, int fallback) { return isNum(v) && v >= 0 && v <= 1000 ? static_cast<int>(v) : fallback; }
Ms minutesParam(double v, double fallback) {
  return static_cast<Ms>((isNum(v) && v > 0 && v <= 24 * 60 ? v : fallback) * kMinute);
}

// Läuft gerade eine Gabe, die nicht vom eigenen Regler stammt?
bool foreignDose(const ControlEnv& env, const char* ownPrefix) {
  const auto& a = env.doser.active();
  return a && a->id.rfind(ownPrefix, 0) != 0;
}

constexpr double kPhStartEffect = 5.0;    // pH je ml/L pH− (RAT-050: kleinste Dosis)
constexpr double kEcStartEffect = 0.275;  // mS/cm je ml/L Rezept (RAT-055)
constexpr double kEcFromPhDown = 1.04;    // mS/cm je ml/L pH− (RAT-053)
constexpr Epoch kEcRestS = 240;           // Ruhezeit EC-Gate nach EC-Gabe (RAT-058)
constexpr Ms kMaxPhAge = 10 * kMinute;    // älter → keine Korrektur (RAT-043)

std::string mmss(Ms ms) {
  Ms s = std::max<Ms>(0, ms / 1000);
  std::string sec = std::to_string(s % 60);
  return std::to_string(s / 60) + ":" + (sec.size() < 2 ? "0" + sec : sec);
}

bool automation(const Ctx& c, CtlStatus& st) {
  if (c.stopped) {
    st.state = "blocked";
    st.line = say("ctl.stopped");
    return false;
  }
  if (c.epoch < c.maintenanceUntil) {
    st.state = "waiting";
    st.line = say("ctl.maintenance", {{"until", c.maintenanceUntil}});
    return false;
  }
  return true;
}

const CanisterCfg* phDownCanister(const Ctx& c, double& flow) {
  for (const auto& k : c.cfg.canisters) {
    if (k.kind != "ph_down" || k.pump.empty()) continue;
    auto it = c.pumps.find(k.pump);
    if (it != c.pumps.end() && it->second.online && isNum(it->second.flowMlPerMin) && it->second.flowMlPerMin > 0) {
      flow = it->second.flowMlPerMin;
      return &k;
    }
  }
  return nullptr;
}

const RecipeCfg* activeRecipe(const Ctx& c, const ParamView& p) {
  std::string id = p.str("recipe");
  if (!id.empty())
    if (const RecipeCfg* r = c.cfg.recipe(id)) return r;
  return c.cfg.recipes.empty() ? nullptr : &c.cfg.recipes.front();
}

bool enabled(const Ctx& c, const std::string& fn) {
  auto it = c.cfg.functions.find(fn);
  return it != c.cfg.functions.end() && it->second.enabled;
}

}  // namespace

// ---------------------------------------------------------------- EC

void EcController::tick(const Ctx& c, ControlEnv& env) {
  st_.checks.clear();
  st_.info = json::object();
  if (!enabled(c, "ec_control")) {
    st_.state = "off";
    st_.line = say("ec.off");
    reset();
    return;
  }
  auto p = effectiveParams(c.cat, c.cfg, "ec_control");
  const auto& ec = c.truth.get("tank.ec");
  const auto& ph = c.truth.get("tank.ph");
  double tol = p.num("ec_tolerance");
  double target = p.num("ec_target");
  // Vorhalt für die folgende pH−-Gabe (RAT-053): das pH− hebt die EC mit.
  if (enabled(c, "ph_control") && ph.usable()) {
    auto pp = effectiveParams(c.cat, c.cfg, "ph_control");
    double gap = *ph.value - pp.num("ph_target");
    double eff = clampEffect(c.rt.phEffect, kPhStartEffect);
    if (gap > 0) target -= std::min(0.3, gap / eff * kEcFromPhDown);
  }
  target_ = target;
  const RecipeCfg* recipe = activeRecipe(c, p);
  st_.info = {{"target", numOrNull(p.num("ec_target"))}, {"targetEffective", numOrNull(target)},
              {"tolerance", numOrNull(tol)}, {"round", round_}};

  st_.checks.push_back({ec.usable(), ec.usable() ? say("check.ec_ok") : say("check.ec_bad", {{"reason", ec.reason}})});
  st_.checks.push_back({recipe != nullptr, recipe ? say("check.recipe", {{"name", recipe->name}}) : say("check.no_recipe")});
  bool circOk = !env.act.inhibit(c, "tank.circulation").has_value();
  st_.checks.push_back({circOk, say(circOk ? "check.circ_free" : "check.circ_blocked")});
  st_.checks.push_back({!c.rt.latches.count("ec.no_effect"), say("check.no_latch")});
  st_.checks.push_back({!env.userJob, say(env.userJob ? "check.job_running" : "check.no_job")});

  if (!automation(c, st_)) {
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, "Automatik aus");
    reset();
    return;
  }
  if (env.calibrating) {
    st_.state = "waiting";
    st_.line = say("ctl.calibrating");
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, "Sonde wird kalibriert");
    reset();
    return;
  }
  if (c.rt.latches.count("ec.no_effect")) {
    st_.state = "latched";
    st_.line = say("ec.latched");
    reset();
    return;
  }

  // Wirkung nur aus sauberen Gaben lernen: kein Zulauf, keine fremde Gabe dazwischen (RAT-057).
  if (phase_ != Phase::Idle && (env.refilling || env.userJob || foreignDose(env, "ec-"))) clean_ = false;
  if (phase_ == Phase::Settling) {
    if (c.now < settleUntil_) {
      st_.state = "working";
      st_.line = say("ec.settling", {{"from", numOrNull(ecBefore_)}, {"target", numOrNull(target)}, {"round", round_},
                                     {"left", mmss(settleUntil_ - c.now)}});
      return;
    }
    if (!ec.usable()) {
      st_.state = "blocked";
      st_.line = say("ec.invalid", {{"reason", ec.reason}});
      reset();
      return;
    }
    double rise = *ec.value - ecBefore_;
    if (clean_ && mlRound_ > 0 && isNum(env.volumeL)) {
      double observed = rise / (mlRound_ / env.volumeL);
      c.rt.ecEffect = clampEffect(observed, kEcStartEffect);
    }
    if (clean_) noEffect_ = rise < 0.02 ? noEffect_ + 1 : 0;
    if (noEffect_ >= 2) {
      c.rt.latches["ec.no_effect"] = {{"at", c.epoch}};
      noEffect_ = 0;  // nach dem Quittieren wieder zwei Runden
      c.log.add(c.epoch, "alarm", "alarm", say("ev.ec.no_effect"), say("ev.ec.no_effect.text"));
      reset();
      return;
    }
    if (*ec.value >= target - tol || round_ >= countParam(p.num("max_doses"), 1)) {
      bool reached = *ec.value >= target - tol;
      c.log.add(c.epoch, "control", reached ? "info" : "warn", say(reached ? "ev.ec.done" : "ev.ec.max"),
                say(round_ == 1 ? "ev.ec.rounds_one" : "ev.ec.rounds", {{"from", numOrNull(startEc_)}, {"to", *ec.value}, {"n", round_}}),
                {{"from", startEc_}, {"to", *ec.value}, {"rounds", round_}});
      if (!reached) cooldownUntil_ = c.epoch + 30 * 60;
      phase_ = Phase::Idle;
      round_ = 0;
    } else {
      phase_ = Phase::Idle;  // nächste Runde im selben Takt planen
    }
  }

  if (phase_ == Phase::Dosing) {
    st_.state = "working";
    st_.line = say("ec.dosing", {{"from", numOrNull(ecBefore_)}, {"target", numOrNull(target)}, {"round", round_}});
    if (!env.doser.busy() && !queue_.empty() && env.circulationOn) {
      DoseOrder o;
      o.id = "ec-" + std::to_string(++seq_);
      o.purpose = "ec";
      o.step = queue_.front();
      Msg err;
      if (env.doser.start(c, env.act, o, err)) {
        queue_.erase(queue_.begin());
      } else {
        st_.state = "blocked";
        st_.line = say("ec.dose_failed", {{"reason", err}});
        reset();
      }
    } else if (!env.circulationOn && !env.doser.busy() && !queue_.empty()) {
      // Kommt die Umwälzpumpe nicht (Rolle entfernt, Ausgang gestört), bricht die
      // Runde ab, statt ewig „beschäftigt“ zu bleiben.
      if (circWaitSince_ == 0) circWaitSince_ = std::max<Ms>(c.now, 1);
      if (!c.cfg.binding("tank.circulation") || c.now - circWaitSince_ > 2 * kMinute) {
        c.log.add(c.epoch, "control", "warn", say("ev.ec.round_cancelled"), say("ev.ec.round_cancelled.text"));
        st_.state = "blocked";
        st_.line = say("ec.circ_failed");
        cooldownUntil_ = c.epoch + 10 * 60;
        reset();
        return;
      }
      st_.line = say("ec.wait_circ");
    }
    if (env.circulationOn) circWaitSince_ = 0;
    return;
  }

  // Idle
  if (!ec.usable()) {
    st_.state = "blocked";
    st_.line = say("ec.invalid", {{"reason", ec.reason}});
    return;
  }
  if (!recipe) {
    st_.state = "blocked";
    st_.line = say("ec.no_recipe");
    return;
  }
  if (*ec.value > target + tol) {
    st_.state = "idle";
    st_.line = say("ec.above", {{"ec", *ec.value}});
    return;
  }
  if (*ec.value >= target - tol) {
    st_.state = "idle";
    st_.line = say("ec.ok", {{"ec", *ec.value}});
    round_ = 0;
    return;
  }
  if (c.epoch < cooldownUntil_) {
    st_.state = "waiting";
    st_.line = say("ec.cooldown");
    return;
  }
  if (env.userJob) {
    st_.state = "waiting";
    st_.line = say("ec.wait_job");
    return;
  }
  if (env.refilling) {
    st_.state = "waiting";
    st_.line = say("ec.wait_refill");
    return;
  }
  if (env.phBusy) {
    st_.state = "waiting";
    st_.line = say("ec.wait_ph");
    return;
  }
  if (!circOk) {
    st_.state = "blocked";
    st_.line = say("ec.no_circ");
    return;
  }
  auto plan = planEcDose(c.cfg, c.pumps, *recipe, env.volumeL, target - *ec.value,
                         clampEffect(c.rt.ecEffect, kEcStartEffect), kEcStartEffect, p.num("max_ec_step"));
  if (!plan.ok) {
    st_.state = "blocked";
    st_.line = say("ec.plan", {{"reason", plan.reason}});
    return;
  }
  if (round_ == 0) startEc_ = *ec.value;
  round_++;
  ecBefore_ = *ec.value;
  mlRound_ = 0;
  clean_ = true;
  queue_ = plan.steps;
  phase_ = Phase::Dosing;
  st_.state = "working";
  st_.line = say("ec.start", {{"from", *ec.value}, {"target", numOrNull(target)}, {"round", round_}});
}

void EcController::onDoseFinished(const Ctx& c, ControlEnv& env, const DoseProgress& p) {
  mlRound_ += p.mlDone;
  lastDoseAt_ = c.epoch;
  if (p.state != DoseProgress::State::Done) {
    c.log.add(c.epoch, "block", "warn", say("ev.ec.aborted"), p.error);
    reset();
    cooldownUntil_ = c.epoch + 15 * 60;
    return;
  }
  if (queue_.empty()) {
    auto pv = effectiveParams(c.cat, c.cfg, "ec_control");
    phase_ = Phase::Settling;
    settleUntil_ = c.now + minutesParam(pv.num("settle_min"), 5);
    env.truth.expectChange("tank.ec", settleUntil_ + kMinute);
    env.truth.expectChange("tank.ph", settleUntil_ + kMinute);
  }
}

// ---------------------------------------------------------------- pH

void PhController::tick(const Ctx& c, ControlEnv& env) {
  st_.checks.clear();
  pendingCirc_ = false;
  st_.info = json::object();
  if (!enabled(c, "ph_control")) {
    st_.state = "off";
    st_.line = say("ph.off");
    reset();
    return;
  }
  auto p = effectiveParams(c.cat, c.cfg, "ph_control");
  const auto& ph = c.truth.get("tank.ph");
  const auto& ec = c.truth.get("tank.ec");
  double target = p.num("ph_target"), tol = p.num("ph_tolerance"), floor = p.num("ec_floor");
  st_.info = {{"target", numOrNull(target)}, {"tolerance", numOrNull(tol)}, {"doses", doses_}};

  bool phOk = ph.usable() && ph.ageMs <= kMaxPhAge;
  bool ecOk = ec.usable();
  bool gateOk = ecOk && *ec.value >= floor;
  Epoch sinceEc = env.lastEcDoseAt > 0 ? c.epoch - env.lastEcDoseAt : kEcRestS;
  bool restOk = sinceEc >= kEcRestS;  // fehlende Historie sperrt nicht (RAT-046)
  double flow = kNaN;
  const CanisterCfg* down = phDownCanister(c, flow);
  bool circOk = !env.act.inhibit(c, "tank.circulation").has_value();

  st_.checks.push_back({phOk, phOk ? say("check.ph_ok") : say("check.ph_bad", {{"reason", ph.reason}})});
  st_.checks.push_back({gateOk, ecOk ? say(gateOk ? "check.gate_ok" : "check.gate_low", {{"ec", *ec.value}, {"floor", numOrNull(floor)}})
                                     : say("check.ec_bad", {{"reason", ec.reason}})});
  st_.checks.push_back({restOk, restOk ? say("check.rest_over")
                                       : say("check.rest_left", {{"left", mmss((kEcRestS - sinceEc) * 1000)}})});
  st_.checks.push_back({!env.ecBusy, say(env.ecBusy ? "check.ec_first" : "check.ec_done")});
  st_.checks.push_back({down != nullptr, down ? say("check.ph_down", {{"name", down->name}}) : say("check.no_ph_down")});
  st_.checks.push_back({circOk, say(circOk ? "check.circ_free" : "check.circ_blocked")});

  if (!automation(c, st_)) {
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, "Automatik aus");
    reset();
    return;
  }
  if (env.calibrating) {
    st_.state = "waiting";
    st_.line = say("ctl.calibrating");
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, "Sonde wird kalibriert");
    reset();
    return;
  }
  if (c.rt.latches.count("ph.no_effect")) {
    st_.state = "latched";
    st_.line = say("ph.latched");
    reset();
    return;
  }

  if (phase_ != Phase::Idle && (env.refilling || env.userJob || foreignDose(env, "ph-"))) clean_ = false;
  if (phase_ == Phase::Settling) {
    if (c.now < settleUntil_) {
      st_.state = "working";
      st_.line = say("ph.settling", {{"from", numOrNull(phBefore_)}, {"target", numOrNull(target)}, {"n", doses_},
                                     {"max", numOrNull(p.num("max_doses"))}, {"left", mmss(settleUntil_ - c.now)}});
      return;
    }
    if (!phOk) {
      st_.state = "blocked";
      st_.line = say("ph.invalid", {{"reason", ph.reason}});
      reset();
      return;
    }
    double drop = phBefore_ - *ph.value;
    if (clean_ && mlLast_ > 0 && isNum(env.volumeL)) c.rt.phEffect = clampEffect(drop / (mlLast_ / env.volumeL), kPhStartEffect);
    if (clean_) noEffect_ = drop < 0.03 ? noEffect_ + 1 : 0;  // RAT-020, RAT-041
    if (noEffect_ >= 2) {
      c.rt.latches["ph.no_effect"] = {{"at", c.epoch}};
      noEffect_ = 0;
      c.log.add(c.epoch, "alarm", "alarm", say("ev.ph.no_effect"), say("ev.ph.no_effect.text"));
      reset();
      return;
    }
    if (*ph.value <= target + tol || doses_ >= countParam(p.num("max_doses"), 1)) {
      bool reached = *ph.value <= target + tol;
      c.log.add(c.epoch, "control", reached ? "info" : "warn", say(reached ? "ev.ph.done" : "ev.ph.max"),
                say(doses_ == 1 ? "ev.ph.doses_one" : "ev.ph.doses", {{"from", numOrNull(startPh_)}, {"to", *ph.value}, {"n", doses_}}),
                {{"from", startPh_}, {"to", *ph.value}, {"doses", doses_}});
      if (!reached) cooldownUntil_ = c.epoch + 30 * 60;
      phase_ = Phase::Idle;
      doses_ = 0;
    } else {
      phase_ = Phase::Idle;
    }
  }

  if (phase_ == Phase::Dosing) {
    st_.state = "working";
    st_.line = say("ph.dosing", {{"from", numOrNull(phBefore_)}, {"target", numOrNull(target)}, {"n", doses_ + 1}});
    return;
  }

  // Idle: Reihenfolge der Sperren wie in der Checkliste.
  auto block = [&](Msg m) {
    st_.state = "blocked";
    st_.line = std::move(m);
  };
  if (!phOk) return block(say("ph.invalid", {{"reason", ph.reason}}));
  if (!ecOk) return block(say("ph.ec_invalid"));
  if (!gateOk) return block(say("ph.gate", {{"ec", *ec.value}, {"floor", numOrNull(floor)}}));
  if (*ph.value <= target + tol) {
    st_.state = "idle";
    doses_ = 0;
    if (*ph.value < target - tol)
      st_.line = say("ph.below", {{"ph", *ph.value}});
    else
      st_.line = say("ph.ok", {{"ph", *ph.value}});
    return;
  }
  if (c.epoch < cooldownUntil_) {
    st_.state = "waiting";
    st_.line = say("ph.cooldown");
    return;
  }
  if (env.ecBusy) {
    st_.state = "waiting";
    st_.line = say("ph.wait_ec");
    return;
  }
  if (!restOk) {
    st_.state = "waiting";
    st_.line = say("ph.wait_rest", {{"left", mmss((kEcRestS - sinceEc) * 1000)}});
    return;
  }
  if (env.userJob) {
    st_.state = "waiting";
    st_.line = say("ph.wait_job");
    return;
  }
  if (env.refilling) {
    st_.state = "waiting";
    st_.line = say("ph.wait_refill");
    return;
  }
  if (!down) return block(say("ph.no_down"));
  if (!circOk) return block(say("ph.no_circ"));
  if (!env.circulationOn) {
    st_.state = "working";
    st_.line = say("ph.wait_circ");
    pendingCirc_ = true;
    return;
  }
  auto plan = planPhDose(*ph.value, target, clampEffect(c.rt.phEffect, kPhStartEffect), env.volumeL, p.num("max_ml_per_dose"));
  if (!plan.ok) return block(say("ph.plan", {{"reason", plan.reason}}));
  DoseStep s;
  s.canister = down->id;
  s.name = down->name;
  s.pump = down->pump;
  s.color = down->color;
  s.ml = plan.ml;
  s.flowMlPerMin = flow;
  Msg err;
  s.runs = splitRuns(plan.ml, flow, c.cfg.limits, c.cfg.limits.bounded().maxPartialRuns, err);
  if (!err.key.empty()) {
    // Gabe unter 1 s: nicht dosieren, sichtbar ruhen (RAT-050)
    st_.state = "idle";
    st_.line = say("ph.too_small", {{"ml", numOrNull(plan.ml)}});
    return;
  }
  DoseOrder o;
  o.id = "ph-" + std::to_string(++seq_);
  o.purpose = "ph";
  o.step = s;
  if (!env.doser.start(c, env.act, o, err)) return block(say("ph.dose_failed", {{"reason", err}}));
  if (doses_ == 0) startPh_ = *ph.value;
  phBefore_ = *ph.value;
  clean_ = true;
  phase_ = Phase::Dosing;
  st_.state = "working";
  st_.line = say("ph.start", {{"from", *ph.value}, {"target", numOrNull(target)}, {"n", doses_ + 1}, {"ml", numOrNull(plan.ml)}});
}

void PhController::onDoseFinished(const Ctx& c, ControlEnv& env, const DoseProgress& p) {
  if (p.state != DoseProgress::State::Done) {
    c.log.add(c.epoch, "block", "warn", say("ev.ph.aborted"), p.error);
    reset();
    cooldownUntil_ = c.epoch + 15 * 60;
    return;
  }
  auto pv = effectiveParams(c.cat, c.cfg, "ph_control");
  mlLast_ = p.mlDone;
  doses_++;
  phase_ = Phase::Settling;
  settleUntil_ = c.now + minutesParam(pv.num("settle_min"), 5);
  env.truth.expectChange("tank.ph", settleUntil_ + kMinute);
}

// ---------------------------------------------------------------- Zulauf

void RefillController::tick(const Ctx& c, ControlEnv& env) {
  st_.checks.clear();
  if (!enabled(c, "refill")) {
    st_.state = "off";
    st_.line = say("refill.off");
    if (filling_) {
      Msg e;
      env.act.setRole(c, "tank.inlet", false, "Nachfüllen ausgeschaltet", e);
      filling_ = false;
    }
    return;
  }
  auto p = effectiveParams(c.cat, c.cfg, "refill");
  const auto& level = c.truth.get("tank.level");
  st_.checks.push_back({level.usable(), level.usable() ? say("check.level_ok") : say("check.level_bad", {{"reason", level.reason}})});
  auto inh = env.act.inhibit(c, "tank.inlet");
  st_.checks.push_back({!inh, inh ? *inh : say("check.inlet_free")});
  auto open = env.act.roleState(c.cfg, "tank.inlet");

  if (filling_) {
    // Menge wird gerechnet, der Sensor ist nur Prüfung und Notabschaltung (RAT-001).
    bool done = c.now - startedMs_ >= plannedMs_;
    bool sensorStop = level.usable() && *level.value >= p.num("target_l") + 1.0;
    if (!open || !*open) {
      filling_ = false;  // vom Gateway abgeschaltet (Notgrenze, Zeitlimit, Not-Halt)
      st_.state = "latched";
      st_.line = say("refill.stopped");
      return;
    }
    if (done || sensorStop || !automation(c, st_)) {
      Msg e;
      env.act.setRole(c, "tank.inlet", false, "Nachfüllen fertig", e);
      filling_ = false;
      cooldownUntil_ = c.epoch + 10 * 60;
      double added = level.usable() ? *level.value - startL_ : kNaN;
      c.log.add(c.epoch, "tank", "info", say("ev.refilled"),
                say("ev.refilled.text", {{"measured", numOrNull(added)}, {"planned", numOrNull(plannedL_)}}),
                {{"plannedL", plannedL_}, {"measuredL", numOrNull(added)}});
      st_.state = "idle";
      st_.line = say("refill.done");
      return;
    }
    st_.state = "working";
    st_.line = say("refill.filling", {{"from", numOrNull(startL_)}, {"target", numOrNull(p.num("target_l"))},
                                      {"left", mmss(plannedMs_ - (c.now - startedMs_))}});
    return;
  }
  if (!automation(c, st_)) return;
  if (c.rt.latches.count("inlet.fault")) {
    st_.state = "latched";
    st_.line = say("refill.latched");
    return;
  }
  if (!level.usable()) {
    st_.state = "blocked";
    st_.line = say("refill.level", {{"reason", level.reason}});
    return;
  }
  double start = p.num("start_below_l"), target = p.num("target_l"), flow = p.num("flow_l_per_min");
  if (isNum(c.cfg.tank().capacityL)) target = std::min(target, c.cfg.tank().capacityL - 1.0);
  if (*level.value >= start) {
    st_.state = "idle";
    st_.line = say("refill.ok", {{"level", *level.value}});
    return;
  }
  if (c.epoch < cooldownUntil_) {
    st_.state = "waiting";
    st_.line = say("refill.cooldown");
    return;
  }
  if (inh) {
    st_.state = "blocked";
    st_.line = say("refill.inhibit", {{"reason", *inh}});
    return;
  }
  if (!isNum(flow) || flow <= 0) {
    st_.state = "blocked";
    st_.line = say("refill.flow");  // kein Ersatzwert (RAT-038)
    return;
  }
  if (env.doser.busy() || env.userJob || env.ecBusy || env.phBusy) {
    // Zulauf verdünnt: nicht während einer Gabe oder ihres Einschwingens (RAT-055)
    st_.state = "waiting";
    st_.line = say("refill.dosing");
    return;
  }
  plannedL_ = target - *level.value;
  if (plannedL_ < 0.5) {
    st_.state = "idle";
    st_.line = say("refill.near");
    return;
  }
  plannedMs_ = static_cast<Ms>(plannedL_ / flow * kMinute);
  Msg e;
  if (!env.act.setRole(c, "tank.inlet", true, "Nachfüllen", e)) {
    st_.state = "blocked";
    st_.line = say("refill.open", {{"reason", e}});
    return;
  }
  filling_ = true;
  startedMs_ = c.now;
  startL_ = *level.value;
  st_.state = "working";
  st_.line = say("refill.start", {{"litres", numOrNull(plannedL_)}});
}

// ---------------------------------------------------------------- Umwälzen

void CirculationController::tick(const Ctx& c, ControlEnv& env, bool demand) {
  st_.checks.clear();
  if (!c.cfg.binding("tank.circulation")) {
    st_.state = "off";
    st_.line = say("circ.unbound");
    env.circulationOn = false;
    return;
  }
  bool on = enabled(c, "circulation");
  bool desired = demand;
  Msg why = say("circ.on_dosing");
  if (on && !c.stopped) {
    auto p = effectiveParams(c.cat, c.cfg, "circulation");
    if (p.str("mode") == "always") {
      desired = true;
      why = say("circ.on_always");
    } else {
      Epoch period = static_cast<Epoch>(p.num("period_min") * 60), onS = static_cast<Epoch>(p.num("on_min") * 60);
      if (period > 0 && c.epoch % period < onS) {
        desired = true;
        if (!demand) why = say("circ.on_interval");
      }
    }
  }
  auto inh = env.act.inhibit(c, "tank.circulation");
  st_.checks.push_back({!inh, inh ? *inh : say("check.dry_ok")});
  auto state = env.act.roleState(c.cfg, "tank.circulation");
  bool isOn = state && *state;
  Msg e;
  if (desired && !isOn && !inh && !c.stopped) {
    if (env.act.setRole(c, "tank.circulation", true, "Umwälzen", e)) isOn = true;
  } else if (!desired && isOn) {
    env.act.setRole(c, "tank.circulation", false, "Umwälzen", e);
    isOn = false;
  }
  env.circulationOn = isOn;
  if (inh && desired) {
    st_.state = c.rt.latches.count("circulation.dry") ? "latched" : "blocked";
    const bool latched = st_.state == "latched";
    st_.line = say(latched ? "circ.latched" : "circ.blocked", {{"reason", *inh}});
  } else if (isOn) {
    st_.state = "working";
    st_.line = why;
  } else {
    st_.state = on ? "idle" : "off";
    st_.line = say(on ? "circ.idle" : "circ.off");
  }
}

}  // namespace gc
