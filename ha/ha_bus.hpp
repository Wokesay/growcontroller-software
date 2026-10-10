// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: Home Assistant as the device layer. Each selected entity
// (e.g. sensor.grow_ph) appears to the hub as a device that provides one
// measure. The user selects from the candidates: sensors whose Home
// Assistant description (device class, unit) says they measure something
// the hub uses; or the mapping file lists the entities. Home Assistant
// calibrates its sensors itself; the device classes carry
// externalCalibration, so pH, EC and level are shown but are no values for
// control until the hub can check them. Nothing is switched: every run or
// switch command is refused.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
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

// The mapping file: {"url": "...", "entities": [{"entity": "...", "measures": "ph"}]};
// "entities" may be left out, the user then selects in the web app.
// Home Assistant's address, checked once (#76): the token goes only to the
// host a person reads in it. http(s)://host[:port][/path], the host a name
// or IPv4 address (letters, digits, '-', '.') or an IPv6 address in [ ];
// the path letters, digits, '-' and '_' in front of Home Assistant's /api.
struct Address {
  bool tls = false;
  std::string host;  // an IPv6 address without its brackets
  int port = 0;
  std::string base;  // "" or a path such as "/core"
  std::string url() const;  // the address again, normalised
};
std::optional<Address> parseAddress(std::string url, std::string& err);

struct Mapping {
  std::string url;
  std::vector<Entity> entities;
};
Mapping parseMapping(const gc::json& j, std::string& err);
// A list of entities as the mapping file writes it.
std::vector<Entity> parseEntities(const gc::json& j, std::string& err);
// domain.object_id in lower case, at most 255 characters, as Home Assistant
// writes entity IDs; they become device IDs.
bool validEntityId(const std::string& id);
bool knownMeasure(const std::string& measures);
// Home Assistant's answer to GET /api/states, keeping only sensors and of
// them only what the hub reads (entity_id, state, the report times and four
// attributes); everything else is never built. Discarded if the answer is
// no JSON or nests deeper than 32 levels.
gc::json parseStates(const std::string& body);
gc::json entitiesJson(const std::vector<Entity>& entities);

// A sensor in Home Assistant the hub could use. kind is what its device
// class and unit say: "ph", "ec", "temperature", "humidity", "co2" or
// "level"; a temperature can serve as water or air temperature.
struct Candidate {
  std::string entityId, name, kind;
  double value = gc::kNaN;  // in the hub's unit; NaN if there is none now
  double raw = gc::kNaN;    // as Home Assistant reports it
  std::string unit;         // Home Assistant's unit
  std::string problem;      // "", "unit_missing" or "unit_unsupported"
};
// What a state describes by Home Assistant's own words; "" if nothing the hub uses.
std::string classify(const gc::json& state);
// The measures a kind can serve ("temperature" → water_temp, air_temp).
std::vector<std::string> measuresOf(const std::string& kind);

// The built-in catalog plus one device class per measure (ha_ph, ha_ec, …).
gc::Catalog catalog();

// "2026-10-09T18:36:12.123456+00:00" → milliseconds since 1970; none if unreadable.
std::optional<std::int64_t> parseTimestampMs(const std::string& s);
// An HTTP date ("Thu, 09 Oct 2026 12:00:00 GMT") → milliseconds since 1970.
std::optional<std::int64_t> parseHttpDateMs(const std::string& s);

class HaBus : public gc::IBus {
 public:
  explicit HaBus(std::vector<Entity> entities = {});
  static constexpr size_t kMaxPerKind = 100;

  static std::string deviceId(const std::string& entityId) { return "ha." + entityId; }

  // The state of one selected entity from Home Assistant.
  // haNowMs is Home Assistant's own time of the answer (its Date header), so
  // the age of a report does not depend on two clocks agreeing; nowMs is the
  // hub's clock.
  void update(const std::string& entityId, const gc::json& state, std::int64_t haNowMs, gc::Ms nowMs);
  // A fault of an entity (e.g. an unsupported unit), "" if none.
  std::string fault(const std::string& entityId) const;
  // Home Assistant or the entity could not be read: the device goes offline.
  void lost(const std::string& entityId);
  void lostAll();
  // All states at once (GET /api/states): updates the selected entities and
  // refreshes the candidates. A selected entity missing from the list is lost.
  void updateAll(const gc::json& states, std::int64_t haNowMs, gc::Ms nowMs);
  // Use a candidate for a measure. Refused (why: ha.unknown, ha.mismatch,
  // ha.used) if it is no candidate, cannot serve that measure, or is already
  // used for another one.
  bool select(const std::string& entityId, const std::string& measures, gc::Msg& why);
  // Use an entity without checking that it is a candidate now: the picks the
  // hub's configuration holds, at start.
  void adopt(const Entity& entity);
  // The measure an entity is used for, "" if none.
  std::string selectedMeasure(const std::string& entityId) const;
  // Stop using an entity; it stays a candidate.
  void deselect(const std::string& entityId);
  // How the last round went, for the web app: "starting", "ok", "unreachable" or "refused".
  void setConnection(const std::string& c);

  std::vector<Entity> entities() const;  // a copy: the web app may select while the poller reads
  std::vector<Candidate> candidates() const;
  // For the web app: {"connection": …, "truncated": [kinds], "candidates": [{entity,
  // name, kind, measures, value, raw, unit, problem, used}]}, "used" naming
  // the measure a selected one serves.
  gc::json candidatesJson() const;

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
    std::string name;              // Home Assistant's friendly name
  };
  void updateLocked(const Entity& e, const gc::json& state, std::int64_t haNowMs, gc::Ms nowMs);
  std::vector<Entity> entities_;
  std::vector<Candidate> candidates_;
  std::map<std::string, gc::json> raw_;  // the candidates' last states, to start a pick with
  std::set<std::string> truncated_;  // kinds with more candidates than kMaxPerKind
  std::int64_t lastHaNowMs_ = 0;
  gc::Ms lastNowMs_ = 0;
  std::string connection_ = "starting";
  std::map<std::string, State> states_;  // by entity ID
  mutable std::mutex m_;                 // the poller thread updates, the web app selects
};

}  // namespace ha
