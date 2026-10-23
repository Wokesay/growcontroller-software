// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/hub.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <set>
#include <sstream>
#include <string_view>

#include "gc/embedded.hpp"
#include "gc/messages.hpp"
#include "gc/sha256.hpp"

namespace gc {

Result Result::fail(int st, const Msg& m, json extra) {
  json b = {{"error", m}};
  for (auto& [k, v] : extra.items()) b[k] = v;
  return {st, b};
}

namespace {

// How long a mix waits for the user to stir by hand before it stops, open to go on (SD-057, SD-089)
constexpr Ms kStirWaitMax = 30 * kMinute;
// A pump that reads off or unknown this long while a mix mixes in stands still; a single missed
// report does not (an assumption, like the mixing time per volume)
constexpr Ms kPumpStandsAfter = 10 * kSecond;
constexpr const char* kConfigFile = "config.json";
constexpr const char* kStateFile = "state.json";
constexpr const char* kAuthFile = "auth.json";
constexpr const char* kEventsFile = "events.json";
constexpr const char* kHistoryFile = "history.bin";
constexpr const char* kJobFile = "job.json";
// Storage (#68): history and events are kept as a daily snapshot plus a
// journal that only grows, so an SD card is not rewritten for every value.
constexpr const char* kHistoryLog = "history.log";
constexpr const char* kEventsLog = "events.log";
// Copies of a broken file, written once at the start and never read
constexpr const char* kConfigBroken = "config.broken.json";
constexpr const char* kStateBroken = "state.broken.json";
constexpr const char* kHistoryBroken = "history.broken.bin";
constexpr Epoch kSnapshotS = 24 * 3600;
// Never more events in the journal than the log keeps, so failed logins
// (refused cheaply while locked) cannot fill the card.
constexpr std::uint64_t kJournalMaxEvents = 5000;
constexpr Epoch kMaxMaintenanceS = 240 * 60;  // longest maintenance window

// A raw text in an event (a name, a version, an error from a device): the
// same in both languages.
Msg plain(const std::string& text) { return say("ev.plain", {{"text", text}}); }

// What is in the tank: "Part A 12.0 ml, Part B 8.0 ml" as a list of
// messages, so each language writes the numbers its own way.
json amounts(const std::vector<JobStep>& steps) {
  json out = json::array();
  for (const auto& s : steps)
    if (s.mlDone > 0) out.push_back(say("amount", {{"name", s.dose.name}, {"ml", s.mlDone}}));
  return out;
}

// A released latch by name, as the web app's Release button shows it.
Msg latchName(const std::string& id) {
  if (id == "ec.no_effect") return say("latch.ec.no_effect");
  if (id == "ph.no_effect") return say("latch.ph.no_effect");
  if (id == "circulation.dry") return say("latch.circulation.dry");
  if (id == "inlet.fault") return say("latch.inlet.fault");
  return plain(id);
}

// What a sensor calibration was for (pH, EC or level).
Msg kindName(const std::string& kind) {
  if (kind == "ph") return say("kind.ph");
  if (kind == "ec") return say("kind.ec");
  if (kind == "tank_curve") return say("kind.tank_curve");
  return plain(kind);
}

// Kurzname für IDs: Kleinbuchstaben, Ziffern, Bindestrich; deutsche Umlaute
// umschrieben („Blüte“ → „bluete“), damit IDs lesbar bleiben.
std::string slug(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c == 0xC3 && i + 1 < s.size()) {
      const auto n = static_cast<unsigned char>(s[i + 1]);
      const char* rep = n == 0xA4 || n == 0x84 ? "ae" : n == 0xB6 || n == 0x96 ? "oe" : n == 0xBC || n == 0x9C ? "ue" : n == 0x9F ? "ss" : nullptr;
      if (rep) {
        out += rep;
        ++i;
        continue;
      }
    }
    if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    else if (!out.empty() && out.back() != '-') out += '-';
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  return out.empty() ? "x" : out.substr(0, 24);
}

// At most this many findings in one answer; `more` counts the rest
constexpr size_t kMaxFindings = 50;

Result errors(const std::vector<Msg>& errs) {
  json arr = json::array();
  for (size_t i = 0; i < errs.size() && i < kMaxFindings; ++i) arr.push_back(errs[i]);
  // The error is the first finding with its key and arguments, so a page in
  // the other language can show it in its own (SD-032); the rest follow.
  json first = errs.empty() ? json{{"key", "validation"}, {"text", ""}} : json(errs.front());
  json body = {{"error", first}, {"errors", arr}};
  if (errs.size() > kMaxFindings) body["more"] = errs.size() - kMaxFindings;
  return {422, body};
}

// A pair needs two: a bottle left alone with the label is no longer paired, otherwise every
// later check of the whole configuration refuses it (#147, #152). Returns its name, or "".
std::string unpairLone(Config& cfg, const std::string& pair) {
  if (pair.empty()) return "";
  std::vector<CanisterCfg*> rest;
  for (auto& k : cfg.canisters)
    if (k.pair == pair) rest.push_back(&k);
  if (rest.size() != 1) return "";
  rest[0]->pair.clear();
  return rest[0]->name;
}

// The first bottle of a new pair may stand alone until its partner is added, and a recipe may
// already hold it, so a pair can be built in any order (#152). A recipe left with half of a
// pair is still refused.
void allowLonePair(std::vector<Msg>& errs) {
  errs.erase(std::remove_if(errs.begin(), errs.end(), [](const Msg& m) { return m.key == "cfg.pair.single"; }), errs.end());
}

}  // namespace

Hub::Hub(const Catalog& cat, IBus& bus, IStorage& storage, const IClock& clock, RandomFn rng)
    : cat_(cat), bus_(bus), store_(storage), clock_(clock), rng_(rng), auth_(rng), truth_(cat), act_(bus) {}

std::string Hub::newId(const std::string& prefix) {
  std::uint8_t b[3];
  rng_(b, sizeof b);
  return prefix + "-" + toHex(b, sizeof b);
}

Ctx Hub::ctx() {
  // A calibration on the device of the tank's pH or EC probe, idle ones too: its readings are not the tank's
  bool probeCalibrating = false;
  for (const char* role : {"tank.ph", "tank.ec"})
    if (const Binding* b = cfg_.binding(role))
      for (const auto& [key, sess] : probeSessions_)
        probeCalibrating = probeCalibrating || jstr(sess, "device") == b->device;
  return Ctx{cat_, cfg_, rt_, truth_, log_, pumps_, clock_.nowMs(), clock_.epoch(), stopped_, maintenanceUntil_, probeCalibrating};
}

void Hub::logEvent(const std::string& type, const std::string& sev, Msg title, Msg text) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  log_.add(clock_.epoch(), type, sev, std::move(title), std::move(text));
  saveEvents();
}

// ------------------------------------------------------------------ Start

void Hub::boot() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  doser_.setBootTag(newId("b"));  // Bus-Job-IDs über Neustarts eindeutig
  // A file that is there but cannot be read (no permission, a read error) is
  // kept as it is: the hub starts as with a broken one, says so, and does not
  // write over it (SD-081). state.json is written anew at a Stop or Resume or
  // a new lock or latch, job.json by the next job. A snapshot and its journal
  // go together: neither is written while the other is kept, from the moment
  // it is read.
  const auto read = [&](const char* name) {
    std::optional<std::string> s = store_.read(name);
    if (!s && store_.unreadable(name)) {
      unreadable_.push_back(name);
      kept_.insert(name);
      for (const auto& [snapshot, journal] : {std::pair{kEventsFile, kEventsLog}, std::pair{kHistoryFile, kHistoryLog}})
        if (name == std::string(snapshot) || name == std::string(journal)) {
          kept_.insert(snapshot);
          kept_.insert(journal);
        }
    }
    return s;
  };
  // Every start writes its events and a state.json: events there, or kept
  // unread, mean the hub ran before (#238)
  bool ranBefore = false;
  if (auto s = read(kEventsFile)) {
    ranBefore = true;
    auto j = json::parse(*s, nullptr, false);
    if (!j.is_discarded()) log_.load(j);
  }
  if (auto s = read(kEventsLog)) {
    ranBefore = true;
    log_.replay(*s);
  }
  ranBefore = ranBefore || kept_.count(kEventsFile) || kept_.count(kEventsLog);
  savedEventId_ = log_.lastId();
  // Time base first (PD-069): without a secured time the clock continues
  // from the moment saved last; a newer event moves it forward by at most 1 h.
  const std::optional<std::string> stateRaw = read(kStateFile);
  const json stateJson = stateRaw ? json::parse(*stateRaw, nullptr, false) : json();
  const bool stateObj = stateJson.is_object();
  const std::uint32_t boot = stateObj && stateJson.contains("bootCount") && stateJson["bootCount"].is_number_unsigned()
                                 ? stateJson["bootCount"].get<std::uint32_t>() + 1
                                 : 1;
  clock_.start(stateObj && stateJson.contains("clock") ? stampFromJson(stateJson["clock"]) : std::nullopt, log_.newestTs(),
               boot);
  bootEpoch_ = clock_.epoch();
  bootMs_ = clock_.nowMs();
  lastTickEpoch_ = bootEpoch_;
  lastTickMs_ = bootMs_;
  bool settingsUnknown = false;  // broken, kept unread or gone: whether an inlet is assigned is unknown too
  bool settingsThere = false;
  if (auto s = read(kConfigFile)) {
    settingsThere = true;
    auto j = json::parse(*s, nullptr, false);
    std::optional<Msg> broken;
    if (j.is_discarded()) {
      broken = say("reason.not_json");
    } else {
      try {
        cfg_ = configFromJson(j);
        // A zone saved before the hub checked it falls back to the default when read: say so
        if (j.contains("system") && j["system"].is_object() && j["system"].contains("timezone")) {
          const json& tz = j["system"]["timezone"];
          if (!tz.is_string() || tz.get<std::string>() != cfg_.system.timezone)
            log_.add(bootEpoch_, "system", "notice", say("ev.cfg.timezone_reset"),
                     say("ev.cfg.timezone_reset.text", {{"zone", utf8Prefix(tz.is_string() ? tz.get<std::string>() : tz.dump(), 64)}, {"default", cfg_.system.timezone}}));
        }
      } catch (const std::exception& e) {
        broken = plain(e.what());
      }
    }
    if (broken) {
      // Mit der letzten guten Konfiguration ist hier nichts mehr zu retten:
      // Werkseinstellung, alle Aktoren aus, laut melden (Vorschlag architekt).
      writeFile(kConfigBroken, *s);
      cfg_ = Config{};
      settingsUnknown = true;
      log_.add(bootEpoch_, "system", "alarm", say("ev.config_unreadable"), say("ev.config_unreadable.text", {{"reason", *broken}}));
    }
  }
  if (cfg_.tanks.empty()) cfg_.tanks.emplace_back();
  // A hub that ran before and has none: gone, or a damaged entry the flash reports as not found (SD-086)
  settingsUnknown = settingsUnknown || kept_.count(kConfigFile) > 0 || (!settingsThere && ranBefore);
  // A run-time state that is broken or cannot be read is unknown: what
  // protects the tank errs on the safe side
  bool inletUnknown = false;  // the inlet latched for it: its alarm names the inlet (SD-087)
  const auto unknownState = [&] {
    rt_ = RuntimeState{};
    rt_.stopped = true;  // whether an emergency stop was active is unknown: stopped until someone resumes (PD-076)
    rt_.lastMixAt = std::max<Epoch>(bootEpoch_, 1);  // a mix may have just ended: a second one needs confirmation (RAT-003)
    rt_.selfReleased["circulation.dry"] = 0;  // when a dry run last released itself is unknown: the next one holds (SD-067)
    // Whether the inlet was latched, or a fill was being judged, is unknown: the stricter kind, released
    // by a person, not by Resume alone (RAT-031, RAT-063, SD-087). Also with unknown settings, which may
    // assign one; never where the hub switches nothing.
    inletUnknown = !jbool(platform_, "readOnly", false) && (cfg_.binding("tank.inlet") || settingsUnknown);
    if (inletUnknown)
      rt_.latches["inlet.fault"] = {{"at", std::max<Epoch>(bootEpoch_, 1)}, {"why", say("why.state_unknown")}, {"cause", "state_unknown"}};
  };
  if (stateRaw) {
    std::optional<Msg> broken;
    if (!stateObj) {
      broken = say("reason.not_json_object");
    } else {
      try {
        rt_ = runtimeFromJson(stateJson);
      } catch (const std::exception& e) {
        broken = plain(e.what());
      }
    }
    if (broken) {
      writeFile(kStateBroken, *stateRaw);
      unknownState();
      log_.add(bootEpoch_, "system", "alarm", say("ev.state_unreadable"),
               say(inletUnknown ? "ev.state_unreadable.text_inlet" : "ev.state_unreadable.text", {{"reason", *broken}}));
    }
  } else if (kept_.count(kStateFile)) {
    unknownState();  // as with a broken one (SD-081)
  } else if (ranBefore) {
    // A hub that ran before always left one: a state.json that is gone is
    // unknown, not new (#238)
    unknownState();
    log_.add(bootEpoch_, "system", "alarm", say("ev.state_missing"),
             say(inletUnknown ? "ev.state_missing.text_inlet" : "ev.state_missing.text", {{"file", kStateFile}}));
  }
  // A state saved before each recipe learned its own EC effect: the one learned value carries over to
  // every recipe whose start it does not undercut (where it came from is not known), so nothing doses
  // more after the update (SD-092)
  if (stateObj && !stateJson.contains("ecEffects") && isNum(rt_.ecEffect))
    for (const auto& r : cfg_.recipes)
      if (rt_.ecEffect >= recipeEffect(r, kNaN).start) rt_.ecEffects[r.id] = rt_.ecEffect;
  const bool stateSaid = kept_.count(kStateFile) > 0 || (!stateRaw && ranBefore);

  if (auto s = read(kAuthFile)) auth_.load(json::parse(*s, nullptr, false));
  if (auth_.hasPassword() && !cfg_.system.passwordSet) {
    cfg_.system.passwordSet = true;  // Geräte von vor dieser Kennzeichnung nachziehen
    saveConfig({});
  }
  if (credentialsLost() && !passwordUnreadable())  // its advice, a factory reset, would wipe a kept one
    log_.add(bootEpoch_, "system", "alarm", say("ev.credentials_lost"), say("ev.credentials_lost.text"));
  if (auto s = read(kHistoryFile))
    if (!history_.load(*s)) writeFile(kHistoryBroken, *s);  // kept: the snapshot at the end of boot replaces it
  if (auto s = read(kHistoryLog)) {
    std::set<std::string> ids = {"tank.volume"};  // only series the hub records
    for (const auto& [roleId, role] : cat_.roles)
      if (role.series) ids.insert(roleId);
    history_.replay(*s, ids);
  }
  rt_.bootCount = boot;
  // Nach dem Start ist alles aus; Abläufe werden nicht fortgesetzt (R6).
  // Fans keep their state: their sockets come back on after a power loss
  // (PD-050), and after a restart of the hub alone they keep running.
  // A Stop survives the restart and stops the fans too (PD-076).
  stopped_ = rt_.stopped;
  act_.stopAll(cat_, cfg_, clock_.nowMs(), !stopped_);
  act_.holdWatering(clock_.nowMs());  // a dose the restart cut has not mixed in (SD-093)
  if (stopped_ && !stateSaid)  // a kept or missing state says why everything stays off
    log_.add(bootEpoch_, "system", "alarm", say("ev.stop_kept"), say("ev.stop_kept.text"));
  if (auto s = read(kJobFile)) {
    auto j = json::parse(*s, nullptr, false);
    const std::string state = jstr(j, "state");
    const bool mix = jstr(j, "type") == "mix";
    // A mix cut off by the restart, or left failed, ends now: reported with what is in the tank, and
    // the window against mixing twice counts from here, not from its start (RAT-003)
    if (activeJobState(state) || (mix && state == "failed")) {
      if (mix) rt_.lastMixAt = std::max<Epoch>({rt_.lastMixAt, bootEpoch_, 1});  // 1: a clock at 0 still counts (lastMixAt 0 is "never")
      double idxNum = jnum(j, "index", 0);
      size_t idx = isNum(idxNum) && idxNum >= 0 ? static_cast<size_t>(idxNum) : 0;
      const json steps = j.contains("steps") && j["steps"].is_array() ? j["steps"] : json::array();
      size_t total = steps.size();
      json done = json::array();
      for (const auto& st : steps) {
        double ml = jnum(st, "mlDone", 0);
        if (isNum(ml) && ml > 0) done.push_back(say("amount", {{"name", jstr(st, "name")}, {"ml", ml}}));
      }
      json at{{"step", idx + 1}, {"total", total}, {"done", done}};
      const bool none = done.empty();
      // All parts were in when the restart cut its time to mix in: said so, not as a part missing
      const bool allIn = state == "settling";
      const bool ctl = controlOn();
      log_.add(bootEpoch_, "mix", "warn", say(mix ? "ev.mix_reboot" : "ev.job_reboot"),
               allIn ? say(ctl ? "ev.reboot.text_settling" : "ev.reboot.text_settling_plain", at)
                     : say(none ? "ev.reboot.text_none" : "ev.reboot.text", at));
      // Nutrients went in that nothing settled: EC and pH wait for the circulation pump first (SD-057, R6)
      if (mix && !none) settleControllersFirst(false);
      j["state"] = "aborted";
      j["message"] = say("job.reboot");
      writeJob(j.dump());
    }
  }
  // A mix it may have held was cut off by the restart, as far as anyone knows:
  // the window against mixing twice counts from here (RAT-003)
  if (kept_.count(kJobFile)) rt_.lastMixAt = std::max<Epoch>({rt_.lastMixAt, bootEpoch_, 1});
  // One entry per kind of data, naming the files that cannot be read (docs/UX.md §1)
  std::vector<std::pair<std::string, std::string>> keptKinds;  // its first file, the files
  for (const std::string& name : unreadable_) {
    const std::string kind = name == kEventsLog ? kEventsFile : name == kHistoryLog ? kHistoryFile : name;
    auto it = std::find_if(keptKinds.begin(), keptKinds.end(), [&](const auto& k) { return k.first == kind; });
    if (it == keptKinds.end()) keptKinds.emplace_back(kind, name);
    else it->second += ", " + name;
  }
  for (const auto& [kind, names] : keptKinds) {
    const bool config = kind == kConfigFile, state = kind == kStateFile, auth = kind == kAuthFile, job = kind == kJobFile,
               events = kind == kEventsFile;
    log_.add(bootEpoch_, "system", "alarm",
             say(config ? "ev.kept.config" : state ? "ev.kept.state" : auth ? "ev.kept.auth" : job ? "ev.kept.job" : events ? "ev.kept.events" : "ev.kept.history"),
             say(config                  ? "ev.kept.config.text"
                 : state && inletUnknown ? "ev.kept.state.text_inlet"
                 : state                 ? "ev.kept.state.text"
                 : auth                  ? "ev.kept.auth.text"
                 : job                   ? "ev.kept.job.text"
                 : events                ? "ev.kept.events.text"
                                         : "ev.kept.history.text",
                 {{"file", names}}));
  }
  log_.add(bootEpoch_, "system", "info", say("ev.started"), say("ev.started.text", {{"version", embedded::kVersion}}));
  saveState();  // the boot count and the stop are durable from the start
  // Snapshots before the first sample is appended: a journal torn by a
  // power loss is replaced, so nothing written after it is lost or misread.
  saveSnapshot();
  lastSnapshot_ = clock_.epoch();
}

