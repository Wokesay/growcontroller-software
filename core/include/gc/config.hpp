// SPDX-License-Identifier: AGPL-3.0-or-later
// Konfiguration: Bereichsbaum plus flaches Geräteinventar (Vorschlag architekt).
// Rollen binden an die Geräte-ID, nie an den Port. Laufzeitzustand (Vorrat,
// Rastungen, Sprungsperren, gelernte Wirkungen) steht getrennt in RuntimeState,
// damit nicht jede Dosierung eine neue Konfigurations-Revision erzeugt.
#pragma once

#include <array>
#include <map>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

#include "gc/catalog.hpp"
#include "gc/common.hpp"

namespace gc {

constexpr int kSchemaVersion = 2;

struct Binding {
  std::string device;
  int channel = 0;
};
using RoleMap = std::map<std::string, Binding>;

struct TankCfg {
  std::string id = "t1";
  std::string name = "Tank 1";
  double capacityL = kNaN;  // Nutzvolumen, Plausibilitätsgrenze beim Mischen
  double minL = kNaN;       // Trockenlaufgrenze der Umwälzpumpe
  std::string water = "ro"; // ro | tap
  RoleMap roles;            // "tank.ph" → Gerät
};

// Bereich, in dem die Pflanzen stehen: Raum, Zelt oder Gewächshaus. Das
// Datenmodell erlaubt mehrere, v1 nutzt eine. Rollen heißen „zone.*“
// (Schema v2; vorher „tent.*“ am Tank).
struct ZoneCfg {
  std::string id = "z1";
  std::string name = "Raum 1";
  std::string kind = "room";  // room | tent | greenhouse
  std::string tank = "t1";    // Tank, der diesen Bereich versorgt
  RoleMap roles;              // "zone.air_temp" → Gerät
};

struct DeviceCfg {
  std::string id, cls, name;
  // A sign of life for a sensor on its own schedule (SD-042): the source
  // that gives the time of its last measurement (a Home Assistant entity)
  // and, for a sign-of-life text, its channel ("" for a timestamp sensor).
  // The platform's bus reads it for freshness; the watchdog groups a silent
  // device's sensors by it (SD-047, #107). Empty without a sign of life.
  std::string life, lifeChannel;
  DeviceCfg() = default;
  DeviceCfg(std::string id_, std::string cls_, std::string name_, std::string life_ = "", std::string lifeChannel_ = "")
      : id(std::move(id_)), cls(std::move(cls_)), name(std::move(name_)), life(std::move(life_)), lifeChannel(std::move(lifeChannel_)) {}
};

struct CanisterCfg {
  std::string id, name;
  std::string kind = "nutrient";  // nutrient | ph_down | ph_up
  std::string pump;               // Geräte-ID der Pumpenkappe
  std::string pair;               // Paar-Gruppe, z. B. "AB"
  std::string color = "#4c9a52";
  double capacityMl = kNaN;
};

struct RecipeStep {
  std::string canister;
  double mlPerL = kNaN;
};

struct RecipeCfg {
  std::string id, name, note;
  std::vector<RecipeStep> steps;  // Reihenfolge = Dosierreihenfolge
};

struct FunctionCfg {
  bool enabled = false;
  json params = json::object();
};

// Phase = Parametersatz mit frei wählbarem Namen. Die Logik liest nur die
// Parameter, nie den Namen (Quelle: RAT-076).
struct PhaseCfg {
  std::string name;
  int days = 0;
  json params = json::object();
};

struct GrowCfg {
  std::string state = "none";  // none | running | completed
  std::string name;
  Epoch startedAt = 0;
  int phase = 0;
  Epoch phaseStartedAt = 0;
  Epoch harvestedAt = 0;  // Ernte ist ein Ereignis (RAT-077); hier nur der Zeitpunkt
  std::vector<PhaseCfg> phases;
};

// Feste Sicherheitsgrenzen im Code. Die Konfiguration darf sie nur verschärfen
// (R7); Gateway und Planung rechnen immer mit `bounded()`.
constexpr double kHardMinRunS = 1.0;          // kürzere Läufe werden nicht dosiert (RAT-050)
constexpr double kHardMaxRunS = 60.0;         // längere Gaben in Teilgaben (RAT-055)
constexpr double kHardHandDoseMaxMl = 50.0;   // je Handgabe
constexpr int kHardMaxPartialRuns = 6;        // Teilläufe je Regel-Gabe

struct Limits {
  double handDoseMaxMl = 5.0;  // Quelle: RAT-039
  double minRunS = kHardMinRunS;
  double maxRunS = kHardMaxRunS;
  int maxPartialRuns = kHardMaxPartialRuns;

