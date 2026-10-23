// SPDX-License-Identifier: AGPL-3.0-or-later
// Schicht 7: Regelfunktionen. Sie lesen nur die Sensorwahrheit und ihre
// wirksamen Parameter (ParamView), nie Phasennamen (R4), und stellen nur
// Anträge an das Aktor-Gateway (R1).
//
// Jeder Regler liefert eine Regelzeile mit genau einem von wenigen Zuständen
// und eine Checkliste der Bedingungen ("Warum dosiert er gerade nicht?",
// Vorschlag anwender; Quelle: RAT-037, RAT-039, RAT-046).
#pragma once

#include <deque>
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
  bool pairOpen = false;     // a mix to the target stopped with a pair half in: EC and pH wait until it is resumed or ended (SD-102)
  bool circulationOn = false;
  double volumeL = kNaN;     // Füllstand, sonst bekanntes Volumen aus Mischläufen
  Epoch lastEcDoseAt = 0;
  bool ecBusy = false;
  bool phBusy = false;       // pH dosiert oder schwingt ein: EC-Runde wartet (RAT-057)
  bool refilling = false;    // Zulauf offen: Wirkung nicht messbar, nicht dosieren (RAT-055)
  bool refillDue = false;    // a fill is due and waits for a dose to settle: no new round meanwhile (SD-090)
  bool calibrating = false;  // Sonde in Pufferlösung: Messwerte gelten nicht für den Tank
  bool levelCalibrating = false;  // the level head is being calibrated: its readings say nothing about a refill (SD-082)
  bool watering = false;     // the watering pump runs or its state is not known, or either, or its binding or the hub's start, was less than 10 min ago (SD-103)
};

// Where EC control aims: its target minus what the pH− dose that follows adds (RAT-053); NaN without
// a target. A mix to the target aims at the same point (SD-102).
double ecAim(const Ctx& c);

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
  // Nutrients went in outside a round that settled them (a mix cut, failed or interrupted, SD-057): wait
  // for the circulation pump and settle first, as a round a Stop cut does (SD-073), for at least
  // `floor`. byStop keeps the Stop's words on the line. Without a round of its own, the settle ends
  // without judging one: nothing is learned or logged.
  void settleFirst(bool byStop, Ms floor) {
    quiet_ = (cut_ && quiet_) || phase_ == Phase::Idle;  // a quiet settle stays quiet when cut again
    cutFloor_ = cut_ ? std::max(cutFloor_, floor) : floor;
    cut_ = true;
    cutByMix_ = !byStop;
    reset();
  }
  bool dosing() const { return phase_ == Phase::Dosing; }
  // The first step of a mix to the target (way 1, SD-102), mixed in by the mix, as this controller's round 1
  // (D7): judged at once (D6) and, when clean, learned from the rise over all ml that went in, the fixed
  // parts taken as strong as the rest, as the plan counts them (D5); then decided in this tick's own run of
  // tick, which follows the mix's, as after a round's settle time (D8). A rise below the larger of 0.02 mS/cm and a quarter of the one expected
  // locks at once and learns nothing: a dry or misplaced probe reads about 0 and counts as usable. Not
  // judged after the inlet opened or a probe was calibrated, not learned after anything unclean (RAT-057).
  struct FirstStepIn {
    std::string recipe;
    double ecBefore = kNaN, ecNow = kNaN, ml = kNaN, litres = kNaN, expectedRise = kNaN;
    bool learn = false, judge = false;
  };
  enum class FirstStep { Adopted, Locked };
  FirstStep adoptFirstStep(const Ctx& c, const FirstStepIn& in);
  // The parts of the recipe a round doses, or their ratio, changed under it (an edit or an import): it
  // doses no part that is fixed now and learns nothing, so the effect stays on the parts EC control
  // scales now (RAT-085, RAT-057, SD-092). A fixed part is never one of a pair, so no pair is split. A
  // removed part is still dosed: a pair leaves a recipe only whole, and dropping its second part after
  // the first went in would split it (RAT-054); the round stays within the cap it was planned on.
  void recipeChanged(const RecipeCfg& r);
  // The recipe a round doses is gone (deleted, left out of an import) or a new one took its ID: the
  // round learns nothing (SD-092, RAT-057)
  void recipeGone(const std::string& id) {
    if (id == roundRecipe_) {
      clean_ = false;
      disturbed_ = true;
    }
    if (id == small_.recipe) small_ = {};
  }
  // What was learned for a recipe is forgotten (a higher reference on the same parts, SD-092): its
  // small rounds were expected at it
  void effectForgotten(const std::string& id) {
    if (id == small_.recipe) small_ = {};
  }
  // The wall clock stepped by `d`: keep the distance of the pauses.
  void shiftClock(Epoch d) {
    if (lastDoseAt_ > 0) lastDoseAt_ += d;
    if (cooldownUntil_ > 0) cooldownUntil_ += d;
  }

 private:
  enum class Phase { Idle, Dosing, Settling } phase_ = Phase::Idle;
  std::vector<DoseStep> queue_;
  int round_ = 0, noEffect_ = 0, seq_ = 0;  // noEffect_ in points: 2 a clean round, 1 one watering made unclean (SD-103)
  bool clean_ = true;  // Wirkung nur aus sauberen Gaben lernen (ohne Zulauf dazwischen)
  bool limited_ = false;    // the round was made smaller to fit the partial-run limit (SD-102)
  double expectedRise_ = kNaN;  // what the round should raise EC by, at the effect it was planned on (SD-102)
  // Clean rounds of one recipe made smaller for slow pumps, each too small to judge on its own: summed
  // until they should raise EC by kSmallRoundEc between them, then judged and learned as one round
  // (SD-102, PD-317)
  struct SmallRounds {
    double rise = 0, expected = 0;
    double mlPerL = 0;       // each round's millilitres on its own litres
    bool learnable = true;   // every round knew its litres
    std::string recipe;
  } small_;
  bool disturbed_ = false;  // something other than watering came in between: the round does not count at all
  bool cut_ = false;        // a Stop cut the round: settle it once the circulation pump runs
  bool cutByMix_ = false;   // … or a mix was cut with nutrients in (settleFirst)
  bool quiet_ = false;      // … with no round of its own to judge when it has settled
  Ms cutFloor_ = 0;         // the least the cut settles (settleFirst)
  bool wantsCirc_ = false;  // the cut round waits for the circulation pump
  double ecBefore_ = kNaN, mlRound_ = 0, startEc_ = kNaN, target_ = kNaN;
  double volRound_ = kNaN;  // the volume the round's dose went into, read when it started (SD-103)
  std::string roundRecipe_;  // the recipe the round doses: its effect is learned, whatever the phase is now (RAT-057, SD-092)
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
  void settleFirst(bool byStop, Ms floor) {  // as EcController::settleFirst
    quiet_ = (cut_ && quiet_) || phase_ == Phase::Idle;
    cutFloor_ = cut_ ? std::max(cutFloor_, floor) : floor;
    cut_ = true;
    cutByMix_ = !byStop;
    reset();
  }
  bool dosing() const { return phase_ == Phase::Dosing; }
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
  bool disturbed_ = false;  // as in EcController
  bool cut_ = false;     // as in EcController
  bool cutByMix_ = false;
  bool quiet_ = false;
  Ms cutFloor_ = 0;
  Ms cutWaitSince_ = 0;
  int doses_ = 0, noEffect_ = 0, seq_ = 0;  // noEffect_ in points, as in EcController
  double phBefore_ = kNaN, mlLast_ = 0, startPh_ = kNaN;
  double volLast_ = kNaN;  // the volume the dose went into, read when it started (SD-103)
  Ms settleUntil_ = 0;
  Epoch cooldownUntil_ = 0;
  CtlStatus st_;
};

