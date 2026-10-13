// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ha_bus.hpp"

#include <algorithm>
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
// The measures that can come as a raw signal the app calibrates (SD-041), and their class suffix
const char* const kSignalMeasures[] = {"ph"};
const std::string kSignalSuffix = "_raw";
bool signalMeasure(const std::string& m) {
  return std::any_of(std::begin(kSignalMeasures), std::end(kSignalMeasures), [&](const char* x) { return m == x; });
}
bool signalUnit(const std::string& unit) { return unit == "mV" || unit == "V"; }

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

}  // namespace

// Text from Home Assistant as it may appear in a fault or a name: printable
// and short, because it reaches the terminal, the state and diagnostics.
// Keeps printable ASCII and UTF-8 sequences from U+00A0 on (µ, °); drops
// control characters, C1 controls (U+0080–U+009F), invisible format
// characters that can reorder or hide text (U+00AD, U+061C, U+180E,
// U+200B–U+200F, U+2028–U+202E, U+2060–U+2069, U+FEFF, U+FFF9–U+FFFB, tag
// characters) and stray bytes. Only well-formed UTF-8 is kept: no overlong
// form, no surrogate and nothing above U+10FFFF, even if the input ever comes
// from somewhere other than the JSON parser, which already rejects them.
std::string printable(const std::string& s, size_t maxBytes) {
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
    if (ok && n > 2) {  // the second byte's range after these lead bytes (Unicode table 3-7)
      const auto b1 = static_cast<unsigned char>(s[i + 1]);
      if ((c == 0xe0 && b1 < 0xa0) || (c == 0xf0 && b1 < 0x90)) ok = false;  // overlong
      if (c == 0xed && b1 >= 0xa0) ok = false;                                // a surrogate, U+D800–DFFF
      if (c == 0xf4 && b1 >= 0x90) ok = false;                                // above U+10FFFF
    }
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

namespace {

const gc::json& attributesOf(const gc::json& state) {
  static const gc::json kNone = gc::json::object();
  return state.contains("attributes") && state["attributes"].is_object() ? state["attributes"] : kNone;
}

// What a state offers as a sign of life (SD-042): a sign-of-life text, or a
// timestamp sensor (offered also while it states no time).
std::optional<LifeCandidate> lifeOf(const std::string& id, const gc::json& state, std::int64_t haNowMs) {
  const std::string st = gc::jstr(state, "state");
  const gc::json& a = attributesOf(state);
  LifeCandidate c;
  c.entityId = id;
  c.name = printable(gc::jstr(a, "friendly_name"), 60);
  if (gc::jstr(a, "device_class") == "timestamp") {
    c.timestamp = true;
    // A time ahead is no last measurement: no age, so nothing invites picking a "next …" sensor
    if (auto t = parseTimestampMs(st); t && *t <= haNowMs + 2 * gc::kSecond) c.ageS = std::max<std::int64_t>(0, (haNowMs - *t) / 1000);
    return c;
  }
  auto channels = parseLifeText(st);
  if (!channels) return std::nullopt;
  // The ages now: as sent, plus how long ago the text was sent
  const auto reported = parseTimestampMs(gc::jstr(state, "last_reported"));
  const auto updated = parseTimestampMs(gc::jstr(state, "last_updated"));
  const std::int64_t sentAt = std::max(reported.value_or(INT64_MIN), updated.value_or(INT64_MIN));
  const int since = reported || updated ? static_cast<int>(std::clamp<std::int64_t>((haNowMs - sentAt) / 1000, 0, 9999999)) : 0;
  for (auto& [code, age] : *channels)
    if (age) *age += since;
  c.channels = std::move(*channels);
  return c;
}

// A plain decimal number as Home Assistant writes one: an optional minus,
// digits, an optional fraction and exponent (Python writes 1e-05). No space,
// plus, hex, comma or leading point: such a state is no value rather than a
// guess (RAT-006).
bool plainNumber(const std::string& s) {
  size_t i = 0;
  auto digits = [&] {
    const size_t from = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    return i > from;
  };
  if (i < s.size() && s[i] == '-') ++i;
  if (!digits()) return false;
  if (i < s.size() && s[i] == '.') {
    ++i;
    if (!digits()) return false;
  }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    if (!digits()) return false;
  }
  return i == s.size();
}

// The numeric value of a state; NaN for "unknown", "unavailable", empty or
// anything else that is not a plain number, never 0 (R5).
double numericState(const gc::json& state) {
  const std::string raw = gc::jstr(state, "state");
  if (!plainNumber(raw)) return gc::kNaN;
  // strtod must read all of it: under a locale with a decimal comma it would
  // stop at the point and read 6.8 as 6, so that gives no value either
  char* end = nullptr;
  const double v = std::strtod(raw.c_str(), &end);
  if (end != raw.c_str() + raw.size() || !std::isfinite(v)) return gc::kNaN;
  return v == 0 ? 0.0 : v;  // -0 shows as 0
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

// A raw signal in the hub's raw scale (SD-041): a pH probe's voltage as the pH
// an ideal probe would read (kNernstMvPerPh), NaN with a fault for any other
// unit, so a node that starts reporting a calibrated pH never meets the old
// calibration (RAT-006).
double signalValue(const std::string& measures, double v, const std::string& unit, std::string& fault) {
  fault.clear();
  if (unit.empty()) {
    fault = "unit missing";
    return gc::kNaN;
  }
  if (measures == "ph" && unit == "mV") return 7.0 - v / kNernstMvPerPh;
  if (measures == "ph" && unit == "V") return 7.0 - v * 1000.0 / kNernstMvPerPh;
  fault = "unit " + printable(unit) + " is no raw signal";
  return gc::kNaN;
}

// The words of an entity ID or a name, in lower case ("sensor.Tank_pH1" → sensor, tank, ph1).
std::vector<std::string> wordsOf(const std::string& text) {
  std::vector<std::string> out;
  std::string word;
  for (size_t i = 0; i <= text.size(); ++i) {
    const char c = i < text.size() ? text[i] : ' ';
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      word += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
      continue;
    }
    if (!word.empty()) out.push_back(word);
    word.clear();
  }
  return out;
}

// "ph" as a word ("sensor.tank_ph_mv", "pH Sonde", "ph1"), not inside one
// ("phone"), and no ORP or redox probe, which also reports a voltage.
bool saysPh(const std::string& text) {
  bool ph = false;
  for (const auto& w : wordsOf(text)) {
    if (w == "orp" || w == "redox") return false;
    if (w.size() >= 2 && w.compare(0, 2, "ph") == 0 && std::all_of(w.begin() + 2, w.end(), [](char c) { return c >= '0' && c <= '9'; }))
      ph = true;
  }
  return ph;
}

// A voltage a pH front end without gain can deliver: none reads more than 5 V
// (assumption from the ADC inputs such front ends feed). Higher ones (mains, a
// UPS, a car battery) are no pH signal and are not passed on at all.
// A missing value ("unknown" right after Home Assistant starts) does not
// exclude it: only a known one above 5 V does.
bool signalRange(double v, const std::string& unit) {
  return !std::isfinite(v) || std::fabs(unit == "V" ? v * 1000.0 : v) <= 5000.0;
}

// A suggestion as a raw signal (SD-041): a voltage Home Assistant calls pH, or
// one without a device class or of class voltage whose ID or name says pH. Any
// other voltage (a battery, the mains) is never suggested; it can still be
// picked from the other sensors.
bool phSignal(const gc::json& state) {
  const gc::json& attrs = attributesOf(state);
  const std::string dc = gc::jstr(attrs, "device_class"), unit = gc::jstr(attrs, "unit_of_measurement");
  if (!signalUnit(unit) || !signalRange(numericState(state), unit)) return false;
  if (dc == "ph") return true;
  return (dc.empty() || dc == "voltage") && (saysPh(gc::jstr(state, "entity_id")) || saysPh(gc::jstr(attrs, "friendly_name")));
}

// The device classes of the measurements the hub reads (classify()).
bool ownDeviceClass(const std::string& dc) {
  return dc == "ph" || dc == "conductivity" || dc == "temperature" || dc == "humidity" || dc == "carbon_dioxide" ||
         dc == "volume_storage" || dc == "volume";
}

// An other sensor (SD-036), or none. Kept are only sensors someone could
// pick, so the web app learns no more about the house than that: a sensor
// without a device class whose unit fits a measure exactly (the same
// conversion as for any pick; a litre counter that only adds up is no
// level), with its value; and, without a value, one whose unit would fit
// but whose device class says it measures something else (a battery in %,
// so the picker can say why it is not offered), or one of the hub's own
// device classes whose unit the hub cannot use. Text, dates, energy, power
// and any other unit are dropped here, not kept or logged.
std::optional<Candidate> otherOf(const std::string& id, const gc::json& state) {
  if (id.rfind("sensor.", 0) != 0) return std::nullopt;
  const gc::json& attrs = attributesOf(state);
  const std::string unit = gc::jstr(attrs, "unit_of_measurement");
  // A device class that is no text still says it measures something
  const bool hasClass = attrs.contains("device_class") && !(attrs["device_class"].is_string() && attrs["device_class"].get<std::string>().empty());
  const std::string dc = hasClass ? (attrs["device_class"].is_string() ? attrs["device_class"].get<std::string>() : "?") : "";
  const double v = numericState(state);
  if (!std::isfinite(v) && unit.empty()) return std::nullopt;
  const std::string sc = gc::jstr(attrs, "state_class");
  std::vector<std::string> fits;
  for (const char* m : kMeasures) {
    std::string fault;
    convert(m, 0.0, unit, fault);
    if (fault.empty() && !(std::string(m) == "level" && (sc == "total" || sc == "total_increasing"))) fits.push_back(m);
  }
  Candidate c;
  c.entityId = id;
  c.name = printable(gc::jstr(attrs, "friendly_name"), 60);
  c.unit = printable(unit, 16);
  // A voltage can be a pH probe's raw signal (SD-041), also with Home
  // Assistant's device class voltage, which says exactly that; the app's
  // calibration refuses one that does not move between the buffers.
  if (signalUnit(unit) && (!hasClass || dc == "voltage")) {
    if (!signalRange(v, unit)) return std::nullopt;  // the mains or a battery bank is no pH signal, nor kept
    c.measures = {"ph"};
    c.signal = true;
    c.raw = v;
    return c;
  }
  if (hasClass && ownDeviceClass(dc)) {  // e.g. humidity without %: the unit is the reason, or a meter
    const bool meter = (dc == "volume" || dc == "volume_storage") && (sc == "total" || sc == "total_increasing");
    c.problem = meter ? "meter" : unit.empty() ? "unit_missing" : "unit_unsupported";
    return c;
  }
  if (fits.empty()) return std::nullopt;
  if (hasClass) {
    c.problem = "device_class";
    c.deviceClass = printable(dc, 32);
    return c;
  }
  c.measures = std::move(fits);
  c.raw = v;
  std::string fault;
  if (std::isfinite(v)) c.value = convert(c.measures.front(), v, unit, fault);
  return c;
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

bool validLifeChannel(const std::string& code) {
  return !code.empty() && code.size() <= 16 &&
         std::all_of(code.begin(), code.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::optional<LifeChannels> parseLifeText(const std::string& text, std::string* counter) {
  constexpr size_t kMaxChannels = 64;
  constexpr int kMaxAgeS = 9999999;  // about 115 days; a longer age is no number this text sends
  if (text.empty() || text.size() > 255) return std::nullopt;
  auto digits = [](const std::string& s) { return !s.empty() && s.size() <= 9 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); };
  LifeChannels out;
  std::set<std::string> seen;
  size_t start = 0;
  bool first = true;
  while (true) {
    const size_t bar = text.find('|', start);
    const std::string field = text.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
    const size_t eq = field.find('=');
    if (eq == std::string::npos) return std::nullopt;
    const std::string key = field.substr(0, eq), value = field.substr(eq + 1);
    if (first) {  // the counter that makes every send a new state; any count of up to 20 digits
      if (key != "n" || value.empty() || value.size() > 20 || !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
      if (counter) *counter = value;
      first = false;
    } else {
      if (!validLifeChannel(key) || key == "n" || !seen.insert(key).second || out.size() >= kMaxChannels) return std::nullopt;
      if (value == "-") {
        out.emplace_back(key, std::nullopt);
      } else {
        if (!digits(value) || std::stoll(value) > kMaxAgeS) return std::nullopt;
        out.emplace_back(key, static_cast<int>(std::stoll(value)));
      }
    }
    if (bar == std::string::npos) break;
    start = bar + 1;
  }
  if (out.empty()) return std::nullopt;
  return out;
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
    if (e.is_object() && e.contains("signal")) {
      if (!e["signal"].is_boolean() || (e["signal"].get<bool>() && !signalMeasure(x.measures))) {
        err = "\"signal\": true is only for a pH probe's voltage (\"measures\": \"ph\")";
        return {};
      }
      x.signal = e["signal"].get<bool>();
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
  for (const auto& e : entities) {
    gc::json x = {{"entity", e.entityId}, {"measures", e.measures}};
    if (e.signal) x["signal"] = true;
    out.push_back(std::move(x));
  }
  return out;
}

namespace {
bool asciiAlnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
bool asciiHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
}  // namespace

std::string Address::url() const {
  const bool v6 = host.find(':') != std::string::npos;
  const bool usual = port == (tls ? 443 : 80);
  return std::string(tls ? "https://" : "http://") + (v6 ? "[" + host + "]" : host) + (usual ? "" : ":" + std::to_string(port)) + base;
}

std::optional<Address> parseAddress(std::string url, std::string& err) {
  const auto fail = [&](const std::string& why) {
    err = why.empty() ? "\"url\" must be http://host:port or https://host:port (a name of letters, digits, - _ and ., an IPv4 "
                        "address or [IPv6]), optionally followed by a path such as /core (letters, digits, - and _)"
                      : why;
    return std::optional<Address>();
  };
  while (!url.empty() && url.back() == '/') url.pop_back();
  Address a;
  size_t at = 0;
  if (url.rfind("http://", 0) == 0) at = 7;
  else if (url.rfind("https://", 0) == 0) at = 8, a.tls = true;
  else return fail("");
  const size_t slash = url.find('/', at);
  const std::string authority = url.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
  a.base = slash == std::string::npos ? "" : url.substr(slash);
  std::string rest;
  if (!authority.empty() && authority[0] == '[') {  // [IPv6]
    const size_t close = authority.find(']');
    if (close == std::string::npos) return fail("");
    a.host = authority.substr(1, close - 1);
    rest = authority.substr(close + 1);
    if (a.host.find(':') == std::string::npos || !std::all_of(a.host.begin(), a.host.end(), [](char c) { return asciiHex(c) || c == ':'; }))
      return fail("");  // an IPv6 address has colons; [cafe] would be read as a name
  } else {
    const size_t colon = authority.find(':');
    a.host = authority.substr(0, colon);
    rest = colon == std::string::npos ? "" : authority.substr(colon);
    if (a.host.empty() || !std::all_of(a.host.begin(), a.host.end(), [](char c) { return asciiAlnum(c) || c == '-' || c == '_' || c == '.'; }))
      return fail("");  // also no '@', '?', '#' or spaces: no host hidden behind another
  }
  a.port = a.tls ? 443 : 80;
  if (!rest.empty()) {
    const std::string digits = rest.substr(1);
    if (rest[0] != ':' || digits.empty() || digits.size() > 5 || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
      return fail("");
    a.port = std::stoi(digits);
    if (a.port < 1 || a.port > 65535) return fail("");
  }
  // The path: letters, digits, - _ and single slashes; no dot segments, escapes or queries
  for (size_t i = 0; i < a.base.size(); ++i) {
    const char c = a.base[i];
    if (!(asciiAlnum(c) || c == '-' || c == '_' || (c == '/' && (i + 1 >= a.base.size() || a.base[i + 1] != '/')))) return fail("");
  }
  for (size_t from = 0; from < a.base.size();) {  // a segment "api" in any case: the address was pasted with the API in it
    const size_t to = std::min(a.base.find('/', from + 1), a.base.size());
    std::string seg = a.base.substr(from + 1, to - from - 1);
    std::transform(seg.begin(), seg.end(), seg.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (seg == "api") return fail("\"url\" contains /api: leave it out, the hub adds /api/states itself");
    from = to;
  }
  err.clear();
  return a;
}

Mapping parseMapping(const gc::json& j, std::string& err) {
  Mapping m;
  err.clear();
  if (!j.is_object()) {
    err = "the mapping must be a JSON object";
    return m;
  }
  // An add-on reaches Home Assistant at http://supervisor/core/api/…
  const auto address = parseAddress(gc::jstr(j, "url"), err);
  if (!address) return m;
  m.url = address->url();
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
  for (const char* m : kSignalMeasures)
    j["deviceClasses"][classOf(m) + kSignalSuffix] = {{"label", std::string(labelOf(m)) + " (Rohwert)"},
                                                      {"stage", 0},
                                                      {"attach", "ha"},
                                                      {"provides", {std::string("measure.") + m}},
                                                      {"calibrations", {m}},
                                                      {"text", "Rohwert aus Home Assistant; kalibriert wird hier."}};
  gc::Catalog c = gc::Catalog::fromJson(j);
  // Set in code, not from the catalog data, so the catalog cannot loosen
  // them (R7): calibrated in Home Assistant (RAT-025), and reported when the
  // sensor reports, not when the hub asks (SD-035).
  for (const char* m : kMeasures) {
    c.deviceClasses[classOf(m)].externalCalibration = true;
    c.deviceClasses[classOf(m)].ownReportSchedule = true;
  }
  // A raw signal is checked by the hub itself (SD-041): not calibrated elsewhere
  for (const char* m : kSignalMeasures) c.deviceClasses[classOf(m) + kSignalSuffix].ownReportSchedule = true;
  return c;
}

std::string deviceClassOf(const Entity& e) { return classOf(e.measures) + (e.signal ? kSignalSuffix : ""); }

HaBus::HaBus(std::vector<Entity> entities) : entities_(std::move(entities)) {}

std::vector<Entity> HaBus::entities() const {
  std::lock_guard<std::mutex> l(m_);
  return entities_;
}

std::vector<Candidate> HaBus::candidates() const {
  std::lock_guard<std::mutex> l(m_);
  return candidates_;
}

std::vector<Candidate> HaBus::others() const {
  std::lock_guard<std::mutex> l(m_);
  return others_;
}

void HaBus::update(const std::string& entityId, const gc::json& state, std::int64_t haNowMs, gc::Ms nowMs) {
  std::lock_guard<std::mutex> l(m_);
  auto e = std::find_if(entities_.begin(), entities_.end(), [&](const Entity& x) { return x.entityId == entityId; });
  if (e != entities_.end()) updateLocked(*e, state, haNowMs, nowMs);
}

void HaBus::updateAll(const gc::json& states, std::int64_t haNowMs, gc::Ms nowMs) {
  // Looked through without the lock, so the hub's tick never waits for a
  // large answer; only the result is swapped in below.
  std::set<std::string> picked;
  {
    std::lock_guard<std::mutex> l(m_);
    for (const auto& e : entities_) picked.insert(e.entityId);
  }
  std::unordered_map<std::string, const gc::json*> byId;
  std::map<std::string, size_t> perKind;
  std::vector<Candidate> found, others;
  std::map<std::string, gc::json> raw;
  std::set<std::string> truncated;
  std::vector<LifeCandidate> lives;
  std::map<std::string, gc::json> lifeRaw;
  size_t lifeTexts = 0, lifeStamps = 0;
  if (states.is_array())
    for (const auto& s : states) {
      if (!s.is_object()) continue;
      const std::string id = gc::jstr(s, "entity_id");
      if (!validEntityId(id) || byId.count(id)) continue;
      byId[id] = &s;
      if (auto lc = lifeOf(id, s, haNowMs)) {
        // Per kind, so many timestamp sensors cannot hide a device's own text
        size_t& n = lc->timestamp ? lifeStamps : lifeTexts;
        if (n < kMaxLives) {
          ++n;
          lifeRaw[id] = s;
          lives.push_back(std::move(*lc));
        } else {
          truncated.insert("lives");
        }
        continue;  // a sign of life measures nothing itself
      }
      const bool signal = phSignal(s);  // before classify: Home Assistant may call a voltage pH
      // A voltage Home Assistant calls pH is a signal or nothing, never a pH calibrated there
      const gc::json& a0 = attributesOf(s);
      if (!signal && gc::jstr(a0, "device_class") == "ph" && signalUnit(gc::jstr(a0, "unit_of_measurement"))) continue;
      const std::string kind = signal ? "ph" : classify(s);
      if (kind.empty()) {
        // An other sensor, offered only when someone asks (SD-036)
        if (auto o = otherOf(id, s)) others.push_back(std::move(*o));
        continue;
      }
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
      c.signal = signal;
      c.unit = printable(gc::jstr(attrs, "unit_of_measurement"), 16);
      if (gc::jstr(s, "state") != "unavailable") {
        c.raw = numericState(s);
        std::string fault;
        // A signal has no value before the app has calibrated it
        if (!signal) c.value = convert(measuresOf(kind).front(), c.raw, gc::jstr(attrs, "unit_of_measurement"), fault);
        if (!fault.empty()) c.problem = fault == "unit missing" ? "unit_missing" : "unit_unsupported";
      }
      found.push_back(std::move(c));
      raw[id] = s;  // already only the few fields the hub reads
    }
  // Past the limit, the picked sensors and those that can be picked stay;
  // then by name, the entity ID breaking ties so rows do not swap.
  auto rank = [&](const Candidate& c) { return picked.count(c.entityId) ? 0 : c.measures.empty() ? 2 : 1; };
  auto label = [](const Candidate& c) { return c.name.empty() ? c.entityId : c.name; };
  if (others.size() > kMaxOthers) {
    std::stable_sort(others.begin(), others.end(), [&](const Candidate& a, const Candidate& b) { return rank(a) < rank(b); });
    others.resize(kMaxOthers);
    truncated.insert("other");
  }
  for (const auto& o : others)
    if (!o.measures.empty()) raw[o.entityId] = *byId[o.entityId];  // to start a pick with
  std::sort(others.begin(), others.end(), [&](const Candidate& a, const Candidate& b) {
    return std::make_pair(label(a), a.entityId) < std::make_pair(label(b), b.entityId);
  });
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
  others_ = std::move(others);
  raw_ = std::move(raw);
  truncated_ = std::move(truncated);
  lifeCandidates_ = std::move(lives);
  lifeRaw_ = std::move(lifeRaw);
  // The signs of life in use, read every round; one no sensor uses any more is forgotten
  std::set<std::string> used;
  for (const auto& e : entities_)
    if (!e.life.empty()) used.insert(e.life);
  for (auto it = lives_.begin(); it != lives_.end();) it = used.count(it->first) ? std::next(it) : lives_.erase(it);
  for (const auto& id : used) {
    auto b = byId.find(id);
    updateLifeLocked(id, b == byId.end() ? nullptr : b->second, haNowMs, nowMs);
  }
  lastHaNowMs_ = haNowMs;
  lastNowMs_ = nowMs;
}

gc::json HaBus::candidatesJson(bool withOthers, bool withLives) const {
  // Copied under the lock, built without it: up to a thousand others must not hold up the hub's tick
  std::vector<Candidate> candidates, others;
  std::vector<Entity> entities;
  std::vector<LifeCandidate> lives;
  std::string connection;
  std::set<std::string> truncated;
  std::map<std::string, size_t> fits;  // per measure, how many others can serve it
  size_t otherCount = 0;
  {
    std::lock_guard<std::mutex> l(m_);
    candidates = candidates_;
    if (withOthers) others = others_;
    entities = entities_;
    lives = lifeCandidates_;  // their details only when asked for: what Home Assistant has goes out only as needed
    connection = connection_;
    truncated = truncated_;
    for (const auto& o : others_)
      for (const auto& m : o.measures) ++fits[m];
    otherCount = others_.size();
  }
  auto entry = [&](const Candidate& c, const std::vector<std::string>& measures) {
    auto e = std::find_if(entities.begin(), entities.end(), [&](const Entity& x) { return x.entityId == c.entityId; });
    gc::json j{{"entity", c.entityId},
               {"name", c.name},
               {"measures", measures},
               {"value", std::isfinite(c.value) ? gc::json(c.value) : gc::json(nullptr)},
               {"raw", std::isfinite(c.raw) ? gc::json(c.raw) : gc::json(nullptr)},
               {"unit", c.unit},
               {"problem", c.problem.empty() ? gc::json(nullptr) : gc::json(c.problem)},
               {"used", e != entities.end() ? gc::json(e->measures) : gc::json(nullptr)}};
    if (e != entities.end() && !e->life.empty()) j["life"] = {{"entity", e->life}, {"channel", e->lifeChannel}};
    return j;
  };
  auto marked = [](gc::json j, const Candidate& c) {
    if (c.signal) j["signal"] = true;
    return j;
  };
  gc::json list = gc::json::array();
  for (const auto& c : candidates) {
    gc::json j = marked(entry(c, measuresOf(c.kind)), c);
    j["kind"] = c.kind;
    list.push_back(std::move(j));
  }
  gc::json lifeNames = gc::json::object();  // the signs of life in use, by name
  for (const auto& e : entities)
    for (const auto& l : lives)
      if (!e.life.empty() && l.entityId == e.life) lifeNames[l.entityId] = l.name;
  gc::json lifeList = gc::json::array();
  for (const auto& l : withLives ? lives : std::vector<LifeCandidate>{}) {
    gc::json j = {{"entity", l.entityId}, {"name", l.name}, {"kind", l.timestamp ? "timestamp" : "text"}};
    if (l.timestamp) {
      j["ageS"] = l.ageS ? gc::json(*l.ageS) : gc::json(nullptr);
    } else {
      j["channels"] = gc::json::array();
      for (const auto& [code, age] : l.channels) j["channels"].push_back({{"code", code}, {"ageS", age ? gc::json(*age) : gc::json(nullptr)}});
    }
    lifeList.push_back(std::move(j));
  }
  gc::json out = {{"connection", connection}, {"truncated", truncated}, {"candidates", list}, {"otherFits", fits}, {"otherCount", otherCount},
                  {"lifeCount", lives.size()}, {"lifeNames", lifeNames}};
  if (withLives) out["lives"] = std::move(lifeList);
  if (withOthers) {
    gc::json list2 = gc::json::array();
    for (const auto& c : others) {
      gc::json j = marked(entry(c, c.measures), c);
      j["deviceClass"] = c.deviceClass;
      list2.push_back(std::move(j));
    }
    out["others"] = std::move(list2);
  }
  return out;
}

std::string HaBus::selectedMeasure(const std::string& entityId) const {
  std::lock_guard<std::mutex> l(m_);
  for (const auto& e : entities_)
    if (e.entityId == entityId) return e.measures;
  return "";
}

std::optional<bool> HaBus::offeredAsSignal(const std::string& entityId) const {
  std::lock_guard<std::mutex> l(m_);
  for (const auto* list : {&candidates_, &others_})
    for (const auto& c : *list)
      if (c.entityId == entityId) return c.signal;
  return std::nullopt;
}

std::optional<Entity> HaBus::selected(const std::string& entityId) const {
  std::lock_guard<std::mutex> l(m_);
  for (const auto& e : entities_)
    if (e.entityId == entityId) return e;
  return std::nullopt;
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

void HaBus::setLife(const std::string& entityId, const std::string& life, const std::string& channel) {
  std::lock_guard<std::mutex> l(m_);
  for (auto& e : entities_) {
    if (e.entityId != entityId) continue;
    const std::string ch = life.empty() ? std::string() : channel;
    if (e.life == life && e.lifeChannel == ch) return;
    e.life = life;
    e.lifeChannel = ch;
    if (!life.empty() && !lives_.count(life)) {  // known from the last round: no gap until the next one
      auto r = lifeRaw_.find(life);
      updateLifeLocked(life, r == lifeRaw_.end() ? nullptr : &r->second, lastHaNowMs_, lastNowMs_);
    }
    return;
  }
}

std::optional<LifeCandidate> HaBus::lifeCandidate(const std::string& entityId) const {
  std::lock_guard<std::mutex> l(m_);
  for (const auto& c : lifeCandidates_)
    if (c.entityId == entityId) return c;
  return std::nullopt;
}

void HaBus::updateLifeLocked(const std::string& id, const gc::json* state, std::int64_t haNowMs, gc::Ms nowMs) {
  LifeState& ls = lives_[id];
  const LifeState before = ls;
  ls = LifeState{};
  ls.waitingSince = before.waitingSince;  // a channel's wait goes on through the rounds; rebuilt from each readable text
  ls.counter = before.counter;            // so does the last counter, through a round without the text
  ls.counterTs = before.counterTs;
  if (!state || gc::jstr(*state, "entity_id") != id) return;  // not listed: not seen
  ls.seen = true;
  const std::string st = gc::jstr(*state, "state");
  ls.available = !st.empty() && st != "unavailable" && st != "unknown";
  if (!ls.available) return;
  // When Home Assistant last got it, on the hub's clock: a sign of life sent after the
  // sensor's last report must date its measurement no earlier than that report
  const auto reported = parseTimestampMs(gc::jstr(*state, "last_reported"));
  const auto updated = parseTimestampMs(gc::jstr(*state, "last_updated"));
  if (reported || updated) {
    ls.reportedAt = std::max(reported.value_or(INT64_MIN), updated.value_or(INT64_MIN));
    ls.ts = before.reportedAt == ls.reportedAt && before.seen ? before.ts : nowMs - std::max<std::int64_t>(0, haNowMs - ls.reportedAt);
  }
  if (gc::jstr(attributesOf(*state), "device_class") == "timestamp") {
    ls.timestamp = true;
    const auto t = parseTimestampMs(st);
    if (!t) return;
    ls.readable = true;
    ls.stampMs = *t;
    // A time more than 2 s ahead of Home Assistant's own is no last
    // measurement; within that, whole-second rounding, it counts as now
    if (*t - haNowMs > 2 * gc::kSecond) {
      ls.future = true;
      return;
    }
    ls.measuredAt = before.stampMs == *t && before.readable && !before.future ? before.measuredAt : nowMs - std::max<std::int64_t>(0, haNowMs - *t);
    return;
  }
  std::string counter;
  auto channels = parseLifeText(st, &counter);
  if (!channels || (!reported && !updated)) return;
  ls.readable = true;
  // The same counter dated again (a restored or retained text, a state dump on reconnect, also
  // after an outage) is no new send: it keeps the time it was first seen, so an old text gives
  // no fresh sign of life
  if (!before.counter.empty() && before.counter == counter) ls.ts = before.counterTs;
  ls.counter = counter;
  ls.counterTs = ls.ts;
  // A channel waits while it shows "-" in a row; one that left the text or measured starts over
  std::map<std::string, gc::Ms> waiting;
  for (const auto& [code, age] : *channels) {
    ls.ages[code] = age;
    if (!age) {
      auto w = before.waitingSince.find(code);
      waiting[code] = w != before.waitingSince.end() ? w->second : nowMs;
    }
  }
  ls.waitingSince = std::move(waiting);
}

void HaBus::fillLifeLocked(const Entity& e, gc::Sample& s) const {
  s.life = gc::Sample::Life::Fault;
  auto it = lives_.find(e.life);
  if (it == lives_.end() || !it->second.seen || !it->second.available) {
    s.lifeFault = gc::say("truth.life_unavailable");
    return;
  }
  const LifeState& ls = it->second;
  if (e.lifeChannel.empty()) {  // a timestamp sensor
    if (!ls.timestamp || !ls.readable) s.lifeFault = gc::say("truth.life_unreadable");
    else if (ls.future) s.lifeFault = gc::say("truth.life_future");
    else if (mismatchLocked(ls, ls.measuredAt, s.ts)) s.lifeFault = gc::say("truth.life_mismatch");
    else {
      s.life = gc::Sample::Life::Ok;
      s.measuredAt = ls.measuredAt;
    }
    return;
  }
  if (ls.timestamp || !ls.readable) {
    s.lifeFault = gc::say("truth.life_unreadable");
    return;
  }
  s.lifeText = true;
  s.lifeAt = ls.ts;
  auto a = ls.ages.find(e.lifeChannel);
  if (a == ls.ages.end()) {
    s.lifeFault = gc::say("truth.life_no_channel", {{"channel", e.lifeChannel}});
    return;
  }
  if (!a->second) {  // no measurement since the device started
    auto w = ls.waitingSince.find(e.lifeChannel);
    s.life = gc::Sample::Life::Waiting;
    s.measuredAt = w != ls.waitingSince.end() ? w->second : ls.ts;
    return;
  }
  const gc::Ms measured = ls.ts - static_cast<gc::Ms>(*a->second) * gc::kSecond;
  if (mismatchLocked(ls, measured, s.ts)) {
    s.lifeFault = gc::say("truth.life_mismatch");
    return;
  }
  s.life = gc::Sample::Life::Ok;
  s.measuredAt = measured;
}

bool HaBus::mismatchLocked(const LifeState& ls, gc::Ms measuredAt, gc::Ms reportTs) {
  // Sent clearly after the sensor's last report, a sign of life that belongs to the sensor
  // dates its last measurement no earlier than that report (each time is dated to a second)
  return ls.ts > reportTs + gc::kSecond && measuredAt < reportTs - 3 * gc::kSecond;
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
  // A suggestion serves what its kind says, an other sensor what its unit fits (SD-036)
  auto byId = [&](const Candidate& x) { return x.entityId == entityId; };
  std::vector<std::string> can;
  bool signal = false;
  if (auto c = std::find_if(candidates_.begin(), candidates_.end(), byId); c != candidates_.end()) {
    can = measuresOf(c->kind);
    signal = c->signal;
  } else if (auto o = std::find_if(others_.begin(), others_.end(), byId); o != others_.end()) {
    can = o->measures;
    signal = o->signal;
  } else {
    why = gc::say("ha.unknown", {{"entity", entityId}});
    return false;
  }
  if (std::find(can.begin(), can.end(), measures) == can.end()) {
    why = gc::say("ha.mismatch", {{"entity", entityId}});
    return false;
  }
  entities_.push_back({entityId, measures, signal});
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
  const std::string unit = gc::jstr(attributesOf(state), "unit_of_measurement");
  if (st.available && std::isfinite(v))
    st.value = entity.signal ? signalValue(entity.measures, v, unit, st.fault) : convert(entity.measures, v, unit, st.fault);
  if (st.available && !std::isfinite(v) && !raw.empty() && raw != "unknown")
    st.fault = "state \"" + printable(raw, 16) + "\" is no plain number";  // so " 6.2" does not look like "unknown"
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
  for (auto& c : others_) c.value = c.raw = gc::kNaN;
  raw_.clear();
  for (auto& [id, ls] : lives_) {  // only the last counter stays: the same text after the outage is no new send
    LifeState kept;
    kept.counter = ls.counter;
    kept.counterTs = ls.counterTs;
    ls = std::move(kept);
  }
  lifeRaw_.clear();
}

std::vector<gc::DeviceReport> HaBus::devices() const {
  std::lock_guard<std::mutex> l(m_);
  std::vector<gc::DeviceReport> out;
  for (const auto& e : entities_) {
    gc::DeviceReport d;
    d.id = deviceId(e.entityId);
    d.cls = deviceClassOf(e);
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
    gc::Sample s;
    s.raw = it->second.value;
    s.ts = it->second.ts;
    if (!e.life.empty()) fillLifeLocked(e, s);
    return s;
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
