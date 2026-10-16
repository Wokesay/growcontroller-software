// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/truth.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "gc/messages.hpp"

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
constexpr Ms kFrozenAfter = 15 * kMinute;   // Rohwert steht still

// A sensor on its own schedule (SD-035): stale after three of its usual gaps
// between reports, learned from the last ten, never sooner than 5 min (such
// sensors drop a few reports now and then, RAT-027) and never later than
// 30 min; until three gaps are seen, 10 min. All own rules (assumptions).
// A report re-dated by less than 2 s is the same report: the hub dates a
// report again after a lost connection, to Home Assistant's whole seconds.
constexpr size_t kGapsKept = 10;
constexpr size_t kGapsToLearn = 3;
constexpr Ms kShortestOwnLimit = 5 * kMinute;
constexpr Ms kStartLimit = 10 * kMinute;
constexpr Ms kLongestLimit = 30 * kMinute;
constexpr Ms kSameReport = 2 * kSecond;

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

std::optional<Msg> SensorTruth::checkCalibration(const std::string& kind, const json& data) {
  if (!data.is_object()) return say("calib.data");
  if (kind == "ph" && !phLine(data)) return say("calib.ph");
  if (kind == "ec") {
    double f = jnum(data, "factor");
    if (!isNum(f) || f < 0.5 || f > 2.0) return say("calib.ec");
  }
  if (kind == "tank_curve") {
    Msg err;
    if (!Curve::fromJson(data, err)) return err;
  }
  return std::nullopt;
}

