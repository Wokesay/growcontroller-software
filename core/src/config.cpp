// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/config.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace gc {

namespace {

double numOrNaN(const json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return kNaN;
  return j[key].get<double>();
}

const TankCfg& emptyTank() {
  static const TankCfg t;
  return t;
}

}  // namespace

TankCfg& Config::tank() {
  if (tanks.empty()) tanks.emplace_back();
  return tanks.front();
}
const TankCfg& Config::tank() const { return tanks.empty() ? emptyTank() : tanks.front(); }

const DeviceCfg* Config::device(const std::string& id) const {
  for (const auto& d : devices)
    if (d.id == id) return &d;
  return nullptr;
}
const CanisterCfg* Config::canister(const std::string& id) const {
  for (const auto& c : canisters)
    if (c.id == id) return &c;
  return nullptr;
}
const CanisterCfg* Config::canisterByPump(const std::string& pumpId) const {
  if (pumpId.empty()) return nullptr;
  for (const auto& c : canisters)
    if (c.pump == pumpId) return &c;
  return nullptr;
}
const RecipeCfg* Config::recipe(const std::string& id) const {
  for (const auto& r : recipes)
    if (r.id == id) return &r;
  return nullptr;
}
ZoneCfg& Config::zone() {
  if (zones.empty()) zones.emplace_back();
  return zones.front();
}
const ZoneCfg& Config::zone() const {
  static const ZoneCfg empty;
  return zones.empty() ? empty : zones.front();
}
static bool isZoneRole(const std::string& role) { return role.rfind("zone.", 0) == 0; }
RoleMap& Config::rolesFor(const std::string& role) { return isZoneRole(role) ? zone().roles : tank().roles; }
const RoleMap& Config::rolesFor(const std::string& role) const { return isZoneRole(role) ? zone().roles : tank().roles; }

const Binding* Config::binding(const std::string& role) const {
  const auto& roles = rolesFor(role);
  auto it = roles.find(role);
  if (it == roles.end() || it->second.device.empty()) return nullptr;
  return &it->second;
}
const json* Config::calibration(const std::string& dev, const std::string& kind) const {
  auto it = calibrations.find(dev);
  if (it == calibrations.end()) return nullptr;
  auto k = it->second.find(kind);
  return k == it->second.end() ? nullptr : &k->second;
}

void to_json(json& j, const Config& c) {
  j = json::object();
  j["schemaVersion"] = c.schemaVersion;
  j["revision"] = c.revision;
  j["system"] = {{"name", c.system.name},
                 {"setupDone", c.system.setupDone},
                 {"passwordSet", c.system.passwordSet},
                 {"timezone", c.system.timezone},
                 {"language", c.system.language},
                 {"updateCheck", c.system.updateCheck},
                 {"updateChannel", c.system.updateChannel}};
  j["limits"] = {{"handDoseMaxMl", c.limits.handDoseMaxMl},
                 {"minRunS", c.limits.minRunS},
                 {"maxRunS", c.limits.maxRunS},
                 {"maxPartialRuns", c.limits.maxPartialRuns}};
  j["devices"] = json::array();
  for (const auto& d : c.devices) j["devices"].push_back({{"id", d.id}, {"class", d.cls}, {"name", d.name}});
  j["tanks"] = json::array();
  for (const auto& t : c.tanks) {
    json roles = json::object();
    for (const auto& [r, b] : t.roles) roles[r] = {{"device", b.device}, {"channel", b.channel}};
    j["tanks"].push_back({{"id", t.id},
                          {"name", t.name},
                          {"capacityL", numOrNull(t.capacityL)},
                          {"minL", numOrNull(t.minL)},
                          {"water", t.water},
                          {"roles", roles}});
  }
  j["zones"] = json::array();
  // Ohne Zone gilt die Standardzone (wie beim Lesen), damit der Hin- und
  // Rückweg über JSON dasselbe ergibt.
  const std::vector<ZoneCfg> zones = c.zones.empty() ? std::vector<ZoneCfg>{c.zone()} : c.zones;
  for (const auto& z : zones) {
    json roles = json::object();
    for (const auto& [r, b] : z.roles) roles[r] = {{"device", b.device}, {"channel", b.channel}};
    j["zones"].push_back({{"id", z.id}, {"name", z.name}, {"kind", z.kind}, {"tank", z.tank}, {"roles", roles}});
  }
  j["canisters"] = json::array();
  for (const auto& k : c.canisters)
    j["canisters"].push_back({{"id", k.id},
                              {"name", k.name},
                              {"kind", k.kind},
                              {"pump", k.pump},
                              {"pair", k.pair},
                              {"color", k.color},
                              {"capacityMl", numOrNull(k.capacityMl)}});
  j["recipes"] = json::array();
  for (const auto& r : c.recipes) {
    json steps = json::array();
    for (const auto& s : r.steps) steps.push_back({{"canister", s.canister}, {"mlPerL", numOrNull(s.mlPerL)}});
    j["recipes"].push_back({{"id", r.id}, {"name", r.name}, {"note", r.note}, {"steps", steps}});
  }
  j["functions"] = json::object();
  for (const auto& [id, f] : c.functions) j["functions"][id] = {{"enabled", f.enabled}, {"params", f.params}};
  j["calibrations"] = json::object();
  for (const auto& [dev, kinds] : c.calibrations)
    for (const auto& [kind, data] : kinds) j["calibrations"][dev][kind] = data;
  json phases = json::array();
  for (const auto& p : c.grow.phases) phases.push_back({{"name", p.name}, {"days", p.days}, {"params", p.params}});
  j["grow"] = {{"state", c.grow.state},       {"name", c.grow.name},
               {"startedAt", c.grow.startedAt}, {"phase", c.grow.phase},
               {"phaseStartedAt", c.grow.phaseStartedAt}, {"harvestedAt", c.grow.harvestedAt},
               {"phases", phases}};
}

