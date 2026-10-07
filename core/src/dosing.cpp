// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/dosing.hpp"

#include <algorithm>

namespace gc {

namespace {

Msg msg(const std::string& key, const std::string& text, json args = json::object()) {
  return {key, text, std::move(args)};
}

const char* purposeLabel(const std::string& p) {
  if (p == "mix") return "Mischlauf";
  if (p == "manual") return "Handgabe";
  if (p == "ec") return "EC-Nachdosierung";
  if (p == "ph") return "pH-Korrektur";
  if (p == "calibration") return "Einmessen";
  if (p == "prime") return "Schlauch füllen";
  return "Dosierung";
}

constexpr double kInletHysteresisL = 0.5;  // EIN erst 0,5 L über der AUS-Grenze (RAT-078: 2,5/3 L)

}  // namespace

// ---------------------------------------------------------------- Actuators

bool Actuators::startRun(const Ctx& c, const std::string& pump, Ms ms, const std::string& purpose,
                         const std::string& jobId, Msg& err) {
  auto fail = [&](const std::string& key, const std::string& text) {
    err = msg(key, text);
    return false;
  };
  if (c.stopped) return fail("act.stopped", "Not-Halt aktiv – erst fortsetzen");
  if (!runningPump_.empty()) return fail("act.busy", "Es läuft bereits eine Pumpe");
  auto it = c.pumps.find(pump);
  if (it == c.pumps.end() || !it->second.online) return fail("act.offline", "Pumpe nicht erreichbar");
  if (!it->second.fault.empty()) return fail("act.fault", "Pumpe meldet einen Fehler: " + it->second.fault);
  const bool noFlowNeeded = purpose == "calibration" || purpose == "prime";
  if (!noFlowNeeded && !(isNum(it->second.flowMlPerMin) && it->second.flowMlPerMin > 0))
    return fail("act.uncalibrated", "Pumpe nicht eingemessen – ohne Einmesswert wird nicht dosiert");
  const Limits lim = c.cfg.limits.bounded();  // feste Grenzen, Konfiguration verschärft nur (R7)
  if (!noFlowNeeded && ms < static_cast<Ms>(lim.minRunS * 1000))
    return fail("act.too_short", "Lauf unter " + fmt(lim.minRunS, 1) + " s – zu ungenau");
  if (ms <= 0 || ms > static_cast<Ms>(lim.maxRunS * 1000))
    return fail("act.too_long", "Lauf über " + fmt(lim.maxRunS, 0) + " s – in Teilgaben teilen");
  if (purpose == "ph" || purpose == "ec") {
    auto st = roleState(c.cfg, "tank.circulation");
    if (!st || !*st) return fail("act.no_mixing", "Ohne Durchmischung keine Dosierung");
  }
  std::string e;
  if (!bus_.startRun(pump, ms, jobId, e)) return fail("act.bus", "Dosierblock lehnt ab: " + e);
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
    if (c.rt.latches.count("circulation.dry"))
      return msg("act.circ.latched", "Gerastet: Umwälzpumpe wegen Trockenlauf abgeschaltet – Tank füllen, dann quittieren");
    if (levelBound) {
      if (!level.usable()) return msg("act.circ.level_invalid", "Füllstand ungültig – Umwälzpumpe bleibt aus");
      if (isNum(tank.minL) && *level.value < tank.minL + kInletHysteresisL)
        return msg("act.circ.low", "Füllstand " + fmt(*level.value, 1) + " L unter " + fmt(tank.minL + kInletHysteresisL, 1) +
                                       " L – Trockenlaufschutz");
    }
    return std::nullopt;
  }
  if (role == "tank.inlet") {
    if (c.rt.latches.count("inlet.fault"))
      return msg("act.inlet.latched", "Gerastet: Zulauf nach einem Fehler gesperrt – prüfen, dann quittieren");
    if (!levelBound || !level.usable())
      return msg("act.inlet.level", "Ohne gültigen Füllstand kein Zulauf (Notabschaltung fehlt)");
    if (isNum(tank.capacityL) && *level.value >= tank.capacityL) return msg("act.inlet.full", "Tank ist voll");
    if (!isNum(tank.capacityL)) return msg("act.inlet.capacity", "Nutzvolumen des Tanks fehlt");
    return std::nullopt;
  }
  // Befeuchter und Entfeuchter nie zugleich (R7, Quelle: RAT-034). Ist der
  // Zustand des Gegengeräts unbekannt (z. B. Dose nicht erreichbar), könnte es
  // noch laufen: dann ebenfalls gesperrt (R5).
  auto pairBlock = [&](const char* other, const std::string& label) -> std::optional<Msg> {
    if (!c.cfg.binding(other)) return std::nullopt;
    auto st = roleState(c.cfg, other);
    if (!st) return msg("act.climate.pair_unknown", label + " nicht erreichbar – könnte noch laufen");
    if (*st) return msg("act.climate.pair", label + " läuft – bleibt aus");
    return std::nullopt;
  };
  if (role == "zone.humidifier")
    if (auto m = pairBlock("zone.dehumidifier", "Entfeuchter")) return m;
  if (role == "zone.dehumidifier")
    if (auto m = pairBlock("zone.humidifier", "Befeuchter")) return m;
  // Befeuchter: bei zugeordneter Feuchte nur mit gültigem Wert und unter der
  // Obergrenze (Kondensat an der Elektrik).
  if (role == "zone.humidifier" && c.cfg.binding("zone.humidity")) {
    const auto& rh = c.truth.get("zone.humidity");
    if (!rh.usable()) return msg("act.humidifier.rh", "Luftfeuchte ungültig – Befeuchter bleibt aus");
    if (*rh.value >= kHumidifierMaxRh)
      return msg("act.humidifier.rh", "Luftfeuchte " + fmt(*rh.value, 0) + " % – über " + fmt(kHumidifierMaxRh, 0) + " % kein Befeuchter");
  }
  // Irrigation pump: only with a valid level above the minimum level,
  // otherwise it runs dry. An unreadable level blocks (deviation noted in
  // RAT-068; confirmed by SD-017, which adds an emergency dose, irrigation
  // without a level sensor and dry-run detection, not implemented yet). The current level counts, not a
  // predicted one.
  if (role == "zone.irrigation_pump") {
    if (!levelBound || !level.usable()) return msg("act.irrigation.level", "Ohne gültigen Füllstand keine Gießpumpe (Trockenlauf)");
    if (!isNum(tank.minL)) return msg("act.irrigation.level", "Mindestfüllstand des Tanks fehlt");
    if (*level.value < tank.minL + kInletHysteresisL)
      return msg("act.irrigation.level", "Füllstand " + fmt(*level.value, 1) + " L zu niedrig für die Gießpumpe");
  }
  // Kompressor: Mindestpause für den Druckausgleich (Quelle: RAT-034).
  // Den Mindestlauf hält die Funktion ein; Ausschalten geht immer.
  if (const RoleDef* rd = c.cat.role(role); rd && rd->profile == "kompressor") {
    auto off = offSince_.find(role);
    if (off != offSince_.end() && c.now - off->second < kCompressorPause)
      return msg("act.compressor.pause", rd->label + ": Mindestpause " + fmt(kCompressorPause / kMinute, 0) + " min nach dem Ausschalten");
  }
  return std::nullopt;
}

