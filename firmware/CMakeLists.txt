# SPDX-License-Identifier: AGPL-3.0-or-later
# ESP-IDF-Projekt des Hubs (ESP32-S3). Bauen:
#   (cd ../web && npm ci && npm run build) && node ../tools/embed_web.mjs
#   idf.py set-target esp32s3 build
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Version der App wie im Kern (VERSION), nicht aus git describe
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" PROJECT_VER LIMIT_COUNT 1)
project(growcontroller_hub)
