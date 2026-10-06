#include "gc/resolver.hpp"

#include <set>

namespace gc {

void to_json(json& j, const FunctionState& f) {
  json checks = json::array();
  for (const auto& c : f.checks)
    checks.push_back({{"level", c.level}, {"ok", c.ok}, {"soft", c.soft}, {"text", c.text}, {"fix", c.fix}, {"shop", c.shop}});
  j = {{"id", f.id},           {"label", f.label},     {"group", f.group},       {"text", f.text},
       {"stage", f.stage},     {"setup", f.setup},     {"enabled", f.enabled},   {"alwaysOn", f.alwaysOn},
       {"checks", checks},     {"summary", f.summary}};
}

PumpMap pumpsFrom(const std::vector<DeviceReport>& devices) {
  PumpMap m;
  for (const auto& d : devices) {
    if (d.cls != "pump_cap") continue;
    PumpInfo p;
    p.id = d.id;
    p.online = d.online;
    p.flowMlPerMin = jnum(d.info, "flowMlPerMin");
    p.fault = d.fault;
    m[d.id] = p;
  }
  return m;
}

namespace {

const char* kindLabel(const std::string& k) {
  if (k == "ph_down") return "pH−";
  if (k == "ph_up") return "pH+";
  if (k == "nutrient") return "Nährstoff";
  return "Kanister";
}

struct Eval {
  const Catalog& cat;
  const Config& cfg;
  const std::vector<DeviceReport>& devices;
  const PumpMap& pumps;
  const SensorTruth& truth;

  bool classPresent(const std::string& cls) const {
    for (const auto& d : cfg.devices)
      if (d.cls == cls) return true;
    for (const auto& d : devices)
      if (d.cls == cls) return true;
    return false;
  }

  bool capabilityPresent(const std::string& cap) const {
    for (const auto* dc : cat.classesProviding(cap))
      if (classPresent(dc->id)) return true;
    return false;
  }

  std::vector<std::string> shopFor(const std::string& cap) const {
    std::vector<std::string> out;
    for (const auto* dc : cat.classesProviding(cap)) out.push_back(dc->id);
    return out;
  }

  std::string classLabels(const std::string& cap) const {
    std::string s;
    for (const auto* dc : cat.classesProviding(cap)) s += (s.empty() ? "" : " oder ") + dc->label;
    return s;
  }

  bool deviceOnline(const std::string& id) const {
    for (const auto& d : devices)
      if (d.id == id) return d.online;
    return false;
  }

  void role(const Requirement& r, std::vector<ReqResult>& out) const {
    const RoleDef* rd = cat.role(r.role);
    const CapabilityDef* cap = rd ? cat.capability(rd->capability) : nullptr;
    if (!rd || !cap) return;
    if (!capabilityPresent(rd->capability)) {
      out.push_back({"hardware", false, false, "Dafür brauchst du: " + classLabels(rd->capability), "expand",
                     shopFor(rd->capability)});
      return;
    }
    const Binding* b = cfg.binding(r.role);
    if (!b) {
      out.push_back({"setup", false, false, "„" + rd->label + "“ zuordnen", "roles", {}});
      return;
    }
    out.push_back({"setup", true, false, rd->label + " zugeordnet", "roles", {}});
    if (r.calibrated && cap->kind == "measure") {
      std::string kind = rd->capability == "measure.ph" ? "ph" : rd->capability == "measure.ec" ? "ec" : "tank_curve";
      const json* cal = cfg.calibration(b->device, kind);
      double probe = kind == "tank_curve" ? 1.0 : 7.0;
      bool ok = cal && SensorTruth::calibrate(rd->capability, probe, cal).has_value();
      out.push_back({"setup", ok, false,
                     ok ? rd->label + " kalibriert" : cap->label + "-Sonde kalibrieren",
                     "calibrate:" + b->device + ":" + kind, {}});
    }
    if (cap->kind == "measure") {
      const Reading& rd2 = truth.get(r.role);
      out.push_back({"runtime", rd2.usable(), false,
                     rd2.usable() ? cap->label + " gültig" : cap->label + ": " + rd2.reason.text, "", {}});
    } else {
      bool on = deviceOnline(b->device);
      out.push_back({"runtime", on, false, on ? rd->label + " erreichbar" : rd->label + ": Gerät antwortet nicht", "", {}});
    }
  }

  void canisters(const Requirement& r, std::vector<ReqResult>& out) const {
    if (!classPresent("pump_cap")) {
      out.push_back({"hardware", false, false, "Dafür brauchst du: Dosierblock und Pumpe", "expand",
                     {"dosing_block", "pump_cap"}});
      return;
    }
    int withPump = 0, calibrated = 0, online = 0;
    std::string firstUncal;
    for (const auto& k : cfg.canisters) {
      if (r.canisterKind != "any" && k.kind != r.canisterKind) continue;
      if (k.pump.empty()) continue;
      withPump++;
      auto it = pumps.find(k.pump);
      if (it != pumps.end() && it->second.online) online++;
      if (it != pumps.end() && isNum(it->second.flowMlPerMin) && it->second.flowMlPerMin > 0) calibrated++;
      else if (firstUncal.empty()) firstUncal = k.name + "|" + k.pump;
    }
    std::string what = r.canisterKind == "any" ? "Kanister" : kindLabel(r.canisterKind);
    if (!r.calibrated) {
      bool ok = withPump >= r.min;
      out.push_back({"setup", ok, false,
                     ok ? std::to_string(withPump) + " × " + what + " mit Pumpe" : what + "-Kanister anlegen und Pumpe zuordnen",
                     "canisters", {}});
      if (ok) out.push_back({"runtime", online >= r.min, false, online >= r.min ? "Pumpen erkannt" : "Pumpe nicht erkannt – auf den Dosierblock gesteckt?", "", {}});
    } else {
      bool ok = calibrated >= r.min;
      std::string name = firstUncal.substr(0, firstUncal.find('|'));
      std::string pump = firstUncal.empty() ? "" : firstUncal.substr(firstUncal.find('|') + 1);
      out.push_back({"setup", ok, false,
                     ok ? what + "-Pumpen eingemessen"
                        : (withPump == 0 ? what + "-Kanister anlegen und Pumpe zuordnen" : "Pumpe für " + name + " einmessen"),
                     withPump == 0 ? "canisters" : "calibrate-pump:" + pump, {}});
    }
  }

