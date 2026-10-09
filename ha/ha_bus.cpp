// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ha_bus.hpp"

#include <algorithm>
#include <cmath>
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

// The value in the hub's unit, or NaN with a fault when the unit is not one
// the hub can convert. A missing unit is taken as the hub's own.
double convert(const std::string& measures, double v, const std::string& unit, std::string& fault) {
  fault.clear();
  if (unit.empty()) return v;
  if (measures == "ph" && unit == "pH") return v;
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
  fault = "unit " + unit + " not supported";
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

std::int64_t parseTimestampMs(const std::string& s) {
  // YYYY-MM-DDTHH:MM:SS[.fraction][Z|±HH:MM]
  if (s.size() < 19 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != ' ') || s[13] != ':' || s[16] != ':') return -1;
  auto num = [&](size_t pos, size_t len) -> int {
    int v = 0;
    for (size_t i = pos; i < pos + len; ++i) {
      if (s[i] < '0' || s[i] > '9') return -1;
      v = v * 10 + (s[i] - '0');
    }
    return v;
  };
  const int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), se = num(17, 2);
  if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 60) return -1;
  size_t i = 19;
  int ms = 0;
  if (i < s.size() && s[i] == '.') {
    int digits = 0;
    for (++i; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++digits)
      if (digits < 3) ms = ms * 10 + (s[i] - '0');
    for (; digits < 3; ++digits) ms *= 10;
  }
  int offsetMin = 0;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    if (i + 6 > s.size() || s[i + 3] != ':') return -1;
    const int oh = num(i + 1, 2), om = num(i + 4, 2);
    if (oh < 0 || om < 0) return -1;
    offsetMin = (s[i] == '-' ? -1 : 1) * (oh * 60 + om);
    i += 6;
  } else if (i < s.size() && s[i] == 'Z') {
    ++i;
  }
  if (i != s.size()) return -1;
  const std::int64_t days = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
  const std::int64_t secs = days * 86400 + h * 3600 + mi * 60 + se - offsetMin * 60;
  return secs * 1000 + ms;
}

Mapping parseMapping(const gc::json& j, std::string& err) {
  Mapping m;
  err.clear();
  if (!j.is_object()) {
    err = "the mapping must be a JSON object";
    return m;
  }
  m.url = gc::jstr(j, "url");
  if (m.url.empty()) err = "\"url\" is missing";
  if (!j.contains("entities") || !j["entities"].is_array()) {
    err = "\"entities\" must be a list";
    return m;
  }
  for (const auto& e : j["entities"]) {
    Entity x{gc::jstr(e, "entity"), gc::jstr(e, "measures")};
    if (x.entityId.empty() || !knownMeasure(x.measures)) {
      err = "each entity needs \"entity\" and \"measures\" (ph, ec, water_temp, level, air_temp, humidity, co2)";
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
                                      {"externalCalibration", true},
                                      {"text", "Wert aus Home Assistant; kalibriert wird dort."}};
  return gc::Catalog::fromJson(j);
}

HaBus::HaBus(std::vector<Entity> entities) : entities_(std::move(entities)) {}

void HaBus::update(const gc::json& state, std::int64_t nowEpochMs, gc::Ms nowMs) {
  const std::string id = gc::jstr(state, "entity_id");
  auto e = std::find_if(entities_.begin(), entities_.end(), [&](const Entity& x) { return x.entityId == id; });
  if (e == entities_.end()) return;
  State st;
  st.seen = true;
  const std::string raw = gc::jstr(state, "state");
  st.available = raw != "unavailable";
  // "unknown", an empty or a non-numeric state is no value, never 0 (R5).
  char* end = nullptr;
  const double v = raw.empty() ? gc::kNaN : std::strtod(raw.c_str(), &end);
  if (st.available && end && *end == '\0' && std::isfinite(v)) {
    const gc::json attrs = state.contains("attributes") ? state["attributes"] : gc::json::object();
    st.value = convert(e->measures, v, gc::jstr(attrs, "unit_of_measurement"), st.fault);
  }
  // Last report from the device: last_reported (newer Home Assistant) or
  // last_updated, whichever is later. Unreadable: seen now.
  const std::int64_t at = std::max(parseTimestampMs(gc::jstr(state, "last_reported")), parseTimestampMs(gc::jstr(state, "last_updated")));
  st.ts = at < 0 ? nowMs : nowMs - std::max<std::int64_t>(0, nowEpochMs - at);
  std::lock_guard<std::mutex> l(m_);
  states_[id] = st;
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