void Hub::flush() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  saveAuth();
  saveState();
  saveEvents();  // the history's journal is written with every sample
  // A journal that failed waits for its snapshot; on the way out it is tried
  // once more, whatever its pause, so a restart right after space returned
  // keeps what happened since.
  if (unsaved_.count(kEventsFile) || unsaved_.count(kEventsLog)) saveEventsSnapshot();
  if (unsaved_.count(kHistoryFile) || unsaved_.count(kHistoryLog)) saveHistorySnapshot();
}

bool Hub::saveAuth() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (auth_.hasPassword()) return writeFile(kAuthFile, auth_.toJson().dump());
  return stored(true, kAuthFile);  // nothing to save (a setup that was rolled back)
}

void Hub::storageFailed(const std::string& name) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  stored(false, name);
}

bool Hub::writeJob(const std::string& data) {
  jobOnDisk_ = data;  // written again from here if it fails, also once the job has ended
  // A job of this run replaces a job.json kept unread: its old content no longer tells what is
  // in the tank, and a restart during the new job must report it (R6, RAT-003, SD-081)
  kept_.erase(kJobFile);
  return writeFile(kJobFile, data);
}

const std::vector<std::string>& Hub::files() {
  static const std::vector<std::string> kFiles = {kConfigFile, kAuthFile,   kStateFile,   kJobFile,
                                                  kEventsFile, kEventsLog,  kHistoryFile, kHistoryLog};
  return kFiles;
}

const std::vector<std::string>& Hub::brokenCopies() {
  static const std::vector<std::string> kCopies = {kConfigBroken, kStateBroken, kHistoryBroken};
  return kCopies;
}

// A file kept at the start is not written while it is kept; that was said once then. state.json
// and job.json stop being kept when the hub must write them anew (SD-081).
bool Hub::writeFile(const std::string& name, const std::string& data) {
  return !kept_.count(name) && stored(store_.write(name, data), name);
}
bool Hub::appendFile(const std::string& name, const std::string& data) {
  return !kept_.count(name) && stored(store_.append(name, data), name);
}

bool Hub::stored(bool ok, const std::string& name) {
  // One alarm while any file is not on disk; "saved again" only once every
  // one of them has been written. The hub keeps running in memory meanwhile.
  if (ok) {
    if (!unsaved_.erase(name)) return true;
    snapshotRetryAt_ = 0;  // space again: a snapshot still waiting is tried with the next retry
    snapshotBackoffS_ = 10;
    if (unsaved_.empty()) log_.add(clock_.epoch(), "system", "info", say("ev.storage_ok"), say("ev.storage_ok.text"));
    return true;
  }
  if (unsaved_.empty())
    log_.add(clock_.epoch(), "system", "alarm", say("ev.storage_failed"), say("ev.storage_failed.text", {{"file", name}}));
  unsaved_.insert(name);
  return false;
}

void Hub::retryUnsaved(Epoch epoch) {
  // What the hub holds is written again whole, so a change made while the
  // card was full is not lost, and a journal with a torn append is replaced.
  // The small files first (a STOP must fit); the large snapshots less and
  // less often while they keep failing (10 s, doubling up to 10 min).
  const std::set<std::string> names = unsaved_;  // retrying changes the set
  const auto has = [&](const char* n) { return names.count(n) > 0; };
  if (has(kStateFile)) saveState();
  if (has(kAuthFile)) saveAuth();
  if (has(kConfigFile)) writeFile(kConfigFile, json(cfg_).dump(1));  // the same revision, written again
  if (has(kJobFile)) jobOnDisk_.empty() ? stored(true, kJobFile) : writeJob(jobOnDisk_);
  static const std::set<std::string> kKnown(files().begin(), files().end());
  for (const std::string& name : names)  // a copy of a broken file, or one the hub does not keep, cannot be made again
    if (!kKnown.count(name)) stored(true, name);
  const bool events = has(kEventsFile) || has(kEventsLog), history = has(kHistoryFile) || has(kHistoryLog);
  if ((!events && !history) || epoch < snapshotRetryAt_) return;
  if (events) saveEventsSnapshot();
  if (history) saveHistorySnapshot();
  const bool stillFailing = unsaved_.count(kEventsFile) || unsaved_.count(kEventsLog) || unsaved_.count(kHistoryFile) || unsaved_.count(kHistoryLog);
  snapshotBackoffS_ = stillFailing ? std::min<Epoch>(snapshotBackoffS_ * 2, 600) : 10;
  snapshotRetryAt_ = epoch + snapshotBackoffS_;
}

void Hub::saveEvents() {
  const std::uint64_t last = log_.lastId();
  if (last == savedEventId_ || unsaved_.count(kEventsLog)) return;  // a failed journal is replaced by a snapshot
  if (kept_.count(kEventsLog)) {  // kept as it is (SD-081): these events stay in memory
    savedEventId_ = last;
    return;
  }
  const std::uint64_t count = last - savedEventId_;
  // Snapshots keep failing while appends work: the journal stops growing,
  // the snapshot that works at last takes these events.
  if (journaledEvents_ + count > 2 * kJournalMaxEvents) return;
  if (!appendFile(kEventsLog, log_.journalSince(savedEventId_))) return;  // replaced by a snapshot then
  savedEventId_ = last;
  journaledEvents_ += count;
  if (journaledEvents_ >= kJournalMaxEvents && !unsaved_.count(kEventsFile)) saveEventsSnapshot();  // a failing one is retried with a pause
}

void Hub::saveSnapshot() {
  saveEventsSnapshot();
  saveHistorySnapshot();
}

// Snapshot first, then the empty journal, and only after a snapshot that
// is on disk: a journal that outlives its snapshot (power loss in between)
// is skipped when read, not counted twice.
void Hub::saveEventsSnapshot() {
  if (!writeFile(kEventsFile, log_.toJson().dump())) return;  // the journal keeps everything until one works
  savedEventId_ = log_.lastId();
  if (writeFile(kEventsLog, "")) journaledEvents_ = 0;
}

void Hub::saveHistorySnapshot() {
  if (writeFile(kHistoryFile, history_.dump())) writeFile(kHistoryLog, "");
}

void Hub::saveConfig(const std::optional<Msg>& what) {
  cfg_.revision++;
  writeFile(kConfigFile, json(cfg_).dump(1));
  if (what)
    log_.add(clock_.epoch(), "config", "info", *what, say("ev.config.text", {{"rev", cfg_.revision}}), {{"revision", cfg_.revision}});
  saveEvents();
}

void Hub::saveState() {
  // A state.json kept unread started this run with no lock or latch but the inlet latch its unknown
  // state set (SD-087), so any other is new: written anew, with the Stop, a restart keeps both
  // (RAT-044, RAT-063, SD-081). So is that latch once a real cut-off replaced it.
  const bool newLatch = std::any_of(rt_.latches.begin(), rt_.latches.end(), [](const auto& l) { return !unknownStateLatch(l.second); });
  if (newLatch || !rt_.jumpLocks.empty()) stopKeepingState();
  rt_.clock = clock_.stamp();
  // A version before the effect per recipe reads the one value: that of EC control's recipe (SD-092)
  if (const RecipeCfg* r = ecRecipe(); r && rt_.ecEffects.count(r->id)) rt_.ecEffect = rt_.ecEffects.at(r->id);
  writeFile(kStateFile, json(rt_).dump());
  stateDirty_ = false;
}

// A state.json kept unread is written anew from here on, and that is said once: its locks and
// stock levels are gone for good (SD-081)
bool Hub::stopKeepingState() {
  if (!kept_.erase(kStateFile)) return false;
  log_.add(clock_.epoch(), "system", "warn", say("ev.kept.state_replaced"), say("ev.kept.state_replaced.text", {{"file", kStateFile}}));
  return true;
}

void Hub::saveJob() {
  if (job_) writeJob(json(*job_).dump());
}

// ------------------------------------------------------------------ Takt

void Hub::detectDevices() {
  std::set<std::string> online;
  for (const auto& d : devices_)
    if (d.online) online.insert(d.id);
  Epoch e = clock_.epoch();
  // "Not responding" and "back" name a known device alike: its name, else
  // its kind; an unknown device by its ID.
  auto named = [&](const std::string& id, const DeviceCfg* known) -> json {
    return known ? deviceName(cat_, id, known->cls, known->name) : json(id);
  };
  for (const auto& id : online)
    if (!seenOnline_.count(id)) {
      const DeviceCfg* known = cfg_.device(id);
      const bool wasLost = reportedLost_.erase(id) > 0;
      if (known && !wasLost) continue;  // a start, however late it answers: not "back" (#115)
      const DeviceReport* d = nullptr;
      for (const auto& x : devices_)
        if (x.id == id) d = &x;
      const DeviceClassDef* dc = d ? cat_.deviceClass(d->cls) : nullptr;
      Msg where = d && d->slot >= 0 ? say("where.slot", {{"n", d->slot + 1}})
                  : d && d->port > 0 ? say("where.port", {{"n", d->port}})
                  : dc && dc->attach == "net" ? say("where.net")
                  : dc && dc->attach == "ha"  ? say("where.ha")
                                              : say("where.hub");
      // "label" also for "back": events stored before #115 carry the kind under it
      const json label = known ? named(id, known) : dc ? json(nameOf(*dc)) : json(say("device.unnamed"));
      Msg title = say(known ? "ev.device.back" : "ev.device.found", {{"label", label}});
      Msg text = say("ev.device.where", {{"where", where}});
      // A pump back after it was gone while the hub runs: is it still on the same bottle?
      if (known && d && d->cls == "pump_cap")
        if (const CanisterCfg* k = cfg_.canisterByPump(id)) text = say("ev.device.where_pump", {{"where", where}, {"name", k->name}});
      log_.add(e, "device", "info", title, text, {{"device", id}});
    }
  for (const auto& id : seenOnline_)
    if (!online.count(id)) {
      const DeviceCfg* known = cfg_.device(id);
      log_.add(e, "device", known ? "warn" : "info", say("ev.device.lost", {{"name", named(id, known)}}), say("ev.device.lost.text"),
               {{"device", id}});
      reportedLost_.insert(id);
    }
  seenOnline_ = online;
}

double Hub::tankVolume() const {
  const auto& lvl = truth_.get("tank.level");
  if (lvl.usable()) return *lvl.value;
  return rt_.tankVolumeL;
}

void Hub::tick() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  try {
    tickImpl();
    if (tickFault_) {
      tickFault_ = false;
      log_.add(clock_.epoch(), "system", "info", say("ev.tick_ok"), say("ev.tick_ok.text"));
    }
  } catch (const std::exception& e) {
    // Sicherer Zustand statt Absturz oder Boot-Schleife: alles aus, laufende
    // Dosierung abbrechen, einmal laut melden. Fans keep running (PD-077),
    // except during a Stop (PD-076).
    act_.stopAll(cat_, cfg_, clock_.nowMs(), !stopped_);
    try {
      Ctx c = ctx();
      doser_.abort(c, act_, say("abort.internal"));
      if (auto fin = doser_.takeFinished()) {
        const bool controllerDose = fin->orderId.rfind("ec-", 0) == 0 || fin->orderId.rfind("ph-", 0) == 0;
        if (!controllerDose) onJobDose(c, *fin);
      }
      ec_.reset();
      ph_.reset();
      // Nutzerauftrag nicht hängen lassen: beenden und sagen, was drin ist
      if (userJobActive()) {
        const json done = amounts(job_->steps);
        const bool none = done.empty();
        mixCut(false);  // what went in settles first (SD-057)
        finishJob("aborted", say(none ? "job.internal_none" : "job.internal", {{"done", done}}));
      }
    } catch (...) {
      // Aufräumen darf den sicheren Zustand nicht verhindern: Aktoren sind schon aus.
    }
    if (!tickFault_) {
      tickFault_ = true;
      log_.add(clock_.epoch(), "system", "alarm", say("ev.tick_fault"), say("ev.tick_fault.text", {{"reason", e.what()}}));
    }
    try {  // saved here too: a tick that fails every time never reaches its own saving
      saveEvents();
      if (stateDirty_) saveState();
    } catch (...) {
    }
  }
}

namespace {
Msg span(Epoch s) {
  s = s < 0 ? -s : s;
  if (s < 120) return say("span.s", {{"n", s}});
  if (s < 2 * 3600) return say("span.min", {{"n", s / 60}});
  return say("span.h", {{"h", static_cast<double>(s) / 3600.0}});
}
}  // namespace

// Secured time and clock steps (PD-069, PD-073).
void Hub::watchClock(Ms now) {
  const bool was = clock_.secured();
  const std::string before = clock_.source();
  clock_.poll();
  const bool secured = clock_.secured();
  const Epoch expected = lastTickEpoch_ + (now - lastTickMs_) / 1000;
  if (was && !secured) {
    // Lost while running: continue from the last secured time, no jump.
    clock_.lose(expected);
    unsecuredReported_ = true;
    log_.add(expected, "system", "warn", say("ev.clock.lost"), say("ev.clock.lost.text"), {{"source", clock_.source()}});
  }
  const Epoch epoch = clock_.epoch();
  const Epoch jump = epoch - expected;
  if (!was && secured) {
    shiftDeadlines(jump, epoch);
    if (unsecuredReported_ || jump > 120 || jump < -120)
      log_.add(epoch, "system", "info", say("ev.clock.secured"),
               before == "unset" ? say("ev.clock.first")
               : jump > 0        ? say("ev.clock.forward", {{"span", span(jump)}})
               : jump < 0        ? say("ev.clock.back", {{"span", span(jump)}})
                                 : say("ev.clock.received"),
               {{"jumpS", jump}, {"before", before}});
    unsecuredReported_ = false;
    stateDirty_ = true;
  } else if ((secured || (before == "unset" && std::string(clock_.source()) == "unset")) && (jump > 5 || jump < -5)) {
    // The platform clock stepped: network time while secured, or set before
    // the secured flag follows (on the device the time is set first).
    shiftDeadlines(jump, epoch);
    if (secured && (jump > 120 || jump < -120))
      log_.add(epoch, "system", "info", say("ev.clock.set"), say(jump > 0 ? "ev.clock.set_forward" : "ev.clock.back", {{"span", span(jump)}}),
               {{"jumpS", jump}});
    stateDirty_ = true;
  } else if (!secured && !unsecuredReported_ && now - bootMs_ >= 2 * kMinute) {
    unsecuredReported_ = true;
    const bool continued = std::string(clock_.source()) == "continued";
    log_.add(epoch, "system", "warn", say("ev.clock.unsecured"), say(continued ? "ev.clock.continued" : "ev.clock.unset"),
             {{"source", clock_.source()}});
  }
  lastTickEpoch_ = epoch;
  lastTickMs_ = now;
}

