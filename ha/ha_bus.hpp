// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only spike: Home Assistant as the device layer. Each selected entity
// (e.g. sensor.grow_ph) appears to the hub as a device that provides one
// measure. The user selects from the candidates: sensors whose Home
// Assistant description (device class, unit) says they measure something
// the hub uses; or the mapping file lists the entities. Home Assistant
// calibrates its sensors itself; the device classes carry
// externalCalibration, so pH, EC and level are shown but are no values for
// control until the hub can check them. A raw signal (SD-038, SD-041), a pH
// probe's voltage, is calibrated in the app instead, with the hub's own
// checks; its device class has no externalCalibration. Other sensors (SD-036): any other
// sensor whose unit fits a measure can be picked too; it is sent to the web
// app only when asked for. Nothing is switched: every run or switch command
// is refused.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gc/bus.hpp"
#include "gc/catalog.hpp"

namespace ha {

// One Home Assistant entity and what it measures ("ph", "ec", "water_temp",
// "level", "air_temp", "humidity", "co2"). signal: it reports a raw signal
// the app calibrates (SD-041), so far a pH probe's voltage in mV or V.
// life: its sign of life (SD-042), the entity that says when it last
// measured: a sign-of-life text with lifeChannel, or a timestamp sensor
// (lifeChannel ""); "" without one.
struct Entity {
  std::string entityId, measures;
  bool signal = false;
  std::string life, lifeChannel;
  Entity() = default;
  Entity(std::string id, std::string m, bool sig = false, std::string l = "", std::string lc = "")
      : entityId(std::move(id)), measures(std::move(m)), signal(sig), life(std::move(l)), lifeChannel(std::move(lc)) {}
};

// A sign-of-life text (SD-042): n=<counter>|<code>=<s>|…, the age of each
// channel's last measurement in whole seconds, "-" for a channel without one
// since the device started. The channels in their order, none for "-";
// nothing if the text is not one (no counter first, a bad code or age, a
// code twice, more than 64 channels).
using LifeChannels = std::vector<std::pair<std::string, std::optional<int>>>;
std::optional<LifeChannels> parseLifeText(const std::string& text, std::string* counter = nullptr);
// A channel code: 1–16 of a–z, 0–9 and _.
bool validLifeChannel(const std::string& code);

// What can serve as a sign of life: a sign-of-life text (with its channels
// and their ages now) or a timestamp sensor (Home Assistant's device class
// timestamp; ageS how old the time it states is).
struct LifeCandidate {
  std::string entityId, name;
  bool timestamp = false;
  LifeChannels channels;
  std::optional<std::int64_t> ageS;
};

// The ideal slope of a pH electrode at 25 °C (Nernst, RT·ln10/F). A probe's
// voltage E in mV becomes the pH an ideal probe would read, 7 − E/59.16, so the
// hub's own pH check applies unchanged: its slope band 0.8–1.25 is 47.3–74.0
// mV/pH (RAT-025), and an uncalibrated probe already reads about right.
constexpr double kNernstMvPerPh = gc::kNernstMvPerPh;
// The device class of a picked entity: ha_<measure>, ha_ph_raw for a pH signal.
std::string deviceClassOf(const Entity& e);

// Home Assistant's address, checked once (#76): the token goes only to the
// host a person reads in it. http(s)://host[:port][/path], the host a name
// or IPv4 address (letters, digits, '-', '_', '.') or an IPv6 address in [ ];
// the path letters, digits, '-' and '_' in front of Home Assistant's /api.
struct Address {
  bool tls = false;
  std::string host;  // an IPv6 address without its brackets
  int port = 0;
  std::string base;  // "" or a path such as "/core"
  std::string url() const;  // the address again, normalised
};
std::optional<Address> parseAddress(std::string url, std::string& err);

// The mapping file: {"url": "...", "entities": [{"entity": "...", "measures": "ph", "signal": true}]},
// "signal" only for a raw pH signal (a sign of life is set in the web app, SD-042);
// "entities" may be left out, the user then selects in the web app.
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
  std::string problem;      // "", "unit_missing" or "unit_unsupported"; an other also "device_class" or "meter"
  // An other sensor (SD-036): any numeric sensor that is no suggestion. It
  // has no kind; it can serve the measures its unit fits exactly, and none
  // if Home Assistant says it measures something else (its device class).
  std::vector<std::string> measures;
  std::string deviceClass;
  // A raw signal the app calibrates (SD-041): a pH probe's voltage. It has no
  // value of its own before that; raw and unit are what Home Assistant reports.
  bool signal = false;
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
  static constexpr size_t kMaxOthers = 1000;

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
  // Use a candidate or an other sensor for a measure. Refused (why:
  // ha.unknown, ha.mismatch, ha.used) if Home Assistant did not list it, it
  // cannot serve that measure, or it is already used for another one.
  bool select(const std::string& entityId, const std::string& measures, gc::Msg& why);
  // Use an entity without checking that it is a candidate now: the picks the
  // hub's configuration holds, at start.
  void adopt(const Entity& entity);
  // The measure an entity is used for, "" if none.
  std::string selectedMeasure(const std::string& entityId) const;
  // The picked entity, if it is picked.
  std::optional<Entity> selected(const std::string& entityId) const;
  // Whether the list offers it as a raw signal; none if it is not listed.
  std::optional<bool> offeredAsSignal(const std::string& entityId) const;
  // Stop using an entity; it stays a candidate.
  void deselect(const std::string& entityId);
  // An entity's sign of life (SD-042): life "" removes it. Read from the
  // last round at once if that listed it.
  void setLife(const std::string& entityId, const std::string& life, const std::string& channel);
  // A sign of life Home Assistant listed in the last round.
  std::optional<LifeCandidate> lifeCandidate(const std::string& entityId) const;
  static constexpr size_t kMaxLives = 100;  // per kind: texts and timestamp sensors
  // How the last round went, for the web app: "starting", "ok", "unreachable" or "refused".
  void setConnection(const std::string& c);

