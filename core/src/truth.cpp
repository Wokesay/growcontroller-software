#include "gc/truth.hpp"

#include <algorithm>
#include <cmath>

namespace gc {

const char* qualityName(Quality q) {
  switch (q) {
    case Quality::NotBound: return "not_bound";
    case Quality::Offline: return "offline";
    case Quality::NoData: return "no_data";
    case Quality::Stale: return "stale";
    case Quality::Frozen: return "frozen";
    case Quality::Implausible: return "implausible";
    case Quality::Jump: return "jump";
    case Quality::Uncalibrated: return "uncalibrated";
    case Quality::Ok: return "ok";
  }
  return "unknown";
}

namespace {

constexpr Ms kJumpWindow = 5 * kMinute;     // Sprung innerhalb von 5 min (RAT-039)
constexpr Epoch kJumpHoldS = 15 * 60;       // frei nach 15 min Ruhe (RAT-044)
constexpr Ms kFrozenAfter = 15 * kMinute;   // Rohwert steht still

std::string calibrationKind(const std::string& cap) {
  if (cap == "measure.ph") return "ph";
  if (cap == "measure.ec") return "ec";
  if (cap == "measure.level") return "tank_curve";
  return "";
}

bool expectsNoise(const std::string& cap) { return cap == "measure.ph" || cap == "measure.ec"; }

const Reading& emptyReading() {
  static const Reading r;
  return r;
}

struct PhLine {
  double r1, s1, slope;
};

// Zwei Pufferpunkte (roh, Soll). Die Steigung muss plausibel sein (Sonde gesund).
// Liest fremde Daten nur geprüft: kaputte Kalibrierung heißt „kein Wert“, nie Absturz.
std::optional<PhLine> phLine(const json& calib) {
  if (!calib.is_object()) return std::nullopt;
  auto it = calib.find("points");
  if (it == calib.end() || !it->is_array() || it->size() < 2) return std::nullopt;
  auto ok = [](const json& p) { return p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number(); };
  const json& a = (*it)[0];
  const json& b = (*it)[1];
  if (!ok(a) || !ok(b)) return std::nullopt;
  double r1 = a[0].get<double>(), s1 = a[1].get<double>();
  double r2 = b[0].get<double>(), s2 = b[1].get<double>();
  if (std::fabs(r2 - r1) < 0.5) return std::nullopt;
  double slope = (s2 - s1) / (r2 - r1);
  if (slope < 0.8 || slope > 1.25) return std::nullopt;
  return PhLine{r1, s1, slope};
}

}  // namespace

std::optional<std::string> SensorTruth::checkCalibration(const std::string& kind, const json& data) {
  if (!data.is_object()) return std::string("Daten ungültig");
  if (kind == "ph" && !phLine(data)) return std::string("zwei Pufferpunkte mit plausibler Steigung nötig");
  if (kind == "ec") {
    double f = jnum(data, "factor");
    if (!isNum(f) || f < 0.5 || f > 2.0) return std::string("Faktor zwischen 0,5 und 2 nötig");
  }
  if (kind == "tank_curve") {
    std::string err;
    if (!Curve::fromJson(data, err)) return err;
  }
  return std::nullopt;
}

std::optional<Curve> Curve::fromJson(const json& j, std::string& err) {
  if (!j.is_object() || !j.contains("points") || !j["points"].is_array()) {
    err = "Kennlinie fehlt";
    return std::nullopt;
  }
  double minGap = jnum(j, "minGap", 0.003);  // 3 mV bei Spannungs-Rohwerten (RAT-078)
  Curve c;
  for (const auto& p : j["points"]) {
    if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) {
      err = "Stützpunkt ungültig";
      return std::nullopt;
    }
    c.points.emplace_back(p[0].get<double>(), p[1].get<double>());
  }
  if (c.points.size() < 2) {
    err = "Kennlinie braucht mindestens 2 Stützpunkte";
    return std::nullopt;
  }
  for (size_t i = 1; i < c.points.size(); ++i) {
    if (c.points[i].first - c.points[i - 1].first < minGap) {
      err = "Stützpunkte nicht streng steigend";
      return std::nullopt;
    }
    if (c.points[i].second < c.points[i - 1].second) {
      err = "Kennlinie fällt";
      return std::nullopt;
    }
  }
  return c;
}

