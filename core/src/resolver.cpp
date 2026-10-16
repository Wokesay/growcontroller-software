// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/resolver.hpp"

#include <set>

#include "gc/messages.hpp"

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

// Why a soft requirement matters, for the three the catalog has, each by its
// function and role; another keeps the catalog's text without a key (SD-083)
Msg softWhy(const std::string& fn, const Requirement& r) {
  if (fn == "mix" && r.role == "tank.circulation") return say("req.why.stir");
  if (fn == "monitor_tank" && r.role == "tank.water_temp") return say("req.why.temp");
  if (fn == "circulation" && r.role == "tank.level") return say("req.why.dry_run");
  return {"", r.why, json::object()};
}

// The kind of bottle a requirement asks for, as a message
Msg kindLabel(const std::string& k) {
  if (k == "ph_down") return say("req.kind.ph_down");
  if (k == "ph_up") return say("req.kind.ph_up");
  if (k == "nutrient") return say("req.kind.nutrient");
  return say("req.kind.bottle");
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

  // What a capability needs: one device class, or one of several
  Msg needs(const std::string& cap) const {
    json labels = json::array();
    for (const auto* dc : cat.classesProviding(cap)) labels.push_back(nameOf(*dc));
    return labels.size() > 1 ? say("req.needs_one_of", {{"items", labels}}) : say("req.needs", {{"items", labels}});
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
    bool present = false;
    for (const auto& a : rd->accepts) present = present || capabilityPresent(a);
    if (!present) {
      out.push_back({"hardware", false, false, needs(rd->capability), "expand", shopFor(rd->capability)});
      return;
    }
    const Binding* b = cfg.binding(r.role);
    if (!b) {
      out.push_back({"setup", false, false, say("req.assign", {{"label", nameOf(*rd)}}), "roles", {}});
      return;
    }
    out.push_back({"setup", true, false, say("req.assigned", {{"label", nameOf(*rd)}}), "roles", {}});
    const DeviceCfg* dev = cfg.device(b->device);
    const DeviceClassDef* dc = dev ? cat.deviceClass(dev->cls) : nullptr;
    if (r.calibrated && cap->kind == "measure" && dc && dc->externalCalibration) {
      // Calibrated elsewhere; the hub cannot calibrate it and has not checked it (RAT-025).
      out.push_back({"setup", false, false, say("req.external", {{"label", nameOf(*rd)}}), "", {}});
    } else if (r.calibrated && cap->kind == "measure") {
      std::string kind = rd->capability == "measure.ph" ? "ph" : rd->capability == "measure.ec" ? "ec" : "tank_curve";
      const json* cal = cfg.calibration(b->device, kind);
      double probe = kind == "tank_curve" ? 1.0 : 7.0;
      bool ok = cal && SensorTruth::calibrate(rd->capability, probe, cal).has_value();
      out.push_back({"setup", ok, false,
                     kind == "tank_curve" ? (ok ? say("req.calibrated_level") : say("req.calibrate_level"))
                                          : (ok ? say("req.calibrated", {{"label", nameOf(*cap)}}) : say("req.calibrate", {{"label", nameOf(*cap)}})),
                     "calibrate:" + b->device + ":" + kind, {}});
    }
    if (cap->kind == "measure") {
      const Reading& rd2 = truth.get(r.role);
      // Calibrated elsewhere: where the role needs a calibration, the setup line
      // above says so already (one cause, one line); elsewhere this line does,
      // in the resolver's wording
      const bool elsewhere = rd2.reason.key == kCalibratedElsewhere;
      if (!(elsewhere && r.calibrated))
        out.push_back({"runtime", rd2.usable(), false,
                       rd2.usable()  ? say("req.valid", {{"label", nameOf(*cap)}})
                       : elsewhere   ? say("req.external", {{"label", nameOf(*cap)}})
                                     : say("req.reason", {{"label", nameOf(*cap)}, {"reason", rd2.reason}}),
                       "", {}});
    } else {
      bool on = deviceOnline(b->device);
      out.push_back({"runtime", on, false, on ? say("req.reachable", {{"label", nameOf(*rd)}}) : say("req.offline", {{"label", nameOf(*rd)}}), "", {}});
    }
  }

  void canisters(const Requirement& r, std::vector<ReqResult>& out) const {
    if (!classPresent("pump_cap")) {
      out.push_back({"hardware", false, false, say("req.needs_dosing"), "expand", {"dosing_block", "pump_cap"}});
      return;
    }
    int withPump = 0, calibrated = 0, online = 0;
    std::string firstName, firstPump;  // the first bottle whose pump has no calibration value
    for (const auto& k : cfg.canisters) {
      if (r.canisterKind != "any" && k.kind != r.canisterKind) continue;
      if (k.pump.empty()) continue;
      withPump++;
      auto it = pumps.find(k.pump);
      if (it != pumps.end() && it->second.online) online++;
      if (it != pumps.end() && isNum(it->second.flowMlPerMin) && it->second.flowMlPerMin > 0) calibrated++;
      else if (firstPump.empty()) {
        firstName = k.name;
        firstPump = k.pump;
      }
    }
    // Any bottle, or one kind of them
    const bool any = r.canisterKind == "any";
    const Msg add = any ? say("req.add_bottle") : say("req.add_bottle_kind", {{"kind", kindLabel(r.canisterKind)}});
    if (!r.calibrated) {
      bool ok = withPump >= r.min;
      const Msg have = any ? say("req.bottles", {{"n", withPump}}) : say("req.bottles_kind", {{"n", withPump}, {"kind", kindLabel(r.canisterKind)}});
      out.push_back({"setup", ok, false, ok ? have : add, "canisters", {}});
      if (ok) out.push_back({"runtime", online >= r.min, false, online >= r.min ? say("req.pumps_seen") : say("req.pump_missing"), "", {}});
    } else {
      bool ok = calibrated >= r.min;
      const Msg done = any ? say("req.pumps_calibrated") : say("req.pumps_calibrated_kind", {{"kind", kindLabel(r.canisterKind)}});
      out.push_back({"setup", ok, false, ok ? done : (withPump == 0 ? add : say("req.calibrate_pump", {{"name", firstName}})),
                     withPump == 0 ? "canisters" : "calibrate-pump:" + firstPump, {}});
    }
  }

  void config(const Requirement& r, std::vector<ReqResult>& out) const {
    if (r.config == "recipe") {
      bool ok = !cfg.recipes.empty();
      out.push_back({"setup", ok, false, ok ? say("req.recipe") : say("req.add_recipe"), "recipes", {}});
    } else if (r.config == "tank_capacity") {
      bool ok = isNum(cfg.tank().capacityL);
      out.push_back({"setup", ok, false, ok ? say("req.volume") : say("req.set_volume"), "tank", {}});
    }
  }

  void function(const Requirement& r, std::vector<ReqResult>& out) const {
    const FunctionDef* fd = cat.function(r.function);
    auto it = cfg.functions.find(r.function);
    bool on = it != cfg.functions.end() && it->second.enabled;
    const json label = fd ? json(nameOf(*fd)) : json(r.function);
    out.push_back({"setup", on, false, on ? say("req.fn_on", {{"label", label}}) : say("req.fn_setup", {{"label", label}}),
                   "function:" + r.function, {}});
  }

  void device(const Requirement& r, std::vector<ReqResult>& out) const {
    const DeviceClassDef* dc = cat.deviceClass(r.device);
    bool ok = classPresent(r.device);
    const json label = dc ? json(nameOf(*dc)) : json(r.device);
    out.push_back({"hardware", ok, false, ok ? say("req.detected", {{"label", label}}) : say("req.needs", {{"items", json::array({label})}}), "expand",
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
    // A line two requirements share is shown once (e.g. the same head for two readings)
    std::set<std::string> seen;
    std::vector<ReqResult> dedup;
    for (const auto& c : s.checks)
      // by key and English text, as the line reads; never throws, whatever a name holds
      if (seen.insert(c.level + '|' + c.text.key + '|' + c.text.text).second) dedup.push_back(c);
    s.checks = dedup;
    for (const auto& r : f.soft) {
      if (r.kind != Requirement::Kind::Role) continue;
      const RoleDef* rd = cat.role(r.role);
      bool ok = cfg.binding(r.role) != nullptr;
      s.checks.push_back({"setup", ok, true, ok ? say("req.assigned", {{"label", roleName(cat, r.role)}}) : softWhy(f.id, r), ok ? "" : "expand",
                          ok || !rd ? std::vector<std::string>{} : ev.shopFor(rd->capability)});
    }
    bool hw = true, setup = true, soft = true;
    std::vector<Msg> missing;
    for (const auto& c : s.checks) {
      if (c.ok || c.level == "runtime") continue;
      if (c.soft) soft = false;
      else if (c.level == "hardware") hw = false;
      else setup = false;
      if (!c.soft) missing.push_back(c.text);
    }
    // A summary names the first open item, and how many more the checklist names
    auto first = [](const std::vector<Msg>& v) { return json{{"first", v.empty() ? Msg{} : v.front()}, {"more", v.empty() ? 0 : v.size() - 1}}; };
    if (!hw) {
      s.setup = "unavailable";
      std::vector<Msg> hwMissing;
      for (const auto& c : s.checks)
        if (!c.ok && c.level == "hardware") hwMissing.push_back(c.text);
      s.summary = hwMissing.size() > 1 ? say("fn.unavailable_more", first(hwMissing)) : say("fn.unavailable", first(hwMissing));
    } else if (!setup) {
      s.setup = "needs_setup";
      s.summary = missing.size() > 1 ? say("fn.needs_setup_more", first(missing)) : say("fn.needs_setup", first(missing));
    } else if (!soft) {
      s.setup = "limited";
      Msg why;
      for (const auto& c : s.checks)
        if (c.soft && !c.ok) why = c.text;
      s.summary = say("fn.limited", {{"why", why}});
    } else {
      s.setup = "ready";
      s.summary = s.enabled ? say("fn.on") : say("fn.ready");
    }
    out.push_back(s);
  }
  return out;
}

}  // namespace gc
