// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ha_bus.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <type_traits>
#include <unordered_map>

#include "gc/embedded.hpp"
#include "gc/messages.hpp"

namespace ha {

namespace {

const char* const kMeasures[] = {"ph", "ec", "water_temp", "level", "air_temp", "humidity", "co2"};

std::string classOf(const std::string& measures) { return "ha_" + measures; }

// Labels as the catalog writes them (the catalog is still German, #18).
const char* labelOf(const std::string& m) {
  if (m == "ph") return "pH aus Home Assistant";
  if (m == "ec") return "EC aus Home Assistant";
  if (m == "water_temp") return "Wassertemperatur aus Home Assistant";
  if (m == "level") return "Füllstand aus Home Assistant";
  if (m == "air_temp") return "Lufttemperatur aus Home Assistant";
  if (m == "humidity") return "Luftfeuchte aus Home Assistant";
  return "CO2 aus Home Assistant";
}

// Text from Home Assistant as it may appear in a fault or a name: printable
// and short, because it reaches the terminal, the state and diagnostics.
// Keeps printable ASCII and UTF-8 sequences from U+00A0 on (µ, °); drops
// control characters, C1 controls (U+0080–U+009F), invisible format
// characters that can reorder or hide text (U+00AD, U+061C, U+180E,
// U+200B–U+200F, U+2028–U+202E, U+2060–U+2069, U+FEFF, U+FFF9–U+FFFB, tag
// characters) and stray bytes. The input comes from the JSON
// parser, which has already rejected ill-formed UTF-8, so the sequences are
// not checked further.
std::string printable(const std::string& s, size_t maxBytes = 32) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      if (c >= 0x20 && c != 0x7f) out += static_cast<char>(c);
      ++i;
      continue;
    }
    const size_t n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    bool ok = n > 0 && i + n <= s.size();
    for (size_t k = 1; ok && k < n; ++k) ok = (static_cast<unsigned char>(s[i + k]) & 0xc0) == 0x80;
    if (ok && n == 2 && c == 0xc2 && static_cast<unsigned char>(s[i + 1]) < 0xa0) ok = false;  // C1 control
    if (ok && n == 3) {
      const auto b1 = static_cast<unsigned char>(s[i + 1]), b2 = static_cast<unsigned char>(s[i + 2]);
      const bool format = (c == 0xe2 && b1 == 0x80 && ((b2 >= 0x8b && b2 <= 0x8f) || (b2 >= 0xa8 && b2 <= 0xae))) ||
                          (c == 0xe2 && b1 == 0x81 && b2 >= 0xa0 && b2 <= 0xa9) || (c == 0xef && b1 == 0xbb && b2 == 0xbf);
      const bool hidden = (c == 0xe1 && b1 == 0xa0 && b2 == 0x8e) || (c == 0xef && b1 == 0xbf && b2 >= 0xb9 && b2 <= 0xbb);  // U+180E, U+FFF9–FFFB
      if (format || hidden) ok = false;
    }
    if (ok && n == 2 && ((c == 0xc2 && static_cast<unsigned char>(s[i + 1]) == 0xad) || (c == 0xd8 && static_cast<unsigned char>(s[i + 1]) == 0x9c)))
      ok = false;  // soft hyphen U+00AD, Arabic letter mark U+061C
    if (ok && n == 4 && c == 0xf3 && static_cast<unsigned char>(s[i + 1]) == 0xa0 && (static_cast<unsigned char>(s[i + 2]) & 0xfe) == 0x80)
      ok = false;  // tag characters U+E0000–E007F, which can hide text
    if (ok) out.append(s, i, n);
    i += ok ? n : 1;
  }
  return gc::utf8Prefix(out, maxBytes);
}

const gc::json& attributesOf(const gc::json& state) {
  static const gc::json kNone = gc::json::object();
  return state.contains("attributes") && state["attributes"].is_object() ? state["attributes"] : kNone;
}