// A flood takes water out of the tank and the drain-back brings it back, never above where a run of the
// series started within SD-099's longest drain-back of 30 min. So the running watering pump explains a
// level up to 2 L above the lowest reading of the run, a fall; until 10 min after it stops, a level up to
// 2 L above the highest level such a run started at. A reading above is judged as any change: more than
// 20 L within 5 min locks, so a sensor that jumps high still does. A run that starts within the 10 min
// continues the series. A state not known counts as off; a level not usable when a run started explains
// nothing for that run, nor after the stop for its series (RAT-042, SD-101, #324)
void Hub::announceWatering(Ms now) {
  constexpr double kGateL = 2.0;  // a gate, not a threshold: above it the 20 L apply (an assumption)
  constexpr Ms kDrainBack = 30 * kMinute;
  const bool on = act_.roleState(cfg_, "zone.irrigation_pump").value_or(false);
  const auto& lv = truth_.get("tank.level");
  const double l = lv.usable() ? *lv.value : kNaN;  // the last reading, before this tick's
  if (on && !waterOn_) {
    if (now >= waterTailUntil_) waterStarts_.clear();
    // A state that flaps is one run within a minute, so the list holds at most one start a minute
    if (!waterStarts_.empty() && now - waterStarts_.back().first < kMinute) {
      double& last = waterStarts_.back().second;
      last = isNum(last) && isNum(l) ? std::max(last, l) : kNaN;
    } else {
      waterStarts_.emplace_back(now, l);
    }
    waterLowL_ = l;
  }
  while (waterStarts_.size() > 1 && now - waterStarts_.front().first > kDrainBack) waterStarts_.pop_front();
  if (on && isNum(waterLowL_) && isNum(l)) waterLowL_ = std::min(waterLowL_, l);
  waterOn_ = on;
  if (on) waterTailUntil_ = now + 10 * kMinute;
  if (now >= waterTailUntil_) return;
  double topL = kNaN;
  for (const auto& start : waterStarts_) {
    if (!isNum(start.second)) {
      topL = kNaN;
      break;
    }
    topL = isNum(topL) ? std::max(topL, start.second) : start.second;
  }
  truth_.expectChangeUpTo("tank.level", waterTailUntil_, (on ? waterLowL_ : topL) + kGateL);
}

// The wall clock jumped: deadlines in wall time keep their distance; jump
// locks and maintenance never last longer than they can (RAT-044).
void Hub::shiftDeadlines(Epoch jump, Epoch epoch) {
  for (Epoch* t : {&lastSample_, &lastWatch_, &lastStateSave_, &lastSnapshot_, &lastRetry_, &snapshotRetryAt_, &bootEpoch_}) *t += jump;
  if (watch_.evaluatedAt > 0) watch_.evaluatedAt += jump;
  if (rt_.lastMixAt > 0) rt_.lastMixAt += jump;
  if (maintenanceUntil_ > 0) maintenanceUntil_ = std::min(maintenanceUntil_ + jump, epoch + kMaxMaintenanceS);
  for (auto& [role, until] : rt_.jumpLocks) until = std::min(until + jump, epoch + kJumpHoldS);
  for (auto& [latch, at] : rt_.selfReleased)
    if (plausibleEpoch(at)) at = std::min(at + jump, epoch);  // never in the future; an implausible one counts as now (SD-067)
  ec_.shiftClock(jump);
  ph_.shiftClock(jump);
  refill_.shiftClock(rt_, jump);
}

void Hub::tickImpl() {
  Ms now = clock_.nowMs();
  watchClock(now);
  Epoch epoch = clock_.epoch();
  bus_.poll(now);
  devices_ = bus_.devices();
  if (net_) {
    net_->poll(now);
    for (auto& d : net_->devices()) devices_.push_back(std::move(d));
  }
  ports_ = bus_.ports();
  pumps_ = pumpsFrom(devices_);
  detectDevices();
  // Angekündigte Änderungen erklären Sprünge (Quelle: RAT-042): eigene
  // Mischläufe und Handgaben, offener Zulauf, laufende Gießpumpe (nur der Füllstand), Kalibrierung, Pflegemodus.
  if (userJobActive() || (doser_.active() && doser_.active()->purpose != "ph" && doser_.active()->purpose != "ec")) {
    truth_.expectChange("tank.ec", now + 10 * kMinute);
    truth_.expectChange("tank.ph", now + 10 * kMinute);
  }
  // Eigene EC-Runde: Deckel 1,0 mS/cm liegt über der Sprungschwelle 0,5 – ankündigen.
  // pH-Gaben bleiben unangekündigt (Deckel 0,3 pH unter der Schwelle 1,0).
  if (ec_.busy()) truth_.expectChange("tank.ec", now + 10 * kMinute);
  // Fresh water changes EC, pH, level and water temperature; announced until 5 min after the
  // valve closes, as the readings follow late (RAT-042, #272)
  if (act_.roleState(cfg_, "tank.inlet").value_or(false))
    for (const char* r : {"tank.ec", "tank.ph", "tank.level", "tank.water_temp"}) truth_.expectChange(r, now + 5 * kMinute);
  announceWatering(now);
  // Maintenance explains a change only 10 s ahead, never past its end, renewed every tick before
  // the readings are judged: ended early, a jump locks again within 10 s (RAT-042, #292)
  if (epoch < maintenanceUntil_)
    for (const auto& [role, def] : cat_.roles)
      truth_.expectChange(role, now + std::min<Ms>((maintenanceUntil_ - epoch) * kSecond, 10 * kSecond));
  for (const auto& [key, sess] : probeSessions_) {
    if (sess.value("idle", false)) continue;  // left for 30 min: a jump is a jump again (RAT-042)
    std::string dev = jstr(sess, "device");
    cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
      if (b.device == dev) truth_.expectChange(role, now + 10 * kMinute);
    });
  }
  auto locksBefore = rt_.jumpLocks;
  truth_.update(cfg_, bus_, rt_, now, epoch);
  tendProbeSessions(now);
  for (const auto& [role, until] : rt_.jumpLocks)
    if (!locksBefore.count(role)) {
      const auto& r = truth_.get(role);
      log_.add(epoch, "block", "alarm", say("ev.jump", {{"label", roleName(cat_, role)}}), r.reason,
               {{"role", role}, {"until", until}});
      stateDirty_ = true;
    }
  for (const auto& [role, until] : locksBefore)
    if (!rt_.jumpLocks.count(role)) {
      log_.add(epoch, "unblock", "info", say("ev.jump_cleared", {{"label", roleName(cat_, role)}}),
               say("ev.jump_cleared.text"), {{"role", role}});
      stateDirty_ = true;
    }

  Ctx c = ctx();
  const auto latchesBefore = rt_.latches;  // a latch set or released in this tick is saved at once (RAT-063)
  act_.enforce(c);
  if (rt_.latches != latchesBefore) stateDirty_ = true;  // also when a later step of the tick throws
  for (auto it = testOff_.begin(); it != testOff_.end();) {
    if (now < it->second) {
      ++it;
      continue;
    }
    Msg e;
    act_.setRole(c, it->first, false, say("who.test"), e);
    it = testOff_.erase(it);
  }
  doser_.tick(c, act_);
  ControlEnv env{act_, doser_, truth_};
  env.userJob = userJobActive();
  env.volumeL = tankVolume();
  env.lastEcDoseAt = ec_.lastDoseAt();
  env.circulationOn = act_.roleState(cfg_, "tank.circulation").value_or(false);
  env.refilling = act_.roleState(cfg_, "tank.inlet").value_or(false) || refill_.holding(rt_);
  env.calibrating = !probeSessions_.empty();
  if (const Binding* lb = cfg_.binding("tank.level"))
    for (const auto& [key, sess] : probeSessions_)
      env.levelCalibrating = env.levelCalibrating || (!sess.value("idle", false) && jstr(sess, "kind") == "tank_curve" && jstr(sess, "device") == lb->device);
  if (auto fin = doser_.takeFinished()) {
    const std::string& id = fin->orderId;
    if (id.rfind("ec-", 0) == 0) ec_.onDoseFinished(c, env, *fin);
    else if (id.rfind("ph-", 0) == 0) ph_.onDoseFinished(c, *fin);
    else onJobDose(c, *fin);
    stateDirty_ = true;
  }
  tickJob(c);
  env.userJob = userJobActive();
  // Nachfüllen zuerst: Es startet nur, wenn keine Gabe läuft oder einschwingt,
  // und hält dann neue EC- und pH-Runden an (RAT-055).
  env.ecBusy = ec_.busy();
  env.phBusy = ph_.busy();
  const json refillBefore = rt_.refill;
  const auto latchesBeforeRefill = rt_.latches;
  refill_.tick(c, env);
  // A latch or a pending check the refill set is saved at once, also when a later step throws (RAT-063, SD-082)
  if (rt_.refill != refillBefore || rt_.latches != latchesBeforeRefill) stateDirty_ = true;
  env.refilling = act_.roleState(cfg_, "tank.inlet").value_or(false) || refill_.holding(rt_);
  env.refillDue = refill_.dueAfterDose();
  // A round a Stop cut that still waits for the circulation pump holds the other controller (SD-073).
  // pH also waits in the tick in which EC's settling ends: when both end together, EC decides
  // first, as after every round (RAT-004)
  env.phBusy = ph_.busy() || ph_.pending();
  const bool ecWasBusy = ec_.busy() || ec_.pending();
  ec_.tick(c, env);
  env.ecBusy = ecWasBusy || ec_.busy() || ec_.pending();
  env.lastEcDoseAt = ec_.lastDoseAt();
  ph_.tick(c, env);
  bool demand = ec_.wantsCirculation() || ph_.wantsCirculation() || (job_ && job_->circulation && userJobActive());
  circ_.tick(c, env, demand);

  if (epoch - lastSample_ >= 10) {
    lastSample_ = epoch;
    sampleHistory(epoch);
  }
  functions_ = resolveFunctions(cat_, cfg_, devices_, pumps_, truth_);
  if (epoch - lastWatch_ >= 5) {
    lastWatch_ = epoch;
    WatchInput in{cat_, cfg_, rt_, truth_.all(), {}, {}, {}, epoch, bootEpoch_, maintenanceUntil_, stopped_};
    for (const auto& d : devices_) {
      const DeviceCfg* dc = cfg_.device(d.id);
      in.devices.push_back({d.id, dc ? dc->name : d.id, d.online});
    }
    in.controllers = {{"ec", say("watch.ctl.ec"), ec_.status().state, ec_.status().line},
                      {"ph", say("watch.ctl.ph"), ph_.status().state, ph_.status().line},
                      {"refill", say("watch.ctl.refill"), refill_.status().state, refill_.status().line},
                      {"circulation", say("watch.ctl.circulation"), circ_.status().state, circ_.status().line}};
    for (const auto& [id, p] : pumps_) in.pumpFlow[id] = p.flowMlPerMin;
    watch_ = evaluate(in);  // Watchdog liest nur, er bekommt keine Aktoren
  }
  if (rt_.latches != latchesBefore) stateDirty_ = true;
  if (stateDirty_ || epoch - lastStateSave_ >= 60) {
    lastStateSave_ = epoch;
    saveState();
  }
  saveEvents();
  if (epoch - lastSnapshot_ >= kSnapshotS) {  // once a day; the first one comes from boot
    lastSnapshot_ = epoch;
    saveSnapshot();
  }
  if (!unsaved_.empty() && epoch - lastRetry_ >= 10) {
    lastRetry_ = epoch;
    retryUnsaved(epoch);
  }
}

void Hub::sampleHistory(Epoch epoch) {
  std::vector<std::pair<std::string, double>> samples;
  for (const auto& [roleId, role] : cat_.roles) {
    if (!role.series) continue;
    const auto& r = truth_.get(roleId);
    const CapabilityDef* cap = cat_.capability(role.capability);
    const bool derived = cap && cap->kind == "derived";
    if (derived ? r.quality == Quality::NotBound : !cfg_.binding(roleId)) continue;
    samples.emplace_back(roleId, r.forHistory() ? *r.value : kNaN);  // Lücke statt 0
  }
  double v = tankVolume();
  if (isNum(v)) samples.emplace_back("tank.volume", v);
  if (samples.empty()) return;
  for (const auto& [id, value] : samples) history_.add(id, epoch, value);
  // Not behind a failed append: the snapshot that replaces the journal holds these samples
  if (!unsaved_.count(kHistoryLog)) appendFile(kHistoryLog, History::record(epoch, samples));
}

// ------------------------------------------------------------------ Lesen

Epoch Hub::now() const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  return clock_.epoch();
}

bool Hub::passwordUnreadable() const { return kept_.count(kAuthFile) > 0; }

std::string Hub::armSetupCode(Ms validFor) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  return credentialsLost() || passwordUnreadable() ? std::string() : auth_.armSetupCode(clock_.nowMs(), validFor);
}

json Hub::info() const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  return {{"product", "growcontroller"},
          {"version", embedded::kVersion},
          {"api", 1},
          {"catalogVersion", cat_.version},
          {"schemaVersion", kSchemaVersion},
          {"platform", platform_},
          {"setupDone", cfg_.system.setupDone},
          {"hasPassword", auth_.hasPassword()},
          {"setupCode", auth_.setupCodeRequired()},  // the first password needs the code from the log (#79)
          {"name", cfg_.system.name},
          {"language", cfg_.system.language == "de" ? "de" : "en"}};  // the sign-in page's, before a choice in the browser (SD-083)
}

json Hub::state() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Epoch epoch = clock_.epoch();
  json j;
  j["now"] = epoch;
  j["uptimeS"] = (clock_.nowMs() - bootMs_) / 1000;
  j["time"] = {{"secured", clock_.secured()}, {"source", clock_.source()}, {"operatingS", clock_.operatingS()}};
  j["setupDone"] = cfg_.system.setupDone;
  j["stopped"] = stopped_;
  j["maintenanceUntil"] = maintenanceUntil_;
  json ports = json::array();
  for (const auto& p : ports_)
    ports.push_back({{"port", p.port}, {"state", portStateName(p.state)}, {"device", p.deviceId}, {"class", p.cls}, {"message", p.message}});
  j["ports"] = ports;
  json devs = json::array();
  for (const auto& d : devices_) {
    const DeviceCfg* dc = cfg_.device(d.id);
    const DeviceClassDef* cls = cat_.deviceClass(d.cls);
    json cal = json::object();
    if (auto it = cfg_.calibrations.find(d.id); it != cfg_.calibrations.end())
      for (const auto& [k, v] : it->second) {
        const double at = jnum(v, "at", 0);  // from a file: a time the hub can have written, else not set
        cal[k] = isNum(at) && at >= 0 && at <= static_cast<double>(kNotAfter) ? static_cast<Epoch>(at) : 0;
      }
    devs.push_back({{"id", d.id},
                    {"class", d.cls},
                    {"classLabel", cls ? cls->label : d.cls},
                    {"name", dc && ownName(dc->name, dc->cls, cat_) ? dc->name : ""},  // a class name is none (SD-083)
                    {"configured", dc != nullptr},
                    {"online", d.online},
                    {"port", d.port},
                    {"slot", d.slot},
                    {"parent", d.parent},
                    {"fw", d.fw},
                    {"fault", d.fault},
                    {"info", d.info},
                    {"calibrations", cal}});
  }
  // eingerichtete, aber nicht erkannte Geräte ebenfalls zeigen
  for (const auto& d : cfg_.devices) {
    bool seen = false;
    for (const auto& x : devices_) seen = seen || x.id == d.id;
    if (!seen) {
      const DeviceClassDef* cls = cat_.deviceClass(d.cls);
      devs.push_back({{"id", d.id}, {"class", d.cls}, {"classLabel", cls ? cls->label : d.cls}, {"name", ownName(d.name, d.cls, cat_) ? d.name : ""},
                      {"configured", true}, {"online", false}, {"port", 0}, {"slot", -1}, {"parent", ""},
                      {"fw", ""}, {"fault", ""}, {"info", json::object()}, {"calibrations", json::object()}});
    }
  }
  j["devices"] = devs;
  json readings = json::object();
  for (const auto& [role, r] : truth_.all())
    readings[role] = {{"value", numOrNull(r.value)}, {"quality", qualityName(r.quality)}, {"reason", r.reason},
                      {"ageS", r.ageMs >= 0 ? json(r.ageMs / 1000) : json(nullptr)},
                      {"ageMs", r.ageMs >= 0 ? json(r.ageMs) : json(nullptr)}, {"ageOf", r.ageOf}, {"unit", r.unit},
                      {"decimals", r.decimals}, {"usable", r.usable()}};
  j["readings"] = readings;
  const auto& lvl = truth_.get("tank.level");
  j["tank"] = {{"volumeL", numOrNull(tankVolume())},
               {"source", lvl.usable() ? "level" : "mix"},
               {"capacityL", numOrNull(cfg_.tank().capacityL)},
               {"minL", numOrNull(cfg_.tank().minL)}};
  j["controllers"] = {{"ec", ec_.status()}, {"ph", ph_.status()}, {"refill", refill_.status()}, {"circulation", circ_.status()}};
  json outputs = json::object();
  for (const auto& [role, rd] : cat_.roles)
    if (!rd.profile.empty())
      if (auto st = act_.roleState(cfg_, role)) outputs[role] = *st;
  j["outputs"] = outputs;
  j["job"] = job_ ? json(*job_) : json(nullptr);
  // What a job waits for still has this long: mixing in, or the user's stirring (SD-057)
  if (job_ && job_->waitUntil > 0 && (job_->state == "mixing" || job_->state == "settling" || (job_->type == "mix" && job_->state == "waiting_user")))
    j["job"]["waitLeftS"] = std::max<Ms>(0, job_->waitUntil - clock_.nowMs()) / kSecond;
  if (job_ && job_->state == "settling") {
    // what the pump reads (unknown stays unknown, R5), and whether the mix counts it as standing still
    const auto pump = act_.roleState(cfg_, "tank.circulation");
    j["job"]["circulating"] = pump ? json(*pump) : json(nullptr);
    j["job"]["pumpStands"] = job_->pumpStands;
  }
  j["lastJob"] = lastJob_ ? json(*lastJob_) : json(nullptr);
  if (doser_.active()) {
    const auto& o = *doser_.active();
    const auto& p = doser_.progress();
    j["dosing"] = {{"order", o.id}, {"purpose", o.purpose}, {"canister", o.step.canister}, {"name", o.step.name},
                   {"ml", numOrNull(o.step.ml)}, {"mlDone", p.mlDone}, {"run", p.run + 1}, {"runs", o.step.runs.size()}};
  } else {
    j["dosing"] = nullptr;
  }
  json wd = watch_;
  wd["stale"] = epoch - watch_.evaluatedAt > kWatchStaleS;
  j["watchdog"] = wd;
  j["functions"] = functions_;
  j["latches"] = rt_.latches;
  json stock = json::object();
  for (const auto& k : cfg_.canisters) {
    auto it = rt_.stockMl.find(k.id);
    stock[k.id] = it == rt_.stockMl.end() ? json(nullptr) : numOrNull(it->second);
  }
  j["stock"] = stock;
  // EC: what EC control learned for the recipe it doses now, and for each recipe there is (SD-092)
  json byRecipe = json::object();
  for (const auto& r : cfg_.recipes)
    if (rt_.ecEffects.count(r.id)) byRecipe[r.id] = numOrNull(rt_.ecEffects.at(r.id));
  const RecipeCfg* ecr = ecRecipe();
  j["effects"] = {{"ph", numOrNull(rt_.phEffect)}, {"ec", ecr ? numOrNull(learnedEcEffect(rt_, ecr->id)) : json(nullptr)}, {"ecByRecipe", byRecipe}};
  j["lastMixAt"] = rt_.lastMixAt;
  j["manual"] = {{"values", rt_.manual}, {"at", rt_.manualAt}};
  j["grow"] = json(cfg_)["grow"];
  j["eventId"] = log_.lastId();
  json probes = json::object();
  for (const auto& [k, v] : probeSessions_) probes[k] = v;
  j["probeCalibration"] = probes;
  return j;
}