std::optional<Curve> Curve::fromJson(const json& j, Msg& err) {
  if (!j.is_object() || !j.contains("points") || !j["points"].is_array()) {
    err = say("curve.missing");
    return std::nullopt;
  }
  double minGap = jnum(j, "minGap", 0.003);  // 3 mV bei Spannungs-Rohwerten (RAT-078)
  Curve c;
  for (const auto& p : j["points"]) {
    if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) {
      err = say("curve.point");
      return std::nullopt;
    }
    c.points.emplace_back(p[0].get<double>(), p[1].get<double>());
  }
  if (c.points.size() < 2) {
    err = say("curve.two");
    return std::nullopt;
  }
  for (size_t i = 1; i < c.points.size(); ++i) {
    if (c.points[i].first - c.points[i - 1].first < minGap) {
      err = say("curve.order");
      return std::nullopt;
    }
    if (c.points[i].second < c.points[i - 1].second) {
      err = say("curve.falls");
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
    Msg err;
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

Ms SensorTruth::freshLimit(const CapabilityDef& cap, const Track* learned) {
  const Ms catalog = static_cast<Ms>(cap.maxAgeS * 1000.0);
  if (!learned) return catalog;
  if (learned->gaps.size() < kGapsToLearn) return std::max(catalog, kStartLimit);
  // The usual gap is the second longest, so one outage (Home Assistant
  // restarting) does not loosen the limit for the next ten reports.
  std::vector<Ms> gaps(learned->gaps.begin(), learned->gaps.end());
  std::sort(gaps.begin(), gaps.end());
  const Ms usual = gaps[gaps.size() - 2];
  return std::max({catalog, kShortestOwnLimit, std::min(3 * usual, kLongestLimit)});
}

Ms SensorTruth::limit(const std::string& role) const {
  const RoleDef* rd = cat_.role(role);
  const CapabilityDef* cap = rd ? cat_.capability(rd->capability) : nullptr;
  if (!cap) return 0;
  auto it = tracks_.find(role);
  if (it != tracks_.end() && it->second.life) return kLifeLimit;
  return freshLimit(*cap, it != tracks_.end() && it->second.ownSchedule ? &it->second : nullptr);
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
    tr.ownSchedule = false;  // set from the bound device below
    tr.life = false;
    auto setQ = [&](Quality q, Msg reason) {
      r.quality = q;
      r.reason = std::move(reason);
    };
    // A unit after a value, with its no-break space; none for pH
    const std::string unit = cap->unit.empty() ? std::string() : "\u00a0" + cap->unit;
    // A value as the sensor's unit shows it: rounded to its decimals, then shown without trailing zeros
    auto shown = [&](double v) {
      const double p = std::pow(10.0, cap->decimals);
      return numOrNull(std::round(v * p) / p);
    };

    const Binding* b = cfg.binding(roleId);
    if (!b) {
      setQ(Quality::NotBound, say("truth.not_bound"));
      readings_[roleId] = r;
      continue;
    }
    bool online = false;
    const DeviceReport* report = nullptr;
    for (const auto& d : devices)
      if (d.id == b->device) {
        online = d.online;
        report = &d;
      }
    const DeviceCfg* dev = cfg.device(b->device);
    const DeviceClassDef* dc = dev ? cat_.deviceClass(dev->cls) : nullptr;
    // Known from the configured class, also while the sensor is silent: its
    // learned limit holds through a missed round (SD-035, SD-041)
    tr.ownSchedule = dc && dc->ownReportSchedule;
    auto s = bus.sample(b->device, role.capability);
    if (!online) {
      setQ(Quality::Offline, say("truth.offline"));
      readings_[roleId] = r;
      continue;
    }
    // The class the device reports must be the one configured: an import
    // cannot make a sensor calibrated elsewhere one the hub checks (SD-041, RAT-025)
    if (report && dev && report->cls != dev->cls) {
      r.quality = Quality::NoData;
      r.reason = say("truth.class_mismatch");
      readings_[roleId] = r;
      continue;
    }
    // A sign of life (SD-042): the sensor is fresh by its last measurement.
    // Whatever keeps it from saying when that was makes the sensor stale,
    // never the looser learned limit; an old value Home Assistant still holds
    // after the device restarted is no value.
    // Only for a sensor on its own schedule, whose class says so in code (R7):
    // no bus can swap the clock or the limit of the hub's own probes
    const bool life = s && s->life != Sample::Life::None && tr.ownSchedule;
    tr.life = life;
    if (life) {
      auto lifeStale = [&](Msg why) {
        r.quality = Quality::Stale;
        r.reason = std::move(why);
        readings_[roleId] = r;
      };
      if (s->lifeText && now - s->lifeAt > kLifeLimit) {  // the device sends nothing at all: one cause for all its sensors
        lifeStale(say("truth.life_silent", {{"ageS", (now - s->lifeAt + kSecond - 1) / kSecond}}));  // kDeviceSilent (tested)
        continue;
      }
      if (s->life == Sample::Life::Fault) {
        lifeStale(s->lifeFault);
        continue;
      }
      if (s->life == Sample::Life::Waiting) {
        if (now - s->measuredAt > kLifeLimit) {
          lifeStale(say("truth.life_never"));
        } else {
          r.quality = Quality::NoData;
          r.reason = say("truth.life_waiting");
          readings_[roleId] = r;
        }
        continue;
      }
    }
    if (!s || !isNum(s->raw)) {
      setQ(Quality::NoData, say("truth.no_data"));
      readings_[roleId] = r;
      continue;
    }
    r.ts = life ? s->measuredAt : s->ts;
    r.ageMs = life ? std::max<Ms>(0, now - r.ts) : now - r.ts;  // the poller's clock may read a few ms ahead of the tick's
    r.ageOf = life ? "measurement" : tr.ownSchedule ? "change" : "reading";

    // A sensor on its own schedule: learn the gaps between its reports
    // (SD-035); one with a sign of life learns nothing (SD-042)
    const bool ownSchedule = tr.ownSchedule;
    const bool learns = ownSchedule && !life;
    if (!learns || tr.device != b->device) {  // another sensor for the role, or a sign of life added or removed, starts over
      tr.device = learns ? b->device : std::string();
      tr.reported = false;
      tr.gaps.clear();
    }
    if (learns) {
      if (!tr.reported) {
        tr.reported = true;
        tr.lastReport = s->ts;
      } else if (std::llabs(s->ts - tr.lastReport) >= kSameReport) {
        if (s->ts > tr.lastReport) {  // a clock step back is no gap
          tr.gaps.push_back(std::min(s->ts - tr.lastReport, kLongestLimit));
          if (tr.gaps.size() > kGapsKept) tr.gaps.pop_front();
        }
        tr.lastReport = s->ts;
      }
    }
    const Ms limit = life ? kLifeLimit : freshLimit(*cap, learns ? &tr : nullptr);

    // Stillstand des Rohwerts getrennt von der Frische prüfen (RAT-023). A
    // sensor on its own schedule counts by its reports: one that reports
    // every 20 min is not frozen between two of them; one that keeps
    // reporting the same raw value is (SD-041). With a sign of life it
    // counts by its measurements, as Home Assistant drops the unchanged
    // reports of a stuck signal (SD-042).
    const Ms rawClock = life ? s->measuredAt : ownSchedule ? s->ts : now;
    // Only a sign of life switched on or off changes that clock and starts it
    // over; a round offline or without a value in between does not
    if (life != tr.rawLife) {
      tr.rawLife = life;
      tr.lastRaw = kNaN;
    }
    if (s->raw != tr.lastRaw) {
      tr.lastRaw = s->raw;
      tr.lastRawChange = rawClock;
    }
    bool needsCal = !calibrationKind(role.capability).empty();
    // Calibrated elsewhere (e.g. in Home Assistant): shown, but the hub has
    // not checked that calibration, so it is no value for control (RAT-025).
    const bool calibratedElsewhere = needsCal && dc && dc->externalCalibration;
    auto value = calibratedElsewhere ? std::nullopt : calibrate(role.capability, s->raw, cfg.calibration(b->device, calibrationKind(role.capability)));
    if (value) r.value = value;
    else if (calibratedElsewhere || (needsCal && role.capability != "measure.level")) r.value = s->raw;  // Anzeige, nicht für Regelung

    // Plausibility and jumps are judged on the value in its unit: the hub's
    // calibration of the raw value, or a value calibrated elsewhere, which is
    // already in pH, mS/cm or L, so an implausible or jumping one still shows
    // (RAT-021). Standstill is judged on the raw signal (RAT-023); a value
    // calibrated elsewhere has none, only a rounded result, which may rightly
    // stay the same, so it gets no standstill check and freshness covers
    // silence (RAT-059).
    const std::optional<double> assessed = calibratedElsewhere ? std::optional<double>(s->raw) : value;
    if (life && r.ageMs > limit) {
      r.quality = Quality::Stale;
      r.reason = say("truth.life_stale", {{"ageS", (r.ageMs + kSecond - 1) / kSecond}, {"limitS", limit / kSecond}});
    } else if (r.ageMs > limit) {
      setQ(Quality::Stale, say("truth.stale", {{"min", r.ageMs / kMinute}, {"ageS", r.ageMs / 1000}, {"limitS", limit / 1000}}));
    } else if (!assessed) {
      setQ(Quality::Uncalibrated, say("truth.uncalibrated"));
    } else if (expectsNoise(role.capability) && !calibratedElsewhere && rawClock - tr.lastRawChange > kFrozenAfter) {
      setQ(Quality::Frozen, say("truth.frozen"));
    } else if ((isNum(cap->plausMin) && *assessed < cap->plausMin) || (isNum(cap->plausMax) && *assessed > cap->plausMax)) {
      setQ(Quality::Implausible, say("truth.implausible", {{"label", cap->label}, {"value", shown(*assessed)}, {"unit", unit},
                                                           {"min", numOrNull(cap->plausMin)}, {"max", numOrNull(cap->plausMax)}}));
    } else {
      // Jump lock: a change above the threshold within 5 min that no announced
      // intervention explains (mix, hand dose, EC round, inlet, calibration,
      // maintenance; not an own pH dose, RAT-042) locks until 15 min of calm.
      if (s->ts != tr.lastTs) {
        tr.lastTs = s->ts;
        while (!tr.window.empty() && s->ts - tr.window.front().first > kJumpWindow) tr.window.pop_front();
        bool explained = now < tr.explainedUntil;
        if (isNum(cap->jump) && !explained && !tr.window.empty()) {
          auto [mn, mx] = std::minmax_element(tr.window.begin(), tr.window.end(),
                                              [](const auto& a, const auto& c) { return a.second < c.second; });
          double from = std::fabs(*assessed - mn->second) > std::fabs(*assessed - mx->second) ? mn->second : mx->second;
          if (std::fabs(*assessed - from) > cap->jump) {
            rt.jumpLocks[roleId] = epoch + kJumpHoldS;
            json lockInfo = {{"from", from}, {"to", *assessed}, {"at", epoch}};
            rt.latches["jump." + roleId] = lockInfo;
            tr.window.clear();  // neuer Bezugswert; frei 15 min nach dem letzten Sprung
          }
        }
        if (explained) tr.window.clear();
        tr.window.emplace_back(s->ts, *assessed);
      }
      auto lock = rt.jumpLocks.find(roleId);
      if (lock != rt.jumpLocks.end() && epoch < lock->second) {
        json info = rt.latches.count("jump." + roleId) ? rt.latches["jump." + roleId] : json::object();
        // The reason names only causes that can be true: the hub's own pH dose, which announces no
        // change (RAT-042), only lowers pH (RAT-041), so a fall names it; a level names work by hand;
        // a reading of the grow area names work near the plants, one of the tank work at the tank
        const double from = jnum(info, "from"), to = jnum(info, "to");
        const json args = {{"label", cap->label}, {"until", lock->second}, {"from", shown(from)}, {"to", shown(to)}, {"unit", unit}};
        const bool ph = role.capability == "measure.ph", level = role.capability == "measure.level";
        const bool fell = isNum(from) && isNum(to) && to < from, zone = roleId.rfind("zone.", 0) == 0;
        if (ph) setQ(Quality::Jump, say(fell ? "truth.jump_ph_fall" : "truth.jump_ph", args));
        else if (level) setQ(Quality::Jump, say("truth.jump_level", args));
        else setQ(Quality::Jump, say(zone ? "truth.jump_zone" : "truth.jump", args));
      } else {
        if (lock != rt.jumpLocks.end()) {
          rt.jumpLocks.erase(lock);
          rt.latches.erase("jump." + roleId);
          tr.window.clear();
          tr.window.emplace_back(s->ts, *assessed);
        }
        if (calibratedElsewhere) {  // passed every check, but its calibration is not the hub's
          r.quality = Quality::Uncalibrated;
          r.reason = say("truth.external");  // kCalibratedElsewhere (tested)
        } else {
          setQ(Quality::Ok, say("truth.ok"));
        }
      }
    }
    readings_[roleId] = r;
  }
  updateDerived();
}

double SensorTruth::saturationKPa(double t) { return 0.6108 * std::exp(17.27 * t / (t + 237.3)); }
double SensorTruth::airVpdKPa(double t, double rh) { return saturationKPa(t) * (1.0 - rh / 100.0); }

// Derived values in fixed code, not a script: air VPD without a leaf offset;
// without an input there is no value (RAT-017, R5). The hub's own readings
// at most 60 s apart (own rule, assumption). If either reading is on its own
// schedule, both only need to be valid: such sensors often report only on a
// change, so a report's time is not the age of its value (SD-035).
void SensorTruth::updateDerived() {
  auto ownSchedule = [&](const std::string& role) {
    auto it = tracks_.find(role);
    return it != tracks_.end() && it->second.ownSchedule;
  };
  for (const auto& [roleId, role] : cat_.roles) {
    if (role.capability != "derive.vpd") continue;
    const CapabilityDef* cap = cat_.capability(role.capability);
    Reading r;
    r.role = roleId;
    r.capability = role.capability;
    r.unit = cap ? cap->unit : "kPa";
    r.decimals = cap ? cap->decimals : 2;
    const Reading& t = get("zone.air_temp");
    const Reading& h = get("zone.humidity");
    if (t.quality == Quality::NotBound || h.quality == Quality::NotBound) {
      r.quality = Quality::NotBound;
      r.reason = say("truth.vpd.needs");
    } else if (!t.usable() || !h.usable()) {
      r.quality = Quality::NoData;
      r.reason = say("truth.vpd.invalid");
    } else if (!ownSchedule("zone.air_temp") && !ownSchedule("zone.humidity") && std::llabs(t.ts - h.ts) > 60 * kSecond) {
      r.quality = Quality::Stale;
      r.reason = say("truth.vpd.apart");
    } else {
      r.value = airVpdKPa(*t.value, *h.value);
      r.ts = std::max(t.ts, h.ts);
      r.ageMs = std::max(t.ageMs, h.ageMs);
      r.quality = Quality::Ok;
      r.reason = say("truth.ok");
    }
    readings_[roleId] = r;
  }
}

}  // namespace gc