class RefillController {
 public:
  void tick(const Ctx& c, ControlEnv& env);
  const CtlStatus& status() const { return st_; }
  bool filling() const { return filling_; }
  // A fill is due and waits only for a running dose or round to settle: EC and pH start no new round
  // (SD-090). The running one goes on and is learned from (RAT-057), unlike while holding().
  bool dueAfterDose() const { return dueAfterDose_; }
  // From the first opening of a refill until the rise of its last fill is checked, EC and pH wait:
  // a round in a tank half filled would be diluted by the next step (RAT-055, RAT-057, SD-082). So
  // they do while a fill that is due waits for the level reading to settle (SD-090).
  bool holding(const RuntimeState& rt) const {
    return filling_ || settling_ || step_ < steps_ || (rt.refill.is_object() && rt.refill.contains("check"));
  }
  void shiftClock(RuntimeState& rt, Epoch d);
  // A level calibration of the bound level sensor committed this new curve (SD-082)
  static void levelCommitted(RuntimeState& rt, const json& curve);

 private:
  void endSteps() {
    steps_ = step_;
    leftL_ = 0;
  }
  // The open fill becomes a check of its rise at the end of the pause (SD-082)
  void toCheck(const Ctx& c, double expectedL, double openMin);
  // A person stopped the refill or works at the tank: the check and the fills not confirmed are kept as
  // not confirmed, with the level read when the work began (SD-082)
  void keepForPerson(const Ctx& c, double levelNow);
  void latchInlet(const Ctx& c, const Msg& why, const Msg& title, const Msg& text, const json& args);
  bool booted_ = false;
  bool filling_ = false;
  bool settling_ = false;  // a fill is due and waits in this tick for the level reading to settle (SD-090)
  bool dueAfterDose_ = false;  // a fill is due and waits in this tick for a dose to settle (SD-090)
  Ms startedMs_ = 0, plannedMs_ = 0;
  Ms disturbedMs_ = 0;  // the level reading was last disturbed: a start, Stop, maintenance, the function off, a level calibration, a hand on the valve, an inlet latch
  bool latchSeen_ = false;  // the inlet latch stood at the last tick, or was set in it: the tick that sees it released is a disturbance too (SD-088)
  Ms irrigatedMs_ = std::numeric_limits<Ms>::min() / 2;  // the irrigation pump last ran; not since the start
  Ms settleSince_ = -1;  // a fill has waited since then for the reading to settle after irrigation
  // Level readings from 3 min after the irrigation pump stopped, once a second, the last 3 min, and
  // whether the level has been calm since: water that drains back makes it rise (SD-099). Not kept
  // across a restart (R6).
  struct LevelSeen {
    Ms at;
    double litres;
    Ms reported;  // the sensor's report the value comes from
  };
  std::deque<LevelSeen> drainBack_;
  Ms drainSince_ = 0;  // the first of these readings
  bool calmSeen_ = true;
  Epoch bootEpoch_ = 0;
  double startL_ = kNaN, plannedL_ = kNaN;
  // The refill as planned at its first fill: target, rate and open time, then step by step the
  // litres left and the fixed number of steps; the total is computed once (RAT-001, SD-082)
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