  std::vector<Entity> entities() const;  // a copy: the web app may select while the poller reads
  std::vector<Candidate> candidates() const;  // the suggestions
  std::vector<Candidate> others() const;      // the other sensors (SD-036)
  // For the web app: {"connection": …, "truncated": [kinds, "other"], "candidates": [{entity,
  // name, kind, measures, value, raw, unit, problem, used, signal (only if true)}], "otherFits": {measure: n},
  // "otherCount": n}, "used" naming the measure a selected one serves,
  // otherFits how many other sensors can serve each measure and otherCount
  // how many are listed (also those listed only with a reason). withOthers adds "others":
  // [{entity, name, measures, value, raw, unit, problem, deviceClass, used, signal}],
  // sorted by name; only asked for while someone looks. "lifeCount" says how many signs of life
  // (SD-042) Home Assistant offers and "lifeNames" names those in use; a picked sensor carries
  // "life": {entity, channel}. withLives adds "lives": [{entity, name, kind ("text" or
  // "timestamp"), channels: [{code, ageS}] for a text, ageS for a timestamp sensor}], only
  // asked for while someone picks one.
  gc::json candidatesJson(bool withOthers = false, bool withLives = false) const;

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
  // A sign of life as Home Assistant last reported it (SD-042)
  struct LifeState {
    bool seen = false, available = false, readable = false, timestamp = false, future = false;
    std::int64_t reportedAt = -1;  // Home Assistant's time of its last report
    gc::Ms ts = 0;                 // that report on the hub's clock, fixed once seen
    std::string counter;           // a text: its last counter; the same counter again is no new send
    gc::Ms counterTs = 0;          // when that counter was first seen; both kept through rounds without it
    std::int64_t stampMs = -1;     // a timestamp sensor: the time it states, Home Assistant's clock
    gc::Ms measuredAt = 0;         // that time on the hub's clock, fixed once seen
    std::map<std::string, std::optional<int>> ages;  // a text: channel → age in s, none for "-"
    std::map<std::string, gc::Ms> waitingSince;      // channel → since when it shows "-" (hub clock)
  };
  void updateLocked(const Entity& e, const gc::json& state, std::int64_t haNowMs, gc::Ms nowMs);
  void updateLifeLocked(const std::string& id, const gc::json* state, std::int64_t haNowMs, gc::Ms nowMs);
  void fillLifeLocked(const Entity& e, gc::Sample& s) const;
  // A sign of life that dates the sensor's last measurement before a report it was sent after
  // belongs to another sensor (a wrong pick): the hub's own rule, SD-042
  static bool mismatchLocked(const LifeState& ls, gc::Ms measuredAt, gc::Ms reportTs);
  std::vector<Entity> entities_;
  std::vector<Candidate> candidates_;
  std::vector<Candidate> others_;
  std::map<std::string, gc::json> raw_;  // the last states of what can be picked, to start a pick with
  std::vector<LifeCandidate> lifeCandidates_;
  std::map<std::string, gc::json> lifeRaw_;  // their last states, to start a sign of life with
  std::map<std::string, LifeState> lives_;   // by entity ID, the signs of life in use
  std::set<std::string> truncated_;  // kinds with more candidates than kMaxPerKind, "other" past kMaxOthers
  std::int64_t lastHaNowMs_ = 0;
  gc::Ms lastNowMs_ = 0;
  std::string connection_ = "starting";
  std::map<std::string, State> states_;  // by entity ID
  mutable std::mutex m_;                 // the poller thread updates, the web app selects
};

}  // namespace ha