// The numeric value of a state; NaN for "unknown", "unavailable", empty or
// anything else that is not a plain number, never 0 (R5).
double numericState(const gc::json& state) {
  const std::string raw = gc::jstr(state, "state");
  char* end = nullptr;
  const double v = raw.empty() ? gc::kNaN : std::strtod(raw.c_str(), &end);
  return end && *end == '\0' && std::isfinite(v) ? v : gc::kNaN;
}

// The value in the hub's unit, or NaN with a fault when the unit is missing
// or not one the hub can convert. Only pH has no unit; for anything else a
// missing unit is a missing input, never a guess (RAT-006): 5 µS/cm read as
// 5 mS/cm, or a level in % read as litres, would look plausible.
double convert(const std::string& measures, double v, const std::string& unit, std::string& fault) {
  fault.clear();
  if (measures == "ph" && (unit.empty() || unit == "pH")) return v;
  if (unit.empty()) {
    fault = "unit missing";
    return gc::kNaN;
  }
  if (measures == "ec") {
    if (unit == "mS/cm") return v;
    if (unit == "µS/cm" || unit == "μS/cm" || unit == "uS/cm") return v / 1000.0;
  }
  if (measures == "water_temp" || measures == "air_temp") {
    if (unit == "°C") return v;
    if (unit == "°F") return (v - 32.0) * 5.0 / 9.0;
  }
  if (measures == "level" && unit == "L") return v;
  if (measures == "humidity" && unit == "%") return v;
  if (measures == "co2" && unit == "ppm") return v;
  fault = "unit " + printable(unit) + " not supported";
  return gc::kNaN;
}

// Days since 1970-01-01 for a civil date (H. Hinnant's algorithm).
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
  const auto yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

}  // namespace

bool knownMeasure(const std::string& m) {
  return std::any_of(std::begin(kMeasures), std::end(kMeasures), [&](const char* k) { return m == k; });
}

bool validEntityId(const std::string& id) {
  const size_t dot = id.find('.');
  return id.size() <= 255 && dot != std::string::npos && dot > 0 && dot + 1 < id.size() && id.find('.', dot + 1) == std::string::npos &&
         std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; });
}

namespace {

// Reads GET /api/states event by event and builds only what the hub uses:
// for each sensor its entity_id, state, report times and four attributes.
// Nothing else is built, so the work and memory stay in proportion to the
// sensors, however large or crafted the answer; nesting deeper than
// kMaxDepth stops the read.
class StatesReader : public gc::json::json_sax_t {
 public:
  static constexpr int kMaxDepth = 32;
  static constexpr size_t kMaxStates = 20000;  // sensors; a large installation has a few thousand
  static constexpr size_t kMaxText = 256;       // bytes of a kept text: a longer name is cut, any other field dropped
  gc::json out = gc::json::array();

  bool null() override { return scalar(nullptr); }
  bool boolean(bool v) override { return scalar(v); }
  bool number_integer(number_integer_t v) override { return scalar(v); }
  bool number_unsigned(number_unsigned_t v) override { return scalar(v); }
  bool number_float(number_float_t v, const string_t&) override { return scalar(v); }
  bool string(string_t& v) override { return scalar(v); }
  bool binary(binary_t&) override { return scalar(nullptr); }
  bool start_object(std::size_t) override {
    if (++depth_ > kMaxDepth) return false;
    if (skipping()) return true;
    if (depth_ == 2 && inList_) {  // a state
      cur_ = gc::json::object();
      return true;
    }
    if (depth_ == 3 && key_ == "attributes") {
      cur_["attributes"] = gc::json::object();
      inAttrs_ = true;
      return true;
    }
    skipFrom_ = depth_;  // anything else: read past it
    return true;
  }
  bool end_object() override {
    if (!skipping()) {
      if (depth_ == 3) inAttrs_ = false;
      const std::string id = gc::jstr(cur_, "entity_id");
      if (depth_ == 2 && id.rfind("sensor.", 0) == 0 && validEntityId(id) && out.size() < kMaxStates) out.push_back(std::move(cur_));
    }
    return leave();
  }
  bool start_array(std::size_t) override {
    if (++depth_ > kMaxDepth) return false;
    if (skipping()) return true;
    if (depth_ == 1) {
      inList_ = true;
      return true;
    }
    skipFrom_ = depth_;
    return true;
  }
  bool end_array() override { return leave(); }
  bool key(string_t& k) override {
    if (!skipping()) key_ = k.size() > 32 ? string_t() : k;  // only short keys are ever kept
    return true;
  }
  bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
  bool sawList() const { return inList_; }  // the answer was a list, not an object or a bare value

