// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ha_assign.hpp"

#include "gc/messages.hpp"

namespace ha {

namespace {

const std::string kMeasurePrefix = "measure.";
const std::string kDevicePrefix = "ha.";
const std::string kClassPrefix = "ha_";

// The measure a role reads: "zone.air_temp" → "air_temp"; "" for a role that is no measurement.
std::string measureOfRole(const gc::Catalog& cat, const std::string& role) {
  const gc::RoleDef* rd = cat.role(role);
  if (!rd || rd->capability.rfind(kMeasurePrefix, 0) != 0) return "";
  return rd->capability.substr(kMeasurePrefix.size());
}

// The Home Assistant entity behind a device ID ("ha.sensor.x" → "sensor.x").
std::string entityOf(const std::string& device) {
  return device.rfind(kDevicePrefix, 0) == 0 ? device.substr(kDevicePrefix.size()) : "";
}

// A Home Assistant sensor no role uses any more: removed and no longer read.
// Called with the hub's lock held.
void dropIfUnused(gc::Hub& hub, HaBus& bus, const std::string& device) {
  const std::string entity = entityOf(device);
  if (entity.empty()) return;
  bool used = false;
  hub.config().forEachBinding([&](const std::string&, const gc::Binding& b) { used = used || b.device == device; });
  if (used) return;
  if (hub.config().device(device)) hub.removeDevice(device);
  bus.deselect(entity);
}

}  // namespace

gc::Result assign(gc::Hub& hub, HaBus& bus, const std::string& role, const std::string& entity,
                  const std::function<void()>& letHubSee) {
  if (!entity.empty() && !validEntityId(entity)) return gc::Result::fail(422, gc::say("ha.unknown"));
  const std::string device = entity.empty() ? "" : kDevicePrefix + entity;
  std::string before;  // the role's device before this call
  std::string measures;
  bool wasConfigured = false, wasSelected = false;
  {
    std::lock_guard<std::recursive_mutex> l(hub.mutex());  // the hub's config is read here
    measures = measureOfRole(hub.catalog(), role);
    if (measures.empty()) return gc::Result::fail(422, gc::say("ha.role"));
    if (const gc::Binding* b = hub.config().binding(role)) before = b->device;
    if (entity.empty()) {
      if (before.empty()) return gc::Result::ok();
      if (auto r = hub.unbindRole(role); r.status != 200) return r;
      dropIfUnused(hub, bus, before);
      return gc::Result::ok();
    }
    wasConfigured = hub.config().device(device) != nullptr;
    const std::string usedFor = bus.selectedMeasure(entity);
    // Listed in the mapping file for another measure but not set up: free to pick.
    if (!usedFor.empty() && usedFor != measures && !wasConfigured) bus.deselect(entity);
    wasSelected = bus.selectedMeasure(entity) == measures;
    gc::Msg why;
    if (!bus.select(entity, measures, why)) return gc::Result::fail(422, why);
    if (before == device) return gc::Result::ok();  // already so, and read again if it was not
  }
  std::string name = entity;
  for (const auto& c : bus.candidates())
    if (c.entityId == entity && !c.name.empty()) name = c.name;
  // The hub sees a newly selected device with its next tick, and one picked
  // again for another measure with its new class: wait for that, without its lock.
  const std::string cls = kClassPrefix + measures;
  auto seen = [&] {
    std::lock_guard<std::recursive_mutex> l(hub.mutex());
    const gc::json st = hub.state();  // kept: a loop over a temporary's part would dangle
    for (const auto& d : st["devices"])
      if (gc::jstr(d, "id") == device) return gc::jstr(d, "class") == cls;
    return false;
  };
  bool ready = seen();
  for (int i = 0; i < 30 && !ready; ++i) {
    letHubSee();
    ready = seen();
  }
  std::lock_guard<std::recursive_mutex> l(hub.mutex());
  const gc::Result accepted = ready ? hub.acceptDevice(device, name) : gc::Result::fail(404, "device.unknown", "");
  // bindRole replaces the former binding in one step: the role is never without a sensor.
  gc::Result bound = accepted.status == 200 ? hub.bindRole(role, device, 0) : accepted;
  if (bound.status != 200) {  // undo only what this call added
    if (!wasConfigured && hub.config().device(device)) hub.removeDevice(device);
    if (!wasSelected) bus.deselect(entity);
    return bound.status == 404 ? gc::Result::fail(504, gc::say("ha.timeout", {{"entity", entity}})) : bound;
  }
  if (!before.empty() && before != device) dropIfUnused(hub, bus, before);
  return gc::Result::ok();
}

void adoptFromConfig(gc::Hub& hub, HaBus& bus) {
  std::lock_guard<std::recursive_mutex> l(hub.mutex());
  for (const auto& d : hub.config().devices) {
    const std::string entity = entityOf(d.id);
    if (entity.empty() || d.cls.rfind(kClassPrefix, 0) != 0) continue;
    const std::string measures = d.cls.substr(kClassPrefix.size());
    if (!validEntityId(entity) || !knownMeasure(measures)) continue;
    // The configuration wins over the mapping file: a sensor picked for
    // another measure than the file says is read for what it is bound to.
    if (const std::string listed = bus.selectedMeasure(entity); !listed.empty() && listed != measures) bus.deselect(entity);
    bus.adopt({entity, measures});
  }
}

gc::Result candidatesRoute(gc::Api& api, const HaBus& bus, const gc::ApiRequest& req) {
  if (!api.authorized(req)) return gc::Result::fail(401, "auth.required", "Bitte anmelden");
  return gc::Result::ok(bus.candidatesJson());
}

gc::Result assignRoute(gc::Api& api, gc::Hub& hub, HaBus& bus, const gc::ApiRequest& req,
                       const std::function<void()>& letHubSee) {
  if (!api.authorized(req)) return gc::Result::fail(401, "auth.required", "Bitte anmelden");
  const auto body = gc::json::parse(req.body, nullptr, false);
  // "entity" must be given ("" takes the sensor away), so a request that forgot it removes nothing.
  if (!body.is_object() || !body.contains("entity") || !body["entity"].is_string())
    return gc::Result::fail(400, "api.bad_input", "Eingabe hat das falsche Format");
  return assign(hub, bus, gc::jstr(body, "role"), gc::jstr(body, "entity"), letHubSee);
}

}  // namespace ha