  void config(const Requirement& r, std::vector<ReqResult>& out) const {
    if (r.config == "recipe") {
      bool ok = !cfg.recipes.empty();
      out.push_back({"setup", ok, false, ok ? "Rezept vorhanden" : "Rezept anlegen", "recipes", {}});
    } else if (r.config == "tank_capacity") {
      bool ok = isNum(cfg.tank().capacityL);
      out.push_back({"setup", ok, false, ok ? "Nutzvolumen gesetzt" : "Nutzvolumen des Tanks angeben", "tank", {}});
    }
  }

  void function(const Requirement& r, std::vector<ReqResult>& out) const {
    const FunctionDef* fd = cat.function(r.function);
    auto it = cfg.functions.find(r.function);
    bool on = it != cfg.functions.end() && it->second.enabled;
    std::string label = fd ? fd->label : r.function;
    out.push_back({"setup", on, false, on ? label + " eingeschaltet" : "„" + label + "“ einrichten und einschalten",
                   "function:" + r.function, {}});
  }

  void device(const Requirement& r, std::vector<ReqResult>& out) const {
    const DeviceClassDef* dc = cat.deviceClass(r.device);
    bool ok = classPresent(r.device);
    std::string label = dc ? dc->label : r.device;
    out.push_back({"hardware", ok, false, ok ? label + " erkannt" : "Dafür brauchst du: " + label, "expand",
                   ok ? std::vector<std::string>{} : std::vector<std::string>{r.device}});
  }
};

}  // namespace

std::vector<FunctionState> resolveFunctions(const Catalog& cat, const Config& cfg,
                                            const std::vector<DeviceReport>& devices, const PumpMap& pumps,
                                            const SensorTruth& truth) {
  Eval ev{cat, cfg, devices, pumps, truth};
  std::vector<FunctionState> out;
  for (const auto& f : cat.functions) {
    FunctionState s;
    s.id = f.id;
    s.label = f.label;
    s.group = f.group;
    s.text = f.text;
    s.stage = f.stage;
    s.alwaysOn = f.alwaysOn;
    auto it = cfg.functions.find(f.id);
    s.enabled = f.alwaysOn || (it != cfg.functions.end() && it->second.enabled);
    for (const auto& r : f.hard) {
      switch (r.kind) {
        case Requirement::Kind::Role: ev.role(r, s.checks); break;
        case Requirement::Kind::Canisters: ev.canisters(r, s.checks); break;
        case Requirement::Kind::Config: ev.config(r, s.checks); break;
        case Requirement::Kind::Function: ev.function(r, s.checks); break;
        case Requirement::Kind::Device: ev.device(r, s.checks); break;
      }
    }
    // Doppelte Hardware-Hinweise zusammenfassen (z. B. pH und EC aus einem Kopf).
    std::set<std::string> seen;
    std::vector<ReqResult> dedup;
    for (const auto& c : s.checks)
      if (seen.insert(c.level + c.text).second) dedup.push_back(c);
    s.checks = dedup;
    for (const auto& r : f.soft) {
      if (r.kind != Requirement::Kind::Role) continue;
      const RoleDef* rd = cat.role(r.role);
      bool ok = cfg.binding(r.role) != nullptr;
      s.checks.push_back({"setup", ok, true, ok ? (rd ? rd->label : r.role) + " vorhanden" : r.why, ok ? "" : "expand",
                          ok || !rd ? std::vector<std::string>{} : ev.shopFor(rd->capability)});
    }
    bool hw = true, setup = true, soft = true;
    std::vector<std::string> missing;
    for (const auto& c : s.checks) {
      if (c.ok || c.level == "runtime") continue;
      if (c.soft) soft = false;
      else if (c.level == "hardware") hw = false;
      else setup = false;
      if (!c.soft) missing.push_back(c.text);
    }
    auto join = [](const std::vector<std::string>& v) {
      std::string s2;
      for (size_t i = 0; i < v.size() && i < 3; ++i) s2 += (i ? " · " : "") + v[i];
      return s2;
    };
    if (!hw) {
      s.setup = "unavailable";
      std::vector<std::string> hwMissing;
      for (const auto& c : s.checks)
        if (!c.ok && c.level == "hardware") hwMissing.push_back(c.text);
      s.summary = {"fn.unavailable", join(hwMissing), json::object()};
    } else if (!setup) {
      s.setup = "needs_setup";
      s.summary = {"fn.needs_setup", "Noch einzurichten: " + join(missing), json::object()};
    } else if (!soft) {
      s.setup = "limited";
      std::string why;
      for (const auto& c : s.checks)
        if (c.soft && !c.ok) why = c.text;
      s.summary = {"fn.limited", "Eingeschränkt: " + why, json::object()};
    } else {
      s.setup = "ready";
      s.summary = {"fn.ready", s.enabled ? "Eingeschaltet" : "Bereit, aus", json::object()};
    }
    out.push_back(s);
  }
  return out;
}

}  // namespace gc