json migrateConfig(json j) {
  if (!j.is_object()) throw std::runtime_error("Konfiguration ist kein Objekt");
  int v = j.value("schemaVersion", 0);
  if (v > kSchemaVersion)
    throw std::runtime_error("Konfiguration stammt aus einer neueren Version (Schema " + std::to_string(v) + ")");
  // v0 → v1: frühe Entwürfe hatten einen einzelnen "tank" statt "tanks".
  if (v < 1) {
    if (j.contains("tank") && !j.contains("tanks")) {
      j["tanks"] = json::array({j["tank"]});
      j.erase("tank");
    }
    j["schemaVersion"] = 1;
  }
  // v1 → v2: Zonen. Rollen „tent.*“ am Tank werden „zone.*“ an der ersten Zone.
  if (j.value("schemaVersion", 0) < 2) {
    json zone = {{"id", "z1"}, {"name", "Raum 1"}, {"kind", "room"}, {"tank", "t1"}, {"roles", json::object()}};
    if (j.contains("tanks") && j["tanks"].is_array()) {
      for (auto& t : j["tanks"]) {
        if (!t.is_object()) continue;
        if (zone["tank"] == "t1" && t.contains("id") && t["id"].is_string()) zone["tank"] = t["id"];
        if (!t.contains("roles") || !t["roles"].is_object()) continue;
        json keep = json::object();
        for (const auto& [r, b] : t["roles"].items()) {
          if (r.rfind("tent.", 0) == 0) zone["roles"]["zone." + r.substr(5)] = b;
          else keep[r] = b;
        }
        t["roles"] = keep;
      }
    }
    if (!j.contains("zones") || !j["zones"].is_array() || j["zones"].empty()) j["zones"] = json::array({zone});
    j["schemaVersion"] = 2;
  }
  return j;
}

