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
  // The device reports values calibrated elsewhere (e.g. a sensor in Home
  // Assistant). The hub shows them but has not checked that calibration, so
  // pH, EC and level from it are no values for control (RAT-025). Set only
  // in code, never from catalog data, so the catalog cannot loosen it (R7).
  bool externalCalibration = false;
  // The device reports on its own schedule, not when the hub asks (e.g. a
  // sensor in Home Assistant that reports every few minutes or only on a
  // change). The sensor truth learns its usual gap between reports and calls
  // a value stale only when it misses it, within fixed limits (SD-035); its
  // standstill check counts by its reports, and a calibration point waits
  // for its next report (SD-041). Set only in code, never from catalog
  // data, so the catalog cannot loosen freshness (R7).
  bool ownReportSchedule = false;
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
  // What GET /api/v1/catalog serves: the data plus what is set in code
  json forApi() const;

  int version = 0;
  std::map<std::string, CapabilityDef> capabilities;
  std::map<std::string, DeviceClassDef> deviceClasses;
  std::map<std::string, RoleDef> roles;
  std::vector<FunctionDef> functions;
  json templates;
  json raw;  // the catalog data as loaded
};

// A catalog entry's name or text as a message (SD-083 point 3): the key
// names the entry, the text is the catalog's English, and the web app shows
// it in German by the key (web/src/lang/cat.ts). So an event names a role,
// a device class or a function in the language of the page that reads it.
Msg nameOf(const CapabilityDef& c);
Msg nameOf(const DeviceClassDef& d);
Msg nameOf(const RoleDef& r);
Msg nameOf(const FunctionDef& f);
Msg nameOf(const FunctionDef& f, const ParamDef& p);

// Whether a device's stored name is a name of its own (SD-083): not empty
// and not its class's name, now or as the German catalog had it, since a
// device accepted without a name stored that until the catalog was English.
// Without one, the device is named by its class in the page's language.
bool ownName(const std::string& name, const std::string& cls, const Catalog& cat);

// A role by its name as a message, or by its id when the catalog does not know it
json roleName(const Catalog& cat, const std::string& id);
// A device by its own name, else by its class's name as a message, else by its id
json deviceName(const Catalog& cat, const std::string& id, const std::string& cls, const std::string& name);

}  // namespace gc
