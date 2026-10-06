// Hardware-Abstraktion zum Bus (Schicht 0–3). Implementiert vom Simulator
// (sim/simbus.cpp) und später von der ESP32-Plattform (Modbus je Port,
// Port-Freigabe nach PD-012). Der Kern ruft nur diese Schnittstelle.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gc/common.hpp"

namespace gc {

enum class PortState { Empty, Checking, Ok, Fault, Rejected };
const char* portStateName(PortState s);

struct PortReport {
  int port = 0;
  PortState state = PortState::Empty;
  std::string deviceId;
  std::string cls;
  Msg message;  // Klartext bei Fehlsteckung, z. B. Pumpenkappe am Hub-Port
};

struct DeviceReport {
  std::string id, cls;
  std::string parent;  // Dosierblock bei Pumpenkappen
  int port = 0;        // zuletzt gesehen an Hub-Port (Laufzeitattribut, keine Konfiguration)
  int slot = -1;       // Pumpen-Port am Dosierblock
  bool online = false;
  std::string fw;
  json info = json::object();  // z. B. {"flowMlPerMin": 52.8} aus dem ID-Chip (PD-010)
  std::string fault;           // z. B. "blocked"
};

// Rohwert eines Kanals mit Zeitstempel (monoton). Kalibrierung und Gültigkeit
// bestimmt erst die Sensorwahrheit.
struct Sample {
  double raw = kNaN;
  Ms ts = 0;
};

struct RunStatus {
  enum class State { Idle, Running, Done, Failed } state = State::Idle;
  std::string jobId;
  Ms requestedMs = 0;
  Ms actualMs = 0;  // Ist-Laufzeit, gemeldet vom Dosierblock (Lehre RAT-070)
  std::string error;
};

class IBus {
 public:
  virtual ~IBus() = default;
  virtual void poll(Ms now) = 0;
  virtual std::vector<PortReport> ports() const = 0;
  virtual std::vector<DeviceReport> devices() const = 0;
  virtual std::optional<Sample> sample(const std::string& dev, const std::string& cap) const = 0;
  // Pumpenlauf in ms mit Job-ID: ein erneut gesendeter Auftrag mit derselben
  // ID läuft nicht doppelt (Vorschlag firmware). Der Dosierblock erzwingt
  // Zeitlimit und "ein Kanal zugleich" selbst.
  virtual bool startRun(const std::string& pump, Ms ms, const std::string& jobId, std::string& err) = 0;
  virtual RunStatus runStatus(const std::string& pump) const = 0;
  virtual void stopAllPumps() = 0;
  virtual bool setSwitch(const std::string& dev, int channel, bool on, std::string& err) = 0;
  virtual std::optional<bool> switchState(const std::string& dev, int channel) const = 0;
  virtual bool writePumpCalibration(const std::string& pump, double mlPerMin, std::string& err) = 0;
};

// Ablage für Konfiguration und Zustand (Gerät: LittleFS/NVS, Host: Dateien).
// write() muss atomar sein (erst Kopie schreiben, dann umbenennen).
class IStorage {
 public:
  virtual ~IStorage() = default;
  virtual std::optional<std::string> read(const std::string& name) = 0;
  virtual bool write(const std::string& name, const std::string& data) = 0;
};

class MemoryStorage : public IStorage {
 public:
  std::optional<std::string> read(const std::string& name) override;
  bool write(const std::string& name, const std::string& data) override;

 private:
  std::vector<std::pair<std::string, std::string>> files_;
};

}  // namespace gc
