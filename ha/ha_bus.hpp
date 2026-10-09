// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: Home Assistant as the device layer. Each mapped entity
// (e.g. sensor.grow_ph) appears to the hub as a device that provides one
// measure. Home Assistant calibrates its sensors itself, so the device
// classes carry externalCalibration. Nothing is switched: every run or
// switch command is refused.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
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

// "2026-10-09T18:36:12.123456+00:00" → milliseconds since 1970; -1 if unreadable.
std::int64_t parseTimestampMs(const std::string& s);

class HaBus : public gc::IBus {
 public:
  explicit HaBus(std::vector<Entity> entities);

  static std::string deviceId(const std::string& entityId) { return "ha." + entityId; }

  // A state object from Home Assistant (GET /api/states/<entity>), seen at
  // nowMs (hub clock) and nowEpochMs (wall clock).
  void update(const gc::json& state, std::int64_t nowEpochMs, gc::Ms nowMs);
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
    gc::Ms ts = 0;
    std::string fault;       // e.g. a unit the hub cannot convert
  };
  std::vector<Entity> entities_;
  std::map<std::string, State> states_;  // by entity ID
  mutable std::mutex m_;                 // update() comes from the poller thread
};

}  // namespace ha
