#include "gc/catalog.hpp"

#include <cstdio>
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
    r.series = v.value("series", false);
    if (!c.capabilities.count(r.capability))
      throw std::runtime_error("Katalog: Rolle " + id + " mit unbekannter Capability");
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

}  // namespace gc