double Curve::map(double raw) const {
  if (!isNum(raw) || points.size() < 2) return kNaN;
  if (raw <= points.front().first) return points.front().second;
  for (size_t i = 1; i < points.size(); ++i) {
    if (raw <= points[i].first) {
      const auto& a = points[i - 1];
      const auto& b = points[i];
      return a.second + (raw - a.first) * (b.second - a.second) / (b.first - a.first);
    }
  }
  const auto& a = points[points.size() - 2];
  const auto& b = points.back();
  return b.second + (raw - b.first) * (b.second - a.second) / (b.first - a.first);
}

std::optional<double> SensorTruth::calibrate(const std::string& cap, double raw, const json* calib) {
  if (!isNum(raw)) return std::nullopt;
  const std::string kind = calibrationKind(cap);
  if (kind.empty()) return raw;  // Kanal ohne Kalibrierbedarf
  if (!calib) return std::nullopt;
  if (kind == "ph") {
    auto line = phLine(*calib);
    if (!line) return std::nullopt;
    return line->s1 + (raw - line->r1) * line->slope;
  }
  if (kind == "ec") {
    double f = jnum(*calib, "factor");
    if (!isNum(f) || f < 0.5 || f > 2.0) return std::nullopt;
    return raw * f;
  }
  if (kind == "tank_curve") {
    std::string err;
    auto c = Curve::fromJson(*calib, err);
    if (!c) return std::nullopt;
    return c->map(raw);
  }
  return std::nullopt;
}

void SensorTruth::expectChange(const std::string& role, Ms until) {
  auto& t = tracks_[role];
  t.explainedUntil = std::max(t.explainedUntil, until);
}

const Reading& SensorTruth::get(const std::string& role) const {
  auto it = readings_.find(role);
  return it == readings_.end() ? emptyReading() : it->second;
}

