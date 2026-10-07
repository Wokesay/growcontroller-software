// SPDX-License-Identifier: AGPL-3.0-or-later
// Einstieg des Hubs: Ablage, WLAN, Kern, Takt, Webserver.
// Die fachliche Logik steckt vollständig in core/ – derselbe Code wie im Simulator.
#include <cstring>
#include <memory>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "esp_platform.hpp"
#include "gc/embedded.hpp"
#include "sdkconfig.h"

namespace {

constexpr const char* kTag = "gc";

gcfw::EspClock g_clock;
gcfw::EspStorage g_storage;
gcfw::EspBus g_bus;
std::unique_ptr<gc::Catalog> g_catalog;
std::unique_ptr<gc::Hub> g_hub;
std::unique_ptr<gc::Api> g_api;

void randomBytes(std::uint8_t* p, size_t n) { esp_fill_random(p, n); }

void onTimeSync(struct timeval*) { g_clock.markSecured(); }

void onWifi(void*, esp_event_base_t base, int32_t id, void*) {
  if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) esp_wifi_connect();
}

void startWifi() {
  if (std::strlen(CONFIG_GC_WIFI_SSID) == 0) {
    ESP_LOGW(kTag, "Kein WLAN konfiguriert (menuconfig › growcontroller). Einrichtung per Assistent folgt.");
    return;
  }
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifi, nullptr));
  wifi_config_t wc = {};
  std::strncpy(reinterpret_cast<char*>(wc.sta.ssid), CONFIG_GC_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(wc.sta.password), CONFIG_GC_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  sntp_set_time_sync_notification_cb(onTimeSync);
  esp_sntp_init();
}

// Ein Takt für den ganzen Kern (Regel R8). Auf dem Gerät läuft er mit hoher
// Priorität auf Kern 1; WLAN und Webserver laufen auf Kern 0.
void coreTask(void*) {
  for (;;) {
    g_hub->tick();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

}  // namespace

extern "C" void app_main() {
  esp_err_t rc = nvs_flash_init();
  if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    rc = nvs_flash_init();
  }
  ESP_ERROR_CHECK(rc);
  g_bus.init();  // Ausgänge aus, bevor irgendetwas anderes läuft (R6)
  g_storage.mount();
  startWifi();

  g_catalog = std::make_unique<gc::Catalog>(gc::Catalog::builtin());
  g_hub = std::make_unique<gc::Hub>(*g_catalog, g_bus, g_storage, g_clock, randomBytes);
  g_hub->setPlatform({{"kind", "esp32s3"}, {"simulated", false}});
  g_api = std::make_unique<gc::Api>(*g_hub, g_clock);
  g_hub->boot();
  ESP_LOGI(kTag, "growcontroller %s gestartet", gc::embedded::kVersion);

  xTaskCreatePinnedToCore(coreTask, "gc_core", 16384, nullptr, 10, nullptr, 1);
  gcfw::startWebServer(*g_api);

  // Neue Version nach erfolgreichem Start bestätigen; sonst Rückfall auf die alte (A/B).
  // Später erst nach Selbsttest von Bus, Ports und Speicher (docs/RELEASE.md).
  esp_ota_mark_app_valid_cancel_rollback();
}
