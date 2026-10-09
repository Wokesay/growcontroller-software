// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: Home Assistant as the device layer. Each mapped entity
// (e.g. sensor.grow_ph) appears to the hub as a device that provides one
// measure. Home Assistant calibrates its sensors itself; the device classes
// carry externalCalibration, so pH, EC and level are shown but are no values
// for control until the hub can check them. Nothing is switched: every run
// or switch command is refused.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "gc/bus.hpp"
#include "gc/catalog.hpp"

namespace ha {

// One Home Assistant entity and what it measures ("ph", "ec", "water_temp",
// "level", "air_temp", "humidity", "co2").
struct Entity {
  std::string entityId, measures;
};

// The mapping file: {"url": "...", "entities": [{"entity": "...", "measures": "ph"}]}.
struct Mapping {
  std::string url;
  std::vector<Entity> entities;
};
Mapping parseMapping(const gc::json& j, std::string& err);

// The built-in catalog plus one device class per measure (ha_ph, ha_ec, …).
gc::Catalog catalog();

// "2026-10-09T18:36:12.123456+00:00" → milliseconds since 1970; none if unreadable.
std::optional<std::int64_t> parseTimestampMs(const std::string& s);
// An HTTP date ("Thu, 09 Oct 2026 12:00:00 GMT") → milliseconds since 1970.
std::optional<std::int64_t> parseHttpDateMs(const std::string& s);

class HaBus : public gc::IBus {
 public:
  explicit HaBus(std::vector<Entity> entities);

  static std::string deviceId(const std::string& entityId) { return "ha." + entityId; }

  // The state of an entity from Home Assistant (GET /api/states/<entity>).
  // haNowMs is Home Assistant's own time of the answer (its Date header), so
  // the age of a report does not depend on two clocks agreeing; nowMs is the
  // hub's clock.
  void update(const std::string& entityId, const gc::json& state, std::int64_t haNowMs, gc::Ms nowMs);
  // A fault of an entity (e.g. an unsupported unit), "" if none.
  std::string fault(const std::string& entityId) const;
  // Home Assistant or the entity could not be read: the device goes offline.
  void lost(const std::string& entityId);

  const std::vector<Entity>& entities() const { return entities_; }

  void poll(gc::Ms) override {}
  std::vector<gc::PortReport> ports() const override { return {}; }
  std::vector<gc::DeviceReport> devices() const override;
  std::optional<gc::Sample> sample(const std::string& dev, const std::string& cap) const override;
  bool startRun(const std::string& pump, gc::Ms ms, const std::string& jobId, std::string& err) override;
  gc::RunStatus runStatus(const std::string&) const override { return {}; }
  void stopAllPumps() override {}
  bool setSwitch(const std::string& dev, int channel, bool on, std::string& err) override;
  std::optional<bool> switchState(const std::string&, int) const override { return std::nullopt; }
  bool writePumpCalibration(const std::string& pump, double mlPerMin, std::string& err) override;

 private:
  struct State {
    bool seen = false;       // Home Assistant answered for this entity
    bool available = false;  // not "unavailable"
    double value = gc::kNaN;
    std::int64_t reportedAt = -1;  // Home Assistant's time of the last report
    gc::Ms ts = 0;                 // that report on the hub's clock, fixed once seen
    std::string fault;             // e.g. a unit the hub cannot convert
  };
  std::vector<Entity> entities_;
  std::map<std::string, State> states_;  // by entity ID
  mutable std::mutex m_;                 // update() comes from the poller thread
};

}  // namespace ha
