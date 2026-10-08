// SPDX-License-Identifier: AGPL-3.0-or-later
// Schicht 7: Regelfunktionen. Sie lesen nur die Sensorwahrheit und ihre
// wirksamen Parameter (ParamView), nie Phasennamen (R4), und stellen nur
// Anträge an das Aktor-Gateway (R1).
//
// Jeder Regler liefert eine Regelzeile mit genau einem von wenigen Zuständen
// und eine Checkliste der Bedingungen ("Warum dosiert er gerade nicht?",
// Vorschlag anwender; Quelle: RAT-037, RAT-039, RAT-046).
#pragma once

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
  bool userJob = false;      // Mischlauf, Handgabe oder Einmessen aktiv
  bool circulationOn = false;
  double volumeL = kNaN;     // Füllstand, sonst bekanntes Volumen aus Mischläufen
  Epoch lastEcDoseAt = 0;
  bool ecBusy = false;
  bool phBusy = false;       // pH dosiert oder schwingt ein: EC-Runde wartet (RAT-057)
  bool refilling = false;    // Zulauf offen: Wirkung nicht messbar, nicht dosieren (RAT-055)
  bool calibrating = false;  // Sonde in Pufferlösung: Messwerte gelten nicht für den Tank
};

class EcController {
 public:
  void tick(const Ctx& c, ControlEnv& env);
  void onDoseFinished(const Ctx& c, ControlEnv& env, const DoseProgress& p);
  void reset() {
    phase_ = Phase::Idle;
    queue_.clear();
    circWaitSince_ = 0;
  }
  const CtlStatus& status() const { return st_; }
  bool busy() const { return phase_ != Phase::Idle; }
  bool wantsCirculation() const { return phase_ != Phase::Idle; }
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
  double ecBefore_ = kNaN, mlRound_ = 0, startEc_ = kNaN, target_ = kNaN;
  Ms circWaitSince_ = 0;  // seit wann die Runde auf die Umwälzpumpe wartet
  Ms settleUntil_ = 0;
  Epoch lastDoseAt_ = 0, cooldownUntil_ = 0;
  CtlStatus st_;
};

class PhController {
 public:
  void tick(const Ctx& c, ControlEnv& env);
  void onDoseFinished(const Ctx& c, ControlEnv& env, const DoseProgress& p);
  void reset() { phase_ = Phase::Idle; }
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
  void shiftClock(Epoch d) {
    if (cooldownUntil_ > 0) cooldownUntil_ += d;
  }

 private:
  bool filling_ = false;
  Ms startedMs_ = 0, plannedMs_ = 0;
  double startL_ = kNaN, plannedL_ = kNaN;
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
