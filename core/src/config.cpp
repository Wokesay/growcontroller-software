// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <type_traits>

#include "gc/messages.hpp"

namespace gc {

namespace {

// A whole number from a file. A missing key gives `def`; anything else that
// is no whole number in the type's range (null, a fraction, text, a number
// beyond the type) gives `bad`, never another value (R5). For the grow
// cycle's times `bad` is `def`: a time that cannot be read is not set.
template <class T>
T whole(const json& j, const char* key, T def, T bad) {
  static_assert(std::is_signed_v<T>, "the bounds below assume a signed type");
  if (!j.is_object()) return def;
  auto it = j.find(key);
  if (it == j.end()) return def;
  if (it->is_null()) return bad;
  constexpr auto lo = std::numeric_limits<T>::min(), hi = std::numeric_limits<T>::max();
  if (it->is_number_unsigned()) {
    const auto u = it->template get<std::uint64_t>();
    return u <= static_cast<std::uint64_t>(hi) ? static_cast<T>(u) : bad;
  }
  if (it->is_number_integer()) {
    const auto v = it->template get<std::int64_t>();
    return v >= lo && v <= hi ? static_cast<T>(v) : bad;
  }
  if (it->is_number_float()) {
    const double d = it->template get<double>();
    // -lo is a power of two, exact as a double; d below it fits the type
    return std::isfinite(d) && d == std::floor(d) && d >= static_cast<double>(lo) && d < -static_cast<double>(lo) ? static_cast<T>(d) : bad;
  }
  return bad;
}

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

bool isControlArea(std::string_view area) {
  for (auto a : kControlAreas)
    if (a == area) return true;
  return false;
}

bool isControlMode(std::string_view mode) { return mode == "auto" || mode == "outside"; }

bool isTimeZoneName(std::string_view tz) {
  if (tz.empty() || tz.size() > 64) return false;
  const auto letter = [](char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'); };
  if (!letter(tz.front()) || tz.back() == '/') return false;
  char prev = 0;
  for (char ch : tz) {
    const bool ok = letter(ch) || (ch >= '0' && ch <= '9') || ch == '_' || ch == '+' || ch == '-' || ch == '/';
    if (!ok || (ch == '/' && prev == '/')) return false;
    prev = ch;
  }
  return true;
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
                 {"updateChannel", c.system.updateChannel},
                 {"control", c.system.control}};
  j["limits"] = {{"handDoseMaxMl", c.limits.handDoseMaxMl},
                 {"minRunS", c.limits.minRunS},
                 {"maxRunS", c.limits.maxRunS},
                 {"maxPartialRuns", c.limits.maxPartialRuns}};
  j["devices"] = json::array();
  for (const auto& d : c.devices) {
    json dj = {{"id", d.id}, {"class", d.cls}, {"name", d.name}};
    if (!d.life.empty()) dj["life"] = {{"source", d.life}, {"channel", d.lifeChannel}};
    j["devices"].push_back(std::move(dj));
  }
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
    for (const auto& s : r.steps) steps.push_back({{"canister", s.canister}, {"mlPerL", numOrNull(s.mlPerL)}, {"fixed", s.fixed}});
    // A range only as two values, a missing one as null (R5)
    json ph = isNum(r.phLow) && isNum(r.phHigh) ? json::array({r.phLow, r.phHigh}) : json(nullptr);
    j["recipes"].push_back(
        {{"id", r.id}, {"name", r.name}, {"note", r.note}, {"steps", steps}, {"refEc", numOrNull(r.refEc)}, {"ph", ph}});
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
  if (!j.is_object()) throw std::runtime_error("the configuration is not an object");
  const int v = whole(j, "schemaVersion", 0, -1);
  if (v < 0) throw std::runtime_error("the configuration has no valid schema version");
  if (v > kSchemaVersion)
    throw std::runtime_error("the configuration comes from a newer version (schema " + std::to_string(v) + ")");
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
  c.system.language = "de";  // a saved file without a language was saved under the old default (SD-014, SD-083)
  c.schemaVersion = j.value("schemaVersion", kSchemaVersion);
  c.revision = whole(j, "revision", 0, 0);  // an import keeps the hub's own
  if (j.contains("system")) {
    const auto& s = j["system"];
    c.system.name = s.value("name", c.system.name);
    if (c.system.name == "growcontroller") c.system.name = SystemCfg{}.name;  // the default before the name Doseph (#312)
    c.system.setupDone = s.value("setupDone", false);
    c.system.passwordSet = s.value("passwordSet", false);
    // One that is no zone name (saved before the hub checked it) falls back to the default
    // rather than making the file unreadable; an import refuses it (Hub::importConfig).
    if (s.contains("timezone") && s["timezone"].is_string() && isTimeZoneName(s["timezone"].get<std::string>()))
      c.system.timezone = s["timezone"];
    c.system.language = s.value("language", c.system.language);
    c.system.updateCheck = s.value("updateCheck", true);
    c.system.updateChannel = s.value("updateChannel", c.system.updateChannel);
    // Only known areas and modes; anything else is left out rather than refusing the file.
    if (s.contains("control") && s["control"].is_object())
      for (const auto& [area, mode] : s["control"].items())
        if (isControlArea(area) && mode.is_string() && isControlMode(mode.get<std::string>())) c.system.control[area] = mode.get<std::string>();
  }
  if (j.contains("limits")) {
    const auto& l = j["limits"];
    c.limits.handDoseMaxMl = jnum(l, "handDoseMaxMl", c.limits.handDoseMaxMl);
    c.limits.minRunS = jnum(l, "minRunS", c.limits.minRunS);
    c.limits.maxRunS = jnum(l, "maxRunS", c.limits.maxRunS);
    double runs = jnum(l, "maxPartialRuns", c.limits.maxPartialRuns);
    c.limits.maxPartialRuns = isNum(runs) && runs >= 0 && runs <= 1000 ? static_cast<int>(runs) : -1;
  }
  for (const auto& d : j.value("devices", json::array())) {
    DeviceCfg dc{jstr(d, "id"), jstr(d, "class"), jstr(d, "name")};
    if (d.is_object() && d.contains("life") && d["life"].is_object()) {
      dc.life = jstr(d["life"], "source");
      dc.lifeChannel = jstr(d["life"], "channel");
    }
    c.devices.push_back(std::move(dc));
  }
  for (const auto& t : j.value("tanks", json::array())) {
    TankCfg tc;
    tc.id = t.value("id", tc.id);
    tc.name = t.value("name", tc.name);
    tc.capacityL = numOrNaN(t, "capacityL");
    tc.minL = numOrNaN(t, "minL");
    tc.water = t.value("water", tc.water);
    if (t.contains("roles"))
      for (const auto& [r, b] : t["roles"].items()) tc.roles[r] = {jstr(b, "device"), whole(b, "channel", 0, -1)};
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
      for (const auto& [r, b] : z["roles"].items()) zc.roles[r] = {jstr(b, "device"), whole(b, "channel", 0, -1)};
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
    for (const auto& s : r.value("steps", json::array())) rc.steps.push_back({jstr(s, "canister"), numOrNaN(s, "mlPerL"), jbool(s, "fixed", false)});
    rc.refEc = numOrNaN(r, "refEc");
    const json ph = r.value("ph", json());
    if (ph.is_array() && ph.size() == 2 && ph[0].is_number() && ph[1].is_number()) {
      rc.phLow = ph[0].get<double>();
      rc.phHigh = ph[1].get<double>();
    }
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
    // A time the hub can have written, else not set
    auto time = [&](const char* key) {
      const Epoch t = whole(g, key, Epoch{0}, Epoch{0});
      return t >= 0 && t <= kNotAfter ? t : Epoch{0};
    };
    c.grow.startedAt = time("startedAt");
    c.grow.phase = whole(g, "phase", 0, -1);
    c.grow.phaseStartedAt = time("phaseStartedAt");
    c.grow.harvestedAt = time("harvestedAt");
    for (const auto& p : g.value("phases", json::array()))
      c.grow.phases.push_back({jstr(p, "name"), whole(p, "days", 0, -1), p.value("params", json::object())});
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
  auto err = [&](Msg m) { out.push_back(std::move(m)); };
  auto paramOf = [&](const char* fn, const char* key) -> const ParamDef* {
    const FunctionDef* fd = cat.function(fn);
    return fd ? fd->param(key) : nullptr;
  };

  // Grenzen dürfen nur verschärfen (R7).
  const auto& L = c.limits;
  if (!isNum(L.minRunS) || L.minRunS < kHardMinRunS || L.minRunS > 10)
    err(say("cfg.limits.min_run"));
  if (!isNum(L.maxRunS) || L.maxRunS > kHardMaxRunS || L.maxRunS < L.minRunS)
    err(say("cfg.limits.max_run"));
  if (!isNum(L.handDoseMaxMl) || L.handDoseMaxMl <= 0 || L.handDoseMaxMl > kHardHandDoseMaxMl)
    err(say("cfg.limits.hand"));
  if (L.maxPartialRuns < 1 || L.maxPartialRuns > kHardMaxPartialRuns)
    err(say("cfg.limits.runs"));

  std::set<std::string> ids;
  for (const auto& d : c.devices) {
    if (d.id.empty()) err(say("cfg.device.id"));
    if (!ids.insert(d.id).second) err(say("cfg.device.dup", {{"id", d.id}}));
    if (!cat.deviceClass(d.cls)) err(say("cfg.device.class", {{"cls", d.cls}}));
  }
  const auto& t = c.tank();
  if (isNum(t.capacityL) && t.capacityL <= 0) err(say("cfg.tank.capacity"));
  if (isNum(t.minL) && isNum(t.capacityL) && t.minL >= t.capacityL)
    err(say("cfg.tank.min"));
  // v1 der Logik kennt genau eine Zone; Messen und Regeln lesen nur die erste.
  if (c.zones.size() > 1) err(say("cfg.zone.count"));
  for (const auto& z : c.zones) {
    if (z.name.empty()) err(say("cfg.zone.name"));
    if (z.kind != "room" && z.kind != "tent" && z.kind != "greenhouse") err(say("cfg.zone.kind", {{"name", z.name}}));
    bool tankKnown = false;
    for (const auto& tk : c.tanks) tankKnown = tankKnown || tk.id == z.tank;
    if (!tankKnown) err(say("cfg.zone.tank", {{"name", z.name}, {"tank", z.tank}}));
    for (const auto& [role, b] : z.roles)
      if (role.rfind("zone.", 0) != 0) err(say("cfg.role.place", {{"role", role}}));
  }
  for (const auto& tk : c.tanks)
    for (const auto& [role, b] : tk.roles)
      if (role.rfind("zone.", 0) == 0) err(say("cfg.role.place_tank", {{"role", role}}));
  std::vector<std::pair<std::string, Binding>> all;
  c.forEachBinding([&](const std::string& r, const Binding& b) { all.emplace_back(r, b); });
  std::map<std::pair<std::string, int>, json> switchUse;  // Kanal → Rolle
  for (const auto& [role, b] : all) {
    const RoleDef* rd = cat.role(role);
    if (rd && !rd->profile.empty()) {
      auto key = std::make_pair(b.device, b.channel);
      if (switchUse.count(key)) err(say("cfg.role.shared", {{"label", nameOf(*rd)}, {"other", switchUse[key]}}));
      else switchUse[key] = nameOf(*rd);
    }
    if (!rd) {
      err(say("cfg.role.unknown", {{"role", role}}));
      continue;
    }
    const DeviceCfg* d = c.device(b.device);
    if (!d) {
      err(say("cfg.role.device", {{"label", nameOf(*rd)}, {"device", b.device}}));
      continue;
    }
    const DeviceClassDef* dc = cat.deviceClass(d->cls);
    bool ok = false;
    if (dc)
      for (const auto& p : dc->provides) ok = ok || rd->allows(p);
    const json device = deviceName(cat, d->id, d->cls, d->name);
    if (!ok) err(say("cfg.role.cap", {{"label", nameOf(*rd)}, {"device", device}}));
    const int channels = dc && dc->channels > 0 ? dc->channels : 1;
    if (b.channel < 0 || b.channel >= channels) err(say("cfg.role.channel", {{"label", nameOf(*rd)}, {"device", device}, {"n", b.channel + 1}}));
  }
  // A sign of life only for a sensor on its own schedule, whose class says so
  // in code, never by catalog data (SD-042, R7); source and channel as the
  // platform writes them, so nothing else rides along in the configuration.
  auto lifeChars = [](const std::string& s, bool dot) {
    return std::all_of(s.begin(), s.end(), [&](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || (dot && ch == '.'); });
  };
  for (const auto& d : c.devices) {
    if (d.life.empty() && d.lifeChannel.empty()) continue;
    const DeviceClassDef* dc = cat.deviceClass(d.cls);
    if (!dc || !dc->ownReportSchedule || d.life.empty() || d.life.size() > 255 || !lifeChars(d.life, true) ||
        d.lifeChannel.size() > 16 || !lifeChars(d.lifeChannel, false))
      err(say("cfg.device.life", {{"name", deviceName(cat, d.id, d.cls, d.name)}}));
  }
  std::set<std::string> pumps, canIds;
  for (const auto& k : c.canisters) {
    if (k.id.empty() || !canIds.insert(k.id).second) err(say("cfg.canister.id"));
    if (k.kind != "nutrient" && k.kind != "ph_down" && k.kind != "ph_up")
      err(say("cfg.canister.kind", {{"name", k.name}, {"kind", k.kind}}));
    if (!k.pump.empty()) {
      if (!pumps.insert(k.pump).second) err(say("cfg.canister.pump"));
      const DeviceCfg* d = c.device(k.pump);
      if (!d || d->cls != "pump_cap") err(say("cfg.canister.pumpclass", {{"name", k.name}}));
    }
    if (!k.pair.empty() && k.kind != "nutrient") err(say("cfg.canister.pair", {{"name", k.name}}));
  }
  // Paare müssen vollständig sein: eine Paar-Gruppe mit nur einem Kanister ist ein Fehler.
  std::map<std::string, int> pairCount;
  for (const auto& k : c.canisters)
    if (!k.pair.empty()) pairCount[k.pair]++;
  for (const auto& [p, n] : pairCount)
    if (n < 2) err(say("cfg.pair.single", {{"pair", p}}));
  std::set<std::string> recipeIds;  // what each recipe learned is kept by its ID (SD-092)
  for (const auto& r : c.recipes) {
    if (r.id.empty() || !recipeIds.insert(r.id).second) err(say("cfg.recipe.id", {{"name", r.name}}));
    if (r.steps.empty()) err(say("cfg.recipe.empty", {{"name", r.name}}));
    std::set<std::string> pairsInRecipe, seen;
    for (const auto& s : r.steps) {
      const CanisterCfg* k = c.canister(s.canister);
      if (!k) {
        err(say("cfg.recipe.canister", {{"name", r.name}}));
        continue;
      }
      if (!seen.insert(k->id).second) err(say("cfg.recipe.twice", {{"name", r.name}, {"part", k->name}}));
      // pH-Korrektur ist nie Teil eines Rezepts, sie kommt immer zuletzt (RAT-004).
      if (k->kind != "nutrient") err(say("cfg.recipe.ph", {{"name", r.name}, {"part", k->name}}));
      if (!isNum(s.mlPerL) || s.mlPerL <= 0) err(say("cfg.recipe.amount", {{"name", r.name}, {"part", k->name}}));
      if (!k->pair.empty()) pairsInRecipe.insert(k->pair);
      // A fixed part of a pair would leave its partner scaled alone (RAT-054)
      if (s.fixed && !k->pair.empty()) out.push_back(say("cfg.recipe.fixed_pair", {{"name", r.name}, {"part", k->name}}));
    }
    for (const auto& p : pairsInRecipe) {
      int inRecipe = 0;
      for (const auto& s : r.steps) {
        const CanisterCfg* k = c.canister(s.canister);
        if (k && k->pair == p) inRecipe++;
      }
      if (inRecipe != pairCount[p]) err(say("cfg.recipe.pair", {{"name", r.name}, {"pair", p}}));
    }
    // The chart's EC becomes a phase's target, so it lies in the target's range;
    // the effect it implies lies within a quarter to four times the start effect
    // (SD-057, RAT-056). The message names the EC that would fit the amounts.
    if (!std::isnan(r.refEc)) {
      const ParamDef* pd = paramOf("ec_control", "ec_target");
      const double lo = pd && isNum(pd->min) ? pd->min : 0.0, hi = pd && isNum(pd->max) ? pd->max : kNaN;
      double sum = 0;  // the parts EC control scales: the effect is per ml/L of those (RAT-085)
      for (const auto& s : r.steps) sum += !s.fixed && isNum(s.mlPerL) && s.mlPerL > 0 ? s.mlPerL : 0;
      if (!isNum(r.refEc) || r.refEc <= 0 || r.refEc < lo || (isNum(hi) && r.refEc > hi))
        out.push_back(say("cfg.recipe.ref_range", {{"name", r.name}, {"min", lo}, {"max", numOrNull(hi)}}));
      else if (sum > 0) {
        const double effect = r.refEc / sum;
        double fitLo = kRefEffectMin * kEcStartEffect * sum, fitHi = kRefEffectMax * kEcStartEffect * sum;
        if (std::max(fitLo, lo) <= (isNum(hi) ? std::min(fitHi, hi) : fitHi)) {  // within the target's range where it can be
          fitLo = std::max(fitLo, lo);
          if (isNum(hi)) fitHi = std::min(fitHi, hi);
        }
        const bool anyFixed = std::any_of(r.steps.begin(), r.steps.end(), [](const RecipeStep& s) { return s.fixed; });
        const json args = {{"name", r.name},
                           {"ec", r.refEc},
                           {"sum", sum},
                           {"amounts", say(anyFixed ? "cfg.recipe.amounts_scaled" : "cfg.recipe.amounts", {{"sum", sum}})},
                           {"lo", fitLo},
                           {"hi", fitHi}};
        if (effect < kRefEffectMin * kEcStartEffect) out.push_back(say("cfg.recipe.ref_far", args));
        else if (effect > kRefEffectMax * kEcStartEffect) out.push_back(say("cfg.recipe.ref_strong", args));
      }
    }
    // The chart's pH range proposes the grow's pH target: two values, low to high, in its range (RAT-066)
    if (!std::isnan(r.phLow) || !std::isnan(r.phHigh)) {
      const ParamDef* pd = paramOf("ph_control", "ph_target");
      const double lo = pd && isNum(pd->min) ? pd->min : 0.0, hi = pd && isNum(pd->max) ? pd->max : 14.0;
      if (!isNum(r.phLow) || !isNum(r.phHigh) || r.phLow > r.phHigh || r.phLow < lo || r.phHigh > hi)
        out.push_back(say("cfg.recipe.ph_band", {{"name", r.name}, {"min", lo}, {"max", hi}}));
    }
  }
  // A finding names the function and the parameter, and for a phase's value the phase too
  auto checkParam = [&](const FunctionDef& fd, const ParamDef& pd, const json& val, const std::optional<std::string>& phase) {
    if (pd.type != "number") return;
    json args = {{"fn", nameOf(fd)}, {"param", nameOf(fd, pd)}};
    if (phase) args["phase"] = *phase;
    if (!val.is_number()) {
      err(say(phase ? "cfg.phase.type" : "cfg.param.type", args));
      return;
    }
    double v = val.get<double>();
    if (!isNum(v) || (isNum(pd.min) && v < pd.min) || (isNum(pd.max) && v > pd.max)) {
      args["min"] = numOrNull(pd.min);
      args["max"] = numOrNull(pd.max);
      args["unit"] = pd.unit.empty() ? std::string() : "\u00a0" + pd.unit;  // with its space, so none is left without one
      err(say(phase ? "cfg.phase.range" : "cfg.param.range", args));
    }
  };
  for (const auto& [id, f] : c.functions) {
    const FunctionDef* fd = cat.function(id);
    if (!fd) {
      err(say("cfg.function.unknown", {{"id", id}}));
      continue;
    }
    if (!f.params.is_object()) {
      err(say("cfg.params", {{"fn", nameOf(*fd)}}));
      continue;
    }
    for (const auto& [key, val] : f.params.items()) {
      const ParamDef* pd = fd->param(key);
      if (!pd) {
        err(say("cfg.param.unknown", {{"fn", nameOf(*fd)}, {"param", key}}));
        continue;
      }
      checkParam(*fd, *pd, val, std::nullopt);
    }
  }
  // A running grow cycle: its phase is one of its phases, each phase's days a
  // whole number, 0 or more. A finished cycle is only shown, so a value saved
  // before these checks never blocks a change once the cycle is finished.
  const auto& g = c.grow;
  if (g.state == "running") {
    if (g.phase < 0 || static_cast<size_t>(g.phase) >= g.phases.size()) out.push_back(say("cfg.grow.phase"));
    for (const auto& ph : g.phases)
      if (ph.days < 0) out.push_back(say("cfg.phase.days", {{"phase", ph.name}}));
  }
  // Phasen liefern Parameter (R4): dieselben Bereiche wie in den Einstellungen.
  for (const auto& ph : g.phases) {
    if (!ph.params.is_object()) {
      err(say("cfg.phase.params", {{"phase", ph.name}}));
      continue;
    }
    for (const auto& [key, val] : ph.params.items()) {
      bool known = false;
      for (const auto& fd : cat.functions)
        for (const auto& pd : fd.params)
          if (pd.key == key && pd.phase) {
            known = true;
            checkParam(fd, pd, val, ph.name);
          }
      if (!known) err(say("cfg.phase.unknown", {{"phase", ph.name}, {"param", key}}));
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
  return effectiveParams(cat, c, fn, c.grow.state == "running" ? c.grow.phase : -1);
}

ParamView effectiveParams(const Catalog& cat, const Config& c, const std::string& fn, int phase) {
  json v = json::object();
  const FunctionDef* fd = cat.function(fn);
  if (!fd) return ParamView(v);
  for (const auto& p : fd->params)
    if (!p.def.is_null()) v[p.key] = p.def;
  auto it = c.functions.find(fn);
  if (it != c.functions.end())
    for (const auto& [k, val] : it->second.params.items()) v[k] = val;
  if (c.grow.state == "running" && phase >= 0 && phase < static_cast<int>(c.grow.phases.size())) {
    const auto& ph = c.grow.phases[static_cast<size_t>(phase)].params;
    for (const auto& p : fd->params)
      if (p.phase && ph.contains(p.key)) v[p.key] = ph[p.key];
  }
  return ParamView(v);
}

void to_json(json& j, const RuntimeState& s) {
  json stock = json::object();
  for (const auto& [k, v] : s.stockMl) stock[k] = numOrNull(v);
  json effects = json::object();
  for (const auto& [k, v] : s.ecEffects) effects[k] = numOrNull(v);
  j = {{"stockMl", stock},
       {"tankVolumeL", numOrNull(s.tankVolumeL)},
       {"lastMixAt", s.lastMixAt},
       {"phEffect", numOrNull(s.phEffect)},
       {"ecEffect", numOrNull(s.ecEffect)},
       {"ecEffects", effects},
       {"latches", s.latches},
       {"selfReleased", s.selfReleased},
       {"jumpLocks", s.jumpLocks},
       {"manual", s.manual},
       {"manualAt", s.manualAt},
       {"clock", s.clock ? json(*s.clock) : json(nullptr)},
       {"bootCount", s.bootCount},
       {"stopped", s.stopped},
       {"refill", s.refill}};
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
  // An entry that is no positive number counts as nothing learned (R5): the recipe starts afresh
  if (j.contains("ecEffects") && j["ecEffects"].is_object())
    for (const auto& [k, v] : j["ecEffects"].items())
      if (v.is_number() && isNum(v.get<double>()) && v.get<double>() > 0) s.ecEffects[k] = v.get<double>();
  if (j.contains("latches")) s.latches = j["latches"].get<std::map<std::string, json>>();
  // Read as strictly as the latches: a damaged entry makes the whole state damaged (RAT-063)
  // A damaged or out-of-range time reads as implausible: the next latch holds, and the guard
  // counts from when the clock is known (SD-067); the other latches are kept
  if (j.contains("selfReleased")) {
    const json& sr = j["selfReleased"];
    if (!sr.is_object()) s.selfReleased["circulation.dry"] = 0;
    else
      for (const auto& [k, v] : sr.items()) {
        const double at = v.is_number() ? v.get<double>() : kNaN;
        s.selfReleased[k] = isNum(at) && at >= 0 && at <= static_cast<double>(kNotAfter) ? static_cast<Epoch>(at) : 0;
      }
  }
  if (j.contains("jumpLocks")) s.jumpLocks = j["jumpLocks"].get<std::map<std::string, Epoch>>();
  // The refill's record is cleaned when read: a damaged part never stops the hub. A fill that is not
  // an object still counts as a fill a restart cut; a count out of range starts again (SD-082)
  if (j.contains("refill") && j["refill"].is_object()) {
    const json& r = j["refill"];
    for (const char* k : {"fill", "check", "unconfirmed"}) {
      if (!r.contains(k)) continue;
      json part = r[k].is_object() ? r[k] : json::object();
      // An amount or a time that is not a number of zero or more is left out: read as not known
      for (const char* f : {"startL", "plannedL", "expectedL", "sumL", "openMin", "plannedMin", "flow", "at", "due", "seenAt", "closedMs", "personL"})
        if (part.contains(f) && !(part[f].is_number() && std::isfinite(part[f].get<double>()) && part[f].get<double>() >= 0)) part.erase(f);
      s.refill[k] = part;
    }
    const double n = numOrNaN(r, "unjudged");
    s.refill["unjudged"] = isNum(n) && n >= 0 && n < 100 ? static_cast<int>(n) : 0;
  }
  if (j.contains("manual")) s.manual = j["manual"].get<std::map<std::string, double>>();
  s.manualAt = j.value("manualAt", Epoch{0});
  if (j.contains("clock")) s.clock = stampFromJson(j["clock"]);
  if (j.contains("bootCount") && j["bootCount"].is_number_unsigned()) s.bootCount = j["bootCount"].get<std::uint32_t>();
  s.stopped = jbool(j, "stopped", false);
  return s;
}

}  // namespace gc
