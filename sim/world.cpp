// SPDX-License-Identifier: AGPL-3.0-or-later
#include "world.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace sim {

namespace {
constexpr Ms kSampleMs = 5000;        // head delivers every 5 s (as on the reference installation)
constexpr Ms kPhDeadMs = 60000;       // pH-Totzeit (RAT-052: ~80–99 s, hier gekürzt)
constexpr Ms kHwLimitMs = 90000;      // Zeitlimit in Hardware (Vorschlag firmware: 90 s)
constexpr double kPi = 3.14159265358979323846;
}  // namespace

World::World() : rng_(42) { reset(); }

void World::reset() {
  now_ = 0;
  tank = Tank{};
  ports.clear();
  out[0] = out[1] = false;
  netPlugs.clear();
  room = Room{};
  probeBuffer.reset();
  phTrail_.clear();
  runningCap_.clear();
  // Effects from measurements (sources in docs/SIMULATOR.md), pH effect as an assumption
  liquids = {
      {"grow_a", {"Teil A", 0.275, -0.12}},
      {"grow_b", {"Teil B", 0.275, -0.10}},
      {"calmag", {"CalMag", 0.217, -0.02}},
      {"ph_down", {"pH−", 1.04, -4.0}},
  };
}

std::string World::newId(const std::string& prefix) {
  static const char* hex = "0123456789ABCDEF";
  std::string s = prefix + "-";
  for (int i = 0; i < 6; ++i) s += hex[rng_() % 16];
  return s;
}

double World::noise(double sigma) const {
  std::normal_distribution<double> d(0.0, sigma);
  return d(rng_);
}

bool World::plug(int port, const std::string& cls, std::string id) {
  if (port < 1 || port > 8 || ports.count(port)) return false;
  Device d;
  d.cls = cls;
  d.port = port;
  if (id.empty()) {
    std::string prefix = cls == "dosing_block" ? "DB" : cls == "head_ph_ec" ? "PHEC" : cls == "head_ph" ? "PH"
                         : cls == "head_ec" ? "EC" : cls == "head_level" ? "LVL" : cls == "pump_cap" ? "CAP"
                         : cls == "head_climate" ? "CLIM" : cls == "head_co2" ? "CO2" : "DEV";
    id = newId(prefix);
  }
  d.id = id;
  d.pluggedAt = now_;
  ports[port] = d;
  return true;
}

bool World::unplug(int port) {
  auto it = ports.find(port);
  if (it == ports.end()) return false;
  for (auto& s : it->second.slots)
    if (s && s->state == 1) {
      s->state = 3;
      s->error = "Dosierblock getrennt";
      runningCap_.clear();
    }
  ports.erase(it);
  return true;
}

Device* World::deviceAt(int port) {
  auto it = ports.find(port);
  return it == ports.end() ? nullptr : &it->second;
}

Device* World::device(const std::string& id) {
  for (auto& [p, d] : ports)
    if (d.id == id) return &d;
  return nullptr;
}

Cap* World::cap(const std::string& id, Device** block, int* slot) {
  for (auto& [p, d] : ports)
    for (int i = 0; i < 6; ++i)
      if (d.slots[i] && d.slots[i]->id == id) {
        if (block) *block = &d;
        if (slot) *slot = i;
        return &*d.slots[i];
      }
  return nullptr;
}

bool World::plugCap(const std::string& blockId, int slot, const std::string& liquid, std::string id) {
  Device* b = device(blockId);
  if (!b || b->cls != "dosing_block" || slot < 0 || slot > 5 || b->slots[slot]) return false;
  Cap c;
  c.id = id.empty() ? newId("CAP") : id;
  c.liquid = liquid;
  // Flow rates of the pump heads scatter as measured (RAT-054)
  std::uniform_real_distribution<double> fl(42.0, 53.0);
  c.trueFlow = fl(rng_);
  b->slots[slot] = c;
  return true;
}

bool World::unplugCap(const std::string& blockId, int slot) {
  Device* b = device(blockId);
  if (!b || slot < 0 || slot > 5 || !b->slots[slot]) return false;
  if (b->slots[slot]->state == 1) runningCap_.clear();
  b->slots[slot].reset();
  return true;
}

