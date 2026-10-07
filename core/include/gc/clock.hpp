// SPDX-License-Identifier: AGPL-3.0-or-later
// Time base of the hub (PD-069, PD-073, SD-028). The platform clock says
// whether its wall time is secured. Without a secured time after a start,
// the hub continues from the time it saved last; the outage counts as 0.
// The operating time is saved too and counts only while the hub runs.
#pragma once

#include <optional>

#include "gc/common.hpp"

namespace gc {

class HubClock : public IClock {
 public:
  explicit HubClock(const IClock& base) : base_(base) {}
  Ms nowMs() const override { return base_.nowMs(); }
  Epoch epoch() const override;
  bool secured() const override { return base_.secured(); }

  // At the start: the moment saved last (nullopt: nothing saved) and the
  // newest wall time the hub has recorded anywhere, e.g. in the event log.
  // The continued clock never starts before either of them.
  void start(const std::optional<Stamp>& saved, Epoch newestRecorded = 0);
  std::int64_t operatingS() const;
  Stamp stamp() const { return {epoch(), secured(), operatingS()}; }
  // "secured", "continued" (from the saved time) or "unset" (no secured
  // time and nothing saved: the platform's own clock, e.g. 1970).
  const char* source() const;

 private:
  const IClock& base_;
  Ms startMs_ = 0;
  std::optional<Epoch> continueFrom_;
  std::int64_t savedOperatingS_ = 0;
};

}  // namespace gc
