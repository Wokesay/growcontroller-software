// SPDX-License-Identifier: AGPL-3.0-or-later
#include "simbus.hpp"

namespace sim {

using gc::DeviceReport;
using gc::PortReport;
using gc::PortState;

void SimBus::poll(gc::Ms now) { w_.advance(now); }

std::vector<PortReport> SimBus::ports() const {
  std::vector<PortReport> out;
  for (int p = 1; p <= 8; ++p) {
    PortReport r;
    r.port = p;
    auto it = w_.ports.find(p);
    if (it == w_.ports.end()) {
      r.state = PortState::Empty;
      out.push_back(r);
      continue;
    }
    const Device& d = it->second;
    r.deviceId = d.id;
    r.cls = d.cls;
    if (w_.now() - d.pluggedAt < 1000) {
      r.state = PortState::Checking;
      r.message = {"port.checking", "Prüfmessung läuft", gc::json::object()};
    } else if (d.cls == "pump_cap") {
      // Kennung passt nicht zum Hub-Port: Hardware gibt den Port nicht frei (PD-012)
      r.state = PortState::Rejected;
      r.message = {"port.cap_on_hub", "Pumpe direkt am Hub. Bitte auf den Dosierblock stecken.", gc::json::object()};
    } else if (d.fault == "offline") {
      r.state = PortState::Fault;
      r.message = {"port.no_answer", "Gerät antwortet nicht", gc::json::object()};
    } else {
      r.state = PortState::Ok;
    }
    out.push_back(r);
  }
  return out;
}

std::vector<DeviceReport> SimBus::devices() const {
  std::vector<DeviceReport> out;
  DeviceReport hub;
  hub.id = kHubOut;
  hub.cls = "hub_outputs";
  hub.online = true;
  hub.fw = "0.1.0";
  out.push_back(hub);
  for (const auto& [p, d] : w_.ports) {
    if (d.cls == "pump_cap") continue;  // nicht freigegeben
    bool online = d.fault != "offline" && w_.now() - d.pluggedAt >= 1000;
    DeviceReport r;
    r.id = d.id;
    r.cls = d.cls;
    r.port = p;
    r.online = online;
    r.fw = "0.1.0";
    out.push_back(r);
    if (d.cls != "dosing_block") continue;
    for (int i = 0; i < 6; ++i) {
      if (!d.slots[i]) continue;
      const Cap& c = *d.slots[i];
      DeviceReport cr;
      cr.id = c.id;
      cr.cls = "pump_cap";
      cr.parent = d.id;
      cr.port = p;
      cr.slot = i;
      cr.online = online;
      cr.info = {{"flowMlPerMin", gc::numOrNull(c.storedFlow)}};
      out.push_back(cr);
    }
  }
  return out;
}

std::optional<gc::Sample> SimBus::sample(const std::string& dev, const std::string& cap) const {
  const Device* d = nullptr;
  for (const auto& [p, x] : w_.ports)
    if (x.id == dev) d = &x;
  if (!d || d->fault == "offline" || d->lastSample < 0) return std::nullopt;
  std::string key = dev + "|" + cap;
  auto it = cache_.find(key);
  if (it != cache_.end() && it->second.first == d->lastSample) return gc::Sample{it->second.second, d->lastSample};
  double v = gc::kNaN;
  const bool ph = d->cls == "head_ph_ec" || d->cls == "head_ph";
  const bool ec = d->cls == "head_ph_ec" || d->cls == "head_ec";
  if (ph && cap == "measure.ph") v = w_.rawPh(d);
  else if (ec && cap == "measure.ec") v = w_.rawEc(d);
  else if (ec && cap == "measure.water_temp") v = w_.rawTemp();
  else if (d->cls == "head_climate" && cap == "measure.air_temp") v = w_.rawAirTemp();
  else if (d->cls == "head_climate" && cap == "measure.humidity") v = w_.rawHumidity();
  else if (d->cls == "head_co2" && cap == "measure.co2") v = w_.rawCo2();
  else if (d->cls == "head_level" && cap == "measure.level") {
    v = w_.rawLevelV();
  }
  if (!gc::isNum(v)) return std::nullopt;
  cache_[key] = {d->lastSample, v};
  return gc::Sample{v, d->lastSample};
}

bool SimBus::startRun(const std::string& pump, gc::Ms ms, const std::string& jobId, std::string& err) {
  return w_.startRun(pump, ms, jobId, err);
}

gc::RunStatus SimBus::runStatus(const std::string& pump) const {
  gc::RunStatus st;
  Device* block = nullptr;
  Cap* c = w_.cap(pump, &block);
  if (!c) {
    st.state = gc::RunStatus::State::Failed;
    st.error = "Pumpe getrennt";
    return st;
  }
  // Block antwortet nicht: der Hub sieht nur den zuletzt gelesenen Stand.
  if (block && block->fault == "offline") return lastRun_[pump];
  st.jobId = c->jobId;
  st.requestedMs = c->requested;
  st.actualMs = c->elapsed;
  st.error = c->error;
  switch (c->state) {
    case 1: st.state = gc::RunStatus::State::Running; break;
    case 2: st.state = gc::RunStatus::State::Done; break;
    case 3: st.state = gc::RunStatus::State::Failed; break;
    default: st.state = gc::RunStatus::State::Idle;
  }
  lastRun_[pump] = st;
  return st;
}

void SimBus::stopAllPumps() { w_.stopAllPumps(); }

bool SimBus::setSwitch(const std::string& dev, int channel, bool on, std::string& err) {
  if (dev != kHubOut || channel < 0 || channel > 1) {
    err = "unbekannter Ausgang";
    return false;
  }
  w_.out[channel] = on;
  return true;
}

std::optional<bool> SimBus::switchState(const std::string& dev, int channel) const {
  if (dev != kHubOut || channel < 0 || channel > 1) return std::nullopt;
  return w_.out[channel];
}

bool SimBus::writePumpCalibration(const std::string& pump, double mlPerMin, std::string& err) {
  Cap* c = w_.cap(pump);
  if (!c) {
    err = "Pumpe nicht erreichbar";
    return false;
  }
  c->storedFlow = mlPerMin;
  return true;
}

}  // namespace sim
