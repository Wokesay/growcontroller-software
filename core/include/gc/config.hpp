// Konfiguration: Bereichsbaum plus flaches Geräteinventar (Vorschlag architekt).
// Rollen binden an die Geräte-ID, nie an den Port. Laufzeitzustand (Vorrat,
// Rastungen, Sprungsperren, gelernte Wirkungen) steht getrennt in RuntimeState,
// damit nicht jede Dosierung eine neue Konfigurations-Revision erzeugt.
#pragma once

#include <map>
#include <string>
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
};

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

// Laufzeitzustand, getrennt gespeichert. Überlebt Neustarts (Rastungen und
// Sprungsperren bleiben, Quelle: RAT-044, RAT-063).
struct RuntimeState {
  std::map<std::string, double> stockMl;  // Kanister → Vorrat; fehlt = unbekannt
  double tankVolumeL = kNaN;               // ohne Füllstandskopf: aus Mischläufen
  Epoch lastMixAt = 0;
  double phEffect = kNaN;  // gelernte Wirkung pH− (pH je ml/L), nur aus sauberen Gaben
  double ecEffect = kNaN;  // gelernte Wirkung Rezept (mS/cm je ml/L Summe)
  std::map<std::string, json> latches;     // gerastete Sperren mit Grund
  std::map<std::string, Epoch> jumpLocks;  // Rolle → gesperrt bis
  std::map<std::string, double> manual;    // Handmessungen, z. B. "ph"
  Epoch manualAt = 0;
};
void to_json(json& j, const RuntimeState& s);
RuntimeState runtimeFromJson(const json& j);

}  // namespace gc
