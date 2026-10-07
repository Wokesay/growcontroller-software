// SPDX-License-Identifier: AGPL-3.0-or-later
#include "updater.hpp"

#include "gc/embedded.hpp"

namespace sim {

gc::json SimUpdater::status() {
  return {{"current", gc::embedded::kVersion},
          {"available", available_},
          {"lastCheck", lastCheck_},
          {"canRollback", false},
          {"simulated", true},
          {"note", "Im Simulator wird nichts installiert."}};
}

gc::json SimUpdater::check() {
  lastCheck_ = clock_.epoch();
  // Beispiel-Manifest, wie es aus einem Release käme (Kurzfassung für die App).
  available_ = {{"version", "0.1.1-proto.1"},
                {"channel", "beta"},
                {"date", "2026-10-20"},
                {"size", 1843200},
                {"signed", true},
                {"summary",
                 {{"neu", {"Verlauf: Vergleich zweier Durchgänge"}},
                  {"behoben", {"Regelzeile zeigte nach Neustart kurz „Aus“"}},
                  {"beachten", {"Rezepte, Einmesswerte und Einstellungen bleiben erhalten."}},
                  {"sicherheit", gc::json::array()}}},
                {"changelogUrl", "CHANGELOG.md"}};
  return status();
}

gc::json SimUpdater::install(const std::string& version) {
  return {{"ok", false},
          {"simulated", true},
          {"version", version},
          {"message",
           "Simulator: keine Installation. Auf dem Hub: Download, Signaturprüfung, Schreiben in die freie "
           "A/B-Partition, Neustart, Selbsttest – schlägt er fehl, startet die alte Version wieder."}};
}

gc::json SimUpdater::rollback() {
  return {{"ok", false}, {"simulated", true}, {"message", "Simulator: keine Vorversion vorhanden."}};
}

}  // namespace sim
