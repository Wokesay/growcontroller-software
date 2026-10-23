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
constexpr double kEcFromPhDown = 1.04;    // mS/cm je ml/L pH− (RAT-053)
constexpr Epoch kEcRestS = 240;           // Ruhezeit EC-Gate nach EC-Gabe (RAT-058)
constexpr Epoch kAbortRestS = 15 * 60;    // rest after an aborted dose; also the least a round cut by a Stop settles (SD-073)
constexpr Ms kMaxPhAge = 10 * kMinute;    // älter → keine Korrektur (RAT-043)
constexpr double kSmallRoundEc = 0.06;    // three times the no-effect threshold of 0.02 (SD-102)
// "No effect" latches after 2 clean rounds without effect, or 4 that only watering made unclean (SD-103)
constexpr int kNoEffectPoints = 4;

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

double ecAim(const Ctx& c) {
  auto p = effectiveParams(c.cat, c.cfg, "ec_control");
  double target = p.num("ec_target");
  // Vorhalt für die folgende pH−-Gabe (RAT-053): das pH− hebt die EC mit.
  const auto& ph = c.truth.get("tank.ph");
  if (isNum(target) && enabled(c, "ph_control") && ph.usable()) {
    auto pp = effectiveParams(c.cat, c.cfg, "ph_control");
    double gap = *ph.value - pp.num("ph_target");
    double eff = clampEffect(c.rt.phEffect, kPhStartEffect);
    if (gap > 0) target -= std::min(0.3, gap / eff * kEcFromPhDown);
  }
  return target;
}

