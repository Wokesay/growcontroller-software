// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/watchdog.hpp"

#include <cmath>

#include "gc/messages.hpp"  // texts only (SD-032); no path to actuators

namespace gc {

void to_json(json& j, const WatchResult& w) {
  json items = json::array();
  for (const auto& a : w.items)
    items.push_back({{"id", a.id}, {"label", a.label}, {"status", a.status}, {"text", a.text}});
  j = {{"evaluatedAt", w.evaluatedAt}, {"overall", w.overall}, {"ok", w.ok}, {"problems", w.problems},
       {"neutral", w.neutral},         {"headline", w.headline}, {"items", items}};
}

namespace {

constexpr Epoch kStartupGraceS = 120;  // Anlaufschonfrist nur für den Watchdog (RAT-005)
constexpr double kAlarmMargin = 0.2;   // Alarmband weiter als Regelband (RAT-033)

bool fnEnabled(const Config& c, const std::string& id) {
  auto it = c.functions.find(id);
  return it != c.functions.end() && it->second.enabled;
}

}  // namespace

WatchResult evaluate(const WatchInput& in) {
  WatchResult w;
  w.evaluatedAt = in.epoch;
  auto add = [&](const std::string& id, const Msg& label, const std::string& status, const Msg& text) {
    w.items.push_back({id, label, status, text});
  };
  std::optional<Msg> bandNeutral;
  if (in.epoch < in.maintenanceUntil) bandNeutral = say("watch.maintenance");
  else if (in.epoch - in.bootEpoch < kStartupGraceS) bandNeutral = say("watch.startup");

  if (in.stopped) add("stop", say("watch.stop.label"), "problem", say("watch.stop"));

  // Geräte
  std::string offline;
  for (const auto& d : in.cfg.devices) {
    bool on = false;
    for (const auto& s : in.devices)
      if (s.id == d.id) on = s.online;
    if (!on) offline += (offline.empty() ? "" : ", ") + (d.name.empty() ? d.id : d.name);
  }
  const Msg devices = say("watch.devices.label");
  if (in.cfg.devices.empty()) add("devices", devices, "neutral", say("watch.devices.none"));
  else if (offline.empty()) add("devices", devices, "ok", say("watch.devices.ok"));
  else add("devices", devices, "problem", say("watch.devices.offline", {{"names", offline}}));

  // Messwerte: ungültig ist ein Problem ("Datenausfall"), nie neutral (RAT-015).
  for (const auto& [role, r] : in.readings) {
    if (r.quality == Quality::NotBound) continue;
    const RoleDef* rd = in.cat.role(role);
    const Msg label{"", rd ? rd->label : role};  // catalog label, translated with the catalog
    if (r.usable()) add("reading." + role, label, "ok", say("watch.reading.ok"));
    else if (r.quality == Quality::Uncalibrated) {
      bool needed = (role == "tank.ph" && fnEnabled(in.cfg, "ph_control")) ||
                    (role == "tank.ec" && (fnEnabled(in.cfg, "ec_control") || fnEnabled(in.cfg, "ph_control"))) ||
                    (role == "tank.level" && fnEnabled(in.cfg, "refill"));
      // Calibrated outside the hub: the hub cannot calibrate it, so do not ask for that (RAT-021)
      const bool elsewhere = r.reason.key == "truth.external";
      add("reading." + role, label, needed ? "problem" : "neutral",
          say(elsewhere ? "watch.reading.external" : "watch.reading.uncalibrated"));
    } else
      add("reading." + role, label, "problem", say("watch.reading.failed", {{"reason", r.reason}}));
  }

  auto band = [&](const std::string& id, const Msg& label, const std::string& role, const std::string& fn,
                  const char* tKey, const char* tolKey) {
    auto rit = in.readings.find(role);
    if (rit == in.readings.end() || rit->second.quality == Quality::NotBound) return;
    if (!fnEnabled(in.cfg, fn)) {
      add(id, label, "neutral", say("watch.band.off"));
      return;
    }
    if (bandNeutral) {
      add(id, label, "neutral", *bandNeutral);
      return;
    }
    if (!rit->second.usable()) {
      add(id, label, "neutral", say("watch.band.no_value"));  // eine Ursache, eine Meldung
      return;
    }
    auto p = effectiveParams(in.cat, in.cfg, fn);
    double t = p.num(tKey), tol = p.num(tolKey), v = *rit->second.value;
    double lo = t - tol - kAlarmMargin, hi = t + tol + kAlarmMargin;
    json range{{"lo", numOrNull(lo)}, {"hi", numOrNull(hi)}};
    if (v >= lo && v <= hi) add(id, label, "ok", say("watch.band.ok", range));
    else {
      range["value"] = v;
      add(id, label, "problem", say("watch.band.out", range));
    }
  };
  band("band.ph", say("watch.band.ph.label"), "tank.ph", "ph_control", "ph_target", "ph_tolerance");
  band("band.ec", say("watch.band.ec.label"), "tank.ec", "ec_control", "ec_target", "ec_tolerance");

  // Wassertemperatur: bei leerem Tank nicht anwendbar (RAT-048).
  auto wt = in.readings.find("tank.water_temp");
  if (wt != in.readings.end() && wt->second.quality != Quality::NotBound) {
    auto lvl = in.readings.find("tank.level");
    bool empty = lvl != in.readings.end() && lvl->second.usable() && isNum(in.cfg.tank().minL) &&
                 *lvl->second.value < in.cfg.tank().minL;
    const Msg water = say("watch.water.label");
    if (!fnEnabled(in.cfg, "water_temp_watch")) add("band.water_temp", water, "neutral", say("watch.water.off"));
    else if (empty) add("band.water_temp", water, "neutral", say("watch.water.empty"));
    else if (bandNeutral) add("band.water_temp", water, "neutral", *bandNeutral);
    else if (!wt->second.usable()) add("band.water_temp", water, "neutral", say("watch.band.no_value"));
    else {
      auto p = effectiveParams(in.cat, in.cfg, "water_temp_watch");
      double v = *wt->second.value, lo = p.num("min_c"), hi = p.num("max_c");
      if (v >= lo && v <= hi) add("band.water_temp", water, "ok", say("watch.water.ok", {{"value", v}}));
      else add("band.water_temp", water, "problem", say("watch.water.out", {{"value", v}, {"lo", numOrNull(lo)}, {"hi", numOrNull(hi)}}));
    }
  }

  // Füllstand
  auto lvl = in.readings.find("tank.level");
  if (lvl != in.readings.end() && lvl->second.usable() && isNum(in.cfg.tank().minL)) {
    double v = *lvl->second.value;
    const Msg level = say("watch.level.label");
    if (v >= in.cfg.tank().minL) add("level.min", level, "ok", say("watch.level.ok", {{"value", v}}));
    else add("level.min", level, "problem", say("watch.level.low", {{"value", v}, {"min", in.cfg.tank().minL}}));
  }

  // Vorrat und Einmesswerte
  for (const auto& k : in.cfg.canisters) {
    auto st = in.rt.stockMl.find(k.id);
    if (st != in.rt.stockMl.end() && isNum(st->second)) {
      double min = k.kind == "nutrient" ? 150.0 : 20.0;  // Mindeststand (RAT-071)
      if (st->second < min)
        add("stock." + k.id, say("watch.stock.label", {{"name", k.name}}), "problem", say("watch.stock.low", {{"ml", st->second}}));
    }
    if (!k.pump.empty()) {
      auto f = in.pumpFlow.find(k.pump);
      if (f == in.pumpFlow.end() || !isNum(f->second))
        add("calib." + k.id, say("watch.calib.label", {{"name", k.name}}), "problem", say("watch.calib.missing"));
    }
  }

  // Regler: gesperrt oder gerastet ist ein Problem, mit dem Grund aus der Regelzeile.
  for (const auto& c : in.controllers) {
    if (c.state == "off") continue;
    if (c.state == "blocked" || c.state == "latched") add("ctl." + c.id, c.label, "problem", c.line);
    else add("ctl." + c.id, c.label, "ok", c.line);
  }

  for (const auto& a : w.items) {
    if (a.status == "ok") w.ok++;
    else if (a.status == "problem") w.problems++;
    else w.neutral++;
  }
  if (w.problems > 0) {
    w.overall = "problem";
    w.headline = w.problems == 1 ? say("watch.headline.problem") : say("watch.headline.problems", {{"n", w.problems}});
  } else if (w.ok > 0) {
    w.overall = "ok";
    w.headline = w.ok == 1 ? say("watch.headline.ok_one") : say("watch.headline.ok", {{"n", w.ok}});
  } else {
    w.overall = "neutral";
    w.headline = say("watch.headline.none");
  }
  return w;
}

}  // namespace gc
