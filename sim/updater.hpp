// SPDX-License-Identifier: AGPL-3.0-or-later
// Update-Attrappe des Simulators. Auf dem Hub: signiertes Manifest aus den
// GitHub-Releases, Download, Signaturprüfung, A/B-Partition, Neustart,
// Selbsttest, sonst automatischer Rückfall (docs/RELEASE.md).
#pragma once

#include "gc/hub.hpp"

namespace sim {

class SimUpdater : public gc::IUpdater {
 public:
  explicit SimUpdater(const gc::IClock& clock) : clock_(clock) {}
  gc::json status() override;
  gc::json check() override;
  gc::json install(const std::string& version) override;
  gc::json rollback() override;

 private:
  const gc::IClock& clock_;
  gc::json available_ = nullptr;
  gc::Epoch lastCheck_ = 0;
};

}  // namespace sim
