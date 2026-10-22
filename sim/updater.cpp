// SPDX-License-Identifier: AGPL-3.0-or-later
#include "updater.hpp"

#include "gc/embedded.hpp"
#include "gc/messages.hpp"

namespace sim {

gc::json SimUpdater::status() {
  return {{"current", gc::embedded::kVersion},
          {"available", available_},
          {"lastCheck", lastCheck_},
          {"canRollback", false},
          {"simulated", true},
          {"note", gc::say("sim.update.note")}};
}

gc::json SimUpdater::check() {
  lastCheck_ = clock_.epoch();
  // A sample manifest as a release would bring it (the short summary for the app),
  // in English like the release notes (PD-034)
  available_ = {{"version", "0.1.1-proto.1"},
                {"channel", "beta"},
                {"date", "2026-10-20"},
                {"size", 1843200},
                {"signed", true},
                {"summary",
                 {{"neu", {"History: compare two grow cycles"}},
                  {"behoben", {"A control line showed \"Off\" for a moment after a restart"}},
                  {"beachten", {"Recipes, calibrations and settings are kept."}},
                  {"sicherheit", gc::json::array()}}},
                {"changelogUrl", "CHANGELOG.md"}};
  return status();
}

gc::json SimUpdater::install(const std::string& version) {
  return {{"ok", false},
          {"simulated", true},
          {"version", version},
          {"message", gc::say("sim.update.install")}};
}

gc::json SimUpdater::rollback() {
  return {{"ok", false}, {"simulated", true}, {"message", gc::say("sim.update.rollback")}};
}

}  // namespace sim
