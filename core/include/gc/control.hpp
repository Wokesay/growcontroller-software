// SPDX-License-Identifier: AGPL-3.0-or-later
// Schicht 7: Regelfunktionen. Sie lesen nur die Sensorwahrheit und ihre
// wirksamen Parameter (ParamView), nie Phasennamen (R4), und stellen nur
// Anträge an das Aktor-Gateway (R1).
//
// Jeder Regler liefert eine Regelzeile mit genau einem von wenigen Zuständen
// und eine Checkliste der Bedingungen ("Warum dosiert er gerade nicht?",
// Vorschlag anwender; Quelle: RAT-037, RAT-039, RAT-046).
#pragma once

#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "gc/dosing.hpp"

namespace gc {

struct Check {
  bool ok = false;
  Msg msg;
};

struct CtlStatus {
  std::string state = "off";  // off | idle | working | waiting | blocked | latched
  Msg line;
  std::vector<Check> checks;
  json info = json::object();
};
void to_json(json& j, const CtlStatus& s);

struct ControlEnv {
  Actuators& act;
  Doser& doser;
  SensorTruth& truth;
  bool userJob = false;      // a mix, manual dose, pump calibration or tube priming is active
  bool circulationOn = false;
  double volumeL = kNaN;     // Füllstand, sonst bekanntes Volumen aus Mischläufen
  Epoch lastEcDoseAt = 0;
  bool ecBusy = false;
  bool phBusy = false;       // pH dosiert oder schwingt ein: EC-Runde wartet (RAT-057)
  bool refilling = false;    // Zulauf offen: Wirkung nicht messbar, nicht dosieren (RAT-055)
  bool calibrating = false;  // Sonde in Pufferlösung: Messwerte gelten nicht für den Tank
  bool levelCalibrating = false;  // the level head is being calibrated: its readings say nothing about a refill (SD-081)
};

class EcController {
 public:
  void tick(const Ctx& c, ControlEnv& env);
  void onDoseFinished(const Ctx& c, ControlEnv& env, const DoseProgress& p);
  // A Stop ended the round while it dosed or settled (RAT-036). Once automation runs again, the cut
  // round waits for the circulation pump and then settles, as after a restart (SD-028, SD-073): no
  // decision on a reading the cut dose has not mixed into (RAT-052). The mark survives reset()
  // until the settle time has run out, and ends when the function is switched off.
  void stopRound();
  // The cut round waits for the circulation pump: it holds the other controller, but not refill,
  // which the pump may need to bring the level back (RAT-047)
  bool pending() const { return cut_ && phase_ == Phase::Idle; }
  void reset() {
    phase_ = Phase::Idle;
    queue_.clear();
    circWaitSince_ = 0;
    cutWaitSince_ = 0;
  }
  const CtlStatus& status() const { return st_; }
  bool busy() const { return phase_ != Phase::Idle; }
  bool wantsCirculation() const { return phase_ != Phase::Idle || wantsCirc_; }
  Epoch lastDoseAt() const { return lastDoseAt_; }
  // The wall clock stepped by `d`: keep the distance of the pauses.
  void shiftClock(Epoch d) {
    if (lastDoseAt_ > 0) lastDoseAt_ += d;
    if (cooldownUntil_ > 0) cooldownUntil_ += d;
  }