 private:
  bool skipping() const { return skipFrom_ > 0 && depth_ >= skipFrom_; }
  bool leave() {
    if (skipFrom_ > 0 && depth_ == skipFrom_) skipFrom_ = 0;
    --depth_;
    return true;
  }
  template <typename T>
  bool scalar(T&& v) {
    if (skipping()) return true;
    static const std::set<std::string> kState = {"entity_id", "state", "last_reported", "last_updated"};
    static const std::set<std::string> kAttr = {"device_class", "unit_of_measurement", "state_class", "friendly_name"};
    if constexpr (std::is_same_v<std::decay_t<T>, string_t>) {
      // A kept text is short in any real answer. A name is cut, so a huge one costs nothing later;
      // any other field is dropped, because a cut could make an ID valid or change a value or a time.
      if (v.size() > kMaxText) {
        if (key_ != "friendly_name") return true;
        v = gc::utf8Prefix(v, kMaxText);
      }
    }
    if (depth_ == 2 && kState.count(key_)) cur_[key_] = std::forward<T>(v);
    if (depth_ == 3 && inAttrs_ && kAttr.count(key_)) cur_["attributes"][key_] = std::forward<T>(v);
    return true;
  }
  int depth_ = 0, skipFrom_ = 0;
  bool inList_ = false, inAttrs_ = false;
  std::string key_;
  gc::json cur_;
};

}  // namespace

gc::json parseStates(const std::string& body) {
  StatesReader r;
  if (!gc::json::sax_parse(body, &r) || !r.sawList()) return gc::json(gc::json::value_t::discarded);
  return std::move(r.out);
}

std::optional<std::int64_t> parseTimestampMs(const std::string& s) {
  // YYYY-MM-DDTHH:MM:SS[.fraction][Z|±HH:MM]
  if (s.size() < 19 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != ' ') || s[13] != ':' || s[16] != ':')
    return std::nullopt;
  auto num = [&](size_t pos, size_t len) -> int {
    int v = 0;
    for (size_t i = pos; i < pos + len; ++i) {
      if (s[i] < '0' || s[i] > '9') return -1;
      v = v * 10 + (s[i] - '0');
    }
    return v;
  };
  const int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), se = num(17, 2);
  static const int kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > kDays[mo - 1] || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 60)
    return std::nullopt;
  size_t i = 19;
  int ms = 0;
  if (i < s.size() && s[i] == '.') {
    int digits = 0;
    for (++i; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++digits)
      if (digits < 3) ms = ms * 10 + (s[i] - '0');
    if (digits == 0) return std::nullopt;
    for (; digits < 3; ++digits) ms *= 10;
  }
  int offsetMin = 0;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    if (i + 6 > s.size() || s[i + 3] != ':') return std::nullopt;
    const int oh = num(i + 1, 2), om = num(i + 4, 2);
    if (oh < 0 || om < 0) return std::nullopt;
    offsetMin = (s[i] == '-' ? -1 : 1) * (oh * 60 + om);
    i += 6;
  } else if (i < s.size() && s[i] == 'Z') {
    ++i;
  }
  if (i != s.size()) return std::nullopt;
  const std::int64_t days = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
  const std::int64_t secs = days * 86400 + h * 3600 + mi * 60 + se - offsetMin * 60;
  return secs * 1000 + ms;
}

