# SPDX-License-Identifier: AGPL-3.0-or-later
# ESP-IDF project of the hub (ESP32-S3). Build, one folder per board:
#   (cd ../web && npm ci && npm run build) && node ../tools/embed_web.mjs
#   idf.py -B build-n32r16v -D GC_BOARD=n32r16v -D IDF_TARGET=esp32s3 build
#   idf.py -B build-n8r8 -D GC_BOARD=n8r8 -D IDF_TARGET=esp32s3 build
cmake_minimum_required(VERSION 3.16)
# The board (SD-043): ESP32-S3-DevKitC-1-N32R16V, or N8R8 as the fallback.
# Its file in boards/ sets flash, partitions and memory; the sdkconfig lives
# in the build folder, so the two boards never share one.
if(NOT DEFINED GC_BOARD)
  set(GC_BOARD "n32r16v")
endif()
if(NOT GC_BOARD MATCHES "^(n32r16v|n8r8)$")
  message(FATAL_ERROR "GC_BOARD must be n32r16v or n8r8, not '${GC_BOARD}'")
endif()
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;boards/${GC_BOARD}.defaults")
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
# The defaults apply only when a folder is configured the first time: a
# folder of the other board would keep its flash size and partitions
if(EXISTS "${SDKCONFIG}")
  file(STRINGS "${SDKCONFIG}" gc_table REGEX "^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=")
  if(NOT gc_table STREQUAL "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"boards/${GC_BOARD}.csv\"")
    message(FATAL_ERROR "This build folder is not for the board ${GC_BOARD} (${gc_table}). "
                        "Use a folder per board: idf.py -B build-${GC_BOARD} -D GC_BOARD=${GC_BOARD} build")
  endif()
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The app's version is the core's (VERSION), not git describe
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" PROJECT_VER LIMIT_COUNT 1)
project(doseph_hub)