bool Actuators::setRole(const Ctx& c, const std::string& role, bool on, const std::string& who, Msg& err) {
  const Binding* b = c.cfg.binding(role);
  if (!b) {
    err = msg("act.unbound", "Ausgang nicht zugeordnet");
    return false;
  }
  if (on) {
    if (c.stopped) {
      err = msg("act.stopped", "Not-Halt aktiv – erst fortsetzen");
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
        err = msg("act.net.unreachable", "Dose im Netzwerk nicht erreichbar");
        return false;
      }
      if (!sameSafety(*got, safetyForRole(*rd))) {
        err = msg("act.net.safety", "Schutzeinstellung im Gerät fehlt oder weicht ab – Dose neu zuordnen");
        return false;
      }
    }
  }
  auto cur = swState(b->device, b->channel);
  if (cur && *cur == on) return true;
  std::string e;
  if (!sw(b->device, b->channel, on, e)) {
    err = msg("act.bus", "Ausgang lehnt ab: " + e);
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
    c.log.add(c.epoch, "tank", "info", on ? "Zulauf auf" : "Zulauf zu", who, {{"role", role}, {"on", on}});
  return true;
}

void Actuators::stopAll(const Catalog& cat, const Config& cfg, Ms now) {
  bus_.stopAllPumps();
  cfg.forEachBinding([&](const std::string& role, const Binding& b) {
    // Alle Schaltrollen aus, auch wenn der Zustand unbekannt ist (Schaltbox
    // oder Dose nicht lesbar): der Befehl kostet nichts. Messrollen nicht.
    const RoleDef* rd = cat.role(role);
    if (!rd || rd->profile.empty()) return;
    std::string e;
    sw(b.device, b.channel, false, e);
    offSince_[role] = now;  // Mindestpausen gelten auch nach Not-Halt und Neustart
  });
  onSince_.clear();
}