std::optional<std::int64_t> parseHttpDateMs(const std::string& s) {
  // "Thu, 09 Oct 2026 12:00:00 GMT" (RFC 9110 IMF-fixdate)
  static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (s.size() != 29 || s.compare(3, 2, ", ") != 0 || s.compare(25, 4, " GMT") != 0) return std::nullopt;
  int mo = 0;
  while (mo < 12 && s.compare(8, 3, kMonths[mo]) != 0) ++mo;
  if (mo == 12) return std::nullopt;
  const std::string iso = s.substr(12, 4) + "-" + (mo < 9 ? "0" : "") + std::to_string(mo + 1) + "-" + s.substr(5, 2) + "T" +
                          s.substr(17, 8) + "Z";
  return parseTimestampMs(iso);
}

std::string classify(const gc::json& state) {
  if (!state.is_object() || gc::jstr(state, "entity_id").rfind("sensor.", 0) != 0) return "";
  const gc::json& attrs = attributesOf(state);
  const std::string dc = gc::jstr(attrs, "device_class"), unit = gc::jstr(attrs, "unit_of_measurement");
  if (dc == "ph" || unit == "pH") return "ph";
  if (dc == "conductivity" || unit == "mS/cm" || unit == "µS/cm" || unit == "μS/cm" || unit == "uS/cm") return "ec";
  if (dc == "temperature" || unit == "°C" || unit == "°F") return "temperature";
  // %, ppm and L also stand for battery, VOC or water use: only with the device class.
  if (dc == "humidity" && unit == "%") return "humidity";
  if (dc == "carbon_dioxide") return "co2";
  // A volume that only adds up (a water meter) is no level.
  const std::string sc = gc::jstr(attrs, "state_class");
  if ((dc == "volume_storage" || dc == "volume") && unit == "L" && sc != "total" && sc != "total_increasing") return "level";
  return "";
}

std::vector<std::string> measuresOf(const std::string& kind) {
  // a kind is itself a measure, except that a temperature can be water or air
  if (kind == "temperature") return {"water_temp", "air_temp"};
  if (knownMeasure(kind)) return {kind};
  return {};
}

std::vector<Entity> parseEntities(const gc::json& j, std::string& err) {
  std::vector<Entity> out;
  err.clear();
  if (!j.is_array()) {
    err = "\"entities\" must be a list";
    return {};
  }
  for (const auto& e : j) {
    Entity x{gc::jstr(e, "entity"), gc::jstr(e, "measures")};
    // It goes into device IDs and the hub's configuration; only sensors are read.
    if (!validEntityId(x.entityId) || x.entityId.rfind("sensor.", 0) != 0 || !knownMeasure(x.measures)) {
      err = "each entity needs \"entity\" (e.g. sensor.grow_ph) and \"measures\" (ph, ec, water_temp, level, air_temp, humidity, co2)";
      return {};
    }
    if (std::any_of(out.begin(), out.end(), [&](const Entity& o) { return o.entityId == x.entityId; })) {
      err = x.entityId + " is listed twice";
      return {};
    }
    out.push_back(std::move(x));
  }
  return out;
}

gc::json entitiesJson(const std::vector<Entity>& entities) {
  gc::json out = gc::json::array();
  for (const auto& e : entities) out.push_back({{"entity", e.entityId}, {"measures", e.measures}});
  return out;
}