bool World::startRun(const std::string& capId, Ms ms, const std::string& jobId, std::string& err) {
  Device* block = nullptr;
  Cap* c = cap(capId, &block);
  if (!c || !block) {
    err = "Pumpe nicht gefunden";
    return false;
  }
  if (block->fault == "offline") {
    err = "Dosierblock antwortet nicht";
    return false;
  }
  if (c->jobId == jobId && c->state != 0) return true;  // Wiederholung: nicht doppelt dosieren
  if (!runningCap_.empty()) {
    err = "busy: ein Kanal läuft bereits";
    return false;
  }
  if (ms <= 0 || ms > kHwLimitMs) {
    err = "über dem Zeitlimit in Hardware";
    return false;
  }
  c->jobId = jobId;
  c->requested = ms;
  c->started = now_;
  c->elapsed = 0;
  c->state = 1;
  c->error.clear();
  runningCap_ = capId;
  return true;
}

void World::stopAllPumps() {
  for (auto& [p, d] : ports)
    for (auto& s : d.slots)
      if (s && s->state == 1) {
        s->state = 3;
        s->error = "gestoppt";
      }
  runningCap_.clear();
}

void World::fill(double volumeL, double ec, double ph) {
  tank.volumeL = std::max(0.0, volumeL);
  tank.ec = ec;
  tank.ph = ph;
  tank.pendingEc = tank.pendingPh = 0;
  phTrail_.clear();
}

NetPlug* World::netPlug(const std::string& id) {
  for (auto& p : netPlugs)
    if (p.id == id) return &p;
  return nullptr;
}

std::string World::addNetPlug(const std::string& cls, const std::vector<std::pair<std::string, double>>& loads) {
  NetPlug p;
  p.cls = cls;
  // Kennung wie bei Shelly: Modell, Bindestrich, MAC in Kleinbuchstaben
  p.id = (cls == "shelly_strip4" ? newId("shellypstripg4") : newId("shellyplugsg3")) + newId("").substr(1);
  for (auto& ch : p.id) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  p.ip = "192.168.1." + std::to_string(60 + netPlugs.size());
  size_t n = cls == "shelly_strip4" ? 4 : 1;
  for (size_t i = 0; i < n; ++i) {
    NetOutlet o;
    if (i < loads.size()) {
      o.load = loads[i].first;
      o.loadW = loads[i].second;
    }
    p.outlets.push_back(o);
  }
  netPlugs.push_back(p);
  return netPlugs.back().id;
}

void World::powerFail() {
  for (auto& p : netPlugs)
    for (auto& o : p.outlets) {
      o.beforeOutage = o.on;
      o.on = false;
    }
}

void World::powerReturn() {
  for (auto& p : netPlugs)
    for (auto& o : p.outlets) {
      const bool on = o.powerOn == gc::PowerOn::On || (o.powerOn == gc::PowerOn::Restore && o.beforeOutage);
      if (on && !o.on) {
        o.onSince = now_;  // the device's auto-off counts from here
        ++o.switchOns;
      }
      o.on = on;
    }
}

bool World::loadOn(const std::string& load) const {
  for (const auto& p : netPlugs)
    for (const auto& o : p.outlets)
      if (o.on && o.load == load) return true;  // Strom fließt auch ohne WLAN
  return false;
}

bool World::circulating() const { return out[0] || loadOn("circulation"); }

double World::rawAirTemp() const { return std::round((room.temp + noise(0.05)) * 100.0) / 100.0; }
double World::rawHumidity() const { return std::round((room.rh + noise(0.3)) * 10.0) / 10.0; }
double World::rawCo2() const { return std::round(room.co2 + noise(8.0)); }

void World::advance(Ms now) {
  if (now <= now_) return;
  // in Schritten von höchstens 1 s rechnen
  while (now_ < now) {
    Ms dt = std::min<Ms>(1000, now - now_);
    now_ += dt;
    step(dt);
    for (auto& p : netPlugs)
      for (auto& o : p.outlets)
        if (o.on && !std::isnan(o.autoOffS) && now_ - o.onSince >= static_cast<Ms>(o.autoOffS * 1000)) o.on = false;
  }
}