json Hub::configJson() const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  return cfg_;
}

json Hub::history(const std::string& series, Epoch from, Epoch to, size_t points) const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto p = history_.query(series, from, to, points);
  json t = p.t, avg = json::array(), mn = json::array(), mx = json::array();
  for (size_t i = 0; i < p.t.size(); ++i) {
    avg.push_back(numOrNull(p.avg[i]));
    mn.push_back(numOrNull(p.min[i]));
    mx.push_back(numOrNull(p.max[i]));
  }
  return {{"series", series}, {"stepS", p.stepS}, {"t", t}, {"avg", avg}, {"min", mn}, {"max", mx}};
}

json Hub::events(Epoch from, Epoch to, const std::string& type, size_t limit) const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  json arr = json::array();
  for (const auto& e : log_.query(from, to, type, limit)) arr.push_back(e);
  return {{"events", arr}};
}

std::string Hub::language() const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  return cfg_.system.language == "de" ? "de" : "en";
}

std::string Hub::exportCsv(const std::vector<std::string>& series, Epoch from, Epoch to, const std::string& lang) const {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // German: semicolon and decimal comma; English: comma and decimal point, so
  // a spreadsheet in either language reads the numbers (RAT-016, SD-083)
  const bool en = lang != "de";
  const char sep = en ? ',' : ';';
  std::ostringstream os;
  os << (en ? "time_utc" : "zeit_utc");
  std::vector<SeriesPoints> pts;
  for (const auto& s : series) {
    os << sep << s;
    pts.push_back(history_.query(s, from, to, 0));
  }
  os << '\n';
  std::map<Epoch, std::vector<double>> rows;
  for (size_t i = 0; i < pts.size(); ++i)
    for (size_t k = 0; k < pts[i].t.size(); ++k) {
      auto& row = rows[pts[i].t[k]];
      row.resize(series.size(), kNaN);
      row[i] = pts[i].avg[k];
    }
  for (const auto& [t, row] : rows) {
    os << t;
    for (double v : row) {
      os << sep;
      if (!isNum(v)) continue;  // a gap stays an empty field, never 0 (RAT-016, RAT-006)
      std::string n = fmt(v, 3);
      if (en) std::replace(n.begin(), n.end(), ',', '.');
      os << n;
    }
    os << '\n';
  }
  return os.str();
}

json Hub::diagnostics() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // The diagnostic package of "Report a problem": no secrets (no password hash,
  // no sessions, no Wi-Fi); the page shows its content before it is downloaded.
  std::uint8_t b[4];
  rng_(b, sizeof b);
  json cfg = cfg_;
  json st = state();
  json evs = json::array();
  for (const auto& e : log_.query(clock_.epoch() - 72 * 3600, clock_.epoch(), "", 300)) evs.push_back(e);
  return {{"reportId", "GC-" + toHex(b, sizeof b)},
          {"createdAt", clock_.epoch()},
          {"info", info()},
          {"config", cfg},
          {"ports", st["ports"]},
          {"devices", st["devices"]},
          {"readings", st["readings"]},
          {"controllers", st["controllers"]},
          {"watchdog", st["watchdog"]},
          {"latches", st["latches"]},
          {"events", evs},
          // what the package leaves out, as keys; the web app names them in the page's language (#73)
          {"redacted", {"password_hash", "sessions", "wifi_credentials", "ip_addresses"}}};
}

// ------------------------------------------------------------------ Einrichtung

void Hub::markPasswordSet() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (cfg_.system.passwordSet) return;
  cfg_.system.passwordSet = true;
  saveConfig({});
}

Result Hub::completeSetup() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  cfg_.system.setupDone = true;
  saveConfig(say("ev.cfg.setup_done"));
  return Result::ok();
}

Result Hub::setSystem(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // Erst alles prüfen, dann übernehmen: eine Ablehnung ändert nichts.
  SystemCfg sys = cfg_.system;
  Limits lim = cfg_.limits;
  if (j.contains("name") && j["name"].is_string()) sys.name = utf8Prefix(j["name"].get<std::string>(), 40);
  if (j.contains("timezone")) {
    if (!j["timezone"].is_string() || !isTimeZoneName(j["timezone"].get<std::string>())) return Result::fail(422, say("system.timezone"));
    sys.timezone = j["timezone"];
  }
  if (j.contains("language") && j["language"].is_string()) {
    std::string lang = j["language"];
    if (lang != "de" && lang != "en") return Result::fail(422, say("system.language"));
    sys.language = lang;
  }
  if (j.contains("updateCheck") && j["updateCheck"].is_boolean()) sys.updateCheck = j["updateCheck"];
  if (j.contains("updateChannel") && j["updateChannel"].is_string()) {
    std::string ch = j["updateChannel"];
    if (ch != "stable" && ch != "beta") return Result::fail(422, say("system.channel"));
    sys.updateChannel = ch;
  }
  if (j.contains("handDoseMaxMl") && j["handDoseMaxMl"].is_number()) {
    double v = j["handDoseMaxMl"];
    if (v < 0.5 || v > 50) return Result::fail(422, say("system.hand"));
    lim.handDoseMaxMl = v;
  }
  if (j.contains("control")) {
    // Adds to what was chosen; null takes an area's choice back (SD-037).
    if (!j["control"].is_object()) return Result::fail(422, say("system.control"));
    for (const auto& [area, mode] : j["control"].items()) {
      if (!isControlArea(area)) return Result::fail(422, say("system.control"));
      if (mode.is_null())
        sys.control.erase(area);
      else if (mode.is_string() && isControlMode(mode.get<std::string>()))
        sys.control[area] = mode.get<std::string>();
      else
        return Result::fail(422, say("system.control"));
    }
  }
  cfg_.system = sys;
  cfg_.limits = lim;
  saveConfig(say("ev.cfg.system"));
  return Result::ok();
}

Result Hub::acceptDevice(const std::string& id, const std::string& name) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const DeviceReport* d = nullptr;
  for (const auto& x : devices_)
    if (x.id == id) d = &x;
  if (!d) return Result::fail(404, say("device.not_detected"));
  if (!cat_.deviceClass(d->cls)) return Result::fail(422, say("device.class"));
  if (cfg_.device(id)) return Result::ok();
  const DeviceClassDef* dc = cat_.deviceClass(d->cls);
  // Without a name of its own it stores none and is named by its class in the page's language (SD-083)
  cfg_.devices.push_back({id, d->cls, utf8Prefix(name, 40)});
  // Netzgerät: jeden Kanal „nach Stromausfall aus“ setzen (R6 gilt auch dort).
  if (net_ && net_->owns(id))
    for (int ch = 0; ch < std::max(1, dc->channels); ++ch) {
      std::string e;
      if (!net_->configure(id, ch, SwitchSafety{}, e))
        log_.add(clock_.epoch(), "device", "warn", say("ev.net.unprotected", {{"name", deviceName(cat_, id, d->cls, cfg_.devices.back().name)}}), plain(e), {{"device", id}});
    }
  // Messrollen automatisch zuordnen, wenn genau ein Gerät passt (nie bei
  // Dosier- oder Schaltrollen). Sonst wählt der Nutzer unter Zuordnung.
  autoBindMeasures();
  saveConfig(say("ev.cfg.device_added", {{"name", deviceName(cat_, id, d->cls, cfg_.devices.back().name)}}));
  return Result::ok();
}

void Hub::autoBindMeasures() {
  if (cfg_.devices.empty()) return;
  auto provides = [&](const DeviceCfg& d, const std::string& cap) {
    const DeviceClassDef* c = cat_.deviceClass(d.cls);
    return c && std::find(c->provides.begin(), c->provides.end(), cap) != c->provides.end();
  };
  const DeviceCfg& added = cfg_.devices.back();
  for (const auto& [roleId, role] : cat_.roles) {
    if (cfg_.binding(roleId)) continue;
    const CapabilityDef* cap = cat_.capability(role.capability);
    if (!cap || cap->kind != "measure") continue;
    // Nur Rollen, die das neue Gerät liefert: Eine bewusst gelöste Rolle
    // bleibt gelöst, wenn ein anderes Gerät dazukommt.
    if (!provides(added, role.capability)) continue;
    std::vector<const DeviceCfg*> cand;
    for (const auto& d : cfg_.devices)
      if (provides(d, role.capability)) cand.push_back(&d);
    if (cand.size() == 1) cfg_.rolesFor(roleId)[roleId] = {cand.front()->id, 0};
  }
}

Result Hub::renameDevice(const std::string& id, const std::string& name) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  for (auto& d : cfg_.devices)
    if (d.id == id) {
      d.name = utf8Prefix(name, 40);
      saveConfig(say("ev.cfg.device_renamed", {{"name", deviceName(cat_, d.id, d.cls, d.name)}}));
      return Result::ok();
    }
  return Result::fail(404, say("device.unknown"));
}

Result Hub::setDeviceLife(const std::string& id, const std::string& source, const std::string& channel) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  auto d = std::find_if(next.devices.begin(), next.devices.end(), [&](const DeviceCfg& x) { return x.id == id; });
  if (d == next.devices.end()) return Result::fail(404, say("device.unknown"));
  const std::string ch = source.empty() ? std::string() : channel;
  if (d->life == source && d->lifeChannel == ch) return Result::ok();
  d->life = source;
  d->lifeChannel = ch;
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  const json name = deviceName(cat_, d->id, d->cls, d->name);
  cfg_ = std::move(next);
  saveConfig(source.empty() ? say("ev.cfg.device_life_off", {{"name", name}}) : say("ev.cfg.device_life", {{"name", name}}));
  return Result::ok();
}

Result Hub::removeDevice(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto it = std::find_if(cfg_.devices.begin(), cfg_.devices.end(), [&](const DeviceCfg& d) { return d.id == id; });
  if (it == cfg_.devices.end()) return Result::fail(404, say("device.unknown"));
  for (auto p = probeSessions_.begin(); p != probeSessions_.end();)  // a calibration ends with its device
    p = jstr(p->second, "device") == id ? probeSessions_.erase(p) : std::next(p);
  const json name = deviceName(cat_, it->id, it->cls, it->name);
  // Ausgänge dieses Geräts erst aus, dann vergessen.
  std::vector<std::string> on;
  cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
    if (b.device == id) on.push_back(role);
  });
  for (const auto& role : on)
    if (const RoleDef* rd = cat_.role(role); rd && !rd->profile.empty()) {
      Msg e;
      Ctx c = ctx();
      if (!act_.setRole(c, role, false, say("who.device_removed"), e))
        log_.add(clock_.epoch(), "block", "warn", say("ev.off_unconfirmed", {{"label", nameOf(*rd)}}),
                 say("ev.off_unconfirmed.device_removed", {{"reason", e}}), {{"role", role}, {"device", id}});
      if (const Binding* b = cfg_.binding(role)) releaseSocket(*rd, *b);
    }
  it = std::find_if(cfg_.devices.begin(), cfg_.devices.end(), [&](const DeviceCfg& d) { return d.id == id; });
  cfg_.devices.erase(it);
  auto unbind = [&](RoleMap& roles) {
    for (auto r = roles.begin(); r != roles.end();) r = r->second.device == id ? roles.erase(r) : std::next(r);
  };
  for (auto& t : cfg_.tanks) unbind(t.roles);
  for (auto& z : cfg_.zones) unbind(z.roles);
  for (auto& k : cfg_.canisters)
    if (k.pump == id) k.pump.clear();
  cfg_.calibrations.erase(id);
  saveConfig(say("ev.cfg.device_removed", {{"name", name}}));
  return Result::ok();
}

Result Hub::bindRole(const std::string& role, const std::string& device, int channel) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  next.rolesFor(role)[role] = {device, channel};
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  const RoleDef* rd = cat_.role(role);
  // Netzgerät: Schutzeinstellung schreiben und zurücklesen, erst dann binden.
  if (net_ && net_->owns(device) && rd && !rd->profile.empty()) {
    // During a Stop a fan socket stays off after a power loss too (PD-076).
    const SwitchSafety want = stopped_ && rd->onAfterPowerLoss ? SwitchSafety{} : safetyForRole(*rd);
    std::string e;
    if (!net_->configure(device, channel, want, e))
      return Result::fail(502, say("role.net.write", {{"reason", e}}));
    auto got = net_->readConfig(device, channel);
    if (!got || !sameSafety(*got, want))
      return Result::fail(502, say("role.net.verify"));
    const bool on = want.powerOn == PowerOn::On;
    Msg text = isNum(want.autoOffS) ? say(on ? "ev.net.power_on_auto" : "ev.net.power_off_auto", {{"min", want.autoOffS / 60}})
                                    : say(on ? "ev.net.power_on" : "ev.net.power_off");
    log_.add(clock_.epoch(), "device", "info", say("ev.net.protected", {{"label", nameOf(*rd)}}), text, {{"device", device}, {"channel", channel}});
  }
  // War die Rolle schon anderswo zugeordnet: dort erst ausschalten, sonst
  // verliert der Hub einen laufenden Ausgang aus dem Blick.
  if (const Binding* old = cfg_.binding(role); old && rd && !rd->profile.empty() && (old->device != device || old->channel != channel)) {
    Msg e;
    Ctx c = ctx();
    if (!act_.setRole(c, role, false, say("who.role_changed"), e))
      log_.add(clock_.epoch(), "block", "warn", say("ev.off_unconfirmed", {{"label", nameOf(*rd)}}),
               say("ev.off_unconfirmed.old_output", {{"reason", e}}), {{"role", role}, {"device", old->device}});
    releaseSocket(*rd, *old);
  }
  cfg_ = next;
  saveConfig(say("ev.cfg.role", {{"label", roleName(cat_, role)}}));
  return Result::ok();
}

Result Hub::switchRole(const std::string& role, bool on) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const RoleDef* rd = cat_.role(role);
  if (!rd || rd->profile.empty()) return Result::fail(404, say("role.unknown"));
  Ctx c = ctx();
  Msg e;
  if (!act_.setRole(c, role, on, say("who.manual"), e)) return Result::fail(409, e);
  testOff_.erase(role);
  log_.add(clock_.epoch(), "manual", "info", say(on ? "ev.manual_on" : "ev.manual_off", {{"label", nameOf(*rd)}}), say("ev.manual.text"),
           {{"role", role}, {"on", on}});
  return Result::ok();
}