Mapping parseMapping(const gc::json& j, std::string& err) {
  Mapping m;
  err.clear();
  if (!j.is_object()) {
    err = "the mapping must be a JSON object";
    return m;
  }
  // http(s)://host[:port], optionally with a plain path in front of the API
  // (an add-on reaches Home Assistant at http://supervisor/core/api/…).
  m.url = gc::jstr(j, "url");
  while (!m.url.empty() && m.url.back() == '/') m.url.pop_back();
  const size_t scheme = m.url.rfind("http://", 0) == 0 ? 7 : m.url.rfind("https://", 0) == 0 ? 8 : 0;
  const size_t slash = scheme == 0 ? std::string::npos : m.url.find('/', scheme);
  const std::string path = slash == std::string::npos ? "" : m.url.substr(slash);
  bool plain = true;  // letters, digits, - _ and single slashes; no dot segments, no escapes, no query
  for (size_t i = 0; i < path.size(); ++i) {
    const char c = path[i];
    plain = plain && (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || (c == '/' && (i + 1 >= path.size() || path[i + 1] != '/')));
  }
  if (scheme == 0 || m.url.size() == scheme || slash == scheme || !plain) {
    err = "\"url\" must be http://host:port or https://host:port, with at most a plain path";
    return m;
  }
  if (j.contains("entities")) m.entities = parseEntities(j["entities"], err);
  return m;
}

gc::Catalog catalog() {
  auto j = gc::json::parse(gc::embedded::kCatalogJson);
  for (const char* m : kMeasures)
    j["deviceClasses"][classOf(m)] = {{"label", labelOf(m)},
                                      {"stage", 0},
                                      {"attach", "ha"},
                                      {"provides", {std::string("measure.") + m}},
                                      {"text", "Wert aus Home Assistant; kalibriert wird dort."}};
  gc::Catalog c = gc::Catalog::fromJson(j);
  // Set in code, not from the catalog data, so the catalog cannot loosen it (R7).
  for (const char* m : kMeasures) c.deviceClasses[classOf(m)].externalCalibration = true;
  return c;
}

HaBus::HaBus(std::vector<Entity> entities) : entities_(std::move(entities)) {}

std::vector<Entity> HaBus::entities() const {
  std::lock_guard<std::mutex> l(m_);
  return entities_;
}

std::vector<Candidate> HaBus::candidates() const {
  std::lock_guard<std::mutex> l(m_);
  return candidates_;
}

void HaBus::update(const std::string& entityId, const gc::json& state, std::int64_t haNowMs, gc::Ms nowMs) {
  std::lock_guard<std::mutex> l(m_);
  auto e = std::find_if(entities_.begin(), entities_.end(), [&](const Entity& x) { return x.entityId == entityId; });
  if (e != entities_.end()) updateLocked(*e, state, haNowMs, nowMs);
}

void HaBus::updateAll(const gc::json& states, std::int64_t haNowMs, gc::Ms nowMs) {
  // Looked through without the lock, so the hub's tick never waits for a
  // large answer; only the result is swapped in below.
  std::unordered_map<std::string, const gc::json*> byId;
  std::map<std::string, size_t> perKind;
  std::vector<Candidate> found;
  std::map<std::string, gc::json> raw;
  std::set<std::string> truncated;
  if (states.is_array())
    for (const auto& s : states) {
      if (!s.is_object()) continue;
      const std::string id = gc::jstr(s, "entity_id");
      if (!validEntityId(id) || byId.count(id)) continue;
      byId[id] = &s;
      // Everything that is no candidate is dropped here, not kept or logged.
      const std::string kind = classify(s);
      if (kind.empty()) continue;
      if (perKind[kind] >= kMaxPerKind) {  // per kind, so 100 device temperatures cannot hide the tank's pH
        truncated.insert(kind);
        continue;
      }
      ++perKind[kind];
      const gc::json& attrs = attributesOf(s);
      Candidate c;
      c.entityId = id;
      c.name = printable(gc::jstr(attrs, "friendly_name"), 60);
      c.kind = kind;
      c.unit = printable(gc::jstr(attrs, "unit_of_measurement"), 16);
      if (gc::jstr(s, "state") != "unavailable") {
        c.raw = numericState(s);
        std::string fault;
        c.value = convert(measuresOf(kind).front(), c.raw, gc::jstr(attrs, "unit_of_measurement"), fault);
        if (!fault.empty()) c.problem = fault == "unit missing" ? "unit_missing" : "unit_unsupported";
      }
      found.push_back(std::move(c));
      raw[id] = s;  // already only the few fields the hub reads
    }
  std::lock_guard<std::mutex> l(m_);
  for (const auto& e : entities_) {
    auto it = byId.find(e.entityId);
    if (it != byId.end()) {
      updateLocked(e, *it->second, haNowMs, nowMs);
    } else {
      State gone;
      gone.fault = "not in Home Assistant";  // renamed or removed there
      states_[e.entityId] = gone;
    }
  }
  candidates_ = std::move(found);
  raw_ = std::move(raw);
  truncated_ = std::move(truncated);
  lastHaNowMs_ = haNowMs;
  lastNowMs_ = nowMs;
}

