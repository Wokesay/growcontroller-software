// SPDX-License-Identifier: AGPL-3.0-or-later
#include "netbus.hpp"

namespace sim {

std::vector<gc::DeviceReport> SimNetBus::devices() const {
  std::vector<gc::DeviceReport> out;
  for (const auto& p : w_.netPlugs) {
    gc::DeviceReport d;
    d.id = p.id;
    d.cls = p.cls;
    d.online = p.fault != "offline";
    d.fw = "1.4.4";
    json outs = json::array();
    for (const auto& o : p.outlets)
      outs.push_back({{"on", d.online ? json(o.on) : json(nullptr)},
                      {"powerW", d.online ? json(o.on ? o.loadW : 0.0) : json(nullptr)}});
    d.info = {{"ip", p.ip}, {"outlets", outs}};
    out.push_back(d);
  }
  return out;
}

bool SimNetBus::owns(const std::string& dev) const { return w_.netPlug(dev) != nullptr; }

NetOutlet* SimNetBus::outlet(const std::string& dev, int channel, std::string& err) const {
  NetPlug* p = w_.netPlug(dev);
  if (!p) {
    err = "Gerät unbekannt";
    return nullptr;
  }
  if (p->fault == "offline") {
    err = "im Netzwerk nicht erreichbar";
    return nullptr;
  }
  if (channel < 0 || channel >= static_cast<int>(p->outlets.size())) {
    err = "Dose " + std::to_string(channel + 1) + " gibt es nicht";
    return nullptr;
  }
  return &p->outlets[static_cast<size_t>(channel)];
}

bool SimNetBus::setSwitch(const std::string& dev, int channel, bool on, std::string& err) {
  NetOutlet* o = outlet(dev, channel, err);
  if (!o) return false;
  if (w_.netPlug(dev)->fault == "stuck") {
    err = "Schaltbefehl abgelehnt";  // nur Befehle; Auto-Off im Gerät und Stromausfall wirken weiter
    return false;
  }
  if (on && !o->on) {
    o->onSince = w_.now();
    ++o->switchOns;
  }
  o->on = on;
  return true;
}

std::optional<bool> SimNetBus::switchState(const std::string& dev, int channel) const {
  std::string err;
  NetOutlet* o = outlet(dev, channel, err);
  if (!o) return std::nullopt;
  return o->on;
}

std::optional<double> SimNetBus::powerW(const std::string& dev, int channel) const {
  std::string err;
  NetOutlet* o = outlet(dev, channel, err);
  if (!o) return std::nullopt;
  return o->on ? o->loadW : 0.0;
}

bool SimNetBus::configure(const std::string& dev, int channel, const gc::SwitchSafety& s, std::string& err) {
  NetOutlet* o = outlet(dev, channel, err);
  if (!o) return false;
  NetPlug* p = w_.netPlug(dev);
  if (p->fault == "readonly") {
    err = "Gerät lehnt die Einstellung ab (Anmeldung?)";
    return false;
  }
  if (p->fault == "ignore") return true;  // meldet Erfolg, speichert aber nichts – das Rücklesen fällt auf
  o->initialOff = s.initialOff;
  o->autoOffS = s.autoOffS;
  o->powerLimitW = s.powerLimitW;
  return true;
}

std::optional<gc::SwitchSafety> SimNetBus::readConfig(const std::string& dev, int channel) const {
  std::string err;
  NetOutlet* o = outlet(dev, channel, err);
  if (!o) return std::nullopt;
  gc::SwitchSafety s;
  s.initialOff = o->initialOff;
  s.autoOffS = o->autoOffS;
  s.powerLimitW = o->powerLimitW;
  return s;
}

}  // namespace sim
