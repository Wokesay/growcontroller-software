// Von tools/embed_web.mjs erzeugt: Web-App als gzip-Daten im App-Image.
#pragma once

#include <cstddef>

namespace gcfw {

struct WebAsset {
  const char* path;
  const char* type;
  const unsigned char* data;
  size_t size;
};

extern const WebAsset kWebAssets[];
extern const size_t kWebAssetCount;

}  // namespace gcfw