  // Werte auf die festen Grenzen geklemmt; fehlende oder unsinnige Werte → feste Grenze.
  Limits bounded() const;
};

struct SystemCfg {
  std::string name = "growcontroller";
  bool setupDone = false;
  bool passwordSet = false;  // Erstpasswort einmal gesetzt: Setup über das Netz danach gesperrt
  std::string timezone = "Europe/Berlin";
  std::string language = "de";
  bool updateCheck = true;
  std::string updateChannel = "stable";
  // What the app is to do per area (SD-037): "auto" or "outside" (left to something else, such
  // as Home Assistant). Only what the owner chose; an area not listed has no choice yet. Feeds no
  // control: today it only decides what the area pages show. A choice recorded while nothing
  // switched is no consent to switch: the version that wires this into control asks again.
  std::map<std::string, std::string> control;
};

// Areas and modes `SystemCfg::control` accepts (SD-037); the web app lists them in api.ts.
inline constexpr std::array<std::string_view, 6> kControlAreas{"climate", "light", "irrigation", "refill", "circulation", "dosing"};
bool isControlArea(std::string_view area);
bool isControlMode(std::string_view mode);
// A name in the form of the time zone database ("Europe/Berlin", "Etc/GMT+1", "UTC"): starting
// with a letter; letters, digits, "_", "+" and "-" in parts separated by "/"; at most 64
// characters; no part empty. The web app checks the same form (timeZone in web/src/format.ts).
// No name in the database uses ".", so it is left out on purpose: the firmware may one day
// look the zone up as a file, and "../" must never reach that. The core has no
// time zone database, so this checks the form, not that the zone exists; the web app checks
// that against the browser's. Nothing in the hub uses the zone yet; light and watering
// times will, so a value that is no zone name is refused rather than stored.
bool isTimeZoneName(std::string_view tz);

struct Config {
  int schemaVersion = kSchemaVersion;
  int revision = 0;
  SystemCfg system;
  Limits limits;
  std::vector<DeviceCfg> devices;
  std::vector<TankCfg> tanks;  // Datenmodell als Liste, UI und Logik v0: ein Tank
  std::vector<ZoneCfg> zones;  // ebenso: eine Zone
  std::vector<CanisterCfg> canisters;
  std::vector<RecipeCfg> recipes;
  std::map<std::string, FunctionCfg> functions;
  // Gerät → Art ("ph", "ec", "tank_curve") → Daten. Die Förderrate der Kappe
  // liegt im ID-Chip (PD-010) und wird über den Bus gelesen.
  std::map<std::string, std::map<std::string, json>> calibrations;
  GrowCfg grow;

  TankCfg& tank();
  const TankCfg& tank() const;
  ZoneCfg& zone();
  const ZoneCfg& zone() const;
  // Rollen liegen dort, wo sie wirken: „zone.*“ an der Zone, sonst am Tank.
  RoleMap& rolesFor(const std::string& role);
  const RoleMap& rolesFor(const std::string& role) const;
  template <typename F>
  void forEachBinding(F f) const {
    for (const auto& t : tanks)
      for (const auto& [r, b] : t.roles) f(r, b);
    for (const auto& z : zones)
      for (const auto& [r, b] : z.roles) f(r, b);
  }
  const DeviceCfg* device(const std::string& id) const;
  const CanisterCfg* canister(const std::string& id) const;
  const CanisterCfg* canisterByPump(const std::string& pumpId) const;
  const RecipeCfg* recipe(const std::string& id) const;
  const Binding* binding(const std::string& role) const;
  const json* calibration(const std::string& dev, const std::string& kind) const;
};

void to_json(json& j, const Config& c);
Config configFromJson(const json& j);  // wirft bei Strukturfehlern

// Migrationen sind reine Funktionen vN → vN+1 (Vorschlag architekt).
json migrateConfig(json j);

// Prüfung in drei Stufen: Struktur (beim Lesen), Verweise, fachlich/sicherheitlich.
std::vector<Msg> validateConfig(const Config& c, const Catalog& cat);

// Wirksame Parameter einer Funktion: Katalog-Vorgabe ⊕ Einstellung ⊕ aktive
// Phase. Funktionen erhalten nur diese Sicht, nie das Phasenobjekt (Regel R4).
class ParamView {
 public:
  ParamView() = default;
  explicit ParamView(json values) : v_(std::move(values)) {}
  double num(const std::string& key) const;
  std::string str(const std::string& key) const;
  const json& values() const { return v_; }

 private:
  json v_ = json::object();
};
ParamView effectiveParams(const Catalog& cat, const Config& c, const std::string& fn);

// At or below this, a canister's stock counts as booked below zero: it shows
// as at least -1 ml, and a rounding difference under 0.5 ml raises no alarm (SD-066).
constexpr double kStockBelowZeroMl = -0.5;

// Laufzeitzustand, getrennt gespeichert. Überlebt Neustarts (Rastungen und
// Sprungsperren bleiben, Quelle: RAT-044, RAT-063).
struct RuntimeState {
  std::map<std::string, double> stockMl;  // canister → stock in ml; missing = unknown; may be below zero (SD-066)
  double tankVolumeL = kNaN;               // ohne Füllstandskopf: aus Mischläufen
  Epoch lastMixAt = 0;  // start of the running mix, then the end of the last one (RAT-003)
  double phEffect = kNaN;  // gelernte Wirkung pH− (pH je ml/L), nur aus sauberen Gaben
  double ecEffect = kNaN;  // gelernte Wirkung Rezept (mS/cm je ml/L Summe)
  std::map<std::string, json> latches;     // gerastete Sperren mit Grund
  std::map<std::string, Epoch> selfReleased;  // latch → when it last released itself (SD-022)
  std::map<std::string, Epoch> jumpLocks;  // Rolle → gesperrt bis
  std::map<std::string, double> manual;    // Handmessungen, z. B. "ph"
  Epoch manualAt = 0;
  std::optional<Stamp> clock;  // time and operating time when saved (PD-069)
  std::uint32_t bootCount = 0;   // number of starts; tells moments of one start apart
  bool stopped = false;          // emergency stop survives a restart (RAT-036, PD-076)
};
void to_json(json& j, const RuntimeState& s);
RuntimeState runtimeFromJson(const json& j);

}  // namespace gc