Result Hub::testRole(const std::string& role) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const RoleDef* rd = cat_.role(role);
  if (!rd || rd->profile.empty()) return Result::fail(404, say("role.unknown"));
  if (act_.roleState(cfg_, role).value_or(false)) return Result::ok();  // läuft schon: nicht nach 3 s abschalten
  Ctx c = ctx();
  Msg e;
  if (!act_.setRole(c, role, true, say("who.test"), e)) return Result::fail(409, e);
  testOff_[role] = c.now + 3 * kSecond;
  return Result::ok();
}

// A socket that loses a role which comes back on after a power loss goes
// back to "off after power loss" (PD-050).
void Hub::releaseSocket(const RoleDef& rd, const Binding& b) {
  if (!rd.onAfterPowerLoss || !net_ || !net_->owns(b.device)) return;
  std::string e;
  bool ok = net_->configure(b.device, b.channel, SwitchSafety{}, e);
  json reason = e;
  if (ok) {
    auto got = net_->readConfig(b.device, b.channel);
    ok = got && sameSafety(*got, SwitchSafety{});
    if (!ok) reason = say("net.not_confirmed");
  }
  if (!ok)
    log_.add(clock_.epoch(), "device", "warn", say("ev.net.stays_on", {{"label", nameOf(rd)}}), say("ev.net.stays_on.text", {{"reason", reason}}),
             {{"device", b.device}, {"channel", b.channel}});
}

// Fan sockets come back on after a power loss, except during a Stop
// (PD-050, PD-076). Written and read back; a failure is reported.
void Hub::setFanSockets(bool comeBackOn) {
  if (!net_) return;
  cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
    const RoleDef* rd = cat_.role(role);
    if (!rd || !rd->onAfterPowerLoss || !net_->owns(b.device)) return;
    const SwitchSafety want = comeBackOn ? safetyForRole(*rd) : SwitchSafety{};
    std::string e;
    bool ok = net_->configure(b.device, b.channel, want, e);
    json reason = e;
    if (ok) {
      auto got = net_->readConfig(b.device, b.channel);
      ok = got && sameSafety(*got, want);
      if (!ok) reason = say("net.not_confirmed");
    }
    if (!ok)
      log_.add(clock_.epoch(), "device", "warn", say(comeBackOn ? "ev.net.on_not_set" : "ev.net.off_not_set", {{"label", nameOf(*rd)}}),
               say(comeBackOn ? "ev.net.on_not_set.text" : "ev.net.off_not_set.text", {{"reason", reason}}),
               {{"role", role}, {"device", b.device}});
  });
}

Result Hub::unbindRole(const std::string& role) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const RoleDef* rd = cat_.role(role);
  if (rd && !rd->profile.empty()) {
    Msg e;
    Ctx c = ctx();
    const Binding* old = cfg_.binding(role);
    if (old && !act_.setRole(c, role, false, say("who.role_removed"), e))
      log_.add(clock_.epoch(), "block", "warn", say("ev.off_unconfirmed", {{"label", nameOf(*rd)}}),
               say("ev.off_unconfirmed.unassigned", {{"reason", e}}), {{"role", role}, {"device", old->device}});
    if (old) releaseSocket(*rd, *old);
  }
  cfg_.rolesFor(role).erase(role);
  saveConfig(say("ev.cfg.role_removed", {{"role", roleName(cat_, role)}}));
  return Result::ok();
}

Result Hub::putTank(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  auto& t = next.tank();
  if (j.contains("name") && j["name"].is_string()) t.name = utf8Prefix(j["name"].get<std::string>(), 40);
  if (j.contains("capacityL")) t.capacityL = j["capacityL"].is_number() ? j["capacityL"].get<double>() : kNaN;
  if (j.contains("minL")) t.minL = j["minL"].is_number() ? j["minL"].get<double>() : kNaN;
  if (j.contains("water") && j["water"].is_string()) t.water = j["water"];
  if (j.contains("volumeL") && j["volumeL"].is_number()) rt_.tankVolumeL = j["volumeL"];
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  cfg_ = next;
  saveConfig(say("ev.cfg.tank", {{"name", t.name}}));
  return Result::ok();
}

Result Hub::putZone(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  auto& z = next.zone();
  if (j.contains("name") && j["name"].is_string()) z.name = utf8Prefix(j["name"].get<std::string>(), 40);
  if (j.contains("kind") && j["kind"].is_string()) z.kind = j["kind"];
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  cfg_ = next;
  saveConfig(say("ev.cfg.zone", {{"name", z.name}}));
  return Result::ok();
}

Result Hub::putCanister(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  std::string id = jstr(j, "id");
  CanisterCfg* k = nullptr;
  for (auto& x : next.canisters)
    if (x.id == id) k = &x;
  const std::string oldPair = k ? k->pair : "";
  const bool created = !k;
  if (!k) {
    std::string base = slug(jstr(j, "name", "kanister"));
    id = base;
    for (int n = 2; next.canister(id); ++n) id = base + "-" + std::to_string(n);
    next.canisters.push_back({});
    k = &next.canisters.back();
    k->id = id;
  }
  if (j.contains("name")) k->name = utf8Prefix(jstr(j, "name"), 40);
  if (j.contains("kind")) k->kind = jstr(j, "kind");
  if (j.contains("pump")) k->pump = jstr(j, "pump");
  if (j.contains("pair")) k->pair = jstr(j, "pair");
  if (j.contains("color")) k->color = jstr(j, "color");
  if (j.contains("capacityMl")) k->capacityMl = j["capacityMl"].is_number() ? j["capacityMl"].get<double>() : kNaN;
  if (k->name.empty()) return Result::fail(422, say("canister.name"));
  // A bottle that leaves its pair takes the label from a partner left alone, as removing it does
  const std::string unpaired = k->pair != oldPair ? unpairLone(next, oldPair) : "";
  auto errs = validateConfig(next, cat_);
  allowLonePair(errs);
  if (!errs.empty()) return errors(errs);
  if (j.contains("stockMl") && j["stockMl"].is_number()) {
    // A stock entered is never below zero; an unchanged one still saves, so a
    // bottle booked below zero can be renamed (SD-066)
    const double ml = j["stockMl"];
    auto was = created ? rt_.stockMl.end() : rt_.stockMl.find(id);
    if ((!isNum(ml) || ml < 0) && !(was != rt_.stockMl.end() && was->second == ml)) return Result::fail(422, say("canister.stock"));
  }
  cfg_ = next;
  // A new bottle starts with the stock entered; sent without one at all (the setup sets bottles up
  // full) at its size; with one left empty, or without a size, unknown. Never with a stock left
  // behind by a bottle of the same name (#161). An existing bottle keeps its stock, an unknown one
  // too, even when it first gets a size: a size says nothing about how full it is (RAT-015, #184).
  if (j.contains("stockMl") && j["stockMl"].is_number()) rt_.stockMl[id] = j["stockMl"];
  else if (created && !j.contains("stockMl") && isNum(k->capacityMl)) rt_.stockMl[id] = k->capacityMl;
  else if (created) rt_.stockMl.erase(id);
  stateDirty_ = true;
  saveConfig(unpaired.empty() ? say("ev.cfg.canister", {{"name", k->name}})
                              : say("ev.cfg.canister_unpairs", {{"name", k->name}, {"partner", unpaired}}));
  return Result::ok({{"id", id}});
}

Result Hub::deleteCanister(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  for (const auto& r : cfg_.recipes)
    for (const auto& s : r.steps)
      if (s.canister == id) return Result::fail(409, say("canister.used", {{"recipe", r.name}}));
  auto it = std::find_if(cfg_.canisters.begin(), cfg_.canisters.end(), [&](const CanisterCfg& k) { return k.id == id; });
  if (it == cfg_.canisters.end()) return Result::fail(404, say("canister.unknown"));
  const std::string name = it->name, pair = it->pair;
  cfg_.canisters.erase(it);
  rt_.stockMl.erase(id);
  stateDirty_ = true;  // the stock goes from the card too, or a new bottle of the same name would get it (#152)
  const std::string partner = unpairLone(cfg_, pair);
  if (!partner.empty()) saveConfig(say("ev.cfg.canister_removed_pair", {{"name", name}, {"partner", partner}}));
  else saveConfig(say("ev.cfg.canister_removed", {{"name", name}}));
  return Result::ok();
}

Result Hub::setStock(const std::string& id, double ml) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const CanisterCfg* k = cfg_.canister(id);
  if (!k) return Result::fail(404, say("canister.unknown"));
  if (!isNum(ml) || ml < 0) return Result::fail(422, say("canister.stock"));
  rt_.stockMl[id] = ml;
  stateDirty_ = true;
  log_.add(clock_.epoch(), "config", "info", say("ev.bottle_changed"), say("ev.bottle_changed.text", {{"name", k->name}, {"ml", ml}}));
  return Result::ok();
}

Result Hub::putRecipe(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  Config next = cfg_;
  std::string id = jstr(j, "id");
  RecipeCfg* r = nullptr;
  for (auto& x : next.recipes)
    if (x.id == id) r = &x;
  if (!r) {
    std::string base = slug(jstr(j, "name", "rezept"));
    id = base;
    for (int n = 2; next.recipe(id); ++n) id = base + "-" + std::to_string(n);
    next.recipes.push_back({});
    r = &next.recipes.back();
    r->id = id;
  }
  if (j.contains("name")) r->name = utf8Prefix(jstr(j, "name"), 40);
  if (j.contains("note")) r->note = jstr(j, "note");
  if (j.contains("steps")) {
    r->steps.clear();
    for (const auto& s : j["steps"]) {
      // Read wrongly as false, a fixed part would be scaled after all (RAT-085)
      if (s.contains("fixed") && !s["fixed"].is_boolean()) return Result::fail(422, say("recipe.fixed"));
      r->steps.push_back({jstr(s, "canister"), jnum(s, "mlPerL"), jbool(s, "fixed", false)});
    }
  }
  // The chart's EC and pH range (SD-057): a missing key keeps them, null clears them (R5)
  if (j.contains("refEc")) {
    if (!j["refEc"].is_null() && !j["refEc"].is_number()) return Result::fail(422, say("recipe.ref_ec"));
    r->refEc = jnum(j, "refEc");
  }
  if (j.contains("ph")) {
    const json& ph = j["ph"];
    if (ph.is_null()) {
      r->phLow = r->phHigh = kNaN;
    } else if (ph.is_array() && ph.size() == 2 && ph[0].is_number() && ph[1].is_number()) {
      r->phLow = ph[0].get<double>();
      r->phHigh = ph[1].get<double>();
    } else {
      return Result::fail(422, say("recipe.ph"));
    }
  }
  if (r->name.empty()) return Result::fail(422, say("recipe.name"));
  auto errs = validateConfig(next, cat_);
  allowLonePair(errs);
  if (!errs.empty()) return errors(errs);
  if (const RecipeCfg* before = cfg_.recipe(id)) recipeEdited(*before, *r);
  else forgetEcEffect(id);  // a new recipe never takes over what was learned for one of that ID (SD-092, as #161)
  cfg_ = next;
  saveConfig(say("ev.cfg.recipe", {{"name", r->name}}));
  return Result::ok({{"id", id}});
}

// An edit or an import of a recipe: what its learned EC effect keeps (scaled up by parts no longer
// scaled, kept only at or above its new start), and a round planned on its old parts learns nothing
// (SD-092, RAT-085, RAT-057)
void Hub::recipeEdited(const RecipeCfg& before, const RecipeCfg& after) {
  const EffectEdit e = editEcEffect(before, after, learnedEcEffect(rt_, after.id));
  if (!e.changed) return;
  ec_.recipeChanged(after);
  if (isNum(e.learned)) rt_.ecEffects[after.id] = e.learned;
  else rt_.ecEffects.erase(after.id);
  stateDirty_ = true;
}

// What was learned for a recipe goes with it, and a round on it that still settles learns nothing (SD-092)
void Hub::forgetEcEffect(const std::string& id) {
  ec_.recipeGone(id);
  if (rt_.ecEffects.erase(id)) stateDirty_ = true;
}

Result Hub::deleteRecipe(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto it = std::find_if(cfg_.recipes.begin(), cfg_.recipes.end(), [&](const RecipeCfg& r) { return r.id == id; });
  if (it == cfg_.recipes.end()) return Result::fail(404, say("recipe.unknown"));
  std::string name = it->name;
  cfg_.recipes.erase(it);
  forgetEcEffect(id);
  saveConfig(say("ev.cfg.recipe_removed", {{"name", name}}));
  return Result::ok();
}

Result Hub::applyRecipeTemplate(const std::string& templateId, const json& map, const std::string& langIn) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // Name, note and part names in the language of the page that asks (it can
  // differ from the hub's), otherwise in the system language (#32).
  if (!langIn.empty() && langIn != "de" && langIn != "en")
    return Result::fail(422, say("recipe.template.lang"));
  const std::string lang = !langIn.empty() ? langIn : language();  // English first (SD-014)
  auto localized = [&](const json& o, const char* key) {
    const std::string en = jstr(o, (std::string(key) + "En").c_str());
    return lang == "en" && !en.empty() ? en : jstr(o, key);
  };
  // Zuordnung je Teil der Vorlage: ausdrücklich (map: Rolle → Kanister),
  // sonst über den Namen (ohne Groß/Klein). Die Reihenfolge der Vorlage ist die
  // Mischreihenfolge der Herstellertabelle (Athena: B vor A, CalMag danach).
  // Ein Kanister darf nur einen Teil tragen; ein Paar der Vorlage (A/B) wird
  // auf Kanister ohne eigenes Paar übernommen, damit sie gemeinsam skalieren.
  auto lower = [](std::string x) {
    for (auto& ch : x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return x;
  };
  for (const auto& t : cat_.templates.value("recipes", json::array())) {
    if (jstr(t, "id") != templateId) continue;
    json steps = json::array();
    std::vector<std::string> missing;
    std::map<std::string, std::string> pairs;  // Kanister → Paar aus der Vorlage
    std::set<std::string> used;
    for (const auto& s : t.value("steps", json::array())) {
      const std::string role = jstr(s, "role");
      const CanisterCfg* k = nullptr;
      if (map.is_object() && map.contains(role) && map[role].is_string()) k = cfg_.canister(map[role].get<std::string>());
      // By name: first in the language of the request, then in the other.
      const std::string shown = localized(s, "name");
      for (const std::string& name : {shown, jstr(s, "name"), jstr(s, "nameEn")})
        for (const auto& x : cfg_.canisters)
          if (!k && !name.empty() && lower(x.name) == lower(name)) k = &x;
      if (!k) {
        missing.push_back(shown);
        continue;
      }
      if (!used.insert(k->id).second)
        return Result::fail(422, say("recipe.template.twice", {{"name", k->name}}));
      if (!jstr(s, "pair").empty()) pairs[k->id] = jstr(s, "pair");
      steps.push_back({{"canister", k->id}, {"mlPerL", jnum(s, "mlPerL")}});
    }
    if (!missing.empty()) return Result::fail(422, say("recipe.template", {{"missing", missing}}), {{"missing", missing}});
    // The chart's EC and pH range come along (SD-057)
    Result r = putRecipe({{"name", localized(t, "name")},
                          {"note", localized(t, "note")},
                          {"steps", steps},
                          {"refEc", t.contains("ec") ? t["ec"] : json(nullptr)},
                          {"ph", t.contains("ph") ? t["ph"] : json(nullptr)}});
    if (r.status != 200) return r;
    // Ist der Paarname schon an anderen Kanistern vergeben, einen freien
    // wählen (AB2, AB3, …), sonst zählte das Paar vier Kanister.
    Config next = cfg_;
    std::map<std::string, std::string> rename;  // Paar der Vorlage → freier Name
    for (const auto& [id, p] : pairs) {
      if (rename.count(p)) continue;
      auto takenByOthers = [&](const std::string& name) {
        for (const auto& k : next.canisters)
          if (k.pair == name && !pairs.count(k.id)) return true;
        return false;
      };
      std::string name = p;
      for (int n = 2; takenByOthers(name); ++n) name = p + std::to_string(n);
      rename[p] = name;
    }
    bool changed = false;
    for (auto& k : next.canisters)
      if (pairs.count(k.id) && k.pair.empty() && k.kind == "nutrient") {
        k.pair = rename[pairs[k.id]];
        changed = true;
      }
    if (!changed) return r;
    // The pairing goes in when it adds no problem the configuration did not have already: another
    // bottle waiting for its partner does not stop it, and no part is left alone with a label (#152).
    // Counted, since two recipes may share a name and so the text of a problem.
    std::map<std::pair<std::string, std::string>, int> before;
    for (const auto& m : validateConfig(cfg_, cat_)) ++before[{m.key, m.text}];
    bool addsProblem = false;
    for (const auto& m : validateConfig(next, cat_)) addsProblem |= --before[{m.key, m.text}] < 0;
    if (!addsProblem) {
      cfg_ = next;
      saveConfig(say("ev.cfg.pair_template"));
    }
    return r;
  }
  return Result::fail(404, say("recipe.template.unknown"));
}