void SensorTruth::update(const Config& cfg, const IBus& bus, RuntimeState& rt, Ms now, Epoch epoch) {
  auto devices = bus.devices();
  for (const auto& [roleId, role] : cat_.roles) {
    const CapabilityDef* cap = cat_.capability(role.capability);
    if (!cap || cap->kind != "measure") continue;
    Reading r;
    r.role = roleId;
    r.capability = role.capability;
    r.unit = cap->unit;
    r.decimals = cap->decimals;
    auto& tr = tracks_[roleId];
    auto setQ = [&](Quality q, const std::string& key, const std::string& text, json args = json::object()) {
      r.quality = q;
      r.reason = {key, text, std::move(args)};
    };

    const Binding* b = cfg.binding(roleId);
    if (!b) {
      setQ(Quality::NotBound, "truth.not_bound", "Nicht zugeordnet");
      readings_[roleId] = r;
      continue;
    }
    bool online = false;
    for (const auto& d : devices)
      if (d.id == b->device) online = d.online;
    auto s = bus.sample(b->device, role.capability);
    if (!online) {
      setQ(Quality::Offline, "truth.offline", "Gerät antwortet nicht");
      readings_[roleId] = r;
      continue;
    }
    if (!s || !isNum(s->raw)) {
      setQ(Quality::NoData, "truth.no_data", "Noch kein Messwert");
      readings_[roleId] = r;
      continue;
    }
    r.ts = s->ts;
    r.ageMs = now - s->ts;

    // Stillstand des Rohwerts getrennt von der Frische prüfen (RAT-023).
    if (s->raw != tr.lastRaw) {
      tr.lastRaw = s->raw;
      tr.lastRawChange = now;
    }
    auto value = calibrate(role.capability, s->raw, cfg.calibration(b->device, calibrationKind(role.capability)));
    bool needsCal = !calibrationKind(role.capability).empty();
    if (value) r.value = value;
    else if (needsCal && role.capability != "measure.level") r.value = s->raw;  // Anzeige, nicht für Regelung

    if (r.ageMs > static_cast<Ms>(cap->maxAgeS * 1000.0)) {
      setQ(Quality::Stale, "truth.stale", "Letzter Wert vor " + std::to_string(r.ageMs / kMinute) + " min",
           {{"ageS", r.ageMs / 1000}});
    } else if (!value) {
      setQ(Quality::Uncalibrated, "truth.uncalibrated", "Nicht kalibriert");
    } else if (expectsNoise(role.capability) && now - tr.lastRawChange > kFrozenAfter) {
      setQ(Quality::Frozen, "truth.frozen", "Wert steht seit über 15 min still – Sonde prüfen");
    } else if ((isNum(cap->plausMin) && *value < cap->plausMin) || (isNum(cap->plausMax) && *value > cap->plausMax)) {
      setQ(Quality::Implausible, "truth.implausible",
           cap->label + " " + fmt(*value, cap->decimals) + " außerhalb " + fmt(cap->plausMin, 1) + "–" +
               fmt(cap->plausMax, 1),
           {{"value", *value}, {"min", cap->plausMin}, {"max", cap->plausMax}});
    } else {
      // Sprungsperre: Änderung größer als die Schwelle innerhalb von 5 min ohne
      // Erklärung durch eine eigene Gabe → gesperrt bis 15 min Ruhe.
      if (s->ts != tr.lastTs) {
        tr.lastTs = s->ts;
        while (!tr.window.empty() && s->ts - tr.window.front().first > kJumpWindow) tr.window.pop_front();
        bool explained = now < tr.explainedUntil;
        if (isNum(cap->jump) && !explained && !tr.window.empty()) {
          auto [mn, mx] = std::minmax_element(tr.window.begin(), tr.window.end(),
                                              [](const auto& a, const auto& c) { return a.second < c.second; });
          double from = std::fabs(*value - mn->second) > std::fabs(*value - mx->second) ? mn->second : mx->second;
          if (std::fabs(*value - from) > cap->jump) {
            rt.jumpLocks[roleId] = epoch + kJumpHoldS;
            json lockInfo = {{"from", from}, {"to", *value}, {"at", epoch}};
            rt.latches["jump." + roleId] = lockInfo;
            tr.window.clear();  // neuer Bezugswert; frei 15 min nach dem letzten Sprung
          }
        }
        if (explained) tr.window.clear();
        tr.window.emplace_back(s->ts, *value);
      }
      auto lock = rt.jumpLocks.find(roleId);
      if (lock != rt.jumpLocks.end() && epoch < lock->second) {
        json info = rt.latches.count("jump." + roleId) ? rt.latches["jump." + roleId] : json::object();
        setQ(Quality::Jump, "truth.jump",
             cap->label + " sprang ohne Dosierung von " + fmt(jnum(info, "from"), cap->decimals) + " auf " +
                 fmt(jnum(info, "to"), cap->decimals) + ". Sonde prüfen. Hebt sich nach 15 min Ruhe auf.",
             {{"until", lock->second}, {"from", numOrNull(jnum(info, "from"))}, {"to", numOrNull(jnum(info, "to"))}});
      } else {
        if (lock != rt.jumpLocks.end()) {
          rt.jumpLocks.erase(lock);
          rt.latches.erase("jump." + roleId);
          tr.window.clear();
          tr.window.emplace_back(s->ts, *value);
        }
        setQ(Quality::Ok, "truth.ok", "Gültig");
      }
    }
    readings_[roleId] = r;
  }
}

}  // namespace gc