gc::json HaBus::candidatesJson() const {
  std::lock_guard<std::mutex> l(m_);
  gc::json list = gc::json::array();
  for (const auto& c : candidates_) {
    auto e = std::find_if(entities_.begin(), entities_.end(), [&](const Entity& x) { return x.entityId == c.entityId; });
    list.push_back({{"entity", c.entityId},
                    {"name", c.name},
                    {"kind", c.kind},
                    {"measures", measuresOf(c.kind)},
                    {"value", std::isfinite(c.value) ? gc::json(c.value) : gc::json(nullptr)},
                    {"raw", std::isfinite(c.raw) ? gc::json(c.raw) : gc::json(nullptr)},
                    {"unit", c.unit},
                    {"problem", c.problem.empty() ? gc::json(nullptr) : gc::json(c.problem)},
                    {"used", e != entities_.end() ? gc::json(e->measures) : gc::json(nullptr)}});
  }
  return {{"connection", connection_}, {"truncated", truncated_}, {"candidates", list}};
}

std::string HaBus::selectedMeasure(const std::string& entityId) const {
  std::lock_guard<std::mutex> l(m_);
  for (const auto& e : entities_)
    if (e.entityId == entityId) return e.measures;
  return "";
}

void HaBus::adopt(const Entity& entity) {
  std::lock_guard<std::mutex> l(m_);
  if (std::none_of(entities_.begin(), entities_.end(), [&](const Entity& e) { return e.entityId == entity.entityId; }))
    entities_.push_back(entity);
}

void HaBus::deselect(const std::string& entityId) {
  std::lock_guard<std::mutex> l(m_);
  entities_.erase(std::remove_if(entities_.begin(), entities_.end(), [&](const Entity& e) { return e.entityId == entityId; }),
                  entities_.end());
  states_.erase(entityId);
}

void HaBus::setConnection(const std::string& c) {
  std::lock_guard<std::mutex> l(m_);
  connection_ = c;
}

bool HaBus::select(const std::string& entityId, const std::string& measures, gc::Msg& why) {
  std::lock_guard<std::mutex> l(m_);
  for (const auto& e : entities_)
    if (e.entityId == entityId) {
      if (e.measures == measures) return true;
      why = gc::say("ha.used", {{"entity", entityId}});
      return false;
    }
  auto c = std::find_if(candidates_.begin(), candidates_.end(), [&](const Candidate& x) { return x.entityId == entityId; });
  if (c == candidates_.end()) {
    why = gc::say("ha.unknown", {{"entity", entityId}});
    return false;
  }
  const auto can = measuresOf(c->kind);
  if (std::find(can.begin(), can.end(), measures) == can.end()) {
    why = gc::say("ha.mismatch", {{"entity", entityId}});
    return false;
  }
  entities_.push_back({entityId, measures});
  // Its last state is known from the round that offered it: the device is
  // online at once, not only after the next round.
  if (auto r = raw_.find(entityId); r != raw_.end()) updateLocked(entities_.back(), r->second, lastHaNowMs_, lastNowMs_);
  return true;
}

