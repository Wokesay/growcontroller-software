// SPDX-License-Identifier: AGPL-3.0-or-later
// Verlauf (Schicht 10): Messreihen in drei Auflösungsstufen als Ringpuffer.
//   L0: 10 s, Mittelwert, 24 h
//   L1: 1 min, min/Ø/max, 7 Tage
//   L2: 15 min, min/Ø/max, 1 Jahr
// Werte als float; Lücken sind NaN und erscheinen in der API als null.
// Auf dem Gerät liegt dieselbe Struktur in einer eigenen Flash-Partition
// (Anhängen, Löschen nur beim Umlauf); im Simulator im RAM mit Dateiabzug.
// Storage (#68): a snapshot (dump) plus a journal with one record per
// sampling round, so a card is not rewritten for every new value. A sample
// is replayed only when it is newer than its series in the snapshot, so a
// journal that survived its snapshot is not counted twice; each record
// carries a checksum, so a torn record is never read as values.
#pragma once

#include <array>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gc/common.hpp"

namespace gc {

struct SeriesPoints {
  int stepS = 0;
  std::vector<Epoch> t;
  std::vector<double> avg, min, max;
};

class Series {
 public:
  Series();
  void add(Epoch t, double v);
  SeriesPoints query(Epoch from, Epoch to, size_t maxPoints) const;
  void dump(std::string& out) const;
  bool load(const char*& p, const char* end);

  struct Tier {
    int stepS = 0;
    size_t cap = 0;
    bool minmax = false;
    std::vector<float> avg, mn, mx;
    std::int64_t head = -1;     // letzter abgeschlossener Slot
    std::int64_t cur = -1;      // laufender Slot
    double sum = 0, cmin = 0, cmax = 0;
    int n = 0;
  };

 private:
  void commit(Tier& tr, std::int64_t slot);
  std::array<Tier, 3> tiers_;
};

class History {
 public:
  void add(const std::string& id, Epoch t, double v);
  // One journal record: the samples of one round, all at time t.
  static std::string record(Epoch t, const std::vector<std::pair<std::string, double>>& samples);
  // Adds the journal's samples of the given series that are newer than what is held;
  // a record with an implausible time is skipped, a torn or damaged one ends the replay.
  // Returns the number of records with samples added.
  size_t replay(const std::string& journal, const std::set<std::string>& ids);
  bool has(const std::string& id) const { return series_.count(id) > 0; }
  SeriesPoints query(const std::string& id, Epoch from, Epoch to, size_t maxPoints) const;
  std::vector<std::string> ids() const;
  std::string dump() const;
  bool load(const std::string& data);

 private:
  std::map<std::string, Series> series_;
  std::map<std::string, Epoch> newest_;  // newest sample per series, also in the snapshot
};

}  // namespace gc
