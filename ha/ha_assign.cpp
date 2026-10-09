// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ha_assign.hpp"

namespace ha {

namespace {

// "measure.air_temp" → "air_temp"; "" for a role that is no measurement.
std::string measureOf(const gc::Catalog& cat, const std::string& role) {
  const gc::RoleDef* rd = cat.role(role);
  if (!rd || rd->capability.rfind("measure.", 0) != 0) return "";
  return rd->capability.substr(8);
}

// The Home Assistant entity behind a device ID ("ha.sensor.x" → "sensor.x").
std::string entityOf(const std::string& device) { return device.rfind("ha.", 0) == 0 ? device.substr(3) : ""; }

// Takes the role's current Home Assistant sensor away, if it has one.
gc::Result release(gc::Hub& hub, HaBus& bus, const std::string& role) {
  const gc::Binding* b = hub.config().binding(role);
  if (!b) return gc::Result::ok();
  const std::string device = b->device, entity = entityOf(device);
  if (auto r = hub.unbindRole(role); r.status != 200) return r;
  if (entity.empty()) return gc::Result::ok();  // not from Home Assistant: only unbound
  bool usedElsewhere = false;
  hub.config().forEachBinding([&](const std::string&, const gc::Binding& x) { usedElsewhere = usedElsewhere || x.device == device; });
  if (usedElsewhere) return gc::Result::ok();
  if (hub.config().device(device)) hub.removeDevice(device);
  bus.deselect(entity);
  return gc::Result::ok();
}

}  // namespace

gc::Result assign(gc::Hub& hub, HaBus& bus, const std::string& role, const std::string& entity,
                  const std::function<void()>& letHubSee) {
  const std::string device = entity.empty() ? "" : HaBus::deviceId(entity);
  {
    std::lock_guard<std::recursive_mutex> l(hub.mutex());  // the hub's config is read here
    const std::string measures = measureOf(hub.catalog(), role);
    if (measures.empty()) return gc::Result::fail(422, "ha.role", "Not a measuring role: " + role);
    const gc::Binding* now = hub.config().binding(role);
    if (now && !entity.empty() && now->device == device) return gc::Result::ok();  // already so
    if (!entity.empty()) {
      std::string why;
      if (!bus.select(entity, measures, why)) return gc::Result::fail(422, "ha.select", why);
    }
    if (auto r = release(hub, bus, role); r.status != 200) {
      if (!entity.empty()) bus.deselect(entity);
      return r;
    }
    if (entity.empty()) return gc::Result::ok();
  }
  std::string name = entity;
  for (const auto& c : bus.candidates())
    if (c.entityId == entity && !c.name.empty()) name = c.name;
  // The hub sees a newly selected device with its next tick; wait without its lock.
  gc::Result accepted = hub.acceptDevice(device, name);
  for (int i = 0; i < 30 && accepted.status == 404; ++i) {
    letHubSee();
    accepted = hub.acceptDevice(device, name);
  }
  std::lock_guard<std::recursive_mutex> l(hub.mutex());
  gc::Result bound = accepted.status == 200 ? hub.bindRole(role, device, 0) : accepted;
  if (bound.status != 200) {  // nothing half done stays behind
    if (hub.config().device(device)) hub.removeDevice(device);
    bus.deselect(entity);
    return bound.status == 404 ? gc::Result::fail(504, "ha.timeout", "The hub did not see " + entity + " in time") : bound;
  }
  return gc::Result::ok();
}

}  // namespace ha