void EcController::tick(const Ctx& c, ControlEnv& env) {
  st_.checks.clear();
  st_.info = json::object();
  wantsCirc_ = false;
  if (!enabled(c, "ec_control")) {
    st_.state = "off";
    st_.line = say("ec.off");
    reset();
    cut_ = quiet_ = false;
    cutFloor_ = 0;
    return;
  }
  auto p = effectiveParams(c.cat, c.cfg, "ec_control");
  const auto& ec = c.truth.get("tank.ec");
  double tol = p.num("ec_tolerance");
  const double target = ecAim(c);
  target_ = target;
  const RecipeCfg* recipe = activeRecipe(c, p);
  st_.info = {{"target", numOrNull(p.num("ec_target"))}, {"targetEffective", numOrNull(target)},
              {"tolerance", numOrNull(tol)}, {"round", round_}, {"recipe", recipe ? json(recipe->id) : json(nullptr)}};

  st_.checks.push_back({ec.usable(), ec.usable() ? say("check.ec_ok") : say("check.ec_bad", {{"reason", ec.reason}})});
  st_.checks.push_back({recipe != nullptr, recipe ? say("check.recipe", {{"name", recipe->name}}) : say("check.no_recipe")});
  bool circOk = !env.act.inhibit(c, "tank.circulation").has_value();
  st_.checks.push_back({circOk, say(circOk ? "check.circ_free" : "check.circ_blocked")});
  st_.checks.push_back({!c.rt.latches.count("ec.no_effect"), say("check.no_latch")});
  st_.checks.push_back({!env.userJob && !env.pairOpen,
                        env.userJob ? say("check.job_running") : env.pairOpen ? say("check.pair_open") : say("check.no_job")});

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
    small_ = {};  // readings before and after a calibration are not comparable
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
      disturbed_ = true;
      settleUntil_ = c.now + std::max<Ms>({kAbortRestS * kSecond, minutesParam(p.num("settle_min"), 5), cutFloor_});
    } else {
      wantsCirc_ = true;
      if (cutWaitSince_ == 0) cutWaitSince_ = std::max<Ms>(c.now, 1);
      // A refusal the gateway knows shows at once (the circulation line names why); a pump that
      // just does not come on after 2 min, as for a round waiting to dose (RAT-037, RAT-039)
      const bool late = c.now - cutWaitSince_ > 2 * kMinute;
      st_.state = !circOk || late ? "blocked" : quiet_ ? "waiting" : "working";
      st_.line = say(!circOk && cutByMix_ ? "ec.mixcut_circ_blocked"
                     : !circOk            ? "ec.cut_circ_blocked"
                     : late && cutByMix_  ? "ec.mixcut_no_circ"
                     : late               ? "ec.cut_no_circ"
                     : quiet_             ? "ec.mixcut_wait_circ"
                                          : "ec.wait_circ");
      return;
    }
  }
  // Learn the effect only from clean doses: no inlet, no foreign dose in between (RAT-057), no watering
  // and no drain-back after it (SD-103). A round only watering made unclean still counts towards "no
  // effect", by half.
  if (phase_ != Phase::Idle && (env.refilling || env.userJob || env.pairOpen || foreignDose(env, "ec-"))) {
    clean_ = false;
    disturbed_ = true;
  }
  if (phase_ != Phase::Idle && env.watering) clean_ = false;
  if (phase_ == Phase::Settling) {
    if (c.now < settleUntil_) {
      st_.state = quiet_ ? "waiting" : "working";
      st_.line = quiet_ ? say("ec.mixcut_settling", {{"left", mmss(settleUntil_ - c.now)}})
                        : say("ec.settling", {{"from", numOrNull(ecBefore_)}, {"target", numOrNull(target)}, {"round", round_},
                                              {"left", mmss(settleUntil_ - c.now)}});
      return;
    }
    cut_ = false;  // a cut round has settled: decide as after any settle time
    cutFloor_ = 0;
  }
  if (phase_ == Phase::Settling && quiet_) {
    quiet_ = false;  // nothing of its own to judge: decide afresh below
    phase_ = Phase::Idle;
  }
  if (phase_ == Phase::Settling) {
    if (!ec.usable()) {
      st_.state = "blocked";
      st_.line = say("ec.invalid", {{"reason", ec.reason}});
      reset();
      return;
    }
    double rise = *ec.value - ecBefore_;
    // A round made smaller for slow pumps that should raise EC by less than 0.06 mS/cm says too little on
    // its own: clean ones of one recipe are judged and learned together once they should raise EC by 0.06
    // between them, so any pump stays usable and a dry probe is still caught (SD-102, PD-317). Each round's
    // rise counts from its own start, so what happens to the tank between rounds stays out; a round that
    // is not clean is left out, and the sum goes on.
    const bool tiny = limited_ && isNum(expectedRise_) && expectedRise_ < kSmallRoundEc;
    // over the volume the dose went into, read when the round started (SD-103)
    const double mlPerL = isNum(volRound_) && volRound_ > 0 ? mlRound_ / volRound_ : kNaN;
    double judged = kNaN, judgedMlPerL = kNaN;  // the rise judged and learned now, and the ml per litre behind it
    if (small_.recipe != roundRecipe_ || (clean_ && !tiny)) small_ = {};
    if (clean_ && !tiny) {
      judged = rise;
      judgedMlPerL = mlPerL;
    } else if (clean_) {
      small_.recipe = roundRecipe_;
      small_.rise += rise;
      small_.expected += expectedRise_;
      if (isNum(mlPerL)) small_.mlPerL += mlPerL;
      else small_.learnable = false;
      if (small_.expected >= kSmallRoundEc) {
        judged = small_.rise;
        judgedMlPerL = small_.learnable ? small_.mlPerL : kNaN;
        small_ = {};
      }
    }
    // Learned for the recipe the round dosed, within the band, below it that recipe's start (SD-092, RAT-057)
    if (const RecipeCfg* dosed = c.cfg.recipe(roundRecipe_); dosed && isNum(judged) && isNum(judgedMlPerL) && judgedMlPerL > 0) {
      const double observed = judged / judgedMlPerL;
      c.rt.ecEffects[roundRecipe_] = c.rt.ecEffect = clampEcEffect(observed, recipeEffect(*dosed, kNaN).start);
    }
    // "No effect" counts a judged rise 2 points and the rise of a round only watering made unclean 1 (SD-103);
    // a small round that is not clean is left out, as above, and a disturbed round counts nothing
    if (isNum(judged)) noEffect_ = judged < 0.02 ? noEffect_ + 2 : 0;
    else if (!clean_ && !disturbed_ && !tiny) noEffect_ = rise < 0.02 ? noEffect_ + 1 : 0;
    if (noEffect_ >= kNoEffectPoints) {
      c.rt.latches["ec.no_effect"] = {{"at", c.epoch}};
      noEffect_ = 0;  // after the acknowledgement, 4 points again (2 clean or 4 watered rounds)
      small_ = {};
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

  // The queue emptied with no own dose running: the recipe's fixed parts changed under the round
  // (recipeChanged, RAT-085). What went in settles; with nothing in, the round ends.
  if (phase_ == Phase::Dosing && queue_.empty()) {
    const auto& own = env.doser.active();
    if (!(own && own->id.rfind("ec-", 0) == 0)) {
      if (mlRound_ > 0) {
        phase_ = Phase::Settling;
        settleUntil_ = c.now + minutesParam(p.num("settle_min"), 5);
        env.truth.expectChange("tank.ec", settleUntil_ + kMinute);
        env.truth.expectChange("tank.ph", settleUntil_ + kMinute);
        return;
      }
      reset();
    }
  }
  if (phase_ == Phase::Dosing) {
    st_.state = "working";
    st_.line = limited_ ? say("ec.dosing_limited", {{"from", numOrNull(ecBefore_)}, {"target", numOrNull(target)}, {"round", round_},
                                                    {"runs", c.cfg.limits.bounded().maxPartialRuns}})
                        : say("ec.dosing", {{"from", numOrNull(ecBefore_)}, {"target", numOrNull(target)}, {"round", round_}});
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
  if (env.userJob || env.pairOpen) {
    st_.state = "waiting";
    st_.line = say(env.userJob ? "ec.wait_job" : "ec.wait_pair");
    return;
  }
  if (env.refilling || env.refillDue) {
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
  const RecipeEffect eff = recipeEffect(*recipe, learnedEcEffect(c.rt, recipe->id));
  auto plan = planEcDose(c.cfg, c.pumps, *recipe, env.volumeL, target - *ec.value, eff.dose, eff.largest, p.num("max_ec_step"));
  if (!plan.ok) {
    st_.state = "blocked";
    st_.line = say("ec.plan", {{"reason", plan.reason}});
    return;
  }
  if (round_ == 0) startEc_ = *ec.value;
  round_++;
  roundRecipe_ = recipe->id;
  ecBefore_ = *ec.value;
  mlRound_ = 0;
  volRound_ = env.volumeL;
  clean_ = true;
  disturbed_ = false;
  limited_ = plan.limited;
  {
    double ml = 0;
    for (const auto& s : plan.steps) ml += isNum(s.ml) ? s.ml : 0;
    // at the effect the round is planned on: a recipe that learned a small effect is not expected to do more
    expectedRise_ = isNum(env.volumeL) && env.volumeL > 0 ? eff.dose * ml / env.volumeL : kNaN;
  }
  queue_ = plan.steps;
  phase_ = Phase::Dosing;
  st_.state = "working";
  st_.line = limited_ ? say("ec.dosing_limited", {{"from", *ec.value}, {"target", numOrNull(target)}, {"round", round_},
                                                  {"runs", c.cfg.limits.bounded().maxPartialRuns}})
                      : say("ec.start", {{"from", *ec.value}, {"target", numOrNull(target)}, {"round", round_}});
}

EcController::FirstStep EcController::adoptFirstStep(const Ctx& c, const FirstStepIn& in) {
  reset();
  cut_ = quiet_ = false;
  cutFloor_ = 0;
  // A step not judged (fresh water, a refill, maintenance) says nothing about the probe: what was counted stays
  if (in.judge) {
    noEffect_ = 0;
    small_ = {};
  }
  const double rise = in.ecNow - in.ecBefore;
  const bool expected = isNum(in.expectedRise);
  if (in.judge && rise < std::max(0.02, 0.25 * (expected ? in.expectedRise : 0))) {
    c.rt.latches["ec.no_effect"] = {{"at", c.epoch}};
    c.log.add(c.epoch, "alarm", "alarm", say("ev.ec.first_no_effect"),
              expected ? say("ev.ec.first_no_effect.text", {{"from", in.ecBefore}, {"to", in.ecNow}, {"expected", in.expectedRise}})
                       : say("ev.ec.first_no_effect.text_plain", {{"from", in.ecBefore}, {"to", in.ecNow}}));
    return FirstStep::Locked;
  }
  if (in.judge && expected && rise > 4 * in.expectedRise)
    c.log.add(c.epoch, "control", "warn", say("ev.ec.first_strong"), say("ev.ec.first_strong.text", {{"rise", rise}, {"expected", in.expectedRise}}));
  if (const RecipeCfg* dosed = c.cfg.recipe(in.recipe); dosed && in.learn && in.ml > 0 && isNum(in.litres) && in.litres > 0)
    c.rt.ecEffects[in.recipe] = c.rt.ecEffect = clampEcEffect(rise / (in.ml / in.litres), recipeEffect(*dosed, kNaN).start);
  phase_ = Phase::Settling;
  settleUntil_ = c.now;
  round_ = 1;
  startEc_ = ecBefore_ = in.ecBefore;
  roundRecipe_ = in.recipe;
  mlRound_ = 0;    // learned here already
  clean_ = false;  // … and judged: its settle neither learns nor counts it again
  disturbed_ = true;
  return FirstStep::Adopted;
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

void EcController::recipeChanged(const RecipeCfg& r) {
  if (r.id == small_.recipe) small_ = {};  // small rounds summed on the parts as they were (RAT-057)
  if (r.id != roundRecipe_) return;  // the recipe the round doses, whatever the phase's is now
  clean_ = false;
  disturbed_ = true;
  queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                              [&](const DoseStep& d) {
                                return std::any_of(r.steps.begin(), r.steps.end(),
                                                   [&](const RecipeStep& s) { return s.fixed && s.canister == d.canister; });
                              }),
               queue_.end());
}

void EcController::stopRound() {
  if (phase_ == Phase::Idle) return;
  cut_ = true;
  cutByMix_ = false;  // the Stop's words; a quiet settle stays quiet and keeps its floor
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
    cut_ = quiet_ = false;
    cutFloor_ = 0;
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
      disturbed_ = true;
      settleUntil_ = c.now + std::max<Ms>({kAbortRestS * kSecond, minutesParam(p.num("settle_min"), 5), cutFloor_});
    } else {
      pendingCirc_ = true;
      if (cutWaitSince_ == 0) cutWaitSince_ = std::max<Ms>(c.now, 1);
      const bool late = c.now - cutWaitSince_ > 2 * kMinute;
      st_.state = !circOk || late ? "blocked" : quiet_ ? "waiting" : "working";
      st_.line = say(!circOk && cutByMix_ ? "ph.mixcut_circ_blocked"
                     : !circOk            ? "ph.cut_circ_blocked"
                     : late && cutByMix_  ? "ph.mixcut_no_circ"
                     : late               ? "ph.cut_no_circ"
                     : quiet_             ? "ph.mixcut_wait_circ"
                                          : "ph.wait_circ");
      return;
    }
  }
  if (phase_ != Phase::Idle && (env.refilling || env.userJob || env.pairOpen || foreignDose(env, "ph-"))) {
    clean_ = false;
    disturbed_ = true;
  }
  if (phase_ != Phase::Idle && env.watering) clean_ = false;  // as in EcController (SD-103)
  if (phase_ == Phase::Settling) {
    if (c.now < settleUntil_) {
      st_.state = quiet_ ? "waiting" : "working";
      st_.line = quiet_ ? say("ph.mixcut_settling", {{"left", mmss(settleUntil_ - c.now)}})
                        : say("ph.settling", {{"from", numOrNull(phBefore_)}, {"target", numOrNull(target)}, {"n", doses_},
                                              {"max", numOrNull(p.num("max_doses"))}, {"left", mmss(settleUntil_ - c.now)}});
      return;
    }
    cut_ = false;
    cutFloor_ = 0;
  }
  if (phase_ == Phase::Settling && quiet_) {  // as in EcController
    quiet_ = false;
    phase_ = Phase::Idle;
  }
  if (phase_ == Phase::Settling) {
    if (!phOk) {
      st_.state = "blocked";
      st_.line = say("ph.invalid", {{"reason", ph.reason}});
      reset();
      return;
    }
    double drop = phBefore_ - *ph.value;
    // A dose without a measurable fall, clean or watered, tells nothing about the effect: it learns only
    // towards the smaller dose, the effect at least its start and never lower than learned, so the doses up
    // to the "no effect" latch do not grow (SD-103)
    if (!disturbed_ && drop < 0.03) {
      c.rt.phEffect = std::max(clampEffect(c.rt.phEffect, kPhStartEffect), kPhStartEffect);
    } else if (clean_ && mlLast_ > 0 && isNum(volLast_)) {
      c.rt.phEffect = clampEffect(drop / (mlLast_ / volLast_), kPhStartEffect);
    }
    if (!disturbed_) noEffect_ = drop < 0.03 ? noEffect_ + (clean_ ? 2 : 1) : 0;  // RAT-020, RAT-041, SD-103
    if (noEffect_ >= kNoEffectPoints) {
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
  if (env.userJob || env.pairOpen) {
    st_.state = "waiting";
    st_.line = say(env.userJob ? "ph.wait_job" : "ph.wait_pair");
    return;
  }
  if (env.refilling || env.refillDue) {
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
  volLast_ = env.volumeL;
  clean_ = true;
  disturbed_ = false;
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
  cutByMix_ = false;  // the Stop's words; a quiet settle stays quiet and keeps its floor
  reset();
}

// ---------------------------------------------------------------- Zulauf

namespace {

// A refill in steps with a check of each rise (SD-082). Assumptions, not measured (RAT-038):
constexpr Epoch kRefillPauseS = 10 * 60;   // pause after each fill; its rise is checked at the end
constexpr double kOpenMarginMin = 1.0;     // a fill closes this much before max_open_min
constexpr double kRiseShare = 0.5;         // the level must rise by at least this share of what flowed in
constexpr double kJudgeMinL = 4.0;         // a smaller fill is judged together with the next ones
constexpr int kMaxUnjudged = 2;            // fills in a row that could not be judged, then a person looks
constexpr Ms kSettleMs = 3 * kMinute;      // the level reading lags 1.5–2 min behind the level
constexpr Ms kSettleMaxMs = 30 * kMinute;  // a reading that never settles that long: the fill goes ahead, marked
// After irrigation the level is calm once the median of its last 90 s is at most 0.3 L above that of the
// 90 s before; a fall is calm. The readings count from 3 min after the pump stopped, when the lagging
// reading has caught up, so the first verdict comes at 6 min (SD-099, RAT-038). Assumptions, not measured.
constexpr Ms kCalmWindowMs = 90 * kSecond;
constexpr double kCalmRiseL = 0.3;
constexpr Epoch kLevelGraceS = 3 * 60;     // a check due without a valid level (right after a start) waits this long
constexpr double kFollowsL = 1.0;          // a reading that moved this much since a person's work follows the tank (RAT-030, RAT-078)

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


// Needs at least one value
double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  const size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

// Water that drains back after irrigation (ebb and flow) makes the level rise for a while: it is calm
// once the median of the last 90 s is at most 0.3 L above that of the 90 s before. A fall is calm
// (run-to-waste drip). Readings that do not yet span both windows are not, nor a window with fewer than
// three reports of the sensor: a value held between reports would look calm while the level still rises,
// so a sensor reporting less often than every 30 s waits the full half hour (SD-099).
template <typename Seen>
bool levelCalm(const std::deque<Seen>& readings, Ms since, Ms now) {
  if (readings.empty() || now - since < 2 * kCalmWindowMs) return false;
  std::vector<double> before, last;
  std::set<Ms> reportsBefore, reportsLast;
  for (const auto& r : readings) {
    if (r.at > now - kCalmWindowMs) {
      last.push_back(r.litres);
      if (r.reported > now - kCalmWindowMs) reportsLast.insert(r.reported);
    } else if (r.at > now - 2 * kCalmWindowMs) {
      before.push_back(r.litres);
      if (r.reported > now - 2 * kCalmWindowMs) reportsBefore.insert(r.reported);
    }
  }
  if (reportsBefore.size() < 3 || reportsLast.size() < 3) return false;
  return median(last) - median(before) <= kCalmRiseL;
}

// The irrigation pump draws from the tank. Bound and on, or in a state not known, it may have run.
enum class Irrigation { None, On, Unknown };
Irrigation irrigation(const Ctx& c, const ControlEnv& env) {
  if (!c.cfg.binding("zone.irrigation_pump")) return Irrigation::None;
  const auto on = env.act.roleState(c.cfg, "zone.irrigation_pump");
  return !on ? Irrigation::Unknown : *on ? Irrigation::On : Irrigation::None;
}

// The saved record is read without throwing: a damaged one never stops the hub (SD-082)
double jnumOr(const json& j, const char* key, double fallback) {
  return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<double>() : fallback;
}
bool jflag(const json& j, const char* key) { return j.is_object() && j.contains(key) && j[key].is_boolean() && j[key].get<bool>(); }
Epoch jepoch(const json& j, const char* key) {
  const double v = jnumOr(j, key, 0);
  return v > 0 && v <= static_cast<double>(kNotAfter) ? static_cast<Epoch>(v) : 0;
}

// A check added to the fills not confirmed before it: from where the first began, what flowed in all and
// how long the valve was open, on the curve the check's readings were taken on. A value not known stays
// so (R5), and the fills are then judged as damaged (SD-082)
json unconfirmedOf(const json& k, const json& u) {
  const bool earlier = u.is_object();
  const double fillL = jnumOr(k, "expectedL", kNaN);
  return {{"startL", numOrNull(earlier ? jnumOr(u, "startL", kNaN) : jnumOr(k, "startL", kNaN))},
          {"sumL", numOrNull((isNum(fillL) ? fillL : jnumOr(k, "plannedL", kNaN)) + (earlier ? jnumOr(u, "sumL", kNaN) : 0.0))},
          {"openMin", numOrNull(jnumOr(k, "openMin", kNaN) + (earlier ? jnumOr(u, "openMin", kNaN) : 0.0))},
          {"irrigation", jflag(k, "irrigation") || jflag(u, "irrigation")},
          {"restart", jflag(k, "restart") || jflag(u, "restart")},
          {"curve", k.contains("curve") ? k["curve"] : json(nullptr)}};
}

}  // namespace

void RefillController::shiftClock(RuntimeState& rt, Epoch d) {
  if (cooldownUntil_ > 0) cooldownUntil_ += d;
  if (bootEpoch_ > 0) bootEpoch_ += d;
  if (!rt.refill.is_object()) return;
  for (const char* rec : {"fill", "check"})
    if (rt.refill.contains(rec) && rt.refill[rec].is_object())
      for (const char* key : {"at", "due", "seenAt"})
        if (const Epoch e = jepoch(rt.refill[rec], key)) rt.refill[rec][key] = e + d;
}

void RefillController::toCheck(const Ctx& c, double expectedL, double openMin) {
  json& rec = c.rt.refill;
  json k = rec.contains("fill") && rec["fill"].is_object() ? rec["fill"] : json::object();
  k["at"] = c.epoch;
  k["due"] = cooldownUntil_;
  k["closedMs"] = c.now;
  k["boot"] = c.rt.bootCount;
  k["expectedL"] = numOrNull(expectedL);
  k["openMin"] = numOrNull(openMin);
  rec["check"] = k;
  rec.erase("fill");
}

void RefillController::levelCommitted(RuntimeState& rt, const json& curve) {
  // A level calibration committed a new curve during a person's work: its points needed a sensor
  // that follows the tank, so a move across this curve counts. A session cancelled, or a curve
  // imported, shows nothing of the sensor (SD-082)
  if (rt.refill.is_object() && rt.refill.contains("unconfirmed") && rt.refill["unconfirmed"].is_object() &&
      rt.refill["unconfirmed"].contains("personL"))
    rt.refill["unconfirmed"]["calibrated"] = curve;
}

void RefillController::keepForPerson(const Ctx& c, double levelNow) {
  json& rec = c.rt.refill;
  if (rec.contains("check")) {
    rec["unconfirmed"] = unconfirmedOf(rec["check"], rec.contains("unconfirmed") ? rec["unconfirmed"] : json(nullptr));
    rec.erase("check");
  }
  if (!rec.contains("unconfirmed")) return;
  json& u = rec["unconfirmed"];
  // The first valid reading of the person's work: a stuck sensor's is the same as its later ones
  if (!isNum(jnumOr(u, "personL", kNaN))) u["personL"] = numOrNull(levelNow);
}

void RefillController::latchInlet(const Ctx& c, const Msg& why, const Msg& title, const Msg& text, const json& args) {
  c.rt.latches["inlet.fault"] = {{"at", c.epoch}, {"why", why}};
  latchSeen_ = true;  // also when it is released before the next tick (SD-088)
  c.log.add(c.epoch, "alarm", "alarm", title, text, args);
  c.rt.refill.erase("check");
  c.rt.refill.erase("unconfirmed");
  c.rt.refill["unjudged"] = 0;
  endSteps();
  st_.state = "latched";
  st_.line = say("refill.latched");
}

void RefillController::tick(const Ctx& c, ControlEnv& env) {
  st_.checks.clear();
  settling_ = false;
  dueAfterDose_ = false;
  json& rec = c.rt.refill;
  if (!rec.is_object()) rec = json::object();
  for (const char* r : {"fill", "check", "unconfirmed"})
    if (rec.contains(r) && !rec[r].is_object()) rec[r] = json::object();
  const auto open = env.act.roleState(c.cfg, "tank.inlet");
  const Irrigation irr = irrigation(c, env);
  // What disturbs the level reading; a fill waits until it has settled. A start counts as one: what
  // happened before it is not known, and so do a Stop, the function switched off (SD-090) and an inlet
  // latch up to the tick that sees it released: a person may be at the tank, so the first fill after a
  // release plans from a level read after it (SD-088). Only irrigation may end the wait early, after half
  // an hour: irrigation every few minutes must not keep the tank short, and the wait for the level to
  // stop rising covers water draining back (SD-099); a hand fill or draining could make it larger
  // (RAT-038, SD-082).
  const bool latched = c.rt.latches.count("inlet.fault") > 0;
  if (!booted_ || c.stopped || c.epoch < c.maintenanceUntil || !enabled(c, "refill") || env.levelCalibrating ||
      (open.value_or(false) && !filling_) || latched || latchSeen_)
    disturbedMs_ = c.now;
  latchSeen_ = latched;
  if (irr == Irrigation::On) irrigatedMs_ = c.now;
  // The level from 3 min after the pump stopped, for the drain-back that follows it: once a second while
  // a fill may wait for it, the value repeated when the sensor reports only a change (SD-099)
  {
    const auto& lv = c.truth.get("tank.level");
    if (irr == Irrigation::On) {
      drainBack_.clear();
      calmSeen_ = false;
    } else if (!calmSeen_ && lv.usable() && c.now - irrigatedMs_ >= kSettleMs && (drainBack_.empty() || c.now - drainBack_.back().at >= kSecond)) {
      if (drainBack_.empty()) drainSince_ = c.now;
      drainBack_.push_back({c.now, *lv.value, lv.ts});
    }
    while (!drainBack_.empty() && drainBack_.front().at <= c.now - 2 * kCalmWindowMs) drainBack_.pop_front();
  }
  const Ms settleSince = settleSince_;
  settleSince_ = -1;  // kept only by a tick that waits for the reading to settle
  if (irr != Irrigation::None)
    for (const char* r : {"fill", "check", "unconfirmed"})
      if (rec.contains(r)) rec[r]["irrigation"] = true;
  if (!booted_) {
    booted_ = true;
    bootEpoch_ = c.epoch;
    // A fill the restart cut, or steps it ended, are reported and not resumed (R6, RAT-007). A cut
    // fill's rise cannot be judged; it counts as such, with what may have flowed until the start.
    if (rec.contains("fill")) {
      const json f = rec["fill"];
      const int step = static_cast<int>(std::clamp(jnumOr(f, "step", 1), 1.0, 1000.0));
      const int steps = static_cast<int>(std::clamp(jnumOr(f, "steps", 1), 1.0, 1000.0));
      c.log.add(c.epoch, "tank", "warn", say("ev.refill.restart"),
                steps > 1 ? say("ev.refill.restart.text", {{"step", step}, {"steps", steps}}) : say("ev.refill.restart.cut"));
      // Open until the last minute it was seen open, not through the outage that followed. A value
      // missing from the record is not known (R5): the fills are then judged as damaged
      const double plannedMin = jnumOr(f, "plannedMin", kNaN);
      const Epoch at = jepoch(f, "at"), seen = jepoch(f, "seenAt");
      const double ranMin = isNum(plannedMin) && at > 0 && seen >= at ? std::min(plannedMin, static_cast<double>(seen - at) / 60.0 + 1.0) : plannedMin;
      cooldownUntil_ = c.epoch + kRefillPauseS;
      toCheck(c, kNaN, ranMin);
      rec["check"]["restart"] = true;
      rec["check"]["plannedL"] = numOrNull(jnumOr(f, "flow", kNaN) * ranMin);
    } else if (rec.contains("check") && jflag(rec["check"], "more")) {
      const json& k = rec["check"];
      c.log.add(c.epoch, "tank", "info", say("ev.refill.restart"),
                say("ev.refill.restart.text", {{"step", static_cast<int>(std::clamp(jnumOr(k, "step", 1), 1.0, 1000.0))},
                                                {"steps", static_cast<int>(std::clamp(jnumOr(k, "steps", 1), 1.0, 1000.0))}}));
      rec["check"]["more"] = false;
    }
  }
  // A latch waits for a person: once they have looked, the count starts again, also when they release
  // it during a Stop, maintenance or with the function off, when the refill does not run (SD-082, SD-090)
  if (latched) {
    rec.erase("check");
    rec.erase("unconfirmed");
    rec["unjudged"] = 0;
  }
  const auto& level = c.truth.get("tank.level");
  const double levelNow = level.usable() ? *level.value : kNaN;
  if (!enabled(c, "refill")) {
    st_.state = "off";
    st_.line = say("refill.off");
    if (filling_) {
      Msg e;
      env.act.setRole(c, "tank.inlet", false, say("who.refill_off"), e);
      filling_ = false;
      cooldownUntil_ = c.epoch + kRefillPauseS;
      toCheck(c, std::min(plannedL_, flow_ * static_cast<double>(c.now - startedMs_) / kMinute), static_cast<double>(c.now - startedMs_) / kMinute);
    }
    endSteps();
    rec.erase("fill");
    keepForPerson(c, levelNow);
    return;
  }
  // A mix or a hand dose ends the steps, also one that runs only during a fill or the pause after it:
  // the next step would dilute it (SD-082)
  if (env.userJob && step_ < steps_) {
    endSteps();
    if (rec.contains("check")) rec["check"]["more"] = false;
  }
  auto p = effectiveParams(c.cat, c.cfg, "refill");
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
    const double ranMin = static_cast<double>(c.now - startedMs_) / kMinute;
    const double flowedL = std::min(plannedL_, flow_ * ranMin);
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
        // The gateway's emergency limit (latched), or a person: nothing judged now
        if (c.rt.latches.count("inlet.fault")) rec.erase("fill");
        else {
          toCheck(c, flowedL, ranMin);
          keepForPerson(c, levelNow);
        }
        st_.state = "latched";
        st_.line = say("refill.stopped");
        if (!c.rt.latches.count("inlet.fault")) automation(c, st_);
        return;
      }
      // The device's own auto-off, a person in the app, an internal error: its rise is still checked,
      // after the pause, so the refill does not open again at once
      toCheck(c, flowedL, ranMin);
      checkLine();
      return;
    }
    // Seen open, saved once a minute: a restart then counts what may have flowed until then (SD-082)
    if (rec.contains("fill") && c.epoch - jepoch(rec["fill"], "seenAt") >= 60) rec["fill"]["seenAt"] = c.epoch;
    const bool done = c.now - startedMs_ >= plannedMs_;
    const bool sensorStop = level.usable() && *level.value >= targetL_ + 1.0;
    // A person's Stop or maintenance ends the fill, and so does a level calibration (#256)
    const bool person = !automation(c, st_);
    const bool automatic = !person && !env.levelCalibrating;
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
        if (!person) {
          st_.state = "waiting";
          st_.line = say("refill.calibrating");
        }
        toCheck(c, done ? plannedL_ : flowedL, ranMin);
        keepForPerson(c, levelNow);  // a person stopped it or works at the tank: not judged now
        return;
      }
      // Judged at the end of the pause, against what flowed while the valve was open (SD-082)
      toCheck(c, done ? plannedL_ : flowedL, ranMin);
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
  // A person at the tank (Stop, maintenance, and above the function switched off): a rise checked now
  // would say nothing, but what flowed is kept, with the level read now. A reading that has moved from
  // it by the next fill follows the tank, as after a water change; one that stays, as a stuck sensor
  // does, holds the next fill to what is left (SD-082)
  if (!automation(c, st_)) {
    endSteps();
    keepForPerson(c, levelNow);
    return;
  }
  if (latched) {
    endSteps();  // its check, the fills not confirmed and the count were reset above (SD-090)
    st_.state = "latched";
    const bool unknown = unknownStateLatch(c.rt.latches.at("inlet.fault"));  // set by an unknown run-time state (SD-087)
    st_.line = say(unknown ? "refill.latched_unknown" : "refill.latched");
    return;
  }
  // A person calibrating the level may fill or drain the tank: kept as through a Stop, from the level
  // read when it began (#256, SD-082)
  if (env.levelCalibrating) keepForPerson(c, levelNow);

  // At the end of the pause: did the level rise by at least half of what flowed in? If not, a leak,
  // an empty supply, a stuck valve or a level sensor that does not follow look the same: the inlet
  // waits for a person (RAT-031, SD-082). Fills whose rise is not confirmed yet (too small to judge,
  // or not judged) are added up from where the first began; two not judged in a row latch too.
  if (rec.contains("check")) {
    json& k = rec["check"];
    // A due time far ahead (a clock that was not set before the start, a damaged record) would hold
    // EC and pH: never more than one pause from now
    const Epoch due = std::min(jepoch(k, "due"), c.epoch + kRefillPauseS);
    k["due"] = due;
    if (c.epoch < due) {
      cooldownUntil_ = std::max(cooldownUntil_, due);
      checkLine();
      return;
    }
    const json fillCurve = k.contains("curve") ? k["curve"] : json(nullptr);
    const json merged = unconfirmedOf(k, rec.contains("unconfirmed") ? rec["unconfirmed"] : json(nullptr));
    const double fillL = jnumOr(k, "expectedL", kNaN);
    const bool sameBoot = jnumOr(k, "boot", -1) == static_cast<double>(c.rt.bootCount);
    const bool fresh = !sameBoot || level.ts > static_cast<Ms>(jnumOr(k, "closedMs", 0));
    std::optional<Msg> unjudged;
    if (jflag(k, "restart") || !isNum(fillL)) unjudged = say("refill.unjudged.restart");
    else if (!level.usable()) {
      if (c.epoch < std::max(due, bootEpoch_) + kLevelGraceS) {
        st_.state = "waiting";
        st_.line = say("refill.check_wait", {{"left", mmss((std::max(due, bootEpoch_) + kLevelGraceS - c.epoch) * 1000)}});
        return;
      }
      unjudged = say("refill.unjudged.level", {{"reason", level.reason}});
    } else if (levelCurve(c) != fillCurve) {
      unjudged = say("refill.unjudged.curve");
    } else if (!fresh) {
      // A level read during the fill lags behind (RAT-038): only one read after the valve closed is judged
      unjudged = say("refill.unjudged.stale");
    }
    const double from = jnumOr(merged, "startL", kNaN), expected = jnumOr(merged, "sumL", kNaN);
    const bool irrigated = jflag(merged, "irrigation");
    if (!unjudged && (!isNum(from) || !(expected >= 0))) unjudged = say("refill.unjudged.damaged");
    bool keep = false;
    if (!unjudged) {
      const double rise = *level.value - from;
      if (rise >= kRiseShare * expected) {
        rec.erase("unconfirmed");
        rec["unjudged"] = 0;
      } else if (expected < judgeBound(c, from)) {
        keep = true;  // too small to judge yet: added to the next fill
      } else if (irrigated) {
        unjudged = say("refill.unjudged.irrigation");  // too little, but the irrigation pump drew from the tank
      } else {
        const json args = {{"rise", rise}, {"expected", expected}, {"flow", jnumOr(k, "flow", kNaN)}};
        latchInlet(c, say("why.refill_no_rise", args), say("ev.refill.no_rise"), say("ev.refill.no_rise.text", args), args);
        return;
      }
    }
    if (unjudged) {
      const int n = std::clamp(static_cast<int>(jnumOr(rec, "unjudged", 0)), 0, kMaxUnjudged - 1) + 1;
      if (n >= kMaxUnjudged) {
        const json a = {{"n", n}, {"why", *unjudged}};
        latchInlet(c, say("why.refill_unchecked", a), say("ev.refill.unchecked"), say("ev.refill.unchecked.text", a), a);
        return;
      }
      rec["unjudged"] = n;
      c.log.add(c.epoch, "tank", "warn", say("ev.refill.unjudged"), jflag(k, "more") ? say("refill.unjudged.more", {{"why", *unjudged}}) : *unjudged);
      endSteps();
      keep = true;
    }
    if (keep) rec["unconfirmed"] = merged;
    rec.erase("check");
  }

  if (env.levelCalibrating) {
    // No fill from readings taken while a person calibrates the level (#256)
    endSteps();
    st_.state = "waiting";
    st_.line = say("refill.calibrating");
    return;
  }
  if (!level.usable()) {
    endSteps();
    st_.state = "blocked";
    st_.line = say("refill.level", {{"reason", level.reason}});
    return;
  }
  double start = p.num("start_below_l"), target = p.num("target_l"), flow = p.num("flow_l_per_min"), maxOpen = p.num("max_open_min");
  if (isNum(c.cfg.tank().capacityL)) target = std::min(target, c.cfg.tank().capacityL - 1.0);
  // The next step goes on toward the target, also above the start level, with the settings it was
  // planned with
  if (step_ < steps_ && (target != targetL_ || flow != flow_ || !(maxOpen == maxOpen_ || (!isNum(maxOpen) && !isNum(maxOpen_))))) endSteps();
  const bool stepping = step_ < steps_;
  if (!stepping && !(*level.value < start)) {
    st_.state = "idle";
    st_.line = say("refill.ok", {{"level", *level.value}});
    rec.erase("unconfirmed");  // the level is back: nothing waits to be confirmed (the count stays until a rise passes)
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
  // shorter), so a late tick still closes it first; the rest comes in steps (#155, SD-082).
  const double openFitsMin = isNum(maxOpen) ? maxOpen - std::min(kOpenMarginMin, maxOpen / 2) : std::numeric_limits<double>::infinity();
  const double fits = flow * openFitsMin;
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
    // Zulauf verdünnt: nicht während einer Gabe oder ihres Einschwingens (RAT-055). Meanwhile no new
    // round starts, so the fill does not wait for a whole correction and then dilute it (SD-090)
    dueAfterDose_ = true;
    st_.state = "waiting";
    st_.line = say("refill.dosing");
    return;
  }
  // Every fill is judged from the level when it opens: not while the reading still follows irrigation,
  // draining or a valve opened by hand (RAT-038, SD-082). A reading that never settles for half an hour
  // (irrigation every few minutes) does not keep the tank short: the fill goes ahead, marked as irrigated.
  bool unsettled = false;
  if (c.now - disturbedMs_ < kSettleMs) {
    settling_ = true;  // EC and pH wait too: the fill would dilute a round started now (RAT-055, SD-090)
    st_.state = "waiting";
    st_.line = say("refill.settle", {{"left", mmss(kSettleMs - (c.now - disturbedMs_))}});
    return;
  }
  // After irrigation it waits until the level has stopped rising as well: in ebb and flow water drains
  // back from the table for a while, and a fill planned from the low level would overfill once it is
  // back (SD-099). Half an hour from the start of the wait or from the pump's stop, whichever comes
  // first, ends it: irrigation every few minutes must not keep the tank short. A drain-back still running
  // then is filled on top; the 1 L margin below the capacity is what is left.
  if (!calmSeen_ && levelCalm(drainBack_, drainSince_, c.now)) calmSeen_ = true;
  if (!calmSeen_ && c.now - irrigatedMs_ >= kSettleMaxMs) {
    calmSeen_ = true;
    unsettled = true;  // not calm after half an hour: the fill goes ahead, marked as irrigated
  }
  if (c.now - irrigatedMs_ < kSettleMs || !calmSeen_) {
    settleSince_ = settleSince < 0 ? c.now : settleSince;
    if (c.now - settleSince_ < kSettleMaxMs) {
      settling_ = true;
      st_.state = "waiting";
      const Ms left = std::min(kSettleMaxMs - (c.now - settleSince_), kSettleMaxMs - (c.now - irrigatedMs_));
      st_.line = c.now - irrigatedMs_ < kSettleMs ? say("refill.settle", {{"left", mmss(kSettleMs - (c.now - irrigatedMs_))}})
                                                   : say("refill.drain_back", {{"left", mmss(left)}});
      return;
    }
    unsettled = true;
  }
  const double gap = target - *level.value;
  double planned = 0, left = 0;
  int step = 1, steps = 1;
  if (stepping) {
    planned = std::min({leftL_, fits, gap});
    step = step_ + 1;
    steps = steps_;
  } else {
    planned = std::min(gap, fits);
    steps = gap > fits ? static_cast<int>(std::ceil(gap / fits)) : 1;
  }
  // Fills whose rise is not confirmed yet never add more than the target allows from where the first
  // began, nor stay open longer in total than one fill may: a level stuck low would otherwise make
  // each of them plan the whole gap again. When nothing more fits, they are judged now.
  if (rec.contains("unconfirmed")) {
    // Kept through a person's work, and the reading moved since: it follows the tank. Across a new
    // curve only when a level calibration committed this very curve (levelCommitted)
    const json& u = rec["unconfirmed"];
    const double personL = jnumOr(u, "personL", kNaN);
    const bool sameCurve = levelCurve(c) == (u.contains("curve") ? u["curve"] : json(nullptr));
    const bool calibrated = u.contains("calibrated") && u["calibrated"] == levelCurve(c);
    if (isNum(personL) && std::fabs(*level.value - personL) >= kFollowsL && (sameCurve || calibrated)) rec.erase("unconfirmed");
  }
  if (rec.contains("unconfirmed")) {
    const json& u = rec["unconfirmed"];
    const double from = jnumOr(u, "startL", kNaN), sum = jnumOr(u, "sumL", kNaN), openMin = jnumOr(u, "openMin", kNaN);
    const bool damaged = !isNum(from) || !(sum >= 0) || !(openMin >= 0);
    // Measured on another level curve, they cannot be compared with the level now
    const bool otherCurve = levelCurve(c) != (u.contains("curve") ? u["curve"] : json(nullptr));
    const double room = damaged || otherCurve ? 0.0 : std::min(target - from - sum, flow * (openFitsMin - openMin));
    if (!(room >= 0.5)) {
      // Judged now. "Did not rise" only when the rise could have shown: never for fills the irrigation
      // pump drew from, a restart cut, too small to judge, measured on another curve, or a damaged
      // record; those ask a person too
      const double rise = *level.value - from;
      if (!damaged && !otherCurve && rise >= kRiseShare * sum) {
        rec.erase("unconfirmed");  // it rose after all
        rec["unjudged"] = 0;
      } else {
        std::optional<Msg> why;
        if (damaged) why = say("refill.unjudged.damaged");
        else if (otherCurve) why = say("refill.unjudged.curve");
        else if (jflag(u, "irrigation")) why = say("refill.unjudged.irrigation");
        else if (jflag(u, "restart")) why = say("refill.unjudged.restart");
        else if (sum < judgeBound(c, from)) why = say("refill.unjudged.small");
        if (why) {
          const json a = {{"why", *why}};
          latchInlet(c, say("why.refill_unconfirmed", a), say("ev.refill.unchecked"), say("ev.refill.unconfirmed.text", a), a);
        } else {
          const json a = {{"rise", rise}, {"expected", sum}, {"flow", flow}};
          latchInlet(c, say("why.refill_no_rise", a), say("ev.refill.no_rise"), say("ev.refill.no_rise.text", a), a);
        }
        return;
      }
    } else {
      planned = std::min(planned, room);
    }
  }
  left = step < steps ? (stepping ? leftL_ : gap) - planned : 0;
  if (!(planned >= 0.5)) {
    endSteps();
    st_.state = "idle";
    st_.line = say("refill.near");
    return;
  }
  const Ms plannedMs = static_cast<Ms>(planned / flow * kMinute);
  // Recorded before the valve opens, so a failure later in this tick still leaves the fill on record
  rec["fill"] = {{"at", c.epoch}, {"seenAt", c.epoch}, {"startL", *level.value}, {"plannedL", planned}, {"plannedMin", static_cast<double>(plannedMs) / kMinute}, {"flow", flow},
                 {"step", step}, {"steps", steps}, {"irrigation", irr != Irrigation::None || unsettled}, {"curve", levelCurve(c)}};
  Msg e;
  if (!env.act.setRole(c, "tank.inlet", true, say("who.refill"), e)) {
    rec.erase("fill");
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
  plannedMs_ = plannedMs;
  step_ = step;
  steps_ = steps;
  leftL_ = left;
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
