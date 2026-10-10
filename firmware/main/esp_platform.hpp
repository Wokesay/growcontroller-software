// SPDX-License-Identifier: AGPL-3.0-or-later
// Plattform-Schicht des Hubs (ESP-IDF): Uhr, Ablage, Bus, Webserver.
#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>

#include "gc/api.hpp"
#include "gc/bus.hpp"

namespace gcfw {

class EspClock : public gc::IClock {
 public:
  gc::Ms nowMs() const override;
  gc::Epoch epoch() const override;
  // Secured after the first network time sync since the start (PD-073).
  // The buffered clock (PD-072) and the device time from the app follow.
  bool secured() const override { return secured_.load(); }
  void markSecured() { secured_.store(true); }

 private:
  std::atomic<bool> secured_{false};
};

// Dateien auf SPIFFS unter /data; schreibt atomar über tmp + rename.
// Ziel laut docs/HISTORY.md: LittleFS für Konfiguration, Verlauf als
// Ringpuffer in der Partition "history".
class EspStorage : public gc::IStorage {
 public:
  bool mount();
  std::optional<std::string> read(const std::string& name) override;
  bool write(const std::string& name, const std::string& data) override;
  bool append(const std::string& name, const std::string& data) override;  // journals (#68)
};

// Bus des Hubs. Stand Gerüst: nur die zwei 12-V-Ausgänge. Modbus je Port mit
// DE/RE, Prüfmessung der Kennung und Freigabe (PD-012) folgen in M1
// (firmware/README.md).
class EspBus : public gc::IBus {
 public:
  void init();
  void poll(gc::Ms now) override;
  std::vector<gc::PortReport> ports() const override;
  std::vector<gc::DeviceReport> devices() const override;
  std::optional<gc::Sample> sample(const std::string& dev, const std::string& cap) const override;
  bool startRun(const std::string& pump, gc::Ms ms, const std::string& jobId, std::string& err) override;
  gc::RunStatus runStatus(const std::string& pump) const override;
  void stopAllPumps() override;
  bool setSwitch(const std::string& dev, int channel, bool on, std::string& err) override;
  std::optional<bool> switchState(const std::string& dev, int channel) const override;
  bool writePumpCalibration(const std::string& pump, double mlPerMin, std::string& err) override;

 private:
  bool out_[2] = {false, false};
};

// HTTP: REST-API des Kerns und die eingebettete Web-App (gzip).
void startWebServer(gc::Api& api);

}  // namespace gcfw
