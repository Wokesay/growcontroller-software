// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/catalog.hpp"

#include <cstdio>
#include <map>
#include <stdexcept>

#include "gc/embedded.hpp"

namespace gc {

void to_json(json& j, const Msg& m) {
  j = json{{"key", m.key}, {"text", m.text}};
  if (!m.args.empty()) j["args"] = m.args;
}

double jnum(const json& j, const char* key, double fallback) {
  if (!j.is_object()) return fallback;
  auto it = j.find(key);
  if (it == j.end() || !it->is_number()) return fallback;
  return it->get<double>();
}

bool jbool(const json& j, const char* key, bool fallback) {
  if (!j.is_object()) return fallback;
  auto it = j.find(key);
  if (it == j.end() || !it->is_boolean()) return fallback;
  return it->get<bool>();
}

std::string utf8Prefix(const std::string& s, size_t maxBytes) {
  if (s.size() <= maxBytes) return s;
  size_t n = maxBytes;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;  // Folgebyte: zurück zum Zeichenanfang
  return s.substr(0, n);
}

std::string jstr(const json& j, const char* key, const std::string& fallback) {
  if (!j.is_object()) return fallback;
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return fallback;
  return it->get<std::string>();
}

std::string fmt(double v, int decimals) {
  if (!isNum(v)) return "–";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  std::string s(buf);
  for (char& c : s)
    if (c == '.') c = ',';
  return s;
}

namespace {

std::vector<std::string> strList(const json& j, const char* key) {
  std::vector<std::string> out;
  if (j.contains(key) && j[key].is_array())
    for (const auto& v : j[key]) out.push_back(v.get<std::string>());
  return out;
}

Requirement parseReq(const json& r) {
  Requirement q;
  q.why = jstr(r, "why");
  if (r.contains("role")) {
    q.kind = Requirement::Kind::Role;
    q.role = r["role"].get<std::string>();
    q.calibrated = r.value("calibrated", false);
  } else if (r.contains("canisters")) {
    q.kind = Requirement::Kind::Canisters;
    const auto& c = r["canisters"];
    q.min = c.value("min", 1);
    q.canisterKind = c.value("kind", std::string("any"));
    q.calibrated = c.value("calibrated", false);
  } else if (r.contains("config")) {
    q.kind = Requirement::Kind::Config;
    q.config = r["config"].get<std::string>();
  } else if (r.contains("function")) {
    q.kind = Requirement::Kind::Function;
    q.function = r["function"].get<std::string>();
  } else if (r.contains("device")) {
    q.kind = Requirement::Kind::Device;
    q.device = r["device"].get<std::string>();
  } else {
    throw std::runtime_error("Katalog: unbekannte Voraussetzung " + r.dump());
  }
  return q;
}

}  // namespace

const ParamDef* FunctionDef::param(const std::string& key) const {
  for (const auto& p : params)
    if (p.key == key) return &p;
  return nullptr;
}

Catalog Catalog::fromJson(const json& j) {
  Catalog c;
  c.raw = j;
  c.version = j.value("catalogVersion", 0);
  for (const auto& [id, v] : j.at("capabilities").items()) {
    CapabilityDef d;
    d.id = id;
    d.label = jstr(v, "label");
    d.unit = jstr(v, "unit");
    d.kind = jstr(v, "kind");
    d.decimals = v.value("decimals", 2);
    if (v.contains("plausible")) {
      d.plausMin = v["plausible"][0].get<double>();
      d.plausMax = v["plausible"][1].get<double>();
    }
    d.maxAgeS = jnum(v, "maxAgeS", 60);
    d.jump = jnum(v, "jump");
    c.capabilities[id] = d;
  }
  for (const auto& [id, v] : j.at("deviceClasses").items()) {
    DeviceClassDef d;
    d.id = id;
    d.label = jstr(v, "label");
    d.attach = jstr(v, "attach");
    d.shop = jstr(v, "shop");
    d.text = jstr(v, "text");
    d.stage = v.value("stage", 0);
    d.slots = v.value("slots", 0);
    d.channels = v.value("channels", 0);
    d.provides = strList(v, "provides");
    d.calibrations = strList(v, "calibrations");
    d.accepts = strList(v, "accepts");
    for (const auto& cap : d.provides)
      if (!c.capabilities.count(cap))
        throw std::runtime_error("Katalog: " + id + " liefert unbekannte Capability " + cap);
    c.deviceClasses[id] = d;
  }
  for (const auto& [id, v] : j.at("roles").items()) {
    RoleDef r;
    r.id = id;
    r.label = jstr(v, "label");
    r.capability = jstr(v, "capability");
    if (v.contains("accepts") && v["accepts"].is_array())
      for (const auto& a : v["accepts"]) r.accepts.push_back(a.get<std::string>());
    if (r.accepts.empty()) r.accepts.push_back(r.capability);
    if (r.capability.empty()) r.capability = r.accepts.front();
    r.series = v.value("series", false);
    r.profile = jstr(v, "profile");
    r.maxOnS = jnum(v, "maxOnS");
    for (const auto& a : r.accepts)
      if (!c.capabilities.count(a)) throw std::runtime_error("Katalog: Rolle " + id + " mit unbekannter Capability " + a);
    if (!r.profile.empty() && r.profile != "dauer" && r.profile != "puls" && r.profile != "kompressor" && r.profile != "heizen")
      throw std::runtime_error("Katalog: Rolle " + id + " mit unbekanntem Profil " + r.profile);
    if ((r.profile == "puls" || r.profile == "heizen") && !(isNum(r.maxOnS) && r.maxOnS > 0))
      throw std::runtime_error("Katalog: Rolle " + id + " braucht eine Höchstlaufzeit (Profil " + r.profile + ")");
    const std::string apl = jstr(v, "afterPowerLoss", "off");
    if (apl != "off" && apl != "on") throw std::runtime_error("Katalog: Rolle " + id + ": afterPowerLoss muss on oder off sein");
    r.onAfterPowerLoss = apl == "on";
    // Only the fans may come back on by themselves (PD-050); the catalog can
    // only tighten safety (R7), so the list lives here, not in the data.
    const bool fan = id == "zone.exhaust" || id == "zone.circulation_fan";
    const bool mainsOnly = r.accepts.size() == 1 && r.accepts.front() == "switch.mains";
    if (r.onAfterPowerLoss && (!fan || !mainsOnly || r.profile != "dauer" || isNum(r.maxOnS)))
      throw std::runtime_error("Katalog: Rolle " + id + ": nach Stromausfall an nur für Lüfter ohne Höchstlaufzeit");
    c.roles[id] = r;
  }
  for (const auto& v : j.at("functions")) {
    FunctionDef f;
    f.id = jstr(v, "id");
    f.label = jstr(v, "label");
    f.group = jstr(v, "group");
    f.text = jstr(v, "text");
    f.stage = v.value("stage", 0);
    f.alwaysOn = v.value("alwaysOn", false);
    if (v.contains("requires"))
      for (const auto& r : v["requires"]) f.hard.push_back(parseReq(r));
    if (v.contains("soft"))
      for (const auto& r : v["soft"]) f.soft.push_back(parseReq(r));
    if (v.contains("params"))
      for (const auto& p : v["params"]) {
        ParamDef d;
        d.key = jstr(p, "key");
        d.label = jstr(p, "label");
        d.type = jstr(p, "type");
        d.unit = jstr(p, "unit");
        d.min = jnum(p, "min");
        d.max = jnum(p, "max");
        d.step = jnum(p, "step");
        d.def = p.value("default", json());
        d.phase = p.value("phase", false);
        if (p.contains("options"))
          for (const auto& o : p["options"])
            d.options.emplace_back(o[0].get<std::string>(), o[1].get<std::string>());
        f.params.push_back(d);
      }
    for (const auto& r : f.hard) {
      if (r.kind == Requirement::Kind::Role && !c.roles.count(r.role))
        throw std::runtime_error("Katalog: " + f.id + " verlangt unbekannte Rolle " + r.role);
    }
    c.functions.push_back(f);
  }
  c.templates = j.value("templates", json::object());
  return c;
}

Catalog Catalog::builtin() { return fromJson(json::parse(embedded::kCatalogJson)); }

Msg nameOf(const CapabilityDef& c) { return {"cat.cap." + c.id, c.label, json::object()}; }
Msg nameOf(const DeviceClassDef& d) { return {"cat.class." + d.id, d.label, json::object()}; }
Msg nameOf(const RoleDef& r) { return {"cat.role." + r.id, r.label, json::object()}; }
Msg nameOf(const FunctionDef& f) { return {"cat.fn." + f.id, f.label, json::object()}; }
Msg nameOf(const FunctionDef& f, const ParamDef& p) { return {"cat.param." + f.id + "." + p.key, p.label, json::object()}; }

bool ownName(const std::string& name, const std::string& cls, const Catalog& cat) {
  if (name.empty()) return false;
  // The class names as the German catalog had them, which a device accepted
  // without a name stored until 0.1.0-proto.5 (SD-083); fixed, never extended
  static const std::map<std::string, std::string> kFormer = {
      {"dosing_block", "Dosierblock"},
      {"pump_cap", "Pumpe"},
      {"head_ph_ec", "Sensorkopf pH/EC"},
      {"head_ph", "Sensorkopf pH"},
      {"head_ec", "Sensorkopf EC"},
      {"head_level", "Sensorkopf Füllstand"},
      {"head_climate", "Sensorkopf Klima"},
      {"head_co2", "Sensorkopf CO2"},
      {"shelly_plug", "Schaltbare Steckdose"},
      {"shelly_strip4", "Schaltbare Steckdosenleiste"},
      {"hub_outputs", "12-V-Ausgänge des Hubs"},
      {"ha_ph", "pH aus Home Assistant"},
      {"ha_ec", "EC aus Home Assistant"},
      {"ha_water_temp", "Wassertemperatur aus Home Assistant"},
      {"ha_level", "Füllstand aus Home Assistant"},
      {"ha_air_temp", "Lufttemperatur aus Home Assistant"},
      {"ha_humidity", "Luftfeuchte aus Home Assistant"},
      {"ha_co2", "CO2 aus Home Assistant"},
      {"ha_ph_raw", "pH aus Home Assistant (Rohwert)"},
  };
  auto former = kFormer.find(cls);
  if (former != kFormer.end() && name == former->second) return false;
  const DeviceClassDef* dc = cat.deviceClass(cls);
  return !(dc && name == dc->label);
}

json roleName(const Catalog& cat, const std::string& id) {
  const RoleDef* rd = cat.role(id);
  return rd ? json(nameOf(*rd)) : json(id);
}

json deviceName(const Catalog& cat, const std::string& id, const std::string& cls, const std::string& name) {
  if (ownName(name, cls, cat)) return name;
  const DeviceClassDef* dc = cat.deviceClass(cls);
  return dc ? json(nameOf(*dc)) : json(id);
}

bool RoleDef::allows(const std::string& cap) const {
  for (const auto& a : accepts)
    if (a == cap) return true;
  return false;
}

const CapabilityDef* Catalog::capability(const std::string& id) const {
  auto it = capabilities.find(id);
  return it == capabilities.end() ? nullptr : &it->second;
}
const DeviceClassDef* Catalog::deviceClass(const std::string& id) const {
  auto it = deviceClasses.find(id);
  return it == deviceClasses.end() ? nullptr : &it->second;
}
const RoleDef* Catalog::role(const std::string& id) const {
  auto it = roles.find(id);
  return it == roles.end() ? nullptr : &it->second;
}
const FunctionDef* Catalog::function(const std::string& id) const {
  for (const auto& f : functions)
    if (f.id == id) return &f;
  return nullptr;
}
std::vector<const DeviceClassDef*> Catalog::classesProviding(const std::string& cap) const {
  std::vector<const DeviceClassDef*> out;
  for (const auto& [id, d] : deviceClasses)
    for (const auto& p : d.provides)
      if (p == cap) out.push_back(&d);
  return out;
}

// The catalog data as loaded, plus what the hub sets in code and the web
// app needs to know: a class calibrated outside the hub (RAT-025), so the
// setup need not guess it. Data that claims it is not read (R7).
json Catalog::forApi() const {
  json out = raw;
  if (out.contains("deviceClasses") && out["deviceClasses"].is_object())
    for (auto& [id, cls] : out["deviceClasses"].items()) {
      const DeviceClassDef* dc = deviceClass(id);
      if (cls.is_object()) cls["externalCalibration"] = dc && dc->externalCalibration;
    }
  return out;
}

}  // namespace gc