Result Hub::putFunction(const std::string& id, const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const FunctionDef* fd = cat_.function(id);
  if (!fd) return Result::fail(404, say("function.unknown"));
  Config next = cfg_;
  auto& f = next.functions[id];
  if (j.contains("params") && j["params"].is_object())
    for (const auto& [k, v] : j["params"].items()) f.params[k] = v;
  if (j.contains("enabled") && j["enabled"].is_boolean()) {
    bool en = j["enabled"];
    if (en && !fd->alwaysOn) {
      // Einschalten nur, wenn die Einrichtung vollständig ist ("eingeschränkt" genügt).
      for (const auto& fs : functions_)
        if (fs.id == id && (fs.setup == "unavailable" || fs.setup == "needs_setup"))
          return Result::fail(409, say("function.not_ready", {{"reason", fs.summary}}));
    }
    f.enabled = en;
  }
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  cfg_ = next;
  functions_ = resolveFunctions(cat_, cfg_, devices_, pumps_, truth_);
  const bool params = !j.contains("enabled");
  saveConfig(say(params ? "ev.cfg.function_params" : f.enabled ? "ev.cfg.function_on" : "ev.cfg.function_off", {{"label", nameOf(*fd)}}));
  return Result::ok();
}

Result Hub::importConfig(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!j.is_object()) return Result::fail(422, say("config.not_object"));
  // A newer schema in words of its own, before anything a newer version may write differently;
  // anything else that is no version is refused when it is read
  if (j.contains("schemaVersion")) {
    const json& v = j["schemaVersion"];
    const bool newer = v.is_number_unsigned() ? v.get<std::uint64_t>() > static_cast<std::uint64_t>(kSchemaVersion)
                     : v.is_number_integer()  ? v.get<std::int64_t>() > kSchemaVersion
                     : v.is_number_float()    ? std::isfinite(v.get<double>()) && v.get<double>() == std::floor(v.get<double>()) && v.get<double>() > kSchemaVersion
                                              : false;
    if (newer) return Result::fail(422, say("config.newer"));
  }
  // Reading the saved file falls back to the default zone; a file chosen to import is refused instead.
  if (j.contains("system") && j["system"].is_object() && j["system"].contains("timezone")) {
    const json& tz = j["system"]["timezone"];
    if (!tz.is_string() || !isTimeZoneName(tz.get<std::string>())) return Result::fail(422, say("system.timezone"));
  }
  Config next;
  try {
    next = configFromJson(j);
  } catch (const std::exception& e) {
    return Result::fail(422, say("config.import", {{"reason", e.what()}}));
  }
  // A calibration of a device that is not in the file (left over in an older backup) is left out
  for (auto c = next.calibrations.begin(); c != next.calibrations.end();)
    c = next.device(c->first) ? std::next(c) : next.calibrations.erase(c);
  auto errs = validateConfig(next, cat_);
  for (const auto& [dev, kinds] : next.calibrations)
    for (const auto& [kind, data] : kinds) {
      if (auto e = SensorTruth::checkCalibration(kind, data))
        errs.push_back(say("cfg.calibration", {{"device", dev}, {"kind", kind}, {"reason", *e}}));
      // Only what the device's class offers: a file cannot give a sensor calibrated elsewhere a calibration (RAT-025)
      const DeviceClassDef* dc = cat_.deviceClass(next.device(dev)->cls);
      if (!dc || std::find(dc->calibrations.begin(), dc->calibrations.end(), kind) == dc->calibrations.end())
        errs.push_back(say("cfg.calibration.offered", {{"device", dev}, {"kind", kind}}));
    }
  if (!errs.empty()) return errors(errs);
  if (userJobActive() || doser_.busy())
    return Result::fail(409, say("config.busy"));
  act_.stopAll(cat_, cfg_, clock_.nowMs());
  // A socket that loses its fan role goes back to "off after power loss";
  // every switched socket in the import gets the setting of its role.
  cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
    const RoleDef* rd = cat_.role(role);
    const Binding* nb = next.binding(role);
    if (rd && (!nb || nb->device != b.device || nb->channel != b.channel)) releaseSocket(*rd, b);
  });
  next.revision = cfg_.revision;
  // Die Sperre gegen eine zweite Einrichtung kommt nie aus einer Datei.
  next.system.passwordSet = cfg_.system.passwordSet || auth_.hasPassword();
  // Only the stock of a bottle the hub had and the file keeps stays; a bottle new to the hub, now or
  // later, never takes over one left behind (#161)
  for (auto it = rt_.stockMl.begin(); it != rt_.stockMl.end();)
    it = cfg_.canister(it->first) && next.canister(it->first) ? std::next(it) : rt_.stockMl.erase(it);
  // So does what EC control learned for a recipe the hub had and the file keeps, as edited by it; one
  // the file leaves out or brings new starts afresh (SD-092, as #161)
  for (const auto& r : cfg_.recipes)
    if (!next.recipe(r.id)) forgetEcEffect(r.id);
  for (const auto& nr : next.recipes) {
    if (const RecipeCfg* before = cfg_.recipe(nr.id)) recipeEdited(*before, nr);
    else forgetEcEffect(nr.id);
  }
  stateDirty_ = true;
  cfg_ = next;
  if (net_)
    cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
      const RoleDef* rd = cat_.role(role);
      if (!rd || rd->profile.empty() || !net_->owns(b.device)) return;
      const SwitchSafety want = stopped_ && rd->onAfterPowerLoss ? SwitchSafety{} : safetyForRole(*rd);
      std::string e;
      bool ok = net_->configure(b.device, b.channel, want, e);
      if (ok) {
        auto got = net_->readConfig(b.device, b.channel);
        ok = got && sameSafety(*got, want);
      }
      if (!ok)
        log_.add(clock_.epoch(), "device", "warn", say("ev.net.import_unconfirmed", {{"label", nameOf(*rd)}}),
                 e.empty() ? say("ev.net.import_unconfirmed.text") : say("ev.net.import_unconfirmed.text_reason", {{"reason", e}}),
                 {{"role", role}, {"device", b.device}});
    });
  saveConfig(say("ev.cfg.imported"));
  return Result::ok();
}

// ------------------------------------------------------------------ Aufträge

// A mix as asked: the recipe's amounts, or a fixed pattern to a target EC from the water's EC, whose
// target lies in the EC target's range (SD-057 way 2, SD-098); a missing value stays missing (R5)
MixRequest Hub::mixRequest(const json& req) const {
  MixRequest r{jstr(req, "recipe"), jnum(req, "waterL"), jstr(req, "mode", "new"), jbool(req, "confirmRepeat", false)};
  // A target or water EC asked without "target", or amounts of another type, never mix the full recipe
  const auto given = [&](const char* k) { return req.is_object() && req.contains(k) && !req[k].is_null(); };
  r.amounts = given("amounts") ? jstr(req, "amounts") : "recipe";
  if (r.amounts == "recipe" && (given("targetEc") || given("waterEc"))) r.amounts.clear();
  r.targetEc = jnum(req, "targetEc");
  r.waterEc = jnum(req, "waterEc");
  if (const FunctionDef* fn = cat_.function("ec_control"))
    if (const ParamDef* pd = fn->param("ec_target")) {
      r.targetMin = pd->min;
      r.targetMax = pd->max;
    }
  r.ecControlOn = controllerFree("ec_control", "ec.no_effect");
  if (r.ecControlOn) r.ecControlTarget = effectiveParams(cat_, cfg_, "ec_control").num("ec_target");
  auto ph = cfg_.functions.find("ph_control");
  if (ph != cfg_.functions.end() && ph->second.enabled) r.phEcFloor = effectiveParams(cat_, cfg_, "ph_control").num("ec_floor");
  return r;
}

Result Hub::mixPlan(const json& req) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  const MixRequest r = mixRequest(req);
  auto it = cfg_.functions.find("ph_control");
  bool phAuto = it != cfg_.functions.end() && it->second.enabled;
  auto plan = planMix(cfg_, rt_, pumps_, r, clock_.epoch(), phAuto);
  return Result::ok(plan);
}

Result Hub::mixStart(const json& req) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (userJobActive()) return Result::fail(409, say("job.busy"));  // kein Doppelstart (RAT-003)
  // A mix stopped part way is resumed or ended first, never mixed over (RAT-003)
  if (job_ && job_->type == "mix" && job_->state == "failed") return Result::fail(409, say("mix.failed_open"));
  if (doser_.busy()) return Result::fail(409, say("job.dosing"));
  if (stopped_) return Result::fail(409, say("act.stopped"));
  const MixRequest r = mixRequest(req);
  auto it = cfg_.functions.find("ph_control");
  bool phAuto = it != cfg_.functions.end() && it->second.enabled;
  auto plan = planMix(cfg_, rt_, pumps_, r, clock_.epoch(), phAuto);
  if (!plan.ok) {
    json b = plan;
    // the first error as the message it is, with its values (SD-032)
    return Result::fail(422, plan.errors.empty() ? say("mix.invalid") : plan.errors.front(),
                        {{"plan", b}});
  }
  // Abgleich des Rezepts mit den wirklich vorhandenen Kanistern vor der ersten Gabe.
  Job j;
  j.id = newId("mix");
  j.type = "mix";
  j.guided = jbool(req, "guided", true);
  j.startedAt = clock_.epoch();
  j.circulation = cfg_.binding("tank.circulation") != nullptr;
  j.info = {{"recipe", plan.recipe}, {"recipeName", plan.recipeName}, {"waterL", plan.waterL}, {"mode", plan.mode},
            {"after", plan.after}, {"amounts", plan.amounts}};
  if (plan.amounts == "target")  // the pattern runs without feedback: what it aimed at stays with it (SD-098)
    j.info.update({{"targetEc", plan.targetEc}, {"waterEc", plan.waterEc}, {"expectedEc", numOrNull(plan.expectedEc)}});
  for (const auto& s : plan.steps) j.steps.push_back({s, "pending", 0});
  rt_.tankVolumeL = plan.mode == "topup" && isNum(rt_.tankVolumeL) ? rt_.tankVolumeL + plan.waterL : plan.waterL;
  rt_.lastMixAt = clock_.epoch();
  stateDirty_ = true;
  const bool topup = plan.mode == "topup";
  json args = {{"recipe", plan.recipeName}, {"water", plan.waterL}};
  if (plan.amounts == "target") args.update({{"target", plan.targetEc}, {"waterEc", plan.waterEc}, {"expected", numOrNull(plan.expectedEc)}});
  const Msg text = plan.amounts == "target" ? say(topup ? "ev.mix.started.target_topup" : "ev.mix.started.target_new", args)
                                            : say(topup ? "ev.mix.started.topup" : "ev.mix.started.new", args);
  log_.add(clock_.epoch(), "mix", "info", say("ev.mix.started"), text, {{"job", j.id}, {"recipe", plan.recipe}, {"waterL", plan.waterL}});
  job_ = j;
  Ctx c = ctx();
  if (!startJobStep(c)) return Result::fail(422, job_->message, {{"job", *job_}});
  saveJob();
  return Result::ok({{"job", *job_}});
}

bool Hub::startJobStep(Ctx& c) {
  Job& j = *job_;
  auto& st = j.steps[j.index];
  DoseOrder o;
  // Jeder Versuch bekommt eine eigene ID: dieselbe ID gilt beim Dosierblock als
  // Wiederholung desselben Auftrags und läuft nicht noch einmal (Schutz gegen Doppeldosierung).
  o.id = j.id + "-" + std::to_string(j.index + 1) + "." + std::to_string(++idSeq_);
  o.purpose = j.type == "mix" ? "mix" : j.type;
  o.step = st.dose;
  // Restmenge nach einer Unterbrechung neu in Läufe teilen
  if (st.mlDone > 0 && isNum(st.dose.ml)) {
    Msg err;
    o.step.ml = st.dose.ml - st.mlDone;
    o.step.runs = splitRuns(o.step.ml, o.step.flowMlPerMin, cfg_.limits, 0, err);
    if (o.step.ml <= 0 || err.key == "dose.too_small") {
      // Kein Rest oder Rest unter einem genauen Lauf: als erledigt werten und
      // weiterschalten. Achtung: Das kann den Auftrag beenden (job_ ist danach leer).
      DoseProgress rest;
      rest.state = DoseProgress::State::Done;
      rest.orderId = o.id;
      onJobDose(c, rest);
      return true;
    }
    if (!err.key.empty()) {
      st.state = "failed";
      j.state = "failed";
      j.message = say("job.start_failed", {{"name", st.dose.name}, {"reason", err}});
      return false;
    }
  }
  Msg err;
  if (!doser_.start(c, act_, o, err)) {
    st.state = "failed";
    j.state = "failed";
    j.message = say("job.start_failed", {{"name", st.dose.name}, {"reason", err}});
    return false;
  }
  st.state = "running";
  j.state = "running";
  j.message = say("job.dosing_step", {{"n", j.index + 1}, {"total", j.steps.size()}, {"name", st.dose.name}, {"ml", numOrNull(o.step.ml)}});
  return true;
}

json Hub::jobJson() const {
  return {{"job", job_ ? json(*job_) : json(nullptr)}, {"lastJob", lastJob_ ? json(*lastJob_) : json(nullptr)}};
}

void Hub::finishJob(const std::string& state, Msg m) {
  Job& j = *job_;
  j.state = state;
  j.message = std::move(m);
  j.finishedAt = clock_.epoch();
  // The window against mixing twice counts from when the nutrients went in, not from the start:
  // a guided mix stirred by hand can take longer than the window (RAT-003)
  if (j.type == "mix") {
    rt_.lastMixAt = std::max(rt_.lastMixAt, j.finishedAt);  // never earlier, even after a clock step
    stateDirty_ = true;
  }
  saveJob();
  lastJob_ = j;
  if (state == "done" || state == "aborted") job_.reset();
}

namespace {
// Each part of a pair with a part in the tank that is not complete, with what is still missing: the
// ratio is off until it is in (RAT-054)
json missingPairParts(const Job& j) {
  json out = json::array();
  for (const auto& s : j.steps) {
    if (s.dose.pair.empty() || s.state == "done") continue;
    const bool started = std::any_of(j.steps.begin(), j.steps.end(),
                                     [&](const JobStep& o) { return o.dose.pair == s.dose.pair && o.mlDone > 0; });
    if (started) out.push_back(say("amount", {{"name", s.dose.name}, {"ml", numOrNull(isNum(s.dose.ml) ? std::max(0.0, s.dose.ml - s.mlDone) : kNaN)}}));
  }
  return out;
}
}  // namespace

bool Hub::controllerFree(const char* fn, const char* latch) const {
  // switched on and not locked: a locked controller waits for a person, not for the mixed tank
  auto it = cfg_.functions.find(fn);
  return it != cfg_.functions.end() && it->second.enabled && !rt_.latches.count(latch);
}

const RecipeCfg* Hub::ecRecipe() const {
  if (const RecipeCfg* r = cfg_.recipe(effectiveParams(cat_, cfg_, "ec_control").str("recipe"))) return r;
  return cfg_.recipes.empty() ? nullptr : &cfg_.recipes.front();
}
bool Hub::controlOn() const { return controllerFree("ec_control", "ec.no_effect") || controllerFree("ph_control", "ph.no_effect"); }

Ms Hub::mixSettleMs() const {
  auto on = [&](const char* fn) {
    auto it = cfg_.functions.find(fn);
    return it != cfg_.functions.end() && it->second.enabled;
  };
  // the controller's own settle time in seconds, read as it reads it: missing or out of range is 5 min
  auto settle = [&](const char* fn) -> double {
    if (!on(fn)) return 0;
    const double m = effectiveParams(cat_, cfg_, fn).num("settle_min");
    return (isNum(m) && m > 0 && m <= 24 * 60 ? m : 5) * 60;
  };
  double s = std::max(settle("ec_control"), settle("ph_control"));
  if (s == 0) s = 5 * 60;  // no control waits, but a pH measured by hand needs a mixed tank too
  if (on("ph_control")) s = std::max(s, 240.0);  // pH is judged 240 s after an EC-raising dose at the earliest (RAT-046)
  // At least the measured time to tolerance with margin, about 100 s at 20 L (RAT-058), scaled with
  // the volume in the tank; the scaling is an assumption, as in the twin's mixing model (RAT-052)
  const double v = tankVolume();
  if (isNum(v) && v > 0) s = std::max(s, 100.0 * v / 20.0);
  // A fixed ceiling (R7), the longest settle time the catalog allows: a volume typed wrong never holds
  // control for longer
  return static_cast<Ms>(std::min(s, 60.0 * 60) * kSecond);
}

void Hub::finishMixDone(Ctx& c, bool unmixed) {
  Job& j = *job_;
  json summary = json::array();
  double total = 0;
  for (const auto& s : j.steps) {
    summary.push_back(say("amount", {{"name", s.dose.name}, {"ml", s.mlDone}}));
    total += s.mlDone;
  }
  const json after = j.info.contains("after") && j.info["after"].is_object() ? j.info["after"] : json(nullptr);
  json args{{"water", numOrNull(jnum(j.info, "waterL"))}, {"recipe", jstr(j.info, "recipeName")}, {"after", after}};
  args["summary"] = summary;
  log_.add(c.epoch, "mix", "info", say("ev.mix.done"), say("ev.mix.done.text", args), {{"job", j.id}, {"totalMl", total}});
  // Without control, nothing waits for the pump afterwards: the texts say to mix before measuring
  const bool ctl = controlOn();
  finishJob("done", !unmixed ? say("mix.done", args) : ctl ? say("mix.done_unmixed", args) : say("mix.done_unmixed_plain", args));
}