 private:
  enum class Phase { Idle, Dosing, Settling } phase_ = Phase::Idle;
  std::vector<DoseStep> queue_;
  int round_ = 0, noEffect_ = 0, seq_ = 0;
  bool clean_ = true;  // Wirkung nur aus sauberen Gaben lernen (ohne Zulauf dazwischen)
  bool cut_ = false;        // a Stop cut the round: settle it once the circulation pump runs
  bool wantsCirc_ = false;  // the cut round waits for the circulation pump
  double ecBefore_ = kNaN, mlRound_ = 0, startEc_ = kNaN, target_ = kNaN;
  Ms circWaitSince_ = 0;  // seit wann die Runde auf die Umwälzpumpe wartet
  Ms cutWaitSince_ = 0;   // since when the cut round waits for the circulation pump
  Ms settleUntil_ = 0;
  Epoch lastDoseAt_ = 0, cooldownUntil_ = 0;
  CtlStatus st_;
};

class PhController {
 public:
  void tick(const Ctx& c, ControlEnv& env);
  void onDoseFinished(const Ctx& c, const DoseProgress& p);
  void stopRound();  // as EcController::stopRound
  bool pending() const { return cut_ && phase_ == Phase::Idle; }
  void reset() {
    phase_ = Phase::Idle;
    cutWaitSince_ = 0;
  }
  const CtlStatus& status() const { return st_; }
  bool busy() const { return phase_ != Phase::Idle; }
  bool wantsCirculation() const { return phase_ != Phase::Idle || pendingCirc_; }
  void shiftClock(Epoch d) {
    if (cooldownUntil_ > 0) cooldownUntil_ += d;
  }

 private:
  enum class Phase { Idle, Dosing, Settling } phase_ = Phase::Idle;
  bool pendingCirc_ = false;
  bool clean_ = true;
  bool cut_ = false;     // as in EcController
  Ms cutWaitSince_ = 0;
  int doses_ = 0, noEffect_ = 0, seq_ = 0;
  double phBefore_ = kNaN, mlLast_ = 0, startPh_ = kNaN;
  Ms settleUntil_ = 0;
  Epoch cooldownUntil_ = 0;
  CtlStatus st_;
};

class RefillController {
 public:
  void tick(const Ctx& c, ControlEnv& env);
  const CtlStatus& status() const { return st_; }
  bool filling() const { return filling_; }
  // From the first opening of a refill until the rise of its last fill is checked, EC and pH wait:
  // a round in a tank half filled would be diluted by the next step (RAT-055, RAT-057, SD-081)
  bool holding(const RuntimeState& rt) const {
    return filling_ || step_ < steps_ || (rt.refill.is_object() && rt.refill.contains("check"));
  }
  void shiftClock(RuntimeState& rt, Epoch d);
  // A level calibration of the bound level sensor committed this new curve (SD-081)
  static void levelCommitted(RuntimeState& rt, const json& curve);

 private:
  void endSteps() {
    steps_ = step_;
    leftL_ = 0;
  }
  // The open fill becomes a check of its rise at the end of the pause (SD-081)
  void toCheck(const Ctx& c, double expectedL, double openMin);
  // A person stopped the refill or works at the tank: the check and the fills not confirmed are kept as
  // not confirmed, with the level read when the work began (SD-081)
  void keepForPerson(const Ctx& c, double levelNow);
  void latchInlet(const Ctx& c, const Msg& why, const Msg& title, const Msg& text, const json& args);
  bool booted_ = false;
  bool filling_ = false;
  Ms startedMs_ = 0, plannedMs_ = 0;
  Ms disturbedMs_ = 0;  // the level reading was last disturbed by maintenance, a hand on the valve or a start
  Ms irrigatedMs_ = std::numeric_limits<Ms>::min() / 2;  // the irrigation pump last ran; not since the start
  Ms settleSince_ = -1;  // a fill has waited since then for the reading to settle after irrigation
  Epoch bootEpoch_ = 0;
  double startL_ = kNaN, plannedL_ = kNaN;
  // The refill as planned at its first fill: target, rate and open time, then step by step the
  // litres left and the fixed number of steps; the total is computed once (RAT-001, SD-081)
  double targetL_ = kNaN, flow_ = kNaN, maxOpen_ = kNaN, leftL_ = 0;
  int step_ = 0, steps_ = 0;
  Epoch cooldownUntil_ = 0;
  CtlStatus st_;
};

class CirculationController {
 public:
  void tick(const Ctx& c, ControlEnv& env, bool demand);
  const CtlStatus& status() const { return st_; }

 private:
  CtlStatus st_;
};

}  // namespace gc
