// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/control.hpp"

#include <algorithm>
#include <cmath>

namespace gc {

void to_json(json& j, const CtlStatus& s) {
  json checks = json::array();
  for (const auto& c : s.checks) checks.push_back({{"ok", c.ok}, {"text", c.text}});
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

Msg line(const std::string& key, const std::string& text, json args = json::object()) {
  return {key, text, std::move(args)};
}

std::string mmss(Ms ms) {
  Ms s = std::max<Ms>(0, ms / 1000);
  std::string sec = std::to_string(s % 60);
  return std::to_string(s / 60) + ":" + (sec.size() < 2 ? "0" + sec : sec);
}

bool automation(const Ctx& c, CtlStatus& st) {
  if (c.stopped) {
    st.state = "blocked";
    st.line = line("ctl.stopped", "Gesperrt: Not-Halt aktiv");
    return false;
  }
  if (c.epoch < c.maintenanceUntil) {
    st.state = "waiting";
    st.line = line("ctl.maintenance", "Ruht: Pflegemodus", {{"until", c.maintenanceUntil}});
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
    st_.line = line("ec.off", "Aus");
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

  st_.checks.push_back({ec.usable(), ec.usable() ? "EC-Sonde liefert gültige Werte" : "EC: " + ec.reason.text});
  st_.checks.push_back({recipe != nullptr, recipe ? "Rezept: " + recipe->name : "Kein Rezept"});
  bool circOk = !env.act.inhibit(c, "tank.circulation").has_value();
  st_.checks.push_back({circOk, circOk ? "Umwälzpumpe frei" : "Umwälzpumpe gesperrt"});
  st_.checks.push_back({!c.rt.latches.count("ec.no_effect"), "Keine Rastung"});
  st_.checks.push_back({!env.userJob, env.userJob ? "Ein Auftrag läuft" : "Kein anderer Auftrag"});

  if (!automation(c, st_)) {
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, "Automatik aus");
    reset();
    return;
  }
  if (env.calibrating) {
    st_.state = "waiting";
    st_.line = line("ctl.calibrating", "Wartet: Sonde wird kalibriert");
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, "Sonde wird kalibriert");
    reset();
    return;
  }
  if (c.rt.latches.count("ec.no_effect")) {
    st_.state = "latched";
    st_.line = line("ec.latched", "Gerastet: EC stieg nach zwei Runden nicht – Pumpen und Kanister prüfen, dann quittieren");
    reset();
    return;
  }

  // Wirkung nur aus sauberen Gaben lernen: kein Zulauf, keine fremde Gabe dazwischen (RAT-057).
  if (phase_ != Phase::Idle && (env.refilling || env.userJob || foreignDose(env, "ec-"))) clean_ = false;
  if (phase_ == Phase::Settling) {
    if (c.now < settleUntil_) {
      st_.state = "working";
      st_.line = line("ec.settling", "Regelt: EC " + fmt(ecBefore_, 2) + " → " + fmt(target, 2) + " · Runde " +
                                         std::to_string(round_) + " · wartet " + mmss(settleUntil_ - c.now) +
                                         " auf Durchmischung");
      return;
    }
    if (!ec.usable()) {
      st_.state = "blocked";
      st_.line = line("ec.invalid", "Gesperrt: EC – " + ec.reason.text);
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
      c.log.add(c.epoch, "alarm", "alarm", "EC-Nachdosierung ohne Wirkung",
                "EC stieg nach zwei Runden nicht. Gerastet bis zur Quittierung.");
      reset();
      return;
    }
    if (*ec.value >= target - tol || round_ >= countParam(p.num("max_doses"), 1)) {
      bool reached = *ec.value >= target - tol;
      c.log.add(c.epoch, "control", reached ? "info" : "warn",
                reached ? "EC nachdosiert" : "EC-Nachdosierung: Höchstzahl Runden",
                "EC " + fmt(startEc_, 2) + " → " + fmt(*ec.value, 2) + " in " + std::to_string(round_) +
                    (round_ == 1 ? " Runde" : " Runden"),
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
    st_.line = line("ec.dosing", "Regelt: EC " + fmt(ecBefore_, 2) + " → " + fmt(target, 2) + " · Runde " +
                                     std::to_string(round_) + " · dosiert");
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
        st_.line = line("ec.dose_failed", "Gesperrt: " + err.text);
        reset();
      }
    } else if (!env.circulationOn && !env.doser.busy() && !queue_.empty()) {
      // Kommt die Umwälzpumpe nicht (Rolle entfernt, Ausgang gestört), bricht die
      // Runde ab, statt ewig „beschäftigt“ zu bleiben.
      if (circWaitSince_ == 0) circWaitSince_ = std::max<Ms>(c.now, 1);
      if (!c.cfg.binding("tank.circulation") || c.now - circWaitSince_ > 2 * kMinute) {
        c.log.add(c.epoch, "control", "warn", "EC-Runde abgebrochen", "Die Umwälzpumpe lief nicht an. Ohne Durchmischung keine Dosierung.");
        st_.state = "blocked";
        st_.line = line("ec.circ_failed", "Gesperrt: Umwälzpumpe lief nicht an – Runde abgebrochen");
        cooldownUntil_ = c.epoch + 10 * 60;
        reset();
        return;
      }
      st_.line = line("ec.wait_circ", "Regelt: Umwälzpumpe startet");
    }
    if (env.circulationOn) circWaitSince_ = 0;
    return;
  }

  // Idle
  if (!ec.usable()) {
    st_.state = "blocked";
    st_.line = line("ec.invalid", "Gesperrt: EC – " + ec.reason.text);
    return;
  }
  if (!recipe) {
    st_.state = "blocked";
    st_.line = line("ec.no_recipe", "Gesperrt: kein Rezept gewählt");
    return;
  }
  if (*ec.value > target + tol) {
    st_.state = "idle";
    st_.line = line("ec.above", "Ruht: EC " + fmt(*ec.value, 2) + " über Ziel – senken geht nur mit frischem Wasser");
    return;
  }
  if (*ec.value >= target - tol) {
    st_.state = "idle";
    st_.line = line("ec.ok", "Ruht: EC im Ziel (" + fmt(*ec.value, 2) + " mS/cm)");
    round_ = 0;
    return;
  }
  if (c.epoch < cooldownUntil_) {
    st_.state = "waiting";
    st_.line = line("ec.cooldown", "Wartet: nächster Versuch nach Pause");
    return;
  }
  if (env.userJob) {
    st_.state = "waiting";
    st_.line = line("ec.wait_job", "Wartet: ein Auftrag läuft");
    return;
  }
  if (env.refilling) {
    st_.state = "waiting";
    st_.line = line("ec.wait_refill", "Wartet: Nachfüllen läuft");
    return;
  }
  if (env.phBusy) {
    st_.state = "waiting";
    st_.line = line("ec.wait_ph", "Wartet: pH-Korrektur läuft noch");
    return;
  }
  if (!circOk) {
    st_.state = "blocked";
    st_.line = line("ec.no_circ", "Gesperrt: Umwälzpumpe gesperrt – ohne Durchmischung keine Dosierung");
    return;
  }
  auto plan = planEcDose(c.cfg, c.pumps, *recipe, env.volumeL, target - *ec.value,
                         clampEffect(c.rt.ecEffect, kEcStartEffect), kEcStartEffect, p.num("max_ec_step"));
  if (!plan.ok) {
    st_.state = "blocked";
    st_.line = line("ec.plan", "Gesperrt: " + plan.reason.text);
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
  st_.line = line("ec.start", "Regelt: EC " + fmt(*ec.value, 2) + " → " + fmt(target, 2) + " · Runde " +
                                  std::to_string(round_));
}

void EcController::onDoseFinished(const Ctx& c, ControlEnv& env, const DoseProgress& p) {
  mlRound_ += p.mlDone;
  lastDoseAt_ = c.epoch;
  if (p.state != DoseProgress::State::Done) {
    c.log.add(c.epoch, "block", "warn", "EC-Nachdosierung abgebrochen", p.error.text);
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
    st_.line = line("ph.off", "Aus");
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

  st_.checks.push_back({phOk, phOk ? "pH-Sonde liefert gültige Werte" : "pH: " + ph.reason.text});
  st_.checks.push_back({gateOk, ecOk ? (gateOk ? "EC " + fmt(*ec.value, 2) + " über " + fmt(floor, 2) + ": pH messbar"
                                               : "EC " + fmt(*ec.value, 2) + " unter " + fmt(floor, 2) +
                                                     ": pH so nicht messbar, erst Nährstoffe")
                                         : "EC: " + ec.reason.text});
  st_.checks.push_back({restOk, restOk ? "Ruhezeit nach EC-Gabe vorbei"
                                       : "Ruhezeit nach EC-Gabe: noch " + mmss((kEcRestS - sinceEc) * 1000)});
  st_.checks.push_back({!env.ecBusy, env.ecBusy ? "EC-Nachdosierung zuerst" : "EC-Nachdosierung fertig"});
  st_.checks.push_back({down != nullptr, down ? "pH− eingemessen: " + down->name : "Kein eingemessenes pH−"});
  st_.checks.push_back({circOk, circOk ? "Umwälzpumpe frei" : "Umwälzpumpe gesperrt"});

  if (!automation(c, st_)) {
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, "Automatik aus");
    reset();
    return;
  }
  if (env.calibrating) {
    st_.state = "waiting";
    st_.line = line("ctl.calibrating", "Wartet: Sonde wird kalibriert");
    if (phase_ == Phase::Dosing && env.doser.busy()) env.doser.abort(c, env.act, "Sonde wird kalibriert");
    reset();
    return;
  }
  if (c.rt.latches.count("ph.no_effect")) {
    st_.state = "latched";
    st_.line = line("ph.latched", "Gerastet: pH bewegte sich nach zwei Gaben nicht – Kanister und Sonde prüfen, dann quittieren");
    reset();
    return;
  }

  if (phase_ != Phase::Idle && (env.refilling || env.userJob || foreignDose(env, "ph-"))) clean_ = false;
  if (phase_ == Phase::Settling) {
    if (c.now < settleUntil_) {
      st_.state = "working";
      st_.line = line("ph.settling", "Regelt: pH " + fmt(phBefore_, 2) + " → " + fmt(target, 2) + " · Teilgabe " +
                                         std::to_string(doses_) + " von " + fmt(p.num("max_doses"), 0) + " · wartet " +
                                         mmss(settleUntil_ - c.now) + " auf Durchmischung");
      return;
    }
    if (!phOk) {
      st_.state = "blocked";
      st_.line = line("ph.invalid", "Gesperrt: pH – " + ph.reason.text);
      reset();
      return;
    }
    double drop = phBefore_ - *ph.value;
    if (clean_ && mlLast_ > 0 && isNum(env.volumeL)) c.rt.phEffect = clampEffect(drop / (mlLast_ / env.volumeL), kPhStartEffect);
    if (clean_) noEffect_ = drop < 0.03 ? noEffect_ + 1 : 0;  // RAT-020, RAT-041
    if (noEffect_ >= 2) {
      c.rt.latches["ph.no_effect"] = {{"at", c.epoch}};
      noEffect_ = 0;
      c.log.add(c.epoch, "alarm", "alarm", "pH-Regelung ohne Wirkung", "pH bewegte sich nach zwei Gaben nicht. Gerastet.");
      reset();
      return;
    }
    if (*ph.value <= target + tol || doses_ >= countParam(p.num("max_doses"), 1)) {
      bool reached = *ph.value <= target + tol;
      c.log.add(c.epoch, "control", reached ? "info" : "warn", reached ? "pH korrigiert" : "pH-Korrektur: Höchstzahl Gaben",
                "pH " + fmt(startPh_, 2) + " → " + fmt(*ph.value, 2) + " mit " + std::to_string(doses_) +
                    (doses_ == 1 ? " Gabe" : " Gaben"),
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
    st_.line = line("ph.dosing", "Regelt: pH " + fmt(phBefore_, 2) + " → " + fmt(target, 2) + " · Teilgabe " +
                                     std::to_string(doses_ + 1) + " · dosiert");
    return;
  }

  // Idle: Reihenfolge der Sperren wie in der Checkliste.
  auto block = [&](const std::string& key, const std::string& text) {
    st_.state = "blocked";
    st_.line = line(key, "Gesperrt: " + text);
  };
  if (!phOk) return block("ph.invalid", "pH – " + ph.reason.text);
  if (!ecOk) return block("ph.ec_invalid", "EC ungültig – pH-Regelung gesperrt");
  if (!gateOk) return block("ph.gate", "EC " + fmt(*ec.value, 2) + " unter " + fmt(floor, 2) + ": pH so nicht messbar, erst Nährstoffe");
  if (*ph.value <= target + tol) {
    st_.state = "idle";
    doses_ = 0;
    if (*ph.value < target - tol)
      st_.line = line("ph.below", "Ruht: pH " + fmt(*ph.value, 2) + " unter Ziel – pH+ ist nicht vorgesehen, nur absenken");
    else
      st_.line = line("ph.ok", "Ruht: pH im Ziel (" + fmt(*ph.value, 2) + ")");
    return;
  }
  if (c.epoch < cooldownUntil_) {
    st_.state = "waiting";
    st_.line = line("ph.cooldown", "Wartet: nächster Versuch nach Pause");
    return;
  }
  if (env.ecBusy) {
    st_.state = "waiting";
    st_.line = line("ph.wait_ec", "Wartet: EC zuerst, pH ist immer der letzte Schritt");
    return;
  }
  if (!restOk) {
    st_.state = "waiting";
    st_.line = line("ph.wait_rest", "Wartet: Ruhezeit nach EC-Gabe, noch " + mmss((kEcRestS - sinceEc) * 1000));
    return;
  }
  if (env.userJob) {
    st_.state = "waiting";
    st_.line = line("ph.wait_job", "Wartet: ein Auftrag läuft");
    return;
  }
  if (env.refilling) {
    st_.state = "waiting";
    st_.line = line("ph.wait_refill", "Wartet: Nachfüllen läuft");
    return;
  }
  if (!down) return block("ph.no_down", "kein eingemessenes pH− zugeordnet");
  if (!circOk) return block("ph.no_circ", "Umwälzpumpe gesperrt – ohne Durchmischung keine Dosierung");
  if (!env.circulationOn) {
    st_.state = "working";
    st_.line = line("ph.wait_circ", "Regelt: Umwälzpumpe startet");
    pendingCirc_ = true;
    return;
  }
  auto plan = planPhDose(*ph.value, target, clampEffect(c.rt.phEffect, kPhStartEffect), env.volumeL, p.num("max_ml_per_dose"));
  if (!plan.ok) return block("ph.plan", plan.reason.text);
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
    st_.line = line("ph.too_small", "Ruht: Korrektur wäre kleiner als ein genauer Pumpenlauf (" + fmt(plan.ml, 2) + " ml)");
    return;
  }
  DoseOrder o;
  o.id = "ph-" + std::to_string(++seq_);
  o.purpose = "ph";
  o.step = s;
  if (!env.doser.start(c, env.act, o, err)) return block("ph.dose_failed", err.text);
  if (doses_ == 0) startPh_ = *ph.value;
  phBefore_ = *ph.value;
  clean_ = true;
  phase_ = Phase::Dosing;
  st_.state = "working";
  st_.line = line("ph.dosing", "Regelt: pH " + fmt(*ph.value, 2) + " → " + fmt(target, 2) + " · Teilgabe " +
                                   std::to_string(doses_ + 1) + " · " + fmt(plan.ml, 1) + " ml");
}

void PhController::onDoseFinished(const Ctx& c, ControlEnv& env, const DoseProgress& p) {
  if (p.state != DoseProgress::State::Done) {
    c.log.add(c.epoch, "block", "warn", "pH-Korrektur abgebrochen", p.error.text);
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
    st_.line = line("refill.off", "Aus");
    if (filling_) {
      Msg e;
      env.act.setRole(c, "tank.inlet", false, "Nachfüllen ausgeschaltet", e);
      filling_ = false;
    }
    return;
  }
  auto p = effectiveParams(c.cat, c.cfg, "refill");
  const auto& level = c.truth.get("tank.level");
  st_.checks.push_back({level.usable(), level.usable() ? "Füllstand gültig" : "Füllstand: " + level.reason.text});
  auto inh = env.act.inhibit(c, "tank.inlet");
  st_.checks.push_back({!inh, inh ? inh->text : "Zulauf frei"});
  auto open = env.act.roleState(c.cfg, "tank.inlet");

  if (filling_) {
    // Menge wird gerechnet, der Sensor ist nur Prüfung und Notabschaltung (RAT-001).
    bool done = c.now - startedMs_ >= plannedMs_;
    bool sensorStop = level.usable() && *level.value >= p.num("target_l") + 1.0;
    if (!open || !*open) {
      filling_ = false;  // vom Gateway abgeschaltet (Notgrenze, Zeitlimit, Not-Halt)
      st_.state = "latched";
      st_.line = line("refill.stopped", "Gerastet: Zulauf wurde abgeschaltet – Ereignisse prüfen");
      return;
    }
    if (done || sensorStop || !automation(c, st_)) {
      Msg e;
      env.act.setRole(c, "tank.inlet", false, "Nachfüllen fertig", e);
      filling_ = false;
      cooldownUntil_ = c.epoch + 10 * 60;
      double added = level.usable() ? *level.value - startL_ : kNaN;
      c.log.add(c.epoch, "tank", "info", "Nachgefüllt",
                "+" + fmt(added, 1) + " L gemessen, " + fmt(plannedL_, 1) + " L berechnet",
                {{"plannedL", plannedL_}, {"measuredL", numOrNull(added)}});
      st_.state = "idle";
      st_.line = line("refill.done", "Ruht: nachgefüllt");
      return;
    }
    st_.state = "working";
    st_.line = line("refill.filling", "Füllt: " + fmt(startL_, 1) + " → " + fmt(p.num("target_l"), 1) + " L · noch " +
                                          mmss(plannedMs_ - (c.now - startedMs_)));
    return;
  }
  if (!automation(c, st_)) return;
  if (c.rt.latches.count("inlet.fault")) {
    st_.state = "latched";
    st_.line = line("refill.latched", "Gerastet: Zulauf nach einem Fehler gesperrt – prüfen, dann quittieren");
    return;
  }
  if (!level.usable()) {
    st_.state = "blocked";
    st_.line = line("refill.level", "Gesperrt: Füllstand – " + level.reason.text);
    return;
  }
  double start = p.num("start_below_l"), target = p.num("target_l"), flow = p.num("flow_l_per_min");
  if (isNum(c.cfg.tank().capacityL)) target = std::min(target, c.cfg.tank().capacityL - 1.0);
  if (*level.value >= start) {
    st_.state = "idle";
    st_.line = line("refill.ok", "Ruht: Füllstand " + fmt(*level.value, 1) + " L");
    return;
  }
  if (c.epoch < cooldownUntil_) {
    st_.state = "waiting";
    st_.line = line("refill.cooldown", "Wartet: Pause nach dem letzten Nachfüllen");
    return;
  }
  if (inh) {
    st_.state = "blocked";
    st_.line = line("refill.inhibit", "Gesperrt: " + inh->text);
    return;
  }
  if (!isNum(flow) || flow <= 0) {
    st_.state = "blocked";
    st_.line = line("refill.flow", "Gesperrt: Zulaufrate fehlt");  // kein Ersatzwert (RAT-038)
    return;
  }
  if (env.doser.busy() || env.userJob || env.ecBusy || env.phBusy) {
    // Zulauf verdünnt: nicht während einer Gabe oder ihres Einschwingens (RAT-055)
    st_.state = "waiting";
    st_.line = line("refill.dosing", "Wartet: Dosierung läuft – Nachfüllen danach");
    return;
  }
  plannedL_ = target - *level.value;
  if (plannedL_ < 0.5) {
    st_.state = "idle";
    st_.line = line("refill.near", "Ruht: Ziel fast erreicht");
    return;
  }
  plannedMs_ = static_cast<Ms>(plannedL_ / flow * kMinute);
  Msg e;
  if (!env.act.setRole(c, "tank.inlet", true, "Nachfüllen", e)) {
    st_.state = "blocked";
    st_.line = line("refill.open", "Gesperrt: " + e.text);
    return;
  }
  filling_ = true;
  startedMs_ = c.now;
  startL_ = *level.value;
  st_.state = "working";
  st_.line = line("refill.start", "Füllt: " + fmt(plannedL_, 1) + " L");
}

// ---------------------------------------------------------------- Umwälzen

void CirculationController::tick(const Ctx& c, ControlEnv& env, bool demand) {
  st_.checks.clear();
  if (!c.cfg.binding("tank.circulation")) {
    st_.state = "off";
    st_.line = line("circ.unbound", "Keine Umwälzpumpe zugeordnet");
    env.circulationOn = false;
    return;
  }
  bool on = enabled(c, "circulation");
  bool desired = demand;
  std::string why = demand ? "für Dosierung" : "";
  if (on && !c.stopped) {
    auto p = effectiveParams(c.cat, c.cfg, "circulation");
    if (p.str("mode") == "always") {
      desired = true;
      why = "immer an";
    } else {
      Epoch period = static_cast<Epoch>(p.num("period_min") * 60), onS = static_cast<Epoch>(p.num("on_min") * 60);
      if (period > 0 && c.epoch % period < onS) {
        desired = true;
        if (!demand) why = "Intervall";
      }
    }
  }
  auto inh = env.act.inhibit(c, "tank.circulation");
  st_.checks.push_back({!inh, inh ? inh->text : "Trockenlaufschutz ok"});
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
    st_.line = line("circ.blocked", (st_.state == "latched" ? "" : "Gesperrt: ") + inh->text);
  } else if (isOn) {
    st_.state = "working";
    st_.line = line("circ.on", "Läuft (" + why + ")");
  } else {
    st_.state = on ? "idle" : "off";
    st_.line = line("circ.idle", on ? "Ruht bis zum nächsten Intervall" : "Aus – läuft nur bei Dosierungen");
  }
}

}  // namespace gc
