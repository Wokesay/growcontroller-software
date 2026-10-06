#include "gc/watchdog.hpp"

#include <cmath>

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
  auto add = [&](const std::string& id, const std::string& label, const std::string& status, const std::string& text) {
    w.items.push_back({id, label, status, text});
  };
  std::string bandNeutral;
  if (in.epoch < in.maintenanceUntil) bandNeutral = "Pflegemodus – Werte werden nicht bewertet";
  else if (in.epoch - in.bootEpoch < kStartupGraceS) bandNeutral = "Anlauf nach Neustart";

  if (in.stopped) add("stop", "Not-Halt", "problem", "Not-Halt aktiv – alle Automatik steht");

  // Geräte
  std::string offline;
  for (const auto& d : in.cfg.devices) {
    bool on = false;
    for (const auto& s : in.devices)
      if (s.id == d.id) on = s.online;
    if (!on) offline += (offline.empty() ? "" : ", ") + (d.name.empty() ? d.id : d.name);
  }
  if (in.cfg.devices.empty()) add("devices", "Geräte", "neutral", "Noch keine Geräte eingerichtet");
  else if (offline.empty()) add("devices", "Geräte", "ok", "Alle eingerichteten Geräte antworten");
  else add("devices", "Geräte", "problem", "Antwortet nicht: " + offline);

  // Messwerte: ungültig ist ein Problem ("Datenausfall"), nie neutral (RAT-015).
  for (const auto& [role, r] : in.readings) {
    if (r.quality == Quality::NotBound) continue;
    const RoleDef* rd = in.cat.role(role);
    std::string label = rd ? rd->label : role;
    if (r.usable()) add("reading." + role, label, "ok", "Gültig");
    else if (r.quality == Quality::Uncalibrated) {
      bool needed = (role == "tank.ph" && fnEnabled(in.cfg, "ph_control")) ||
                    (role == "tank.ec" && (fnEnabled(in.cfg, "ec_control") || fnEnabled(in.cfg, "ph_control"))) ||
                    (role == "tank.level" && fnEnabled(in.cfg, "refill"));
      add("reading." + role, label, needed ? "problem" : "neutral", "Nicht kalibriert");
    } else
      add("reading." + role, label, "problem", "Datenausfall: " + r.reason.text);
  }

  auto band = [&](const std::string& id, const std::string& label, const std::string& role, const std::string& fn,
                  const char* tKey, const char* tolKey, int dec) {
    auto rit = in.readings.find(role);
    if (rit == in.readings.end() || rit->second.quality == Quality::NotBound) return;
    if (!fnEnabled(in.cfg, fn)) {
      add(id, label, "neutral", "Kein Ziel gesetzt (Regelung aus)");
      return;
    }
    if (!bandNeutral.empty()) {
      add(id, label, "neutral", bandNeutral);
      return;
    }
    if (!rit->second.usable()) {
      add(id, label, "neutral", "Kein gültiger Wert – siehe Messwert");  // eine Ursache, eine Meldung
      return;
    }
    auto p = effectiveParams(in.cat, in.cfg, fn);
    double t = p.num(tKey), tol = p.num(tolKey), v = *rit->second.value;
    double lo = t - tol - kAlarmMargin, hi = t + tol + kAlarmMargin;
    if (v >= lo && v <= hi) add(id, label, "ok", "Im Band " + fmt(lo, dec) + "–" + fmt(hi, dec));
    else add(id, label, "problem", fmt(v, dec) + " außerhalb " + fmt(lo, dec) + "–" + fmt(hi, dec));
  };
  band("band.ph", "pH im Zielband", "tank.ph", "ph_control", "ph_target", "ph_tolerance", 2);
  band("band.ec", "EC im Zielband", "tank.ec", "ec_control", "ec_target", "ec_tolerance", 2);

  // Wassertemperatur: bei leerem Tank nicht anwendbar (RAT-048).
  auto wt = in.readings.find("tank.water_temp");
  if (wt != in.readings.end() && wt->second.quality != Quality::NotBound) {
    auto lvl = in.readings.find("tank.level");
    bool empty = lvl != in.readings.end() && lvl->second.usable() && isNum(in.cfg.tank().minL) &&
                 *lvl->second.value < in.cfg.tank().minL;
    if (!fnEnabled(in.cfg, "water_temp_watch")) add("band.water_temp", "Wassertemperatur", "neutral", "Bewertung aus");
    else if (empty) add("band.water_temp", "Wassertemperatur", "neutral", "Nicht anwendbar: Tank leer");
    else if (!bandNeutral.empty()) add("band.water_temp", "Wassertemperatur", "neutral", bandNeutral);
    else if (!wt->second.usable()) add("band.water_temp", "Wassertemperatur", "neutral", "Kein gültiger Wert – siehe Messwert");
    else {
      auto p = effectiveParams(in.cat, in.cfg, "water_temp_watch");
      double v = *wt->second.value, lo = p.num("min_c"), hi = p.num("max_c");
      if (v >= lo && v <= hi) add("band.water_temp", "Wassertemperatur", "ok", fmt(v, 1) + " °C im Band");
      else add("band.water_temp", "Wassertemperatur", "problem", fmt(v, 1) + " °C außerhalb " + fmt(lo, 1) + "–" + fmt(hi, 1));
    }
  }

  // Füllstand
  auto lvl = in.readings.find("tank.level");
  if (lvl != in.readings.end() && lvl->second.usable() && isNum(in.cfg.tank().minL)) {
    double v = *lvl->second.value;
    if (v >= in.cfg.tank().minL) add("level.min", "Füllstand", "ok", fmt(v, 1) + " L");
    else add("level.min", "Füllstand", "problem", fmt(v, 1) + " L unter " + fmt(in.cfg.tank().minL, 1) + " L");
  }

  // Vorrat und Einmesswerte
  for (const auto& k : in.cfg.canisters) {
    auto st = in.rt.stockMl.find(k.id);
    if (st != in.rt.stockMl.end() && isNum(st->second)) {
      double min = k.kind == "nutrient" ? 150.0 : 20.0;  // Mindeststand (RAT-071)
      if (st->second < min) add("stock." + k.id, "Vorrat " + k.name, "problem", fmt(st->second, 0) + " ml – nachfüllen");
    }
    if (!k.pump.empty()) {
      auto f = in.pumpFlow.find(k.pump);
      if (f == in.pumpFlow.end() || !isNum(f->second))
        add("calib." + k.id, "Einmessen " + k.name, "problem", "Pumpe nicht eingemessen – wird nicht dosiert");
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
    w.headline = w.problems == 1 ? "1 Problem" : std::to_string(w.problems) + " Probleme";
  } else if (w.ok > 0) {
    w.overall = "ok";
    w.headline = "Alles in Ordnung (" + std::to_string(w.ok) + " Prüfungen)";
  } else {
    w.overall = "neutral";
    w.headline = "Noch nichts zu bewerten";
  }
  return w;
}

}  // namespace gc