Config configFromJson(const json& in) {
  json j = migrateConfig(in);
  Config c;
  c.schemaVersion = j.value("schemaVersion", kSchemaVersion);
  c.revision = j.value("revision", 0);
  if (j.contains("system")) {
    const auto& s = j["system"];
    c.system.name = s.value("name", c.system.name);
    c.system.setupDone = s.value("setupDone", false);
    c.system.passwordSet = s.value("passwordSet", false);
    c.system.timezone = s.value("timezone", c.system.timezone);
    c.system.language = s.value("language", c.system.language);
    c.system.updateCheck = s.value("updateCheck", true);
    c.system.updateChannel = s.value("updateChannel", c.system.updateChannel);
  }
  if (j.contains("limits")) {
    const auto& l = j["limits"];
    c.limits.handDoseMaxMl = jnum(l, "handDoseMaxMl", c.limits.handDoseMaxMl);
    c.limits.minRunS = jnum(l, "minRunS", c.limits.minRunS);
    c.limits.maxRunS = jnum(l, "maxRunS", c.limits.maxRunS);
    double runs = jnum(l, "maxPartialRuns", c.limits.maxPartialRuns);
    c.limits.maxPartialRuns = isNum(runs) && runs >= 0 && runs <= 1000 ? static_cast<int>(runs) : -1;
  }
  for (const auto& d : j.value("devices", json::array()))
    c.devices.push_back({jstr(d, "id"), jstr(d, "class"), jstr(d, "name")});
  for (const auto& t : j.value("tanks", json::array())) {
    TankCfg tc;
    tc.id = t.value("id", tc.id);
    tc.name = t.value("name", tc.name);
    tc.capacityL = numOrNaN(t, "capacityL");
    tc.minL = numOrNaN(t, "minL");
    tc.water = t.value("water", tc.water);
    if (t.contains("roles"))
      for (const auto& [r, b] : t["roles"].items()) tc.roles[r] = {jstr(b, "device"), b.value("channel", 0)};
    c.tanks.push_back(tc);
  }
  if (c.tanks.empty()) c.tanks.emplace_back();
  for (const auto& z : j.value("zones", json::array())) {
    ZoneCfg zc;
    zc.id = z.value("id", zc.id);
    zc.name = z.value("name", zc.name);
    zc.kind = z.value("kind", zc.kind);
    zc.tank = z.value("tank", c.tanks.front().id);
    if (z.contains("roles") && z["roles"].is_object())
      for (const auto& [r, b] : z["roles"].items()) zc.roles[r] = {jstr(b, "device"), b.value("channel", 0)};
    c.zones.push_back(zc);
  }
  if (c.zones.empty()) {
    c.zones.emplace_back();
    c.zones.front().tank = c.tanks.front().id;
  }
  for (const auto& k : j.value("canisters", json::array())) {
    CanisterCfg cc;
    cc.id = jstr(k, "id");
    cc.name = jstr(k, "name");
    cc.kind = k.value("kind", cc.kind);
    cc.pump = jstr(k, "pump");
    cc.pair = jstr(k, "pair");
    cc.color = k.value("color", cc.color);
    cc.capacityMl = numOrNaN(k, "capacityMl");
    c.canisters.push_back(cc);
  }
  for (const auto& r : j.value("recipes", json::array())) {
    RecipeCfg rc;
    rc.id = jstr(r, "id");
    rc.name = jstr(r, "name");
    rc.note = jstr(r, "note");
    for (const auto& s : r.value("steps", json::array())) rc.steps.push_back({jstr(s, "canister"), numOrNaN(s, "mlPerL")});
    c.recipes.push_back(rc);
  }
  if (j.contains("functions"))
    for (const auto& [id, f] : j["functions"].items())
      c.functions[id] = {f.value("enabled", false), f.value("params", json::object())};
  if (j.contains("calibrations") && j["calibrations"].is_object())
    for (const auto& [dev, kinds] : j["calibrations"].items())
      if (kinds.is_object())
        for (const auto& [kind, data] : kinds.items()) c.calibrations[dev][kind] = data;
  if (j.contains("grow")) {
    const auto& g = j["grow"];
    c.grow.state = g.value("state", c.grow.state);
    c.grow.name = g.value("name", std::string());
    c.grow.startedAt = g.value("startedAt", Epoch{0});
    c.grow.phase = g.value("phase", 0);
    c.grow.phaseStartedAt = g.value("phaseStartedAt", Epoch{0});
    c.grow.harvestedAt = g.value("harvestedAt", Epoch{0});
    for (const auto& p : g.value("phases", json::array()))
      c.grow.phases.push_back({jstr(p, "name"), p.value("days", 0), p.value("params", json::object())});
  }
  return c;
}

Limits Limits::bounded() const {
  Limits b;
  b.minRunS = isNum(minRunS) ? std::clamp(minRunS, kHardMinRunS, 10.0) : kHardMinRunS;
  b.maxRunS = isNum(maxRunS) ? std::clamp(maxRunS, b.minRunS, kHardMaxRunS) : kHardMaxRunS;
  b.handDoseMaxMl = isNum(handDoseMaxMl) && handDoseMaxMl > 0 ? std::min(handDoseMaxMl, kHardHandDoseMaxMl)
                                                                : Limits{}.handDoseMaxMl;
  b.maxPartialRuns = std::clamp(maxPartialRuns, 1, kHardMaxPartialRuns);
  return b;
}