void Hub::mixCut(bool byStop, bool doseCut) {
  if (!job_ || job_->type != "mix" || job_->cutMarked) return;
  const bool anyIn = doseCut || std::any_of(job_->steps.begin(), job_->steps.end(), [](const JobStep& s) { return s.mlDone > 0; });
  if (!anyIn) return;
  job_->cutMarked = true;  // once until more goes in: ending a failed mix later adds nothing new
  settleControllersFirst(byStop);
}

void Hub::settleControllersFirst(bool byStop) {
  // Both settle first, for as long as each other's settle time and the volume need, so they end
  // together and EC decides first, as after every round (RAT-004, the tie in `tickImpl`); pH's own
  // mark keeps it waiting also when EC control is switched off meanwhile. A locked controller carries
  // none: it would hold the other until someone acknowledges. One in a dosing round of its own keeps
  // the round, which settles after its doses anyway; a reset would drop a pair's second part (RAT-054).
  // The settle announces nothing new: EC is announced while it settles, as after any EC round, and a
  // pH reading that jumps while the nutrients mix in locks, the safe side (RAT-042).
  const Ms floor = std::max<Ms>(15 * kMinute, mixSettleMs());
  if (controllerFree("ec_control", "ec.no_effect") && !ec_.dosing()) ec_.settleFirst(byStop, floor);
  if (controllerFree("ph_control", "ph.no_effect") && !ph_.dosing()) ph_.settleFirst(byStop, floor);
}

// A new job replaces a failed mix only after ending it as an abort, which reports what is in the
// tank and starts the window against mixing twice (RAT-003); before, it was dropped without a trace.
void Hub::endFailedMix() {
  if (job_ && job_->type == "mix" && job_->state == "failed") jobAbort(job_->id);
}

void Hub::onJobDose(Ctx& c, const DoseProgress& p) {
  // only a dose of this job, and only while a part is due: a mix that mixes in at its end has none
  if (!job_ || p.orderId.rfind(job_->id + "-", 0) != 0 || job_->index >= job_->steps.size()) return;
  Job& j = *job_;
  auto& st = j.steps[j.index];
  st.mlDone += p.mlDone;
  if (p.state == DoseProgress::State::Aborted) return;
  if (p.state == DoseProgress::State::Failed) {
    st.state = "failed";
    j.state = "failed";
    // Paar-Fehler sichtbar machen: Partner schon drin? (Vorschlag anwender, RAT-054)
    std::string partner;
    if (!st.dose.pair.empty())
      for (const auto& o : j.steps)
        if (&o != &st && o.dose.pair == st.dose.pair && o.mlDone > 0) partner = o.dose.name;
    const bool alone = partner.empty();
    j.message = say(alone ? "job.dose_failed" : "job.pair_failed",
                    {{"name", st.dose.name}, {"reason", p.error}, {"partner", partner}, {"step", j.index}});
    const bool mix = j.type == "mix";
    log_.add(c.epoch, "mix", "alarm", say(mix ? "ev.mix.interrupted" : "ev.job.failed"), j.message, {{"job", j.id}});
    mixCut(false);
    if (j.type != "mix") finishJob("failed", j.message);
    saveJob();
    return;
  }
  st.state = "done";
  if (j.type == "calibration") {
    j.state = "waiting_user";
    j.info["actualMs"] = p.msDone;
    j.message = say("cal.measure");
    saveJob();
    return;
  }
  if (j.type == "prime" || j.type == "manual") {
    finishJob("done", say("job.done", {{"name", st.dose.name}, {"ml", st.mlDone}}));
    return;
  }
  j.index++;
  if (j.index >= j.steps.size()) {
    // With the circulation pump, the nutrients mix in before the mix ends, so EC and pH never decide
    // on a tank that has not mixed (SD-057, RAT-052)
    const Ms settle = j.circulation ? mixSettleMs() : 0;
    if (settle > 0) {
      j.state = "settling";
      j.waitUntil = c.now + settle;
      const Ms min = (settle + kMinute - 1) / kMinute;
      j.message = controlOn() ? say("mix.settling", {{"min", min}}) : say("mix.settling_plain", {{"min", min}});
      saveJob();
      return;
    }
    finishMixDone(c, false);
    return;
  }
  // Zwischen den Gaben durchmischen: mit Umwälzpumpe Countdown, sonst von Hand rühren.
  if (j.circulation) {
    j.state = "mixing";
    j.waitUntil = c.now + 60 * kSecond;
    j.message = say("mix.circulate");
  } else if (j.guided) {
    j.state = "waiting_user";
    j.waitUntil = c.now + kStirWaitMax;  // a mix never waits without end (SD-057, SD-089)
    j.message = say("mix.stir", {{"name", st.dose.name}, {"ml", st.mlDone}});
  } else {
    startJobStep(c);
  }
  saveJob();
}

void Hub::tickJob(Ctx& c) {
  if (!job_) return;
  Job& j = *job_;
  if (j.state == "mixing" && c.now >= j.waitUntil) {
    if (startJobStep(c)) saveJob();
  } else if (j.state == "settling") {
    // A pump that stands still mixes nothing: the card says so, the mix still ends, and EC and pH then
    // wait for the pump themselves instead of deciding on a tank that has not mixed (SD-057, SD-073).
    // Off or unknown for kPumpStandsAfter counts, not a single missed report, which mixes in as much.
    const bool pumping = act_.roleState(cfg_, "tank.circulation").value_or(false);
    if (pumping) j.pumpOffSince = 0;
    else if (j.pumpOffSince == 0) j.pumpOffSince = std::max<Ms>(c.now, 1);
    const bool stands = !pumping && c.now - j.pumpOffSince >= kPumpStandsAfter;
    if (stands) j.unmixed = true;
    if (c.now >= j.waitUntil) {
      const bool unmixed = j.unmixed;
      finishMixDone(c, unmixed);
      if (unmixed) settleControllersFirst(false);
    } else if (stands != j.pumpStands) {
      // Says when it stands still; back on (only a running pump ends standing still), it says how the
      // mix will end: not mixed yet, and mixed once more by control
      j.pumpStands = stands;
      const bool ctl = controlOn();
      if (stands) j.message = ctl ? say("mix.settling_no_circ") : say("mix.settling_no_circ_plain");
      else j.message = ctl ? say("mix.settling_resumed") : say("mix.settling_resumed_plain");
      saveJob();
    }
  } else if (j.type == "mix" && j.state == "waiting_user" && j.waitUntil > 0 && c.now >= j.waitUntil) {
    // Nobody tapped "Continue" in time: the mix stops, open to go on with the next part or to be ended
    // (RAT-003). The parts of a pair still missing are named with what is missing (RAT-054).
    const json partners = missingPairParts(j);
    const std::string last = j.index > 0 ? j.steps[j.index - 1].dose.name : std::string();
    const Ms min = kStirWaitMax / kMinute;
    j.state = "failed";
    j.message = partners.empty() ? say("mix.stir_timeout", {{"min", min}, {"name", last}})
                                 : say("mix.stir_timeout_pair", {{"min", min}, {"name", last}, {"partners", partners}});
    // A forgotten tap is no danger by itself; a pair half in is
    log_.add(c.epoch, "mix", partners.empty() ? "warn" : "alarm", say("ev.mix.stir_timeout"), j.message, {{"job", j.id}});
    mixCut(false);
    saveJob();
  }
}

Result Hub::jobContinue(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!job_ || job_->id != id) return Result::fail(404, say("job.unknown"));
  if (job_->state != "waiting_user" && job_->state != "mixing")
    return Result::fail(409, say("job.state"));
  if (job_->type == "calibration") return Result::fail(409, say("job.state_amount"));
  Ctx c = ctx();
  if (!startJobStep(c)) return Result::fail(422, job_->message, {{"job", *job_}});
  saveJob();
  return Result::ok(jobJson());
}

Result Hub::jobResume(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!job_ || job_->id != id) return Result::fail(404, say("job.unknown"));
  if (job_->state != "failed") return Result::fail(409, say("job.state_resume"));
  // Einmesswert und Pumpe neu lesen (z. B. nach dem Tausch der Kappe)
  auto& st = job_->steps[job_->index];
  if (auto it = pumps_.find(st.dose.pump); it != pumps_.end()) st.dose.flowMlPerMin = it->second.flowMlPerMin;
  const std::string name = st.dose.name;  // st gilt nach startJobStep nicht mehr sicher
  const bool cutShort = st.state == "failed";  // else the mix waited for stirring and goes on (SD-057)
  Ctx c = ctx();
  if (!startJobStep(c)) return Result::fail(422, job_->message, {{"job", *job_}});
  // A rest below an accurate run counts as in and can end the mix right away: then the job is gone
  // and "Mix done" says what happened. Else more goes in, and a later cut marks the controllers again.
  if (!job_) return Result::ok(jobJson());
  job_->cutMarked = false;
  log_.add(clock_.epoch(), "mix", "info", say("ev.mix.resumed"), say(cutShort ? "ev.mix.resumed.text" : "ev.mix.resumed.next", {{"name", name}}));
  saveJob();
  return Result::ok(jobJson());
}

Result Hub::jobAbort(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!job_ || job_->id != id) return Result::fail(404, say("job.unknown"));
  Ctx c = ctx();
  if (doser_.busy() && doser_.active()->id.rfind(id, 0) == 0) doser_.abort(c, act_, say("abort.job"));
  if (auto fin = doser_.takeFinished()) onJobDose(c, *fin);
  const json done = amounts(job_->steps);
  const bool none = done.empty();
  const bool mix = job_->type == "mix";
  log_.add(clock_.epoch(), "mix", "warn", say(mix ? "ev.mix.aborted" : "ev.job.aborted"), say(none ? "ev.contents_none" : "ev.contents", {{"done", done}}), {{"job", id}});
  // Cancelled with a pair half in: the parts still missing are named (SD-057, RAT-054)
  const json partners = mix ? missingPairParts(*job_) : json::array();
  mixCut(false);
  finishJob("aborted", partners.empty() ? say(none ? "job.aborted_none" : "job.aborted", {{"done", done}})
                                        : say("job.aborted_pair", {{"done", done}, {"partners", partners}}));
  return Result::ok();
}

Result Hub::manualDose(const std::string& canister, double ml) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (userJobActive()) return Result::fail(409, say("job.busy"));
  if (doser_.busy()) return Result::fail(409, say("job.dosing"));
  const CanisterCfg* k = cfg_.canister(canister);
  if (!k) return Result::fail(404, say("canister.unknown"));
  if (!isNum(ml) || ml <= 0) return Result::fail(422, say("dose.amount"));
  const Limits lim = cfg_.limits.bounded();
  if (ml > lim.handDoseMaxMl)
    return Result::fail(422, say("dose.hand_limit", {{"max", lim.handDoseMaxMl}}));
  auto pit = pumps_.find(k->pump);
  if (k->pump.empty() || pit == pumps_.end() || !pit->second.online)
    return Result::fail(422, say("dose.pump", {{"name", k->name}}));
  DoseStep s;
  s.canister = k->id;
  s.name = k->name;
  s.pump = k->pump;
  s.color = k->color;
  s.pair = k->pair;
  s.ml = ml;
  s.flowMlPerMin = pit->second.flowMlPerMin;
  Msg err;
  s.runs = splitRuns(ml, s.flowMlPerMin, cfg_.limits, lim.maxPartialRuns, err);
  if (!err.key.empty()) return Result::fail(422, say("job.start_failed", {{"name", k->name}, {"reason", err}}));
  Job j;
  j.id = newId("dose");
  j.type = "manual";
  j.startedAt = clock_.epoch();
  j.steps.push_back({s, "pending", 0});
  endFailedMix();
  job_ = j;
  Ctx c = ctx();
  if (!startJobStep(c)) {
    Msg m = job_->message;
    job_.reset();
    return Result::fail(422, m);
  }
  if (!k->pair.empty())
    log_.add(clock_.epoch(), "dose", "notice", say("ev.manual_pair"), say("ev.manual_pair.text", {{"name", k->name}, {"pair", k->pair}}));
  return Result::ok({{"job", *job_}});
}

Result Hub::startCalibration(const std::string& pump, double seconds) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (userJobActive()) return Result::fail(409, say("job.busy"));
  if (doser_.busy()) return Result::fail(409, say("job.dosing"));
  auto pit = pumps_.find(pump);
  if (pit == pumps_.end() || !pit->second.online) return Result::fail(404, say("pump.unknown"));
  if (!isNum(seconds) || seconds < 10 || seconds > 60)
    return Result::fail(422, say("cal.seconds"));
  const CanisterCfg* k = cfg_.canisterByPump(pump);
  DoseStep s;
  s.canister = k ? k->id : "";
  s.name = k ? k->name : pump;
  s.pump = pump;
  s.runs = {static_cast<Ms>(seconds * 1000)};
  Job j;
  j.id = newId("cal");
  j.type = "calibration";
  j.startedAt = clock_.epoch();
  j.steps.push_back({s, "pending", 0});
  j.info = {{"pump", pump}, {"seconds", seconds}, {"previous", numOrNull(pit->second.flowMlPerMin)}};
  endFailedMix();
  job_ = j;
  Ctx c = ctx();
  if (!startJobStep(c)) {
    Msg m = job_->message;
    job_.reset();
    return Result::fail(422, m);
  }
  job_->message = say("cal.running", {{"s", seconds}});
  return Result::ok({{"job", *job_}});
}

Result Hub::calibrationResult(const std::string& jobId, double ml) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!job_ || job_->id != jobId || job_->type != "calibration" || job_->state != "waiting_user")
    return Result::fail(409, say("cal.state"));
  if (!isNum(ml) || ml <= 0 || ml > 1000) return Result::fail(422, say("cal.amount"));
  Ms actual = static_cast<Ms>(jnum(job_->info, "actualMs", 0));
  if (actual <= 0) return Result::fail(422, say("cal.runtime"));
  // Rate aus der gemeldeten Ist-Laufzeit, nicht aus der angeforderten (RAT-070)
  double flow = ml / (static_cast<double>(actual) / 60000.0);
  std::string pump = jstr(job_->info, "pump");
  std::string e;
  if (!bus_.writePumpCalibration(pump, flow, e)) return Result::fail(502, say("cal.write", {{"reason", e}}));
  if (auto it = pumps_.find(pump); it != pumps_.end()) it->second.flowMlPerMin = flow;  // sofort gültig, nicht erst im nächsten Takt
  double prev = jnum(job_->info, "previous");
  if (isNum(prev) && prev <= 0) prev = kNaN;  // a chip reporting 0 has no previous rate (R5)
  // Assumption: more than 30 % away from the previous rate points to the
  // tubing (kinked, air in it) rather than to wear; only a warning.
  const bool changed = isNum(prev) && std::fabs(flow - prev) / prev > 0.3;
  const json calArgs{{"name", job_->steps[0].dose.name}, {"flow", flow}, {"prev", numOrNull(prev)}};
  log_.add(clock_.epoch(), "calibration", "info", say("ev.pump_calibrated"),
           say(changed ? "ev.pump_calibrated.text_changed" : "ev.pump_calibrated.text", calArgs),
           {{"pump", pump}, {"flowMlPerMin", flow}, {"ml", ml}, {"ms", actual}});
  job_->info["flowMlPerMin"] = flow;
  const Msg done = say(changed ? "cal.done_changed" : "cal.done", calArgs);
  finishJob("done", done);
  return Result::ok({{"flowMlPerMin", flow}, {"changed", changed}, {"message", done}});
}

Result Hub::prime(const std::string& pump, double seconds) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (userJobActive()) return Result::fail(409, say("job.busy"));
  if (doser_.busy()) return Result::fail(409, say("job.dosing"));
  auto pit = pumps_.find(pump);
  if (pit == pumps_.end() || !pit->second.online) return Result::fail(404, say("pump.unknown"));
  if (!isNum(seconds) || seconds <= 0 || seconds > 20) return Result::fail(422, say("prime.seconds"));
  const CanisterCfg* k = cfg_.canisterByPump(pump);
  DoseStep s;
  s.canister = k ? k->id : "";
  s.name = k ? k->name : pump;
  s.pump = pump;
  s.runs = {static_cast<Ms>(seconds * 1000)};
  Job j;
  j.id = newId("prime");
  j.type = "prime";
  j.startedAt = clock_.epoch();
  j.steps.push_back({s, "pending", 0});
  endFailedMix();
  job_ = j;
  Ctx c = ctx();
  if (!startJobStep(c)) {
    Msg m = job_->message;
    job_.reset();
    return Result::fail(422, m);
  }
  return Result::ok({{"job", *job_}});
}

namespace {
// The capability a calibration kind is for; "" for an unknown one.
std::string capabilityOfKind(const std::string& kind) {
  return kind == "ph" ? "measure.ph" : kind == "ec" ? "measure.ec" : kind == "tank_curve" ? "measure.level" : "";
}
}  // namespace

