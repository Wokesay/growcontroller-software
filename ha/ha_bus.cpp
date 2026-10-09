// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ha_bus.hpp"

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdlib>

#include "gc/embedded.hpp"

namespace ha {

namespace {

const char* const kMeasures[] = {"ph", "ec", "water_temp", "level", "air_temp", "humidity", "co2"};

bool knownMeasure(const std::string& m) {
  return std::any_of(std::begin(kMeasures), std::end(kMeasures), [&](const char* k) { return m == k; });
}

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

// Text from Home Assistant as it may appear in a fault: printable, at most
// 32 bytes, because faults reach the terminal, the state and diagnostics.
std::string printable(const std::string& s) {
  std::string out;
  for (unsigned char c : s)
    if (c >= 0x20 && c != 0x7f) out += static_cast<char>(c);
  return gc::utf8Prefix(out, 32);
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

Mapping parseMapping(const gc::json& j, std::string& err) {
  Mapping m;
  err.clear();
  if (!j.is_object()) {
    err = "the mapping must be a JSON object";
    return m;
  }
  // http(s)://host[:port] without a path: the API paths are added to it.
  m.url = gc::jstr(j, "url");
  while (!m.url.empty() && m.url.back() == '/') m.url.pop_back();
  const size_t scheme = m.url.rfind("http://", 0) == 0 ? 7 : m.url.rfind("https://", 0) == 0 ? 8 : 0;
  if (scheme == 0 || m.url.size() == scheme || m.url.find('/', scheme) != std::string::npos) {
    err = "\"url\" must be http://host:port or https://host:port, without a path";
    return m;
  }
  if (!j.contains("entities") || !j["entities"].is_array() || j["entities"].empty()) {
    err = "\"entities\" must be a list with at least one entity";
    return m;
  }
  for (const auto& e : j["entities"]) {
    Entity x{gc::jstr(e, "entity"), gc::jstr(e, "measures")};
    // domain.object_id in lower case, as Home Assistant writes entity IDs; it goes into a URL path.
    const size_t dot = x.entityId.find('.');
    const bool idOk = dot != std::string::npos && dot > 0 && dot + 1 < x.entityId.size() &&
                      std::all_of(x.entityId.begin(), x.entityId.end(), [](char c) {
                        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
                      }) &&
                      x.entityId.find('.', dot + 1) == std::string::npos;
    if (!idOk || !knownMeasure(x.measures)) {
      err = "each entity needs \"entity\" (e.g. sensor.grow_ph) and \"measures\" (ph, ec, water_temp, level, air_temp, humidity, co2)";
      return m;
    }
    if (std::any_of(m.entities.begin(), m.entities.end(), [&](const Entity& o) { return o.entityId == x.entityId; })) {
      err = x.entityId + " is listed twice";
      return m;
    }
    m.entities.push_back(std::move(x));
  }
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

void HaBus::update(const std::string& entityId, const gc::json& state, std::int64_t haNowMs, gc::Ms nowMs) {
  auto e = std::find_if(entities_.begin(), entities_.end(), [&](const Entity& x) { return x.entityId == entityId; });
  if (e == entities_.end()) return;
  std::lock_guard<std::mutex> l(m_);
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
  char* end = nullptr;
  const double v = raw.empty() ? gc::kNaN : std::strtod(raw.c_str(), &end);
  if (st.available && end && *end == '\0' && std::isfinite(v)) {
    static const gc::json kNone = gc::json::object();
    const gc::json& attrs = state.contains("attributes") && state["attributes"].is_object() ? state["attributes"] : kNone;
    st.value = convert(e->measures, v, gc::jstr(attrs, "unit_of_measurement"), st.fault);
  }
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