std::vector<Msg> validateConfig(const Config& c, const Catalog& cat) {
  std::vector<Msg> out;
  auto err = [&](const std::string& key, const std::string& text) { out.push_back({key, text, json::object()}); };

  // Grenzen dürfen nur verschärfen (R7).
  const auto& L = c.limits;
  if (!isNum(L.minRunS) || L.minRunS < kHardMinRunS || L.minRunS > 10)
    err("cfg.limits.min_run", "Kürzester Pumpenlauf muss zwischen 1 und 10 s liegen");
  if (!isNum(L.maxRunS) || L.maxRunS > kHardMaxRunS || L.maxRunS < L.minRunS)
    err("cfg.limits.max_run", "Längster Pumpenlauf muss zwischen dem kürzesten und 60 s liegen");
  if (!isNum(L.handDoseMaxMl) || L.handDoseMaxMl <= 0 || L.handDoseMaxMl > kHardHandDoseMaxMl)
    err("cfg.limits.hand", "Grenze je Handgabe muss zwischen 0 und 50 ml liegen");
  if (L.maxPartialRuns < 1 || L.maxPartialRuns > kHardMaxPartialRuns)
    err("cfg.limits.runs", "Teilläufe je Gabe: 1 bis 6");

  std::set<std::string> ids;
  for (const auto& d : c.devices) {
    if (d.id.empty()) err("cfg.device.id", "Gerät ohne Kennung");
    if (!ids.insert(d.id).second) err("cfg.device.dup", "Gerät doppelt: " + d.id);
    if (!cat.deviceClass(d.cls)) err("cfg.device.class", "Unbekannte Geräteklasse " + d.cls + " (Update nötig)");
  }
  const auto& t = c.tank();
  if (isNum(t.capacityL) && t.capacityL <= 0) err("cfg.tank.capacity", "Nutzvolumen muss größer als 0 sein");
  if (isNum(t.minL) && isNum(t.capacityL) && t.minL >= t.capacityL)
    err("cfg.tank.min", "Mindestfüllstand muss unter dem Nutzvolumen liegen");
  // v1 der Logik kennt genau eine Zone; Messen und Regeln lesen nur die erste.
  if (c.zones.size() > 1) err("cfg.zone.count", "Mehrere Anbaubereiche werden noch nicht unterstützt");
  for (const auto& z : c.zones) {
    if (z.name.empty()) err("cfg.zone.name", "Der Anbaubereich braucht einen Namen");
    if (z.kind != "room" && z.kind != "tent" && z.kind != "greenhouse") err("cfg.zone.kind", z.name + ": Art muss Raum, Zelt oder Gewächshaus sein");
    bool tankKnown = false;
    for (const auto& tk : c.tanks) tankKnown = tankKnown || tk.id == z.tank;
    if (!tankKnown) err("cfg.zone.tank", z.name + ": Tank " + z.tank + " gibt es nicht");
    for (const auto& [role, b] : z.roles)
      if (role.rfind("zone.", 0) != 0) err("cfg.role.place", "Rolle " + role + " gehört nicht an die Zone");
  }
  for (const auto& tk : c.tanks)
    for (const auto& [role, b] : tk.roles)
      if (role.rfind("zone.", 0) == 0) err("cfg.role.place", "Rolle " + role + " gehört nicht an den Tank");
  std::vector<std::pair<std::string, Binding>> all;
  c.forEachBinding([&](const std::string& r, const Binding& b) { all.emplace_back(r, b); });
  std::map<std::pair<std::string, int>, std::string> switchUse;  // Kanal → Rolle
  for (const auto& [role, b] : all) {
    const RoleDef* rd = cat.role(role);
    if (rd && !rd->profile.empty()) {
      auto key = std::make_pair(b.device, b.channel);
      if (switchUse.count(key)) err("cfg.role.shared", rd->label + ": Ausgang ist schon „" + switchUse[key] + "“ zugeordnet");
      else switchUse[key] = rd->label;
    }
    if (!rd) {
      err("cfg.role.unknown", "Unbekannte Rolle " + role);
      continue;
    }
    const DeviceCfg* d = c.device(b.device);
    if (!d) {
      err("cfg.role.device", rd->label + ": Gerät " + b.device + " ist nicht eingerichtet");
      continue;
    }
    const DeviceClassDef* dc = cat.deviceClass(d->cls);
    bool ok = false;
    if (dc)
      for (const auto& p : dc->provides) ok = ok || rd->allows(p);
    if (!ok) err("cfg.role.cap", rd->label + ": " + d->name + " liefert das nicht");
    const int channels = dc && dc->channels > 0 ? dc->channels : 1;
    if (b.channel < 0 || b.channel >= channels) err("cfg.role.channel", rd->label + ": " + d->name + " hat keinen Kanal " + std::to_string(b.channel + 1));
  }
  std::set<std::string> pumps, canIds;
  for (const auto& k : c.canisters) {
    if (k.id.empty() || !canIds.insert(k.id).second) err("cfg.canister.id", "Kanister-Kennung fehlt oder doppelt");
    if (k.kind != "nutrient" && k.kind != "ph_down" && k.kind != "ph_up")
      err("cfg.canister.kind", k.name + ": unbekannter Typ " + k.kind);
    if (!k.pump.empty()) {
      if (!pumps.insert(k.pump).second) err("cfg.canister.pump", "Eine Pumpe ist zwei Kanistern zugeordnet");
      const DeviceCfg* d = c.device(k.pump);
      if (!d || d->cls != "pump_cap") err("cfg.canister.pumpclass", k.name + ": zugeordnetes Gerät ist keine Pumpe des Dosierblocks");
    }
    if (!k.pair.empty() && k.kind != "nutrient") err("cfg.canister.pair", k.name + ": nur Nährstoffe bilden Paare");
  }
  // Paare müssen vollständig sein: eine Paar-Gruppe mit nur einem Kanister ist ein Fehler.
  std::map<std::string, int> pairCount;
  for (const auto& k : c.canisters)
    if (!k.pair.empty()) pairCount[k.pair]++;
  for (const auto& [p, n] : pairCount)
    if (n < 2) err("cfg.pair.single", "Paar " + p + " hat nur einen Kanister");
  for (const auto& r : c.recipes) {
    if (r.steps.empty()) err("cfg.recipe.empty", "Rezept " + r.name + " hat keine Schritte");
    std::set<std::string> pairsInRecipe, seen;
    for (const auto& s : r.steps) {
      const CanisterCfg* k = c.canister(s.canister);
      if (!k) {
        err("cfg.recipe.canister", "Rezept " + r.name + ": unbekannter Kanister");
        continue;
      }
      if (!seen.insert(k->id).second) err("cfg.recipe.twice", "Rezept " + r.name + ": " + k->name + " steht zweimal darin");
      // pH-Korrektur ist nie Teil eines Rezepts, sie kommt immer zuletzt (RAT-004).
      if (k->kind != "nutrient") err("cfg.recipe.ph", "Rezept " + r.name + ": " + k->name + " ist kein Nährstoff (pH kommt zuletzt)");
      if (!isNum(s.mlPerL) || s.mlPerL <= 0) err("cfg.recipe.amount", "Rezept " + r.name + ": Menge für " + k->name + " fehlt");
      if (!k->pair.empty()) pairsInRecipe.insert(k->pair);
    }
    for (const auto& p : pairsInRecipe) {
      int inRecipe = 0;
      for (const auto& s : r.steps) {
        const CanisterCfg* k = c.canister(s.canister);
        if (k && k->pair == p) inRecipe++;
      }
      if (inRecipe != pairCount[p]) err("cfg.recipe.pair", "Rezept " + r.name + ": Paar " + p + " unvollständig");
    }
  }
  auto checkParam = [&](const FunctionDef& fd, const ParamDef& pd, const json& val, const std::string& where) {
    if (pd.type != "number") return;
    if (!val.is_number()) {
      err("cfg.param.type", where + fd.label + ": " + pd.label + " muss eine Zahl sein");
      return;
    }
    double v = val.get<double>();
    if (!isNum(v) || (isNum(pd.min) && v < pd.min) || (isNum(pd.max) && v > pd.max))
      err("cfg.param.range", where + fd.label + ": " + pd.label + " außerhalb " + fmt(pd.min, 2) + "–" + fmt(pd.max, 2));
  };
  for (const auto& [id, f] : c.functions) {
    const FunctionDef* fd = cat.function(id);
    if (!fd) {
      err("cfg.function.unknown", "Unbekannte Funktion " + id);
      continue;
    }
    if (!f.params.is_object()) {
      err("cfg.param.type", fd->label + ": Parameter ungültig");
      continue;
    }
    for (const auto& [key, val] : f.params.items()) {
      const ParamDef* pd = fd->param(key);
      if (!pd) {
        err("cfg.param.unknown", fd->label + ": unbekannter Parameter " + key);
        continue;
      }
      checkParam(*fd, *pd, val, "");
    }
  }
  // Phasen liefern Parameter (R4): dieselben Bereiche wie in den Einstellungen.
  for (const auto& ph : c.grow.phases) {
    const std::string where = "Phase „" + ph.name + "“, ";
    if (!ph.params.is_object()) {
      err("cfg.phase.params", where + "Parameter ungültig");
      continue;
    }
    for (const auto& [key, val] : ph.params.items()) {
      bool known = false;
      for (const auto& fd : cat.functions)
        for (const auto& pd : fd.params)
          if (pd.key == key && pd.phase) {
            known = true;
            checkParam(fd, pd, val, where);
          }
      if (!known) err("cfg.phase.unknown", where + "Parameter " + key + " ist nicht phasenabhängig");
    }
  }
  // Toleranz nie 0 (Quelle: RAT-008) – über die Katalog-Untergrenze abgedeckt.
  return out;
}

