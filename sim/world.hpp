// Digitaler Zwilling: Hub-Ports, Dosierblock mit Pumpenkappen, Köpfe und ein
// einfaches Tankmodell. Zahlen gemessen, wo möglich (siehe
// docs/SIMULATOR.md), sonst als Annahme gekennzeichnet.
#pragma once

#include <array>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "gc/common.hpp"

namespace sim {

using gc::Ms;
using gc::json;

struct Liquid {
  std::string name;
  double ecPerMlL = 0;   // mS/cm je ml/L
  double phPerMlL = 0;   // pH-Änderung je ml/L (negativ = Säure)
};

struct Cap {
  std::string id;
  std::string liquid;        // Schlüssel in World::liquids
  double trueFlow = 50;      // ml/min, wirklich
  double storedFlow = gc::kNaN;  // im ID-Chip (PD-010); NaN = nicht eingemessen
  bool blocked = false;
  // Laufzustand
  std::string jobId;
  Ms requested = 0, started = 0, elapsed = 0;
  int state = 0;  // 0 idle, 1 running, 2 done, 3 failed
  std::string error;
};

struct Device {
  std::string id, cls;
  int port = 0;
  std::array<std::optional<Cap>, 6> slots;  // nur Dosierblock
  std::string fault;                        // offline | frozen | jump | ec_zero | ""
  double frozenPh = gc::kNaN, frozenEc = gc::kNaN;
  Ms pluggedAt = 0;
  Ms lastSample = -100000;
};

struct Tank {
  double volumeL = 0;
  double ec = 0.02;       // durchmischter Zustand
  double ph = 7.0;
  double temp = 20.5;
  double pendingEc = 0;   // noch nicht durchmischte Gabe (Konzentrationsbeitrag)
  double pendingPh = 0;
};

class World {
 public:
  World();
  void reset();
  void advance(Ms now);
  Ms now() const { return now_; }

  // Ports 1–8
  bool plug(int port, const std::string& cls, std::string id = "");
  bool unplug(int port);
  Device* deviceAt(int port);
  Device* device(const std::string& id);
  Cap* cap(const std::string& id, Device** block = nullptr, int* slot = nullptr);
  bool plugCap(const std::string& blockId, int slot, const std::string& liquid, std::string id = "");
  bool unplugCap(const std::string& blockId, int slot);

  // Ausgänge des Hubs (2 Kanäle)
  bool out[2] = {false, false};

  // Sensoren (Rohwerte, wie der Kopf sie liefert)
  double rawPh() const;
  double rawEc() const;
  double rawLevelV() const;
  double rawTemp() const;
  Ms sampleTs(const Device& d) const { return d.lastSample; }

  // Pumpen
  bool startRun(const std::string& capId, Ms ms, const std::string& jobId, std::string& err);
  void stopAllPumps();

  void fill(double volumeL, double ec, double ph);
  json toJson() const;
  // Sichern und Wiederherstellen (Geräte, ID-Chips, Tank) für Neustarts des Simulators
  json save() const;
  void restore(const json& j);

  Tank tank;
  std::map<std::string, Liquid> liquids;
  std::map<int, Device> ports;
  std::optional<double> probeBuffer;  // Sonde steckt in Pufferlösung (Kalibrierung)
  std::string probeKind;              // "ph" | "ec"
  double inletLpm = 2.0;
  double phOffset = 0.18;             // Sondenfehler vor der Kalibrierung (Annahme)
  double phSlope = 0.97;
  double ecFactor = 1.08;

 private:
  void step(Ms dt);
  std::string newId(const std::string& prefix);
  Ms now_ = 0;
  mutable std::mt19937 rng_;
  double noise(double sigma) const;
  std::deque<std::pair<Ms, double>> phTrail_;  // Totzeit der pH-Antwort
  std::string runningCap_;
};

}  // namespace sim
