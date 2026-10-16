// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ha_assign.hpp"

#include "gc/messages.hpp"

namespace ha {

namespace {

const std::string kMeasurePrefix = "measure.";
const std::string kDevicePrefix = "ha.";
const std::string kClassPrefix = "ha_";
const std::string kSignalSuffix = "_raw";

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
    // Listed in the mapping file for another measure, or as a signal or not
    // where the list offers it the other way, but not set up: the pick decides.
    const auto listed = bus.selected(entity);
    const auto offered = bus.offeredAsSignal(entity);
    if (listed && !wasConfigured && (usedFor != measures || (offered && *offered != listed->signal))) bus.deselect(entity);
    wasSelected = bus.selectedMeasure(entity) == measures;
    gc::Msg why;
    if (!bus.select(entity, measures, why)) return gc::Result::fail(422, why);
    if (before == device) return gc::Result::ok();  // already so, and read again if it was not
  }
  std::string name = entity;
  for (const auto& list : {bus.candidates(), bus.others()})
    for (const auto& c : list)
      if (c.entityId == entity && !c.name.empty()) name = c.name;
  // The hub sees a newly selected device with its next tick, and one picked
  // again for another measure with its new class: wait for that, without its lock.
  const auto picked = bus.selected(entity);
  const std::string cls = picked ? deviceClassOf(*picked) : kClassPrefix + measures;
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
  const gc::Result accepted = ready ? hub.acceptDevice(device, name) : gc::Result::fail(404, gc::say("device.not_detected"));
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
    std::string measures = d.cls.substr(kClassPrefix.size());
    const bool signal = measures.size() > kSignalSuffix.size() &&
                        measures.compare(measures.size() - kSignalSuffix.size(), kSignalSuffix.size(), kSignalSuffix) == 0;
    if (signal) measures.resize(measures.size() - kSignalSuffix.size());
    const Entity picked{entity, measures, signal, d.life, d.lifeChannel};
    // Only a class the catalog has: a configuration cannot invent a signal (ha_ec_raw does not exist yet)
    if (!validEntityId(entity) || !knownMeasure(measures) || deviceClassOf(picked) != d.cls || !hub.catalog().deviceClass(d.cls)) continue;
    // The configuration wins over the mapping file: a sensor picked for
    // another measure than the file says, or as a signal or not, is read as it is bound.
    if (const auto listed = bus.selected(entity); listed && deviceClassOf(*listed) != d.cls) bus.deselect(entity);
    bus.adopt(picked);
    bus.setLife(entity, d.life, d.lifeChannel);  // also over one the mapping file lists
  }
}

void syncLife(gc::Hub& hub, HaBus& bus) {
  for (const auto& d : hub.config().devices)
    if (const std::string entity = entityOf(d.id); !entity.empty()) bus.setLife(entity, d.life, d.lifeChannel);
}

gc::Result setLife(gc::Hub& hub, HaBus& bus, const std::string& role, const std::string& source, const std::string& channel) {
  std::lock_guard<std::recursive_mutex> l(hub.mutex());
  const gc::Binding* b = hub.config().binding(role);
  const std::string entity = b ? entityOf(b->device) : "";
  if (measureOfRole(hub.catalog(), role).empty() || entity.empty()) return gc::Result::fail(422, gc::say("ha.life_role"));
  if (!source.empty()) {
    // Only what Home Assistant lists as a sign of life now, and a channel the text has
    // What comes back in a message is only what has the form of an ID, never the raw input
    const auto c = validEntityId(source) && source != entity ? bus.lifeCandidate(source) : std::nullopt;
    if (!c) return gc::Result::fail(422, gc::say("ha.life_unknown", {{"entity", validEntityId(source) ? source : "?"}}));
    const bool hasChannel = std::any_of(c->channels.begin(), c->channels.end(), [&](const auto& ch) { return ch.first == channel; });
    if (c->timestamp ? !channel.empty() : !hasChannel)
      return gc::Result::fail(422, gc::say("ha.life_channel", {{"entity", source}, {"channel", validLifeChannel(channel) ? channel : "?"}}));
  }
  if (auto r = hub.setDeviceLife(b->device, source, channel); r.status != 200) return r;
  bus.setLife(entity, source, channel);
  return gc::Result::ok();
}

gc::Result candidatesRoute(gc::Api& api, const HaBus& bus, const gc::ApiRequest& req) {
  if (!api.authorized(req)) return gc::Result::fail(401, gc::say("auth.required"));
  auto asked = [&](const char* key) {
    auto it = req.query.find(key);
    return it != req.query.end() && it->second == "1";
  };
  return gc::Result::ok(bus.candidatesJson(asked("others"), asked("lives")));
}

gc::Result assignRoute(gc::Api& api, gc::Hub& hub, HaBus& bus, const gc::ApiRequest& req,
                       const std::function<void()>& letHubSee) {
  if (!api.authorized(req)) return gc::Result::fail(401, gc::say("auth.required"));
  const auto body = gc::json::parse(req.body, nullptr, false);
  // "entity" must be given ("" takes the sensor away), so a request that forgot it removes nothing.
  if (!body.is_object() || !body.contains("entity") || !body["entity"].is_string())
    return gc::Result::fail(400, gc::say("api.bad_input"));
  return assign(hub, bus, gc::jstr(body, "role"), gc::jstr(body, "entity"), letHubSee);
}

gc::Result lifeRoute(gc::Api& api, gc::Hub& hub, HaBus& bus, const gc::ApiRequest& req) {
  if (!api.authorized(req)) return gc::Result::fail(401, gc::say("auth.required"));
  const auto body = gc::json::parse(req.body, nullptr, false);
  // "source" must be given ("" removes it), so a request that forgot it removes nothing
  if (!body.is_object() || !body.contains("source") || !body["source"].is_string() ||
      (body.contains("channel") && !body["channel"].is_string()))
    return gc::Result::fail(400, gc::say("api.bad_input"));
  return setLife(hub, bus, gc::jstr(body, "role"), gc::jstr(body, "source"), gc::jstr(body, "channel"));
}

}  // namespace ha