void World::step(Ms dt) {
  const double dts = static_cast<double>(dt) / 1000.0;
  // Raumklima (Annahmen, docs/SIMULATOR.md)
  {
    const bool light = loadOn("light"), exhaust = loadOn("exhaust"), hum = loadOn("humidifier"),
               dehum = loadOn("dehumidifier"), heat = loadOn("heater");
    // Abluft tauscht Luft gegen Außenluft: Wärme- und Feuchtegewinne sinken.
    const double keep = exhaust ? 0.45 : 1.0;
    const double targetT = ambientTemp + keep * ((light ? 6.0 : 0.0) + (heat ? 4.0 : 0.0) + (dehum ? 1.0 : 0.0));
    room.temp += (targetT - room.temp) * (1.0 - std::exp(-dts / (exhaust ? 600.0 : 1800.0)));
    const double targetRh = ambientRh + keep * (light ? 14.0 : 5.0);  // Verdunstung der Pflanzen
    room.rh += (targetRh - room.rh) * (1.0 - std::exp(-dts / (exhaust ? 400.0 : 1500.0)));
    if (hum) room.rh += 0.8 / 60.0 * dts;
    if (dehum) room.rh -= 0.6 / 60.0 * dts;
    room.rh = std::clamp(room.rh, 15.0, 97.0);
    const double targetCo2 = exhaust ? 420.0 : (light ? 380.0 : 600.0);
    room.co2 += (targetCo2 - room.co2) * (1.0 - std::exp(-dts / 900.0));
  }
  // Pumpen
  for (auto& [p, d] : ports)
    for (auto& s : d.slots) {
      if (!s || s->state != 1) continue;
      s->elapsed += dt;
      if (s->blocked && s->elapsed >= 300) {
        s->state = 3;
        s->error = "Blockade erkannt (Motorstrom)";
        runningCap_.clear();
        continue;
      }
      Ms eff = std::min(dt, s->requested - (s->elapsed - dt));
      double ml = s->trueFlow * static_cast<double>(std::max<Ms>(0, eff)) / 60000.0;
      bool intoCup = s->jobId.rfind("cal-", 0) == 0 || s->jobId.rfind("prime-", 0) == 0;
      if (!intoCup && tank.volumeL > 0.1 && ml > 0) {
        const Liquid& l = liquids[s->liquid];
        double mlPerL = ml / tank.volumeL;
        double buffer = 1.6 / (0.6 + std::max(0.0, tank.ec + tank.pendingEc));
        tank.pendingEc += l.ecPerMlL * mlPerL;
        tank.pendingPh += l.phPerMlL * mlPerL * buffer;
        tank.volumeL += ml / 1000.0;
      }
      if (s->elapsed >= s->requested) {
        s->elapsed = s->requested;
        s->state = 2;
        runningCap_.clear();
      }
    }
  // Zulauf (Ausgang 2): frisches Wasser verdünnt
  if (out[1]) {
    double dV = inletLpm / 60.0 * dts;
    double v = tank.volumeL;
    if (v + dV > 0) {
      tank.ec = (tank.ec * v + 0.02 * dV) / (v + dV);
      tank.pendingEc = tank.pendingEc * v / (v + dV);
      tank.ph += (7.0 - tank.ph) * dV / (v + dV) * 0.5;
    }
    tank.volumeL += dV;
  }
  // Durchmischung: mit Umwälzpumpe schnell, sonst langsam (Annahme; RAT-052: t63 26 s bei 20 L)
  double tau = (circulating() ? 26.0 : 240.0) * std::max(0.5, tank.volumeL / 20.0);
  double k = 1.0 - std::exp(-dts / tau);
  tank.ec += tank.pendingEc * k;
  tank.pendingEc *= (1.0 - k);
  tank.ph += tank.pendingPh * k;
  tank.pendingPh *= (1.0 - k);
  // Drift (assumption, not measured): pH rises, EC falls, water evaporates
  double hours = dts / 3600.0;
  if (tank.volumeL > 1) {
    tank.ph += 0.006 * hours * (7.6 - tank.ph) / 1.5;
    tank.ec = std::max(0.0, tank.ec - 0.003 * hours * tank.ec);
    tank.volumeL = std::max(0.0, tank.volumeL - 0.025 * hours * tank.volumeL / 60.0 * 2.0);
  }
  tank.ph = std::clamp(tank.ph, 2.5, 9.5);
  tank.temp = 20.5 + 1.5 * std::sin(2 * kPi * static_cast<double>(now_ % 86400000) / 86400000.0);
  phTrail_.emplace_back(now_, tank.ph);
  while (phTrail_.size() > 2 && phTrail_[1].first <= now_ - kPhDeadMs) phTrail_.pop_front();
  // Köpfe liefern alle 5 s einen Messwert
  for (auto& [p, d] : ports) {
    if (d.cls == "dosing_block" || d.fault == "offline") continue;
    if (now_ - d.lastSample >= kSampleMs && now_ - d.pluggedAt >= 2000) d.lastSample = now_;
  }
}

