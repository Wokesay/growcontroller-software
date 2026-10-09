// SPDX-License-Identifier: AGPL-3.0-or-later
// Gerätekatalog: Capabilities, Geräteklassen, Rollen, Funktionen.
// Der Katalog ist Daten (catalog/catalog.json) und wächst ohne Codeänderung,
// solange die festen Voraussetzungsarten reichen. Sicherheit hängt nicht am
// Katalog: die Mindestsperren stehen fest im Code (actuators.cpp, Regel R7).
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "gc/common.hpp"

namespace gc {

struct CapabilityDef {
  std::string id, label, unit, kind;
  int decimals = 2;
  double plausMin = kNaN, plausMax = kNaN;
  double maxAgeS = 60;
  double jump = kNaN;  // Sprungschwelle zwischen zwei Messungen
};

struct DeviceClassDef {
  std::string id, label, attach, shop, text;
  int stage = 0;
  int slots = 0;     // Pumpen-Ports eines Dosierblocks
  int channels = 0;  // Kanäle eines Ausgangsmoduls
  // The device reports calibrated values itself (e.g. a sensor in Home
  // Assistant calibrated there), so the hub applies no calibration of its own.
  bool externalCalibration = false;
  std::vector<std::string> provides, calibrations, accepts;
};

struct RoleDef {
  std::string id, label, capability;  // capability = erste akzeptierte
  std::vector<std::string> accepts;   // z. B. 12-V-Ausgang oder Netzsteckdose
  bool series = false;                // im Verlauf als Messreihe geführt
  // Sicherheitsprofil eines Schaltausgangs (Konzept §3): dauer | puls |
  // kompressor | heizen. puls und heizen brauchen eine Höchstlaufzeit; daraus
  // folgt die Abschaltung im Gerät (Auto-Off) knapp darüber.
  std::string profile;
  double maxOnS = kNaN;
  // A network socket with this role comes back on after a power loss
  // (fans, PD-050); every other socket stays off. Only for `dauer`.
  bool onAfterPowerLoss = false;
  bool allows(const std::string& cap) const;
};

struct ParamDef {
  std::string key, label, type, unit;
  double min = kNaN, max = kNaN, step = kNaN;
  json def;
  bool phase = false;  // darf von einer Phase überschrieben werden
  std::vector<std::pair<std::string, std::string>> options;
};

// Feste Voraussetzungsarten (Prädikate). Neue Art = Code, neue Funktion = Daten.
struct Requirement {
  enum class Kind { Role, Canisters, Config, Function, Device } kind = Kind::Role;
  std::string role;          // Role
  bool calibrated = false;   // Role, Canisters
  int min = 1;               // Canisters
  std::string canisterKind;  // Canisters: nutrient | ph_down | ph_up | any
  std::string config;        // Config: recipe | tank_capacity
  std::string function;      // Function
  std::string device;        // Device: Geräteklasse
  std::string why;           // nur bei weichen Voraussetzungen: Folge in Klartext
};

struct FunctionDef {
  std::string id, label, group, text;
  int stage = 0;
  bool alwaysOn = false;  // nicht abschaltbar (z. B. Mischen von Hand gestartet)
  std::vector<Requirement> hard, soft;
  std::vector<ParamDef> params;
  const ParamDef* param(const std::string& key) const;
};

class Catalog {
 public:
  static Catalog fromJson(const json& j);
  static Catalog builtin();  // eingebetteter Katalog

  const CapabilityDef* capability(const std::string& id) const;
  const DeviceClassDef* deviceClass(const std::string& id) const;
  const RoleDef* role(const std::string& id) const;
  const FunctionDef* function(const std::string& id) const;
  // Geräteklassen, die eine Capability liefern (für "Was fehlt dir?").
  std::vector<const DeviceClassDef*> classesProviding(const std::string& cap) const;

  int version = 0;
  std::map<std::string, CapabilityDef> capabilities;
  std::map<std::string, DeviceClassDef> deviceClasses;
  std::map<std::string, RoleDef> roles;
  std::vector<FunctionDef> functions;
  json templates;
  json raw;  // unverändert für GET /api/v1/catalog
};

}  // namespace gc
