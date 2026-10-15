// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/dosing.hpp"

#include <algorithm>

#include "gc/messages.hpp"

namespace gc {

namespace {

Msg purposeLabel(const std::string& p) {
  if (p == "mix") return say("purpose.mix");
  if (p == "manual") return say("purpose.manual");
  if (p == "ec") return say("purpose.ec");
  if (p == "ph") return say("purpose.ph");
  if (p == "calibration") return say("purpose.calibration");
  if (p == "prime") return say("purpose.prime");
  return say("purpose.other");
}

// The label of a role from the catalog (translated with the catalog later).
std::string roleLabel(const Catalog& cat, const std::string& role) {
  const RoleDef* rd = cat.role(role);
  return rd ? rd->label : role;
}

constexpr double kInletHysteresisL = 0.5;  // EIN erst 0,5 L über der AUS-Grenze (RAT-078: 2,5/3 L)

// A dry-run latch caused only by a low level releases itself (SD-022) once a usable level
// has stayed at or above the minimum plus a margin for 5 min, and not again within an hour
// of releasing itself (own rules, SD-067).
constexpr double kDryMarginL = 0.5;
constexpr double kDryMarginShare = 0.02;  // of the capacity, where that is larger
constexpr Ms kDrySettle = 5 * kMinute;
constexpr Epoch kDryRepeatS = 60 * 60;

double dryReleaseLevel(const TankCfg& tank) {
  return tank.minL + (isNum(tank.capacityL) ? std::max(kDryMarginL, kDryMarginShare * tank.capacityL) : kDryMarginL);
}

// Whether a stored dry-run latch releases itself: caused by a low level, with a minimum to
// measure against, and nothing else stored with it. Any other key (a reason to hold, one
// written by another version, damage) and a latch without a cause hold until acknowledged
// (RAT-063, SD-067).
bool selfReleasing(const json& l, const TankCfg& tank) {
  if (!l.is_object() || !l.contains("cause") || l["cause"] != "level" || !isNum(tank.minL)) return false;
  if (isNum(tank.capacityL) && dryReleaseLevel(tank) > tank.capacityL) return false;  // a level it can never reach
  for (const auto& [k, v] : l.items())
    if (k != "at" && k != "levelL" && k != "cause") return false;
  return true;
}

// Why a dry-run latch from a low level holds this time: "off_failed", "repeat", "jump" (SD-067)
std::string holdReason(const json& l) {
  return l.is_object() && l.contains("hold") && l["hold"].is_string() ? l["hold"].get<std::string>() : "";
}
void holdLatch(json& l, const char* why) {
  if (l.is_object() && !l.contains("hold")) l["hold"] = why;
}

}  // namespace

// ---------------------------------------------------------------- Actuators

bool Actuators::startRun(const Ctx& c, const std::string& pump, Ms ms, const std::string& purpose,
                         const std::string& jobId, Msg& err) {
  auto fail = [&](Msg m) {
    err = std::move(m);
    return false;
  };
  if (c.stopped) return fail(say("act.stopped"));
  if (!runningPump_.empty()) return fail(say("act.busy"));
  auto it = c.pumps.find(pump);
  if (it == c.pumps.end() || !it->second.online) return fail(say("act.offline"));
  if (!it->second.fault.empty()) return fail(say("act.fault", {{"fault", it->second.fault}}));
  const bool noFlowNeeded = purpose == "calibration" || purpose == "prime";
  if (!noFlowNeeded && !(isNum(it->second.flowMlPerMin) && it->second.flowMlPerMin > 0)) return fail(say("act.uncalibrated"));
  const Limits lim = c.cfg.limits.bounded();  // feste Grenzen, Konfiguration verschärft nur (R7)
  if (!noFlowNeeded && ms < static_cast<Ms>(lim.minRunS * 1000)) return fail(say("act.too_short", {{"s", lim.minRunS}}));
  if (ms <= 0 || ms > static_cast<Ms>(lim.maxRunS * 1000)) return fail(say("act.too_long", {{"s", lim.maxRunS}}));
  if (purpose == "ph" || purpose == "ec") {
    auto st = roleState(c.cfg, "tank.circulation");
    if (!st || !*st) return fail(say("act.no_mixing"));
  }
  std::string e;
  if (!bus_.startRun(pump, ms, jobId, e)) return fail(say("act.bus", {{"error", e}}));
  runningPump_ = pump;
  runningPurpose_ = purpose;
  return true;
}

void Actuators::clearRun(const std::string& pump) {
  if (runningPump_ == pump) {
    runningPump_.clear();
    runningPurpose_.clear();
  }
}

void Actuators::stopPumps() { bus_.stopAllPumps(); }

SwitchSafety safetyForRole(const RoleDef& rd) {
  SwitchSafety s;
  if (rd.onAfterPowerLoss) s.powerOn = PowerOn::On;  // fans (PD-050)
  if ((rd.profile == "puls" || rd.profile == "heizen") && isNum(rd.maxOnS)) s.autoOffS = std::ceil(rd.maxOnS * 1.11 / 60.0) * 60.0;
  return s;
}

bool Actuators::sw(const std::string& dev, int channel, bool on, std::string& err) {
  if (net_ && net_->owns(dev)) return net_->setSwitch(dev, channel, on, err);
  return bus_.setSwitch(dev, channel, on, err);
}

std::optional<bool> Actuators::swState(const std::string& dev, int channel) const {
  if (net_ && net_->owns(dev)) return net_->switchState(dev, channel);
  return bus_.switchState(dev, channel);
}

std::optional<bool> Actuators::roleState(const Config& cfg, const std::string& role) const {
  const Binding* b = cfg.binding(role);
  if (!b) return std::nullopt;
  return swState(b->device, b->channel);
}

std::optional<Msg> Actuators::inhibit(const Ctx& c, const std::string& role) const {
  const auto& level = c.truth.get("tank.level");
  const bool levelBound = c.cfg.binding("tank.level") != nullptr;
  const auto& tank = c.cfg.tank();
  if (role == "tank.circulation") {
    if (auto l = c.rt.latches.find("circulation.dry"); l != c.rt.latches.end()) {
      if (levelBound && selfReleasing(l->second, tank)) return say("act.circ.latched_self", {{"back", dryReleaseLevel(tank)}});
      const std::string why = holdReason(l->second);
      if (why == "off_failed") return say("act.circ.latched_off");
      if (why == "repeat") return say("act.circ.latched_repeat");
      if (why == "jump") return say("act.circ.latched_jump");
      return say("act.circ.latched");
    }
    if (levelBound) {
      if (!level.usable()) return say("act.circ.level_invalid");
      if (isNum(tank.minL) && *level.value < tank.minL + kInletHysteresisL)
        return say("act.circ.low", {{"level", *level.value}, {"min", tank.minL + kInletHysteresisL}});
    }
    return std::nullopt;
  }
  if (role == "tank.inlet") {
    if (c.rt.latches.count("inlet.fault")) return say("act.inlet.latched");
    if (!levelBound || !level.usable()) return say("act.inlet.level");
    if (isNum(tank.capacityL) && *level.value >= tank.capacityL) return say("act.inlet.full");
    if (!isNum(tank.capacityL)) return say("act.inlet.capacity");
    return std::nullopt;
  }
  // Befeuchter und Entfeuchter nie zugleich (R7, Quelle: RAT-034). Ist der
  // Zustand des Gegengeräts unbekannt (z. B. Dose nicht erreichbar), könnte es
  // noch laufen: dann ebenfalls gesperrt (R5).
  auto pairBlock = [&](const char* other) -> std::optional<Msg> {
    if (!c.cfg.binding(other)) return std::nullopt;
    auto st = roleState(c.cfg, other);
    if (!st) return say("act.climate.pair_unknown", {{"label", roleLabel(c.cat, other)}});
    if (*st) return say("act.climate.pair", {{"label", roleLabel(c.cat, other)}});
    return std::nullopt;
  };
  if (role == "zone.humidifier")
    if (auto m = pairBlock("zone.dehumidifier")) return m;
  if (role == "zone.dehumidifier")
    if (auto m = pairBlock("zone.humidifier")) return m;
  // Befeuchter: bei zugeordneter Feuchte nur mit gültigem Wert und unter der
  // Obergrenze (Kondensat an der Elektrik).
  if (role == "zone.humidifier" && c.cfg.binding("zone.humidity")) {
    const auto& rh = c.truth.get("zone.humidity");
    if (!rh.usable()) return say("act.humidifier.rh");
    if (*rh.value >= kHumidifierMaxRh) return say("act.humidifier.rh_high", {{"rh", *rh.value}, {"max", kHumidifierMaxRh}});
  }
  // Irrigation pump: only with a valid level above the minimum level,
  // otherwise it runs dry. An unreadable level blocks (deviation noted in
  // RAT-068; confirmed by SD-017, which adds an emergency dose, irrigation
  // without a level sensor and dry-run detection, not implemented yet). The current level counts, not a
  // predicted one.
  if (role == "zone.irrigation_pump") {
    if (!levelBound || !level.usable()) return say("act.irrigation.level");
    if (!isNum(tank.minL)) return say("act.irrigation.min_missing");
    if (*level.value < tank.minL + kInletHysteresisL) return say("act.irrigation.low", {{"level", *level.value}});
  }
  // Kompressor: Mindestpause für den Druckausgleich (Quelle: RAT-034).
  // Den Mindestlauf hält die Funktion ein; Ausschalten geht immer.
  if (const RoleDef* rd = c.cat.role(role); rd && rd->profile == "kompressor") {
    auto off = offSince_.find(role);
    if (off != offSince_.end() && c.now - off->second < kCompressorPause)
      return say("act.compressor.pause", {{"label", rd->label}, {"min", static_cast<double>(kCompressorPause / kMinute)}});
  }
  return std::nullopt;
}

bool Actuators::setRole(const Ctx& c, const std::string& role, bool on, const Msg& who, Msg& err) {
  const Binding* b = c.cfg.binding(role);
  if (!b) {
    err = say("act.unbound");
    return false;
  }
  if (on) {
    if (c.stopped) {
      err = say("act.stopped");
      return false;
    }
    if (auto inh = inhibit(c, role)) {
      err = *inh;
      return false;
    }
    // Netzgerät: Die Schutzeinstellung muss noch im Gerät stehen (Werksreset,
    // Änderung in der Shelly-App, Import ohne Schreiben).
    const RoleDef* rd = c.cat.role(role);
    if (net_ && net_->owns(b->device) && rd && !rd->profile.empty()) {
      auto got = net_->readConfig(b->device, b->channel);
      if (!got) {
        err = say("act.net.unreachable");
        return false;
      }
      if (!sameSafety(*got, safetyForRole(*rd))) {
        err = say("act.net.safety");
        return false;
      }
    }
  }
  auto cur = swState(b->device, b->channel);
  if (cur && *cur == on) return true;
  std::string e;
  if (!sw(b->device, b->channel, on, e)) {
    err = say("act.output_refused", {{"error", e}});
    return false;
  }
  if (on) {
    onSince_[role] = c.now;
    cuts_.erase(role);  // neuer Lauf: eine spätere Abschaltung wird wieder gemeldet
  } else {
    onSince_.erase(role);
    offSince_[role] = c.now;
  }
  if (role == "tank.inlet")
    c.log.add(c.epoch, "tank", "info", say(on ? "ev.inlet.open" : "ev.inlet.closed"), who, {{"role", role}, {"on", on}});
  return true;
}

void Actuators::stopAll(const Catalog& cat, const Config& cfg, Ms now, bool keepPowerLossOn) {
  bus_.stopAllPumps();
  cfg.forEachBinding([&](const std::string& role, const Binding& b) {
    // Alle Schaltrollen aus, auch wenn der Zustand unbekannt ist (Schaltbox
    // oder Dose nicht lesbar): der Befehl kostet nichts. Messrollen nicht.
    const RoleDef* rd = cat.role(role);
    if (!rd || rd->profile.empty()) return;
    if (keepPowerLossOn && rd->onAfterPowerLoss) return;
    std::string e;
    sw(b.device, b.channel, false, e);
    offSince_[role] = now;  // minimum pauses also hold after a Stop and a restart
  });
  onSince_.clear();
}

bool Actuators::cut(const Ctx& c, const std::string& role, const std::string& key, const std::string& type,
                    const std::string& severity, const Msg& title, const Msg& text, const Msg* failedText) {
  const Binding* b = c.cfg.binding(role);
  if (!b) return false;
  std::string e;
  const bool ok = sw(b->device, b->channel, false, e);
  auto it = cuts_.find(role);
  if (it == cuts_.end()) it = cuts_.emplace(role, Cut{{}, b->device, b->channel, false}).first;
  if (it->second.keys.insert(key).second) {
    if (ok) {
      c.log.add(c.epoch, type, severity, title, text, {{"role", role}});
    } else {
      c.log.add(c.epoch, "block", "alarm", say("ev.off_unconfirmed", {{"label", roleLabel(c.cat, role)}}),
                say("ev.off_failed.text", {{"reason", failedText ? *failedText : text}, {"error", e}}), {{"role", role}, {"device", b->device}});
    }
  }
  if (!ok) it->second.failed = true;
  return ok;
}

void Actuators::rearm(const std::string& role, const std::string& key) {
  if (auto it = cuts_.find(role); it != cuts_.end()) it->second.keys.erase(key);
}

void Actuators::noteReported(const std::string& role, const std::string& key) {
  if (auto it = cuts_.find(role); it != cuts_.end()) it->second.keys.insert(key);
}

bool Actuators::dryLatchReleasesItself(const Ctx& c) const {
  auto l = c.rt.latches.find("circulation.dry");
  return l != c.rt.latches.end() && c.cfg.binding("tank.level") && selfReleasing(l->second, c.cfg.tank());
}

// SD-022 (#27): the level has to stay usable, at or above the release level and not above
// the capacity for 5 min, outside maintenance mode (hands in the tank), with the pump read
// as off; any other reading starts the wait again (SD-067).
void Actuators::releaseDryLatch(const Ctx& c) {
  // A self-release stored without a plausible time (before the clock was known, or damaged)
  // counts as now once the clock is known: one more careful hour, then the guard ages out
  if (auto last = c.rt.selfReleased.find("circulation.dry"); last != c.rt.selfReleased.end() && !plausibleEpoch(last->second) && plausibleEpoch(c.epoch))
    last->second = c.epoch;
  auto l = c.rt.latches.find("circulation.dry");
  if (l == c.rt.latches.end()) {
    dryBackSince_.reset();
    dryOffSeen_ = false;
    return;
  }
  // A jump of the level while latched has no explanation the hub knows (a sensor stuck high,
  // condensation, a hose without maintenance mode): a person looks at the tank first
  if (c.rt.jumpLocks.count("tank.level")) holdLatch(l->second, "jump");
  const auto& level = c.truth.get("tank.level");
  const auto& tank = c.cfg.tank();
  const auto pump = roleState(c.cfg, "tank.circulation");
  if (pump && !*pump) dryOffSeen_ = true;  // from now on, running again means it did not stay off
  const bool back = selfReleasing(l->second, tank) && c.cfg.binding("tank.level") && level.usable() && *level.value >= dryReleaseLevel(tank) &&
                    (!isNum(tank.capacityL) || *level.value <= tank.capacityL) && c.epoch >= c.maintenanceUntil && pump && !*pump;
  if (!back) {
    dryBackSince_.reset();
    return;
  }
  if (!dryBackSince_) dryBackSince_ = c.now;
  if (c.now - *dryBackSince_ < kDrySettle) return;
  dryBackSince_.reset();
  c.rt.latches.erase(l);
  c.rt.selfReleased["circulation.dry"] = c.epoch;
  c.log.add(c.epoch, "unblock", "info", say("ev.circ.dry.released"),
            say("ev.circ.dry.released.text", {{"level", *level.value}, {"back", dryReleaseLevel(tank)}}), {{"role", "tank.circulation"}});
}

void Actuators::enforce(const Ctx& c) {
  const auto& level = c.truth.get("tank.level");
  const bool levelBound = c.cfg.binding("tank.level") != nullptr;
  const auto& tank = c.cfg.tank();

  // Gemeldete Abschaltungen abschließen. Umgehängt oder gelöst: ohne
  // Entwarnung (bindRole und unbindRole melden einen alten Ausgang, der
  // nicht aus ging); onSince_ bleibt, die Höchstlaufzeit räumt es selbst ab.
  // Als aus gelesen: war das Ausschalten gescheitert, jetzt bestätigen.
  for (auto it = cuts_.begin(); it != cuts_.end();) {
    const std::string& role = it->first;
    const Binding* b = c.cfg.binding(role);
    if (!b || b->device != it->second.device || b->channel != it->second.channel) {
      it = cuts_.erase(it);
      continue;
    }
    auto st = swState(b->device, b->channel);
    if (!st || *st) {
      ++it;
      continue;
    }
    if (it->second.failed) {
      c.log.add(c.epoch, "block", "info", say("ev.off_confirmed", {{"label", roleLabel(c.cat, role)}}), say("ev.off_confirmed.text"),
                {{"role", role}});
      offSince_[role] = c.now;
    }
    onSince_.erase(role);
    it = cuts_.erase(it);
  }

  // Trockenlaufschutz: schaltet nur aus. Läuft die Pumpe beim Unterschreiten,
  // rastet die Sperre und meldet; war sie aus, gilt nur die Einschaltsperre
  // (Quelle: RAT-062, Testfälle M11-1/M11-2/M11-4). Läuft sie trotz
  // Rastung (Ausschalten gescheitert, Taster am Gerät), schaltet der Hub sie
  // erneut aus.
  releaseDryLatch(c);
  auto circ = roleState(c.cfg, "tank.circulation");
  const bool dryLatched = c.rt.latches.count("circulation.dry") > 0;
  if (circ && *circ && (levelBound || dryLatched)) {
    const bool low = levelBound && level.usable() && isNum(tank.minL) && *level.value < tank.minL;
    bool ok = true, acted = true;
    if (low || dryLatched) {
      if (!dryLatched) {
        // The cause is stored, so the latch can release itself once the tank is refilled
        // (SD-022); low again within the hour after that, a person looks first (SD-067).
        json latch = {{"at", c.epoch}, {"levelL", *level.value}, {"cause", "level"}};
        auto last = c.rt.selfReleased.find("circulation.dry");
        if (last != c.rt.selfReleased.end() && (!plausibleEpoch(last->second) || c.epoch - last->second < kDryRepeatS)) latch["hold"] = "repeat";
        c.rt.latches["circulation.dry"] = latch;
        rearm("tank.circulation", "circulation.dry");  // neue Rastung, z. B. nach Quittierung: neu melden
      }
      json& latch = c.rt.latches["circulation.dry"];
      // Running again after it was read as off (a button on the device, a relay that does not
      // hold): it may have run dry meanwhile, so a person looks (SD-067)
      if (dryLatched && dryOffSeen_ && latch.is_object()) latch["hold"] = "off_failed";
      const bool self = levelBound && selfReleasing(latch, tank);
      // If switching off fails, the alarm asks for a person instead: the pump may be running dry
      Msg text, failed;
      if (low) {
        const json args = {{"level", *level.value}, {"min", tank.minL}, {"back", dryReleaseLevel(tank)}};
        const bool repeat = holdReason(latch) == "repeat";
        text = say(self ? "ev.circ.dry.low_self" : repeat ? "ev.circ.dry.low_repeat" : "ev.circ.dry.low", args);
        failed = say("ev.circ.dry.low", args);
      } else {
        text = self ? say("ev.circ.dry.latched_self", {{"back", dryReleaseLevel(tank)}}) : say("ev.circ.dry.latched");
        failed = say("ev.circ.dry.latched");
      }
      ok = cut(c, "tank.circulation", "circulation.dry", "alarm", "alarm", say("ev.circ.dry"), text, &failed);
      if (!ok && latch.is_object()) latch["hold"] = "off_failed";  // overrides another reason: check the pump first
    } else if (!level.usable()) {
      ok = cut(c, "tank.circulation", "circulation.level", "block", "warn", say("ev.circ.off"), say("ev.circ.off.text", {{"reason", level.reason}}));
    } else {
      acted = false;
    }
    if (acted) {
      if (ok) onSince_.erase("tank.circulation");
      // Ohne Durchmischung keine Regel-Dosierung (RAT-051).
      if (runningPurpose_ == "ph" || runningPurpose_ == "ec") bus_.stopAllPumps();
    }
  }

  // Zulauf: Notgrenze folgt dem Ventil, nicht dem Auftrag (RAT-032); nach einem
  // Fehler kein automatischer Neuanlauf (RAT-031). Offen trotz Rastung →
  // erneut zu.
  auto inlet = roleState(c.cfg, "tank.inlet");
  if (inlet && *inlet) {
    std::optional<Msg> why;  // je Grund ein eigener Meldeschlüssel
    std::string key;
    if (!level.usable()) {
      why = say("why.level_invalid");
      key = "inlet.level";
    } else if (isNum(tank.capacityL) && *level.value >= tank.capacityL) {
      why = say("why.capacity", {{"level", *level.value}});
      key = "inlet.capacity";
    } else {
      auto p = effectiveParams(c.cat, c.cfg, "refill");
      double maxOpen = p.num("max_open_min");
      auto since = onSince_.find("tank.inlet");
      if (isNum(maxOpen) && since != onSince_.end() && c.now - since->second > static_cast<Ms>(maxOpen * kMinute)) {
        why = say("why.open_too_long", {{"min", maxOpen}});
        key = "inlet.open";
      }
    }
    auto latch = c.rt.latches.find("inlet.fault");
    if (why) {
      // Grund und Zeitpunkt werden beim Rasten eingefroren (RAT-062); ein
      // späterer Grund steht in seiner eigenen Meldung. The reason is kept
      // as a message, so it shows in the page language (SD-032).
      if (latch == c.rt.latches.end()) {
        c.rt.latches["inlet.fault"] = {{"at", c.epoch}, {"why", *why}};
        rearm("tank.inlet", key);
      }
      if (cut(c, "tank.inlet", key, "alarm", "alarm", say("ev.inlet.cutoff"), say("ev.inlet.cutoff.text", {{"why", *why}})))
        onSince_.erase("tank.inlet");
      noteReported("tank.inlet", "inlet.latched");  // Fortsetzung über die Rastung ist damit gemeldet
    } else if (latch != c.rt.latches.end()) {
      // Grund weg, Rastung steht: offen trotz Rastung (Ausschalten gescheitert, Taster) → erneut zu
      // A reason saved before SD-032 is plain German text; it stays as it is.
      const json& l = latch->second;
      const json was = l.is_object() && l.contains("why") && (l["why"].is_string() || l["why"].is_object()) ? l["why"] : json(say("ev.inlet.cutoff"));
      if (cut(c, "tank.inlet", "inlet.latched", "alarm", "alarm", say("ev.inlet.cutoff"), say("ev.inlet.cutoff.latched", {{"why", was}})))
        onSince_.erase("tank.inlet");
    }
  }

  // Gießpumpe im Lauf: Füllstand ungültig oder unter dem Mindestfüllstand →
  // aus (Trockenlauf), mit Meldung.
  auto irr = roleState(c.cfg, "zone.irrigation_pump");
  if (irr.value_or(false)) {
    std::optional<Msg> why;
    if (!levelBound || !level.usable()) why = say("why.level_invalid");
    else if (isNum(tank.minL) && *level.value < tank.minL) why = say("why.irrigation_low", {{"level", *level.value}});
    if (why && cut(c, "zone.irrigation_pump", "irrigation.level", "block", "warn", say("ev.irrigation.off"), say("ev.irrigation.off.text", {{"why", *why}}))) {
      onSince_.erase("zone.irrigation_pump");
      offSince_["zone.irrigation_pump"] = c.now;
    }
  }

  // Höchstlaufzeit je Rolle (Profile puls und heizen). Das Gerät schaltet
  // knapp danach selbst ab (Auto-Off), falls der Hub ausfällt.
  for (const auto& [role, rd] : c.cat.roles) {
    if (!isNum(rd.maxOnS)) continue;
    auto since = onSince_.find(role);
    if (since == onSince_.end() || c.now - since->second <= static_cast<Ms>(rd.maxOnS * kSecond)) continue;
    // Gelöst oder schon aus (z. B. Auto-Off im Gerät): nichts mehr zu schalten.
    auto st = roleState(c.cfg, role);
    if (!c.cfg.binding(role) || (st && !*st)) {
      onSince_.erase(since);
      continue;
    }
    // Scheitert das Ausschalten, bleibt onSince_ stehen: der nächste Takt versucht es erneut.
    if (cut(c, role, "maxon", "block", "warn", say("ev.max_on", {{"label", rd.label}}), say("ev.max_on.text", {{"min", rd.maxOnS / 60}}))) {
      onSince_.erase(role);
      offSince_[role] = c.now;
    }
  }
}

// ---------------------------------------------------------------- Doser

std::string Doser::busJob() const {
  std::string job = active_->id + "#" + std::to_string(progress_.run + 1);
  return bootTag_.empty() ? job : job + "." + bootTag_;
}

bool Doser::launch(const Ctx& c, Actuators& act) {
  const auto& o = *active_;
  Ms ms = o.step.runs[progress_.run];
  Msg e;
  if (!act.startRun(c, o.step.pump, ms, o.purpose, busJob(), e)) {
    progress_.error = e;
    return false;
  }
  running_ = true;
  runStartedAt_ = c.now;
  runRequestedMs_ = ms;
  return true;
}

void Doser::fail(const Ctx& c, Msg error) {
  progress_.state = DoseProgress::State::Failed;
  progress_.error = std::move(error);
  logOrder(c);
  finished_ = progress_;
  active_.reset();
}

bool Doser::start(const Ctx& c, Actuators& act, DoseOrder order, Msg& err) {
  if (active_) {
    err = say("dose.busy");
    return false;
  }
  if (order.step.runs.empty()) {
    err = say("dose.empty");
    return false;
  }
  order.id = order.id.empty() ? "d" + std::to_string(++seq_) : order.id;
  active_ = order;
  progress_ = DoseProgress{};
  progress_.state = DoseProgress::State::Running;
  progress_.orderId = active_->id;
  running_ = false;
  pauseUntil_ = 0;
  if (!launch(c, act)) {
    err = progress_.error;
    active_.reset();
    progress_.state = DoseProgress::State::Idle;
    return false;
  }
  return true;
}

void Doser::book(const Ctx& c, Ms ms) {
  const auto& o = *active_;
  double ml = isNum(o.step.flowMlPerMin) ? o.step.flowMlPerMin * static_cast<double>(ms) / 60000.0 : kNaN;
  progress_.msDone += ms;
  if (isNum(ml)) progress_.mlDone += ml;
  if (o.purpose == "calibration" || o.purpose == "prime") return;  // kein Verbrauch (RAT-070)
  // Vorrat je Lauf buchen, nicht erst am Ende (RAT-070): ein Abbruch fehlt sonst
  auto st = c.rt.stockMl.find(o.step.canister);
  if (st == c.rt.stockMl.end() || !isNum(st->second) || !isNum(ml)) return;
  // Booked as it went, also below zero: then the bottle ran dry, or the
  // stock entered or the pump's calibration is wrong. Reported each time it
  // falls below zero, never hidden as 0 (RAT-015, SD-066); a rounding
  // difference alone does not count (kStockBelowZeroMl).
  const bool wasCovered = st->second > kStockBelowZeroMl;
  st->second -= ml;
  if (wasCovered && st->second <= kStockBelowZeroMl)
    c.log.add(c.epoch, "tank", "warn", say("ev.stock.below_zero"), say("ev.stock.below_zero.text", {{"name", o.step.name}}),
              {{"canister", o.step.canister}, {"stockMl", st->second}});
}

void Doser::logOrder(const Ctx& c) {
  const auto& o = *active_;
  if (o.purpose == "calibration" || o.purpose == "prime") return;
  if (progress_.state == DoseProgress::State::Aborted && progress_.msDone <= 0) return;  // nichts gelaufen
  bool ok = progress_.state == DoseProgress::State::Done;
  c.log.add(c.epoch, "dose", ok ? "info" : "warn", say(ok ? "ev.dose" : "ev.dose_incomplete", {{"name", o.step.name}, {"ml", progress_.mlDone}}),
            purposeLabel(o.purpose),
            {{"canister", o.step.canister},
             {"pump", o.step.pump},
             {"ml", progress_.mlDone},
             {"mlPlanned", numOrNull(o.step.ml)},
             {"ms", progress_.msDone},
             {"runs", progress_.run},
             {"purpose", o.purpose},
             {"order", o.id}});
}

void Doser::tick(const Ctx& c, Actuators& act) {
  if (!active_) return;
  const auto& o = *active_;
  if (running_) {
    RunStatus st = act.runStatus(o.step.pump);
    const bool mine = st.jobId == busJob();
    // Fehler ohne Job-ID: Gerät getrennt oder nicht erreichbar.
    const bool lost = !mine && st.state == RunStatus::State::Failed && st.jobId.empty();
    if (!lost && (!mine || st.state == RunStatus::State::Running)) {
      const Ms deadline = runStartedAt_ + runRequestedMs_ + std::max<Ms>(5 * kSecond, runRequestedMs_ / 5);
      if (c.now <= deadline) return;
      act.stopPumps();
      act.clearRun(o.step.pump);
      running_ = false;
      book(c, mine && st.actualMs > 0 ? st.actualMs : runRequestedMs_);
      fail(c, say("dose.no_response", {{"name", o.step.name}}));
      return;
    }
    act.clearRun(o.step.pump);
    running_ = false;
    if (lost) {
      act.stopPumps();
      book(c, std::clamp<Ms>(c.now - runStartedAt_, 0, runRequestedMs_));
      const json reason = st.error.empty() ? json(say("dose.disconnected")) : json(st.error);
      fail(c, say("dose.lost", {{"name", o.step.name}, {"reason", reason}}));
      return;
    }
    if (st.state == RunStatus::State::Done) {
      book(c, st.actualMs);
      progress_.run++;
      if (progress_.run >= o.step.runs.size()) {
        progress_.state = DoseProgress::State::Done;
        logOrder(c);
        finished_ = progress_;
        active_.reset();
        return;
      }
      pauseUntil_ = c.now + 3 * kSecond;  // Pause zwischen Teilläufen (RAT-055)
      return;
    }
    if (st.actualMs > 0) book(c, st.actualMs);
    progress_.state = DoseProgress::State::Failed;
    const json reason = st.error.empty() ? json(say("dose.run_cancelled")) : json(st.error);
    progress_.error = say("dose.failed", {{"name", o.step.name}, {"reason", reason}});
    logOrder(c);
    finished_ = progress_;
    active_.reset();
    return;
  }
  if (c.now < pauseUntil_) return;
  if (!launch(c, act)) {
    progress_.state = DoseProgress::State::Failed;
    logOrder(c);
    finished_ = progress_;
    active_.reset();
  }
}

void Doser::abort(const Ctx& c, Actuators& act, const Msg& reason) {
  if (!active_) return;
  act.stopPumps();
  if (running_) {
    RunStatus st = act.runStatus(active_->step.pump);
    Ms ran = st.jobId == busJob() ? st.actualMs : std::clamp<Ms>(c.now - runStartedAt_, 0, runRequestedMs_);
    if (ran > 0) book(c, ran);
  }
  act.clearRun(active_->step.pump);
  running_ = false;
  progress_.state = DoseProgress::State::Aborted;
  progress_.error = say("dose.aborted", {{"name", active_->step.name}, {"reason", reason}});
  logOrder(c);
  finished_ = progress_;
  active_.reset();
}

void Doser::drop(Actuators& act) {
  if (active_) act.clearRun(active_->step.pump);
  active_.reset();
  running_ = false;
  finished_.reset();
  try {  // a repeat: the Stop switched the pumps off just before
    act.stopPumps();
  } catch (...) {
  }
}

std::optional<DoseProgress> Doser::takeFinished() {
  auto f = finished_;
  finished_.reset();
  return f;
}

void to_json(json& j, const Job& job) {
  json steps = json::array();
  for (const auto& s : job.steps)
    steps.push_back({{"name", s.dose.name},
                     {"canister", s.dose.canister},
                     {"color", s.dose.color},
                     {"pair", s.dose.pair},
                     {"ml", numOrNull(s.dose.ml)},
                     {"mlDone", s.mlDone},
                     {"state", s.state}});
  j = {{"id", job.id},          {"type", job.type},       {"state", job.state},
       {"index", job.index},    {"steps", steps},         {"message", job.message},
       {"startedAt", job.startedAt}, {"finishedAt", job.finishedAt}, {"guided", job.guided},
       {"info", job.info}};
}

}  // namespace gc