double World::rawPh(const Device* d) const {
  double truth = phTrail_.empty() ? tank.ph : phTrail_.front().second;
  if (probeBuffer && probeKind == "ph") truth = *probeBuffer;
  if (tank.volumeL < 0.5 && !probeBuffer) truth = 7.3 + noise(0.2);  // Sonde trocken
  double raw = 7.0 + (truth - 7.0) / phSlope + phOffset + noise(0.006);
  if (d) {
    if (d->fault == "jump") raw += 2.1;
    if (d->fault == "frozen") return d->frozenPh;
  }
  return std::round(raw * 1000.0) / 1000.0;
}

double World::rawEc(const Device* d) const {
  double truth = tank.ec;
  if (probeBuffer && probeKind == "ec") truth = *probeBuffer;
  if (tank.volumeL < 0.5 && !probeBuffer) truth = 0.0;
  double raw = truth * ecFactor + noise(0.004);
  if (d) {
    if (d->fault == "ec_zero") return 0.003;
    if (d->fault == "frozen") return d->frozenEc;
  }
  return std::max(0.0, std::round(raw * 1000.0) / 1000.0);
}

double World::rawLevelV() const {
  double v = tank.volumeL;
  double volts = v < 3.0 ? 0.5 + 0.03 * v : 0.59 + 0.04 * (v - 3.0);  // unten nichtlinear (vgl. RAT-078)
  return std::round((volts + noise(0.002)) * 10000.0) / 10000.0;
}

double World::rawTemp() const { return std::round((tank.temp + noise(0.02)) * 100.0) / 100.0; }

json World::toJson() const {
  json ps = json::array();
  for (const auto& [p, d] : ports) {
    json slots = json::array();
    for (const auto& s : d.slots) {
      if (!s) {
        slots.push_back(nullptr);
        continue;
      }
      slots.push_back({{"id", s->id},
                       {"liquid", s->liquid},
                       {"trueFlow", s->trueFlow},
                       {"storedFlow", gc::numOrNull(s->storedFlow)},
                       {"blocked", s->blocked},
                       {"state", s->state},
                       {"cupMl", s->state >= 2 && (s->jobId.rfind("cal-", 0) == 0)
                                     ? json(std::round(s->trueFlow * static_cast<double>(s->elapsed) / 600.0) / 100.0)
                                     : json(nullptr)}});
    }
    ps.push_back({{"port", p}, {"id", d.id}, {"class", d.cls}, {"fault", d.fault}, {"slots", d.cls == "dosing_block" ? slots : json(nullptr)}});
  }
  return {{"ports", ps},
          {"tank", {{"volumeL", tank.volumeL}, {"ec", tank.ec}, {"ph", tank.ph}, {"temp", tank.temp},
                    {"pendingEc", tank.pendingEc}, {"pendingPh", tank.pendingPh}}},
          {"outputs", {out[0], out[1]}},
          {"netPlugs", [&] {
             json a = json::array();
             for (const auto& p : netPlugs) {
               json outs = json::array();
               for (const auto& o : p.outlets)
                 outs.push_back({{"on", o.on}, {"load", o.load}, {"loadW", o.loadW}, {"powerOn", gc::powerOnName(o.powerOn)},
                                 {"autoOffS", gc::numOrNull(o.autoOffS)}, {"switchOns", o.switchOns}});
               a.push_back({{"id", p.id}, {"class", p.cls}, {"ip", p.ip}, {"fault", p.fault}, {"outlets", outs}});
             }
             return a;
           }()},
          {"probeBuffer", probeBuffer ? json(*probeBuffer) : json(nullptr)},
          {"probeKind", probeKind},
          {"liquids", [&] {
             json l = json::object();
             for (const auto& [k, v] : liquids) l[k] = v.name;
             return l;
           }()}};
}

}  // namespace sim