void HaBus::updateLocked(const Entity& entity, const gc::json& state, std::int64_t haNowMs, gc::Ms nowMs) {
  const std::string& entityId = entity.entityId;
  State& st = states_[entityId];
  const State before = st;
  st = State{};
  st.seen = true;
  if (gc::jstr(state, "entity_id") != entityId) {
    st.fault = "answer for another entity";
    return;
  }
  const std::string raw = gc::jstr(state, "state");
  st.available = raw != "unavailable";
  // Last report from the device: last_reported (Home Assistant 2024.4 and
  // later) or last_updated, whichever is later. Without a readable time the
  // hub cannot tell a fresh value from a stuck one, so there is no value.
  const auto reported = parseTimestampMs(gc::jstr(state, "last_reported"));
  const auto updated = parseTimestampMs(gc::jstr(state, "last_updated"));
  if (!reported && !updated) {
    st.fault = "time of the report unreadable";
    return;
  }
  st.reportedAt = std::max(reported.value_or(INT64_MIN), updated.value_or(INT64_MIN));
  // The same report keeps the time it got when first seen; a new one is dated
  // by its age in Home Assistant's own time, at most now.
  st.ts = before.reportedAt == st.reportedAt ? before.ts : nowMs - std::max<std::int64_t>(0, haNowMs - st.reportedAt);
  // "unknown", an empty or a non-numeric state is no value, never 0 (R5).
  const double v = numericState(state);
  if (st.available && std::isfinite(v)) st.value = convert(entity.measures, v, gc::jstr(attributesOf(state), "unit_of_measurement"), st.fault);
  st.name = printable(gc::jstr(attributesOf(state), "friendly_name"), 60);
}

std::string HaBus::fault(const std::string& entityId) const {
  std::lock_guard<std::mutex> l(m_);
  auto it = states_.find(entityId);
  return it == states_.end() ? std::string() : it->second.fault;
}

void HaBus::lost(const std::string& entityId) {
  std::lock_guard<std::mutex> l(m_);
  states_[entityId] = State{};
}

void HaBus::lostAll() {
  std::lock_guard<std::mutex> l(m_);
  for (const auto& e : entities_) states_[e.entityId] = State{};
  // The candidates' last values are no longer current either
  for (auto& c : candidates_) c.value = c.raw = gc::kNaN;
  raw_.clear();
}

std::vector<gc::DeviceReport> HaBus::devices() const {
  std::lock_guard<std::mutex> l(m_);
  std::vector<gc::DeviceReport> out;
  for (const auto& e : entities_) {
    gc::DeviceReport d;
    d.id = deviceId(e.entityId);
    d.cls = classOf(e.measures);
    auto it = states_.find(e.entityId);
    d.online = it != states_.end() && it->second.seen && it->second.available;
    if (it != states_.end()) d.fault = it->second.fault;
    d.info = {{"entity", e.entityId}};
    if (it != states_.end() && !it->second.name.empty()) d.info["name"] = it->second.name;
    out.push_back(std::move(d));
  }
  return out;
}

std::optional<gc::Sample> HaBus::sample(const std::string& dev, const std::string& cap) const {
  std::lock_guard<std::mutex> l(m_);
  for (const auto& e : entities_) {
    if (deviceId(e.entityId) != dev || cap != "measure." + e.measures) continue;
    auto it = states_.find(e.entityId);
    if (it == states_.end() || !it->second.seen || !std::isfinite(it->second.value)) return std::nullopt;
    return gc::Sample{it->second.value, it->second.ts};
  }
  return std::nullopt;
}

bool HaBus::startRun(const std::string&, gc::Ms, const std::string&, std::string& err) {
  err = "read-only (Home Assistant spike)";
  return false;
}

bool HaBus::setSwitch(const std::string&, int, bool, std::string& err) {
  err = "read-only (Home Assistant spike)";
  return false;
}

bool HaBus::writePumpCalibration(const std::string&, double, std::string& err) {
  err = "read-only (Home Assistant spike)";
  return false;
}

}  // namespace ha
