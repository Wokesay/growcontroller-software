idf_component_register(
  SRCS "main.cpp" "esp_bus.cpp" "esp_storage.cpp" "esp_web.cpp" "web_assets.cpp"
  INCLUDE_DIRS "."
  REQUIRES gc_core esp_http_server esp_wifi esp_netif esp_event nvs_flash spiffs esp_timer driver app_update lwip)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