namespace sim {

json World::save() const {
  json ps = json::array();
  for (const auto& [p, d] : ports) {
    json slots = json::array();
    for (const auto& s : d.slots)
      slots.push_back(s ? json{{"id", s->id}, {"liquid", s->liquid}, {"trueFlow", s->trueFlow},
                               {"storedFlow", gc::numOrNull(s->storedFlow)}}
                        : json(nullptr));
    ps.push_back({{"port", p}, {"id", d.id}, {"class", d.cls}, {"slots", slots}});
  }
  json np = json::array();
  for (const auto& p : netPlugs) {
    json outs = json::array();
    for (const auto& o : p.outlets)
      outs.push_back({{"load", o.load}, {"loadW", o.loadW}, {"powerOn", gc::powerOnName(o.powerOn)},
                      {"autoOffS", gc::numOrNull(o.autoOffS)}, {"powerLimitW", gc::numOrNull(o.powerLimitW)}});
    np.push_back({{"id", p.id}, {"class", p.cls}, {"ip", p.ip}, {"outlets", outs}});
  }
  return {{"ports", ps},
          {"netPlugs", np},
          {"tank", {{"volumeL", tank.volumeL}, {"ec", tank.ec + tank.pendingEc}, {"ph", tank.ph + tank.pendingPh}}}};
}

void World::restore(const json& j) {
  reset();
  for (const auto& p : j.value("ports", json::array())) {
    int port = p.value("port", 0);
    plug(port, p.value("class", std::string()), p.value("id", std::string()));
    Device* d = deviceAt(port);
    if (!d) continue;
    d->pluggedAt = -10000;
    const auto& slots = p.value("slots", json::array());
    for (size_t i = 0; i < slots.size() && i < 6; ++i) {
      if (slots[i].is_null()) continue;
      Cap c;
      c.id = slots[i].value("id", std::string());
      c.liquid = slots[i].value("liquid", std::string());
      c.trueFlow = slots[i].value("trueFlow", 50.0);
      c.storedFlow = gc::jnum(slots[i], "storedFlow");
      d->slots[i] = c;
    }
  }
  for (const auto& p : j.value("netPlugs", json::array())) {
    NetPlug np;
    np.id = p.value("id", std::string());
    np.cls = p.value("class", std::string());
    np.ip = p.value("ip", std::string());
    for (const auto& o : p.value("outlets", json::array())) {
      NetOutlet dose;  // nach dem Neustart des Simulators aus
      dose.load = o.value("load", std::string());
      dose.loadW = o.value("loadW", 0.0);
      if (auto po = gc::powerOnFromName(o.value("powerOn", std::string()))) dose.powerOn = *po;
      else if (o.value("initialOff", false)) dose.powerOn = gc::PowerOn::Off;  // older world.json: a flag
      dose.autoOffS = gc::jnum(o, "autoOffS");
      dose.powerLimitW = gc::jnum(o, "powerLimitW");
      np.outlets.push_back(dose);
    }
    netPlugs.push_back(np);
  }
  const auto& t = j.value("tank", json::object());
  fill(gc::jnum(t, "volumeL", 0), gc::jnum(t, "ec", 0.02), gc::jnum(t, "ph", 7.0));
}

}  // namespace sim
