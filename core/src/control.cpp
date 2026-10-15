// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/control.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
constexpr Epoch kAbortRestS = 15 * 60;    // rest after an aborted dose; also the least a round cut by a Stop settles (SD-073)
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
  wantsCirc_ = false;
  if (!enabled(c, "ec_control")) {
    st_.state = "off";
    st_.line = say("ec.off");
    reset();
    cut_ = false;
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
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, say("abort.auto_off"));
    reset();
    return;
  }
  if (env.calibrating) {
    st_.state = "waiting";
    st_.line = say("ctl.calibrating");
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, say("abort.calibrating"));
    reset();
    return;
  }
  if (c.rt.latches.count("ec.no_effect")) {
    st_.state = "latched";
    st_.line = say("ec.latched");
    reset();
    return;
  }

  // A round a Stop cut waits for the circulation pump, then settles from when it runs (stopRound, SD-073)
  if (pending()) {
    if (env.circulationOn || !c.cfg.binding("tank.circulation")) {
      phase_ = Phase::Settling;
      clean_ = false;  // its effect is not learned (RAT-057)
      settleUntil_ = c.now + std::max<Ms>(kAbortRestS * kSecond, minutesParam(p.num("settle_min"), 5));
    } else {
      wantsCirc_ = true;
      if (cutWaitSince_ == 0) cutWaitSince_ = std::max<Ms>(c.now, 1);
      // A refusal the gateway knows shows at once (the circulation line names why); a pump that
      // just does not come on after 2 min, as for a round waiting to dose (RAT-037, RAT-039)
      const bool late = c.now - cutWaitSince_ > 2 * kMinute;
      st_.state = !circOk || late ? "blocked" : "working";
      st_.line = say(!circOk ? "ec.cut_circ_blocked" : late ? "ec.cut_no_circ" : "ec.wait_circ");
      return;
    }
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
    cut_ = false;  // a cut round has settled: decide as after any settle time
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
    cooldownUntil_ = c.epoch + kAbortRestS;
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

void EcController::stopRound() {
  if (phase_ == Phase::Idle) return;
  cut_ = true;
  reset();
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
    cut_ = false;
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
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, say("abort.auto_off"));
    reset();
    return;
  }
  if (env.calibrating) {
    st_.state = "waiting";
    st_.line = say("ctl.calibrating");
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, say("abort.calibrating"));
    reset();
    return;
  }
  if (c.rt.latches.count("ph.no_effect")) {
    st_.state = "latched";
    st_.line = say("ph.latched");
    reset();
    return;
  }

  if (pending()) {  // as in EcController
    if (env.circulationOn || !c.cfg.binding("tank.circulation")) {
      phase_ = Phase::Settling;
      clean_ = false;
      settleUntil_ = c.now + std::max<Ms>(kAbortRestS * kSecond, minutesParam(p.num("settle_min"), 5));
    } else {
      pendingCirc_ = true;
      if (cutWaitSince_ == 0) cutWaitSince_ = std::max<Ms>(c.now, 1);
      const bool late = c.now - cutWaitSince_ > 2 * kMinute;
      st_.state = !circOk || late ? "blocked" : "working";
      st_.line = say(!circOk ? "ph.cut_circ_blocked" : late ? "ph.cut_no_circ" : "ph.wait_circ");
      return;
    }
  }
  if (phase_ != Phase::Idle && (env.refilling || env.userJob || foreignDose(env, "ph-"))) clean_ = false;
  if (phase_ == Phase::Settling) {
    if (c.now < settleUntil_) {
      st_.state = "working";
      st_.line = say("ph.settling", {{"from", numOrNull(phBefore_)}, {"target", numOrNull(target)}, {"n", doses_},
                                     {"max", numOrNull(p.num("max_doses"))}, {"left", mmss(settleUntil_ - c.now)}});
      return;
    }
    cut_ = false;
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

void PhController::onDoseFinished(const Ctx& c, const DoseProgress& p) {
  if (p.state != DoseProgress::State::Done) {
    c.log.add(c.epoch, "block", "warn", say("ev.ph.aborted"), p.error);
    reset();
    cooldownUntil_ = c.epoch + kAbortRestS;
    return;
  }
  auto pv = effectiveParams(c.cat, c.cfg, "ph_control");
  mlLast_ = p.mlDone;
  doses_++;
  phase_ = Phase::Settling;
  settleUntil_ = c.now + minutesParam(pv.num("settle_min"), 5);
  // Not announced: the dose is sized to move pH by at most 0.3 (RAT-050),
  // below the jump threshold of 1.0, so a jump while it settles locks the
  // reading (RAT-042, RAT-044)
}

void PhController::stopRound() {
  if (phase_ == Phase::Idle) return;
  cut_ = true;
  reset();
}

// ---------------------------------------------------------------- Zulauf

namespace {

// A refill in steps with a check of each rise (SD-075). Assumptions, not measured (RAT-038):
constexpr Epoch kRefillPauseS = 10 * 60;  // pause after each fill; its rise is checked at the end
constexpr double kOpenMarginMin = 1.0;    // a fill closes this much before max_open_min
constexpr double kRiseShare = 0.5;        // the level must rise by at least this share of what flowed in
constexpr double kJudgeMinL = 4.0;        // a smaller fill is judged together with the next one
constexpr int kMaxUnjudged = 2;           // fills in a row that could not be judged, then a person looks
constexpr Ms kSettleMs = 3 * kMinute;     // the level reading lags 1.5–2 min behind the level

// The level curve the hub reads the tank with: a fill checked against another one is not judged
json levelCurve(const Ctx& c) {
  const Binding* b = c.cfg.binding("tank.level");
  if (!b) return nullptr;
  auto dev = c.cfg.calibrations.find(b->device);
  if (dev == c.cfg.calibrations.end()) return nullptr;
  auto curve = dev->second.find("tank_curve");
  return curve == dev->second.end() ? json(nullptr) : curve->second;
}

// The least amount judged on its own: the level reads 0.5–1 L off (RAT-078), and flat below the
// curve's first point (RAT-030), so a fill starting there reads up to that volume too high
double judgeBound(const Ctx& c, double startL) {
  const json curve = levelCurve(c);
  double first = 0;
  if (curve.is_object() && curve.contains("points") && curve["points"].is_array() && !curve["points"].empty()) {
    const json& pt = curve["points"][0];
    if (pt.is_array() && pt.size() == 2 && pt[1].is_number()) first = pt[1].get<double>();
  }
  return isNum(startL) && startL <= first ? std::max(kJudgeMinL, 2.0 * first) : kJudgeMinL;
}

// The irrigation pump draws from the tank. Bound and on, or in a state not known, it may have run.
enum class Irrigation { None, On, Unknown };
Irrigation irrigation(const Ctx& c, const ControlEnv& env) {
  if (!c.cfg.binding("zone.irrigation_pump")) return Irrigation::None;
  const auto on = env.act.roleState(c.cfg, "zone.irrigation_pump");
  return !on ? Irrigation::Unknown : *on ? Irrigation::On : Irrigation::None;
}

double jnumOr(const json& j, const char* key, double fallback) { return j.contains(key) && j[key].is_number() ? j[key].get<double>() : fallback; }

}  // namespace

void RefillController::shiftClock(RuntimeState& rt, Epoch d) {
  if (cooldownUntil_ > 0) cooldownUntil_ += d;
  if (!rt.refill.is_object()) return;
  for (const char* rec : {"fill", "check"})
    if (rt.refill.contains(rec))
      for (const char* key : {"at", "due"})
        if (rt.refill[rec].contains(key) && rt.refill[rec][key].is_number()) rt.refill[rec][key] = rt.refill[rec][key].get<Epoch>() + d;
}

void RefillController::toCheck(const Ctx& c, double expectedL) {
  json& rec = c.rt.refill;
  json k = rec.contains("fill") ? rec["fill"] : json::object();
  k["at"] = c.epoch;
  k["due"] = cooldownUntil_;
  k["closedMs"] = c.now;
  k["boot"] = c.rt.bootCount;
  k["expectedL"] = numOrNull(expectedL);
  rec["check"] = k;
  rec.erase("fill");
}

void RefillController::latchInlet(const Ctx& c, const Msg& why, const Msg& title, const Msg& text, const json& args) {
  c.rt.latches["inlet.fault"] = {{"at", c.epoch}, {"why", why}};
  c.log.add(c.epoch, "alarm", "alarm", title, text, args);
  c.rt.refill.erase("check");
  c.rt.refill["unjudged"] = 0;
  chainAt_ = -1;
  endSteps();
  st_.state = "latched";
  st_.line = say("refill.latched");
}

void RefillController::tick(const Ctx& c, ControlEnv& env) {
  st_.checks.clear();
  json& rec = c.rt.refill;
  if (!rec.is_object()) rec = json::object();
  const auto open = env.act.roleState(c.cfg, "tank.inlet");
  const Irrigation irr = irrigation(c, env);
  // What disturbs the level reading; the first fill of a refill waits until it has settled. A start
  // counts as one: what happened before it is not known (RAT-038, SD-075)
  if (!booted_ || irr == Irrigation::On || c.epoch < c.maintenanceUntil || (open.value_or(false) && !filling_)) disturbedMs_ = c.now;
  if (irr != Irrigation::None)
    for (const char* r : {"fill", "check"})
      if (rec.contains(r)) rec[r]["irrigation"] = true;
  if (!booted_) {
    booted_ = true;
    // A fill the restart cut, or steps it ended, are reported and not resumed (R6, RAT-007); a cut
    // fill's rise cannot be judged and counts as such. A pending check is judged as before.
    if (rec.contains("fill")) {
      const json f = rec["fill"];
      c.log.add(c.epoch, "tank", "warn", say("ev.refill.restart"), say("ev.refill.restart.text", {{"step", f.value("step", 1)}, {"steps", f.value("steps", 1)}}));
      cooldownUntil_ = c.epoch + kRefillPauseS;
      toCheck(c, kNaN);
      rec["check"]["restart"] = true;
    } else if (rec.contains("check") && rec["check"].value("more", false)) {
      const json& k = rec["check"];
      c.log.add(c.epoch, "tank", "info", say("ev.refill.restart"), say("ev.refill.restart.text", {{"step", k.value("step", 1)}, {"steps", k.value("steps", 1)}}));
      rec["check"]["more"] = false;
    }
  }
  if (!enabled(c, "refill")) {
    st_.state = "off";
    st_.line = say("refill.off");
    if (filling_ || open.value_or(false)) {
      Msg e;
      if (filling_ || rec.contains("fill")) env.act.setRole(c, "tank.inlet", false, say("who.refill_off"), e);
      filling_ = false;
    }
    endSteps();
    rec.erase("fill");
    rec.erase("check");
    return;
  }
  auto p = effectiveParams(c.cat, c.cfg, "refill");
  const auto& level = c.truth.get("tank.level");
  st_.checks.push_back({level.usable(), level.usable() ? say("check.level_ok") : say("check.level_bad", {{"reason", level.reason}})});
  auto inh = env.act.inhibit(c, "tank.inlet");
  st_.checks.push_back({!inh, inh ? *inh : say("check.inlet_free")});
  const auto checkLine = [&] {
    st_.state = "waiting";
    const std::string left = mmss((cooldownUntil_ - c.epoch) * 1000);
    st_.line = step_ < steps_ ? say("refill.check_step", {{"next", step_ + 1}, {"steps", steps_}, {"left", left}}) : say("refill.check", {{"left", left}});
  };

  if (filling_) {
    // Menge wird gerechnet, der Sensor ist nur Prüfung und Notabschaltung (RAT-001).
    const double flowedL = std::min(plannedL_, flow_ * static_cast<double>(c.now - startedMs_) / kMinute);
    if (!open || !*open) {
      // Closed outside the refill. A state not known is switched off (R5).
      if (!open) {
        Msg e;
        env.act.setRole(c, "tank.inlet", false, say("who.refill_done"), e);
      }
      filling_ = false;
      endSteps();
      cooldownUntil_ = c.epoch + kRefillPauseS;
      if (c.rt.latches.count("inlet.fault") || c.stopped || c.epoch < c.maintenanceUntil) {
        // The gateway's emergency limit (latched), or a person: nothing more to judge
        rec.erase("fill");
        st_.state = "latched";
        st_.line = say("refill.stopped");
        if (!c.rt.latches.count("inlet.fault")) automation(c, st_);
        return;
      }
      // The device's own auto-off, a person in the app, an internal error: its rise is still checked,
      // after the pause, so the refill does not open again at once
      toCheck(c, flowedL);
      checkLine();
      return;
    }
    const bool done = c.now - startedMs_ >= plannedMs_;
    const bool sensorStop = level.usable() && *level.value >= targetL_ + 1.0;
    const bool automatic = automation(c, st_);
    if (done || sensorStop || !automatic) {
      Msg e;
      if (!env.act.setRole(c, "tank.inlet", false, say("who.refill_done"), e)) {
        // Tried again in the next tick; the gateway's own limit still holds (RAT-032)
        st_.state = "blocked";
        st_.line = say("refill.close_failed", {{"reason", e}});
        return;
      }
      filling_ = false;
      cooldownUntil_ = c.epoch + kRefillPauseS;
      const double added = level.usable() ? *level.value - startL_ : kNaN;
      const json args = {{"measured", numOrNull(added)}, {"planned", numOrNull(plannedL_)}, {"step", step_}, {"steps", steps_}};
      c.log.add(c.epoch, "tank", "info", say("ev.refilled"), say(steps_ > 1 ? "ev.refilled.step" : "ev.refilled.text", args),
                {{"plannedL", plannedL_}, {"measuredL", numOrNull(added)}, {"step", step_}, {"steps", steps_}});
      if (sensorStop || !automatic) endSteps();
      if (!automatic) {
        rec.erase("fill");  // a person stopped it or works at the tank: nothing more is judged or filled
        return;
      }
      // Judged at the end of the pause, against what flowed while the valve was open (SD-075)
      toCheck(c, done ? plannedL_ : flowedL);
      rec["check"]["more"] = step_ < steps_;
      checkLine();
      return;
    }
    st_.state = "working";
    const std::string left = mmss(plannedMs_ - (c.now - startedMs_));
    st_.line = steps_ > 1 ? say("refill.filling_step", {{"step", step_}, {"steps", steps_}, {"from", startL_}, {"to", startL_ + plannedL_},
                                                         {"left", left}, {"min", numOrNull(maxOpen_)}})
                          : say("refill.filling", {{"from", numOrNull(startL_)}, {"target", numOrNull(targetL_)}, {"left", left}});
    return;
  }
  if (!automation(c, st_)) {
    endSteps();
    rec.erase("check");  // a person at the tank: a rise checked now would say nothing
    return;
  }
  if (c.rt.latches.count("inlet.fault")) {
    endSteps();
    rec.erase("check");
    rec["unjudged"] = 0;  // once a person has looked, the count starts again
    st_.state = "latched";
    st_.line = say("refill.latched");
    return;
  }

  // At the end of the pause: did the level rise by at least half of what flowed in? If not, a leak,
  // an empty supply, a stuck valve or a level sensor that does not follow look the same: the inlet
  // waits for a person (RAT-031, SD-075). Fills that cannot be judged are counted: two in a row and
  // a person looks too. A fill too small to judge alone is added to the next one back to back.
  if (rec.contains("check")) {
    json& k = rec["check"];
    const Epoch due = static_cast<Epoch>(jnumOr(k, "due", 0));
    if (c.epoch < due) {
      cooldownUntil_ = std::max(cooldownUntil_, due);
      checkLine();
      return;
    }
    const bool sameBoot = k.value("boot", std::uint32_t{0}) == c.rt.bootCount;
    const bool fresh = !sameBoot || level.ts > static_cast<Ms>(jnumOr(k, "closedMs", 0));
    const bool more = k.value("more", false);
    std::optional<Msg> unjudged;
    if (k.value("restart", false)) unjudged = say("refill.unjudged.restart");
    else if (!level.usable()) unjudged = say("refill.unjudged.level", {{"reason", level.reason}});
    else if (env.levelCalibrating || levelCurve(c) != k.value("curve", json(nullptr))) unjudged = say("refill.unjudged.curve");
    // A level read during the fill lags behind (RAT-038): only one read after the valve closed is
    // judged. A sensor that reports only on a change and sent nothing in the whole pause did not
    // follow the rise: counted as not judged, so it cannot go on unseen
    else if (!fresh) unjudged = say("refill.unjudged.stale");
    if (!unjudged) {
      const bool chained = k.contains("chainStartL");
      const double from = chained ? jnumOr(k, "chainStartL", kNaN) : jnumOr(k, "startL", kNaN);
      const double expected = jnumOr(k, "expectedL", 0) + (chained ? jnumOr(k, "chainExpectedL", 0) : 0.0);
      const double rise = *level.value - from;
      const json args = {{"rise", rise}, {"expected", expected}, {"flow", jnumOr(k, "flow", kNaN)}};
      if (rise >= kRiseShare * expected) {
        rec["unjudged"] = 0;
      } else if (k.value("irrigation", false)) {
        unjudged = say("refill.unjudged.irrigation");  // too little, but the irrigation pump drew from the tank
      } else if (expected < judgeBound(c, from)) {
        rec["chain"] = {{"startL", from}, {"expectedL", expected}};
        chainAt_ = c.now;
      } else {
        latchInlet(c, say("why.refill_no_rise", args), say("ev.refill.no_rise"), say("ev.refill.no_rise.text", args), args);
        return;
      }
    }
    if (unjudged) {
      const int n = rec.value("unjudged", 0) + 1;
      if (n >= kMaxUnjudged) {
        const json a = {{"n", n}, {"why", *unjudged}};
        latchInlet(c, say("why.refill_unchecked", a), say("ev.refill.unchecked"), say("ev.refill.unchecked.text", a), a);
        return;
      }
      rec["unjudged"] = n;
      c.log.add(c.epoch, "tank", "warn", say("ev.refill.unjudged"), more ? say("refill.unjudged.more", {{"why", *unjudged}}) : *unjudged);
      endSteps();
    }
    rec.erase("check");
  }
  if (chainAt_ != c.now) rec.erase("chain");  // only a fill right after the check adds to it

  if (!level.usable()) {
    endSteps();
    st_.state = "blocked";
    st_.line = say("refill.level", {{"reason", level.reason}});
    return;
  }
  double start = p.num("start_below_l"), target = p.num("target_l"), flow = p.num("flow_l_per_min"), maxOpen = p.num("max_open_min");
  if (isNum(c.cfg.tank().capacityL)) target = std::min(target, c.cfg.tank().capacityL - 1.0);
  // The next step goes on toward the target, also above the start level, with the settings it was
  // planned with; a mix or a hand dose meanwhile ends the steps, the next would dilute it
  if (step_ < steps_ && (target != targetL_ || flow != flow_ || !(maxOpen == maxOpen_ || (!isNum(maxOpen) && !isNum(maxOpen_))) || env.userJob)) endSteps();
  const bool stepping = step_ < steps_;
  if (!stepping && !(*level.value < start)) {
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
    endSteps();
    st_.state = "blocked";
    st_.line = say("refill.inhibit", {{"reason", *inh}});
    return;
  }
  if (!isNum(flow) || flow <= 0) {
    endSteps();
    st_.state = "blocked";
    st_.line = say("refill.flow");  // kein Ersatzwert (RAT-038)
    return;
  }
  // The valve may stay open max_open_min, the inlet's fault limit (RAT-031, RAT-032): a fill planned
  // longer would end as a fault. One fill keeps it open a minute less (half as long, if that is
  // shorter), so a late tick still closes it first; the rest comes in steps (#155, SD-075).
  const double fits = isNum(maxOpen) ? flow * (maxOpen - std::min(kOpenMarginMin, maxOpen / 2)) : std::numeric_limits<double>::infinity();
  if (fits < 0.5) {
    endSteps();
    st_.state = "blocked";
    st_.line = say("refill.too_slow", {{"flow", flow}, {"min", maxOpen}});
    return;
  }
  if (open.value_or(false)) {
    // Opened by hand: the refill does not take over a valve whose open time it did not start (#155)
    endSteps();
    st_.state = "waiting";
    st_.line = isNum(maxOpen) ? say("refill.open_by_hand_limit", {{"min", maxOpen}}) : say("refill.open_by_hand");
    return;
  }
  if (env.doser.busy() || env.userJob || env.ecBusy || env.phBusy) {
    // Zulauf verdünnt: nicht während einer Gabe oder ihres Einschwingens (RAT-055)
    st_.state = "waiting";
    st_.line = say("refill.dosing");
    return;
  }
  if (!stepping && c.now - disturbedMs_ < kSettleMs) {
    // The fill is judged from the level when it opens: not while the reading still follows irrigation,
    // draining or a valve opened by hand (RAT-038, SD-075)
    st_.state = "waiting";
    st_.line = say("refill.settle", {{"left", mmss(kSettleMs - (c.now - disturbedMs_))}});
    return;
  }
  const double gap = target - *level.value;
  double planned = 0, left = 0;
  int step = 1, steps = 1;
  if (stepping) {
    planned = std::min({leftL_, fits, gap});
    step = step_ + 1;
    steps = steps_;
    left = step < steps ? leftL_ - planned : 0;
  } else {
    planned = std::min(gap, fits);
    steps = gap > fits ? static_cast<int>(std::ceil(gap / fits)) : 1;
    left = gap - planned;
  }
  // Fills judged together never add more than the target allows from where they began: a level
  // stuck low would otherwise make each of them plan the whole gap again
  if (rec.contains("chain")) {
    const double from = jnumOr(rec["chain"], "startL", kNaN), sum = jnumOr(rec["chain"], "expectedL", 0);
    const double room = target - from - sum;
    if (!(room >= 0.5)) {
      const double rise = *level.value - from;
      if (rise < kRiseShare * sum) {
        const json a = {{"rise", rise}, {"expected", sum}, {"flow", flow}};
        latchInlet(c, say("why.refill_no_rise", a), say("ev.refill.no_rise"), say("ev.refill.no_rise.text", a), a);
        return;
      }
      rec.erase("chain");
    } else {
      planned = std::min(planned, room);
    }
  }
  if (!(planned >= 0.5)) {
    endSteps();
    st_.state = "idle";
    st_.line = say("refill.near");
    return;
  }
  Msg e;
  if (!env.act.setRole(c, "tank.inlet", true, say("who.refill"), e)) {
    endSteps();
    st_.state = "blocked";
    st_.line = say("refill.open", {{"reason", e}});
    return;
  }
  if (!stepping) {
    targetL_ = target;
    flow_ = flow;
    maxOpen_ = maxOpen;
  }
  filling_ = true;
  startedMs_ = c.now;
  startL_ = *level.value;
  plannedL_ = planned;
  plannedMs_ = static_cast<Ms>(planned / flow * kMinute);
  step_ = step;
  steps_ = steps;
  leftL_ = left;
  json f = {{"at", c.epoch}, {"startL", startL_}, {"plannedL", planned}, {"flow", flow}, {"step", step}, {"steps", steps},
            {"irrigation", irr != Irrigation::None}, {"curve", levelCurve(c)}};
  if (rec.contains("chain")) {
    f["chainStartL"] = rec["chain"]["startL"];
    f["chainExpectedL"] = rec["chain"]["expectedL"];
    rec.erase("chain");
  }
  rec["fill"] = f;
  st_.state = "working";
  st_.line = steps > 1 ? say("refill.filling_step", {{"step", step}, {"steps", steps}, {"from", startL_}, {"to", startL_ + planned},
                                                      {"left", mmss(plannedMs_)}, {"min", numOrNull(maxOpen_)}})
                       : say("refill.start", {{"litres", planned}});
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
    if (env.act.setRole(c, "tank.circulation", true, say("who.circulation"), e)) isOn = true;
  } else if (!desired && isOn) {
    env.act.setRole(c, "tank.circulation", false, say("who.circulation"), e);
    isOn = false;
  }
  env.circulationOn = isOn;
  if (inh && desired) {
    // A latch that releases itself needs no person: blocked, though the line names the latch (SD-067)
    const bool latched = c.rt.latches.count("circulation.dry") > 0;
    st_.state = latched && !env.act.dryLatchReleasesItself(c) ? "latched" : "blocked";
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
