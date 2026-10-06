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
  // Gießpumpe: nur mit gültigem Füllstand über dem Mindestfüllstand, sonst
  // läuft sie trocken (Quelle: RAT-067).
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
  } else {
    onSince_.erase(role);
    offSince_[role] = c.now;
  }
  if (role == "tank.inlet")
    c.log.add(c.epoch, "tank", "info", on ? "Zulauf auf" : "Zulauf zu", who, {{"role", role}, {"on", on}});
  return true;
}

void Actuators::stopAll(const Config& cfg, Ms now) {
  bus_.stopAllPumps();
  cfg.forEachBinding([&](const std::string& role, const Binding& b) {
    std::string e;
    // Schaltrollen immer aus, auch wenn der Zustand unbekannt ist (Schaltbox
    // oder Dose nicht lesbar): der Befehl kostet nichts.
    auto st = swState(b.device, b.channel);
    if (st || role.rfind("tank.", 0) == 0 || role.rfind("zone.", 0) == 0) sw(b.device, b.channel, false, e);
    offSince_[role] = now;  // Mindestpausen gelten auch nach Not-Halt und Neustart
  });
  onSince_.clear();
}

void Actuators::enforce(const Ctx& c) {
  const auto& level = c.truth.get("tank.level");
  const bool levelBound = c.cfg.binding("tank.level") != nullptr;
  const auto& tank = c.cfg.tank();
  std::string e;

  // Trockenlaufschutz: schaltet nur aus. Läuft die Pumpe beim Unterschreiten,
  // rastet die Sperre und meldet; war sie aus, gilt nur die Einschaltsperre
  // (Quelle: RAT-062, Testfälle M11-1/M11-2/M11-4).
  auto circ = roleState(c.cfg, "tank.circulation");
  if (circ && *circ && levelBound) {
    const Binding* b = c.cfg.binding("tank.circulation");
    bool low = level.usable() && isNum(tank.minL) && *level.value < tank.minL;
    if (!level.usable() || low) {
      sw(b->device, b->channel, false, e);
      onSince_.erase("tank.circulation");
      if (low) {
        c.rt.latches["circulation.dry"] = {{"at", c.epoch}, {"levelL", *level.value}};
        c.log.add(c.epoch, "alarm", "alarm", "Umwälzpumpe aus: Trockenlauf",
                  "Füllstand " + fmt(*level.value, 1) + " L unter " + fmt(tank.minL, 1) + " L. Gerastet bis zur Quittierung.");
      } else {
        c.log.add(c.epoch, "block", "warn", "Umwälzpumpe aus", "Füllstand ungültig: " + level.reason.text);
      }
      // Ohne Durchmischung keine Regel-Dosierung (RAT-051).
      if (runningPurpose_ == "ph" || runningPurpose_ == "ec") bus_.stopAllPumps();
    }
  }

  // Zulauf: Notgrenze folgt dem Ventil, nicht dem Auftrag (RAT-032); nach einem
  // Fehler kein automatischer Neuanlauf (RAT-031).
  auto inlet = roleState(c.cfg, "tank.inlet");
  if (inlet && *inlet) {
    const Binding* b = c.cfg.binding("tank.inlet");
    std::string why;
    if (!level.usable()) why = "Füllstand ungültig";
    else if (isNum(tank.capacityL) && *level.value >= tank.capacityL) why = "Notgrenze erreicht (" + fmt(*level.value, 1) + " L)";
    else {
      auto p = effectiveParams(c.cat, c.cfg, "refill");
      double maxOpen = p.num("max_open_min");
      auto since = onSince_.find("tank.inlet");
      if (isNum(maxOpen) && since != onSince_.end() && c.now - since->second > static_cast<Ms>(maxOpen * kMinute))
        why = "Ventil länger als " + fmt(maxOpen, 0) + " min offen";
    }
    if (!why.empty()) {
      sw(b->device, b->channel, false, e);
      onSince_.erase("tank.inlet");
      c.rt.latches["inlet.fault"] = {{"at", c.epoch}, {"why", why}};
      c.log.add(c.epoch, "alarm", "alarm", "Zulauf-Notabschaltung", why + ". Gerastet bis zur Quittierung.");
    }
  }

  // Höchstlaufzeit je Rolle (Profile puls und heizen). Das Gerät schaltet
  // knapp danach selbst ab (Auto-Off), falls der Hub ausfällt.
  for (const auto& [role, rd] : c.cat.roles) {
    if (!isNum(rd.maxOnS)) continue;
    auto since = onSince_.find(role);
    if (since == onSince_.end() || c.now - since->second <= static_cast<Ms>(rd.maxOnS * kSecond)) continue;
    const Binding* b = c.cfg.binding(role);
    if (b) sw(b->device, b->channel, false, e);
    onSince_.erase(role);
    offSince_[role] = c.now;
    c.log.add(c.epoch, "block", "warn", rd.label + " aus: Höchstlaufzeit", "Nach " + fmt(rd.maxOnS / 60, 0) + " min abgeschaltet.", {{"role", role}});
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
