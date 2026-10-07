// Web-App als gzip-Daten im Programm (tools/embed_web.mjs). Genutzt von der
// Firmware und vom Simulator-Download (eine Datei, ohne web/dist daneben).
#pragma once

#include <cstddef>

namespace gc {

struct WebAsset {
  const char* path;
  const char* type;
  const unsigned char* data;
  size_t size;
};

extern const WebAsset kWebAssets[];
extern const size_t kWebAssetCount;

}  // namespace gc
