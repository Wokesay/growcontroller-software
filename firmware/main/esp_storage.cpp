// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>

#include <esp_log.h>
#include <esp_spiffs.h>

#include "esp_platform.hpp"

namespace gcfw {

namespace {
constexpr const char* kTag = "gc.storage";
std::string pathOf(const std::string& name) { return "/data/" + name; }
}  // namespace

bool EspStorage::mount() {
  esp_vfs_spiffs_conf_t conf = {};
  conf.base_path = "/data";
  conf.partition_label = "storage";
  conf.max_files = 8;
  conf.format_if_mount_failed = true;
  esp_err_t rc = esp_vfs_spiffs_register(&conf);
  if (rc != ESP_OK) ESP_LOGE(kTag, "Ablage nicht verfügbar: %s", esp_err_to_name(rc));
  return rc == ESP_OK;
}

std::optional<std::string> EspStorage::read(const std::string& name) {
  FILE* f = std::fopen(pathOf(name).c_str(), "rb");
  // SPIFFS kann nicht über eine bestehende Datei umbenennen; zwischen remove und
  // rename liegt nur die fertige .tmp. Ein Stromausfall genau dort verliert nichts.
  if (!f) f = std::fopen(pathOf(name + ".tmp").c_str(), "rb");
  if (!f) return std::nullopt;
  std::string data;
  char buf[1024];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
  std::fclose(f);
  return data;
}

bool EspStorage::write(const std::string& name, const std::string& data) {
  std::string tmp = pathOf(name + ".tmp");
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = std::fclose(f) == 0 && ok;
  if (!ok) return false;
  std::remove(pathOf(name).c_str());
  return std::rename(tmp.c_str(), pathOf(name).c_str()) == 0;
}

bool EspStorage::append(const std::string& name, const std::string& data) {
  FILE* f = std::fopen(pathOf(name).c_str(), "ab");
  if (!f) return false;
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace gcfw