void Hub::tendProbeSessions(Ms now) {
  for (auto it = probeSessions_.begin(); it != probeSessions_.end();) {
    json& sess = it->second;
    const std::string dev = jstr(sess, "device"), cap = capabilityOfKind(jstr(sess, "kind"));
    // A session no one has stepped on for 30 min was left (a lost tab): it no
    // longer explains jumps (RAT-042), but it stays and keeps pH and EC
    // control waiting, as the probe may still be in a buffer; saving or
    // cancelling it ends it (SD-041)
    if (!sess.contains("pending")) {
      if (!sess.value("idle", false) && now - sess.value("at", Ms(0)) > kProbeIdle) {
        sess["idle"] = true;
        stateDirty_ = true;
      }
      ++it;
      continue;
    }
    auto fail = [&] {
      sess.erase("pending");
      sess["failed"] = say("probe.no_raw");
      sess["at"] = now;
      stateDirty_ = true;
    };
    const json& pending = sess["pending"];
    bool online = false;
    for (const auto& d : devices_)
      if (d.id == dev) online = d.online;
    // Gone during the wait (Home Assistant restarting): a report after that may carry an old value
    if (!pending.contains("since") || !online) {
      fail();
      ++it;
      continue;
    }
    const Ms since = pending["since"].get<Ms>();
    auto s = bus_.sample(dev, cap);
    // With a sign of life, a measurement after the request counts, as Home
    // Assistant drops an unchanged report; the point fails once the sensor
    // turns stale (SD-042)
    const DeviceCfg* dcfg = cfg_.device(dev);
    const DeviceClassDef* dclass = dcfg ? cat_.deviceClass(dcfg->cls) : nullptr;
    const bool life = s && s->life != Sample::Life::None && dclass && dclass->ownReportSchedule;  // as sensor truth (R7)
    if (life && (s->life == Sample::Life::Fault || (s->lifeText && now - s->lifeAt > kLifeLimit))) {
      fail();
      ++it;
      continue;
    }
    const Ms taken = !s ? 0 : life ? (s->life == Sample::Life::Ok ? s->measuredAt : 0) : s->ts;
    // A report from before the request may still be from the tank, not the
    // buffer (RAT-006); its time can be dated up to a second late, so it must
    // be clearly later (as SD-035 takes reports under 2 s apart as one)
    // A measurement's time is dated to a second twice (Home Assistant's Date header, the age in
    // whole seconds): it must be 3 s later
    if (s && isNum(s->raw) && taken > since + (life ? 3 * kSecond : kSameReportMs)) {
      addProbePoint(sess, s->raw, pending["reference"].get<double>());
      sess.erase("pending");
      sess["at"] = now;
      stateDirty_ = true;
      ++it;
      continue;
    }
    // At most as long as the sensor's own freshness limit: no newer report means no value
    Ms limit = 0;
    cfg_.forEachBinding([&](const std::string& role, const Binding& b) {
      if (b.device == dev && cat_.role(role) && cat_.role(role)->capability == cap) limit = std::max(limit, truth_.limit(role));
    });
    if (now - since > (limit > 0 ? limit : 10 * kMinute)) fail();
    ++it;
  }
}

void Hub::addProbePoint(json& sess, double raw, double ref) {
  // Taken again for the same reference (the probe had not settled): the later one counts
  json& pts = sess["points"];
  for (auto p = pts.begin(); p != pts.end();)
    p = (*p)[1].get<double>() == ref ? pts.erase(p) : p + 1;
  if (pts.size() < kMaxProbePoints) pts.push_back({raw, ref});
}

Result Hub::probeCalibration(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  std::string dev = jstr(j, "device"), kind = jstr(j, "kind"), action = jstr(j, "action");
  const std::string key = dev + ":" + kind;
  // Ending a session needs nothing more: also for a device that is gone
  if (action == "cancel") {
    if (probeSessions_.erase(key)) stateDirty_ = true;
    return Result::ok();
  }
  if (!cfg_.device(dev)) return Result::fail(404, say("device.unknown"));
  const std::string cap = capabilityOfKind(kind);
  if (cap.empty()) return Result::fail(422, say("probe.kind"));
  // Only kinds the device class offers; a device calibrated elsewhere offers
  // none, and one that reports another class than configured offers none either
  const DeviceClassDef* dc = cat_.deviceClass(cfg_.device(dev)->cls);
  bool reportedOther = false;
  for (const auto& d : devices_)
    if (d.id == dev && d.cls != cfg_.device(dev)->cls) reportedOther = true;
  if (!dc || reportedOther || std::find(dc->calibrations.begin(), dc->calibrations.end(), kind) == dc->calibrations.end())
    return Result::fail(422, say("probe.not_offered"));
  if (action == "start") {
    probeSessions_[key] = {{"device", dev}, {"kind", kind}, {"points", json::array()}, {"at", clock_.nowMs()}};
    stateDirty_ = true;
    return Result::ok(probeSessions_[key]);
  }
  auto it = probeSessions_.find(key);
  if (it == probeSessions_.end()) return Result::fail(409, say("probe.session"));
  if (action == "point") {
    double ref = jnum(j, "reference");
    if (!isNum(ref)) return Result::fail(422, say("probe.reference"));
    // What a buffer, a reference solution or a tank can be
    const bool plausible = kind == "ph" ? ref >= 0 && ref <= 14 : kind == "ec" ? ref > 0 && ref <= 20 : ref >= 0 && ref <= 100000;
    if (!plausible) return Result::fail(422, say("probe.reference_range"));
    // Room for another point: a retake for the same reference replaces its point
    const json& have = it->second["points"];
    if (have.size() >= kMaxProbePoints && std::none_of(have.begin(), have.end(), [&](const json& x) { return x[1].get<double>() == ref; }))
      return Result::fail(422, say("probe.points_max", {{"max", kMaxProbePoints}}));
    it->second["at"] = clock_.nowMs();
    it->second.erase("idle");
    // A sensor on its own schedule (Home Assistant) reports when it reports:
    // the point waits for its next report (tendProbeSessions)
    if (dc->ownReportSchedule) {
      it->second["pending"] = {{"since", clock_.nowMs()}, {"reference", ref}};
      it->second.erase("failed");
      stateDirty_ = true;
      return Result::ok(it->second);
    }
    auto s = bus_.sample(dev, cap);
    // Ein fehlender Rohwert wird nie Kalibrierpunkt (RAT-006)
    if (!s || !isNum(s->raw) || clock_.nowMs() - s->ts > 30 * kSecond)
      return Result::fail(422, say("probe.no_raw"));
    addProbePoint(it->second, s->raw, ref);
    stateDirty_ = true;
    return Result::ok(it->second);
  }
  if (action == "commit") {
    if (it->second.contains("pending")) return Result::fail(409, say("probe.pending"));
    json data;
    const json& pts = it->second["points"];
    if (kind == "ph") {
      if (pts.size() < 2) return Result::fail(422, say("probe.points_two"));
      data = {{"points", json::array({pts[0], pts[1]})}};
    } else if (kind == "ec") {
      if (pts.empty()) return Result::fail(422, say("probe.points_one"));
      double raw = pts.back()[0].get<double>(), ref = pts.back()[1].get<double>();
      if (raw <= 0) return Result::fail(422, say("probe.raw"));
      data = {{"factor", ref / raw}};
    } else {
      json sorted = pts;
      std::sort(sorted.begin(), sorted.end(), [](const json& a, const json& b) { return a[0].get<double>() < b[0].get<double>(); });
      data = {{"points", sorted}};
      Msg err;
      if (!Curve::fromJson(data, err)) return Result::fail(422, err);
    }
    data["at"] = clock_.epoch();
    double probe = kind == "tank_curve" ? pts[0][0].get<double>() : 7.0;
    if (!SensorTruth::calibrate(cap, probe, &data)) {
      // pH: what is wrong, in mV per pH step on the raw scale (RAT-025's band is 47.3–74.0)
      if (kind == "ph") {
        const double dRaw = pts[1][0].get<double>() - pts[0][0].get<double>(), dRef = pts[1][1].get<double>() - pts[0][1].get<double>();
        // Hardly moved between the buffers: not the probe's signal, or it was not in the buffer
        if (std::fabs(dRaw) < 0.5) return Result::fail(422, say("probe.no_move"));
        const double ratio = dRef / dRaw;
        if (isNum(ratio) && ratio < 0) return Result::fail(422, say("probe.swapped"));
        if (isNum(ratio) && ratio != 0) {
          const double mvPerPh = std::round(kNernstMvPerPh / ratio * 10) / 10;
          if (ratio < 0.8) return Result::fail(422, say("probe.too_steep", {{"mv", mvPerPh}}));
          if (ratio > 1.25) return Result::fail(422, say("probe.too_flat", {{"mv", mvPerPh}}));
        }
      }
      return Result::fail(422, say("probe.invalid"));
    }
    cfg_.calibrations[dev][kind] = data;
    probeSessions_.erase(it);
    if (const Binding* lb = cfg_.binding("tank.level"); kind == "tank_curve" && lb && lb->device == dev) {
      RefillController::levelCommitted(rt_, data);
      stateDirty_ = true;
    }
    const Msg kindMsg = kindName(kind);
    const DeviceCfg* d = cfg_.device(dev);
    const json name = deviceName(cat_, dev, d->cls, d->name);  // its own name, or its class (SD-083)
    saveConfig(say("ev.cfg.calibration", {{"kind", kindMsg}, {"name", name}}));
    log_.add(clock_.epoch(), "calibration", "info", say("ev.probe_calibrated"), say("ev.probe_calibrated.text", {{"name", name}, {"kind", kindMsg}}),
             {{"device", dev}});
    return Result::ok({{"calibration", data}});
  }
  return Result::fail(422, say("probe.action"));
}

Result Hub::ackLatch(const std::string& id) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto it = rt_.latches.find(id);
  if (it == rt_.latches.end()) return Result::fail(404, say("latch.unknown"));
  if (id.rfind("jump.", 0) == 0) return Result::fail(409, say("latch.jump"));
  rt_.latches.erase(it);
  stateDirty_ = true;
  log_.add(clock_.epoch(), "block", "info", say("ev.latch_released"), latchName(id));
  return Result::ok();
}

Result Hub::stop(const Msg& who) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  // Stop: an active, idempotent shutdown (RAT-036). It takes effect and everything goes off
  // first, as on an internal error: booking the running dose can then neither keep an output on
  // nor switch one on (the gateway refuses while stopped).
  const bool first = !stopped_;
  stopped_ = true;
  rt_.stopped = true;
  const Ms stopAt = clock_.nowMs();
  act_.stopAll(cat_, cfg_, stopAt);
  const bool wasKept = stopKeepingState();  // so the Stop holds (SD-081)
  saveState();  // at once: a power loss while the dose is booked keeps the stop
  Ctx c = ctx();
  c.now = stopAt;  // a run counts until the stop, not until the sockets were switched
  // When booking the running dose fails, the order is dropped unbooked, so nothing of it runs or
  // is booked later, and that is reported below.
  std::optional<DoseOrder> unbooked;
  json reason;
  if (doser_.busy()) {
    const DoseOrder order = *doser_.active();
    auto drop = [&](json why) {
      doser_.drop(act_);
      unbooked = order;
      reason = std::move(why);
    };
    try {
      doser_.abort(c, act_, say("abort.stop"));
    } catch (const std::exception& e) {
      std::string why;
      for (const char ch : std::string_view(e.what()).substr(0, 200))
        why += (ch >= 0x20 && ch < 0x7f) ? ch : '?';  // a technical text: printable ASCII only
      drop(why.empty() ? json(say("abort.internal")) : json(why));
    } catch (...) {
      drop(say("abort.internal"));
    }
  }
  if (auto fin = doser_.takeFinished()) {
    // Controller doses are not part of the user's job; the controllers end their round below.
    const bool controllerDose = fin->orderId.rfind("ec-", 0) == 0 || fin->orderId.rfind("ph-", 0) == 0;
    if (job_ && !controllerDose) onJobDose(c, *fin);
  }
  ec_.stopRound();
  ph_.stopRound();
  mixCut(true, unbooked && unbooked->purpose == "mix");  // a part dropped unbooked went in all the same
  if (job_) finishJob("aborted", say("job.emergency_stop"));
  saveState();
  if (first || wasKept) log_.add(clock_.epoch(), "system", "alarm", say("ev.stop"), say("ev.stop.text", {{"who", who}}));
  // Calibration and priming runs book no stock (RAT-070), so there is nothing to say for them
  if (unbooked && unbooked->purpose != "calibration" && unbooked->purpose != "prime")
    log_.add(clock_.epoch(), "system", "warn", say("ev.stop.unbooked"),
             say("ev.stop.unbooked.text", {{"name", unbooked->step.name}, {"reason", reason}}), {{"canister", unbooked->step.canister}});
  saveEvents();
  // A Stop by hand stops everything, also after a power loss
  // (PD-076). Written on every stop, so pressing it again retries a socket
  // that did not take the setting.
  setFanSockets(false);
  return Result::ok();
}

Result Hub::resume() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (stopped_) {
    log_.add(clock_.epoch(), "system", "info", say("ev.resumed"), say("ev.resumed.text"));
    setFanSockets(true);
  }
  stopped_ = false;
  rt_.stopped = false;
  stopKeepingState();
  saveState();
  saveEvents();
  return Result::ok();
}

Result Hub::maintenance(double minutes) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (!isNum(minutes) || minutes < 0 || minutes * 60 > kMaxMaintenanceS) return Result::fail(422, say("maint.minutes"));
  maintenanceUntil_ = minutes > 0 ? clock_.epoch() + static_cast<Epoch>(minutes * 60) : 0;
  log_.add(clock_.epoch(), "system", "info", say(minutes > 0 ? "ev.maintenance" : "ev.maintenance_end"),
           minutes > 0 ? say("ev.maintenance.text", {{"min", minutes}}) : Msg{});
  return Result::ok();
}

Result Hub::manualMeasure(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  json values = json::array();
  for (const char* k : {"ph", "ec"}) {
    double v = jnum(j, k);
    if (isNum(v)) {
      rt_.manual[k] = v;
      const bool ph = std::string(k) == "ph";
      values.push_back(say(ph ? "reading.ph" : "reading.ec", {{"value", v}}));
    }
  }
  if (values.empty()) return Result::fail(422, say("manual.empty"));
  rt_.manualAt = clock_.epoch();
  stateDirty_ = true;
  log_.add(clock_.epoch(), "measure", "info", say("ev.manual_reading"), say("ev.manual_reading.text", {{"values", values}}));
  return Result::ok();
}

Result Hub::growStart(const json& j) {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  if (cfg_.grow.state == "running") return Result::fail(409, say("grow.running"));
  GrowCfg g;
  g.state = "running";
  g.name = utf8Prefix(jstr(j, "name", "Durchgang"), 40);
  g.startedAt = clock_.epoch();
  g.phaseStartedAt = g.startedAt;
  const json phases = j.contains("phases") && j["phases"].is_array() ? j["phases"] : json::array();
  for (const auto& p : phases) {
    double days = jnum(p, "days", 0);
    json params = p.is_object() && p.contains("params") ? p["params"] : json::object();
    g.phases.push_back({jstr(p, "name"), isNum(days) && days >= 0 && days < 1000 ? static_cast<int>(days) : 0, params});
  }
  if (g.phases.empty()) return Result::fail(422, say("grow.phases"));
  Config next = cfg_;
  next.grow = g;
  auto errs = validateConfig(next, cat_);
  if (!errs.empty()) return errors(errs);
  cfg_.grow = g;
  saveConfig({});
  log_.add(clock_.epoch(), "grow", "info", say("ev.grow.started"), say("ev.grow.started.text", {{"name", g.name}, {"phase", g.phases[0].name}}));
  return Result::ok();
}

Result Hub::growNextPhase() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto& g = cfg_.grow;
  if (g.state != "running") return Result::fail(409, say("grow.state"));
  // A saved phase outside its list never indexes it (an import is refused: cfg.grow.phase)
  if (g.phase < 0 || static_cast<size_t>(g.phase) >= g.phases.size()) return Result::fail(409, say("cfg.grow.phase"));
  if (static_cast<size_t>(g.phase) + 1 >= g.phases.size()) return Result::fail(409, say("grow.last"));
  g.phase++;
  g.phaseStartedAt = clock_.epoch();
  saveConfig({});
  log_.add(clock_.epoch(), "grow", "info", say("ev.grow.phase"), say("ev.grow.phase.text", {{"phase", g.phases[static_cast<size_t>(g.phase)].name}}));
  return Result::ok();
}

Result Hub::growHarvest() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto& g = cfg_.grow;
  if (g.state != "running") return Result::fail(409, say("grow.state"));
  g.harvestedAt = clock_.epoch();  // Ernte ist ein Ereignis, kein Dauerzustand (RAT-077)
  saveConfig({});
  log_.add(clock_.epoch(), "grow", "info", say("ev.grow.harvest"), plain(g.name));
  return Result::ok();
}

Result Hub::growComplete() {
  std::lock_guard<std::recursive_mutex> l(mtx_);
  auto& g = cfg_.grow;
  if (g.state != "running") return Result::fail(409, say("grow.state"));
  g.state = "completed";
  saveConfig({});
  log_.add(clock_.epoch(), "grow", "info", say("ev.grow.finished"), plain(g.name));
  return Result::ok();
}

}  // namespace gc