bool Actuators::cut(const Ctx& c, const std::string& role, const std::string& key, const std::string& type,
                    const std::string& severity, const std::string& title, const std::string& text) {
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
      const RoleDef* rd = c.cat.role(role);
      c.log.add(c.epoch, "block", "alarm", (rd ? rd->label : role) + ": Aus nicht bestätigt",
                text + " Ausschalten gescheitert: " + e + ".", {{"role", role}, {"device", b->device}});
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
      const RoleDef* rd = c.cat.role(role);
      c.log.add(c.epoch, "block", "info", (rd ? rd->label : role) + ": Aus bestätigt", "Der Ausgang ist jetzt aus.", {{"role", role}});
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
  auto circ = roleState(c.cfg, "tank.circulation");
  const bool dryLatched = c.rt.latches.count("circulation.dry") > 0;
  if (circ && *circ && (levelBound || dryLatched)) {
    const bool low = levelBound && level.usable() && isNum(tank.minL) && *level.value < tank.minL;
    bool ok = true, acted = true;
    if (low || dryLatched) {
      if (!dryLatched) {
        c.rt.latches["circulation.dry"] = {{"at", c.epoch}, {"levelL", *level.value}};
        rearm("tank.circulation", "circulation.dry");  // neue Rastung, z. B. nach Quittierung: neu melden
      }
      ok = cut(c, "tank.circulation", "circulation.dry", "alarm", "alarm", "Umwälzpumpe aus: Trockenlauf",
               low ? "Füllstand " + fmt(*level.value, 1) + " L unter " + fmt(tank.minL, 1) + " L. Gerastet bis zur Quittierung."
                   : std::string("Trockenlauf gerastet, noch nicht quittiert."));
    } else if (!level.usable()) {
      ok = cut(c, "tank.circulation", "circulation.level", "block", "warn", "Umwälzpumpe aus", "Füllstand ungültig: " + level.reason.text + ".");
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
    std::string why, key;  // je Grund ein eigener Meldeschlüssel
    if (!level.usable()) {
      why = "Füllstand ungültig";
      key = "inlet.level";
    } else if (isNum(tank.capacityL) && *level.value >= tank.capacityL) {
      why = "Notgrenze erreicht bei " + fmt(*level.value, 1) + " L";
      key = "inlet.capacity";
    } else {
      auto p = effectiveParams(c.cat, c.cfg, "refill");
      double maxOpen = p.num("max_open_min");
      auto since = onSince_.find("tank.inlet");
      if (isNum(maxOpen) && since != onSince_.end() && c.now - since->second > static_cast<Ms>(maxOpen * kMinute)) {
        why = "Ventil länger als " + fmt(maxOpen, 0) + " min offen";
        key = "inlet.open";
      }
    }
    auto latch = c.rt.latches.find("inlet.fault");
    if (!why.empty()) {
      // Grund und Zeitpunkt werden beim Rasten eingefroren (RAT-062); ein
      // späterer Grund steht in seiner eigenen Meldung.
      if (latch == c.rt.latches.end()) {
        c.rt.latches["inlet.fault"] = {{"at", c.epoch}, {"why", why}};
        rearm("tank.inlet", key);
      }
      if (cut(c, "tank.inlet", key, "alarm", "alarm", "Zulauf-Notabschaltung", why + ". Gerastet bis zur Quittierung."))
        onSince_.erase("tank.inlet");
      noteReported("tank.inlet", "inlet.latched");  // Fortsetzung über die Rastung ist damit gemeldet
    } else if (latch != c.rt.latches.end()) {
      // Grund weg, Rastung steht: offen trotz Rastung (Ausschalten gescheitert, Taster) → erneut zu
      const json& l = latch->second;
      const std::string was = l.is_object() && l.contains("why") && l["why"].is_string() ? l["why"].get<std::string>() : "Zulauf-Notabschaltung";
      if (cut(c, "tank.inlet", "inlet.latched", "alarm", "alarm", "Zulauf-Notabschaltung", "Gerastet: " + was + ", noch nicht quittiert."))
        onSince_.erase("tank.inlet");
    }
  }

  // Gießpumpe im Lauf: Füllstand ungültig oder unter dem Mindestfüllstand →
  // aus (Trockenlauf), mit Meldung.
  auto irr = roleState(c.cfg, "zone.irrigation_pump");
  if (irr.value_or(false)) {
    std::string why;
    if (!levelBound || !level.usable()) why = "Füllstand ungültig";
    else if (isNum(tank.minL) && *level.value < tank.minL) why = "Füllstand " + fmt(*level.value, 1) + " L unter dem Mindestfüllstand";
    if (!why.empty() && cut(c, "zone.irrigation_pump", "irrigation.level", "block", "warn", "Gießpumpe aus: Trockenlaufschutz", why + ".")) {
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
    if (cut(c, role, "maxon", "block", "warn", rd.label + " aus: Höchstlaufzeit", "Höchstlaufzeit " + fmt(rd.maxOnS / 60, 0) + " min erreicht.")) {
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
    err = msg("dose.busy", "Es läuft bereits eine Dosierung");
    return false;
  }
  if (order.step.runs.empty()) {
    err = msg("dose.empty", "Nichts zu dosieren");
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
  if (st != c.rt.stockMl.end() && isNum(st->second) && isNum(ml)) st->second = std::max(0.0, st->second - ml);
}

void Doser::logOrder(const Ctx& c) {
  const auto& o = *active_;
  if (o.purpose == "calibration" || o.purpose == "prime") return;
  if (progress_.state == DoseProgress::State::Aborted && progress_.msDone <= 0) return;  // nichts gelaufen
  bool ok = progress_.state == DoseProgress::State::Done;
  c.log.add(c.epoch, "dose", ok ? "info" : "warn",
            o.step.name + " · " + fmt(progress_.mlDone, 1) + " ml" + (ok ? "" : " (unvollständig)"), purposeLabel(o.purpose),
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
      fail(c, msg("dose.no_response",
                  o.step.name + ": keine Rückmeldung vom Dosierblock – Pumpe abgeschaltet, Menge als gelaufen gezählt"));
      return;
    }
    act.clearRun(o.step.pump);
    running_ = false;
    if (lost) {
      act.stopPumps();
      book(c, std::clamp<Ms>(c.now - runStartedAt_, 0, runRequestedMs_));
      fail(c, msg("dose.lost", o.step.name + ": " + (st.error.empty() ? "Pumpe getrennt" : st.error) +
                                   " – Menge aus der Laufzeit geschätzt"));
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
    progress_.error = msg("dose.failed", o.step.name + ": " + (st.error.empty() ? "Lauf abgebrochen" : st.error));
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

void Doser::abort(const Ctx& c, Actuators& act, const std::string& reason) {
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
  progress_.error = msg("dose.aborted", active_->step.name + ": " + reason);
  logOrder(c);
  finished_ = progress_;
  active_.reset();
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