double ParamView::num(const std::string& key) const {
  auto it = v_.find(key);
  if (it == v_.end() || !it->is_number()) return kNaN;
  return it->get<double>();
}
std::string ParamView::str(const std::string& key) const {
  auto it = v_.find(key);
  if (it == v_.end() || !it->is_string()) return {};
  return it->get<std::string>();
}

ParamView effectiveParams(const Catalog& cat, const Config& c, const std::string& fn) {
  json v = json::object();
  const FunctionDef* fd = cat.function(fn);
  if (!fd) return ParamView(v);
  for (const auto& p : fd->params)
    if (!p.def.is_null()) v[p.key] = p.def;
  auto it = c.functions.find(fn);
  if (it != c.functions.end())
    for (const auto& [k, val] : it->second.params.items()) v[k] = val;
  if (c.grow.state == "running" && c.grow.phase >= 0 && c.grow.phase < static_cast<int>(c.grow.phases.size())) {
    const auto& ph = c.grow.phases[static_cast<size_t>(c.grow.phase)].params;
    for (const auto& p : fd->params)
      if (p.phase && ph.contains(p.key)) v[p.key] = ph[p.key];
  }
  return ParamView(v);
}

void to_json(json& j, const RuntimeState& s) {
  json stock = json::object();
  for (const auto& [k, v] : s.stockMl) stock[k] = numOrNull(v);
  j = {{"stockMl", stock},
       {"tankVolumeL", numOrNull(s.tankVolumeL)},
       {"lastMixAt", s.lastMixAt},
       {"phEffect", numOrNull(s.phEffect)},
       {"ecEffect", numOrNull(s.ecEffect)},
       {"latches", s.latches},
       {"jumpLocks", s.jumpLocks},
       {"manual", s.manual},
       {"manualAt", s.manualAt}};
}

RuntimeState runtimeFromJson(const json& j) {
  RuntimeState s;
  if (!j.is_object()) return s;
  if (j.contains("stockMl"))
    for (const auto& [k, v] : j["stockMl"].items())
      if (v.is_number()) s.stockMl[k] = v.get<double>();
  s.tankVolumeL = numOrNaN(j, "tankVolumeL");
  s.lastMixAt = j.value("lastMixAt", Epoch{0});
  s.phEffect = numOrNaN(j, "phEffect");
  s.ecEffect = numOrNaN(j, "ecEffect");
  if (j.contains("latches")) s.latches = j["latches"].get<std::map<std::string, json>>();
  if (j.contains("jumpLocks")) s.jumpLocks = j["jumpLocks"].get<std::map<std::string, Epoch>>();
  if (j.contains("manual")) s.manual = j["manual"].get<std::map<std::string, double>>();
  s.manualAt = j.value("manualAt", Epoch{0});
  return s;
}

}  // namespace gc
