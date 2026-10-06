# ESP-IDF-Projekt des Hubs (ESP32-S3). Bauen:
#   (cd ../web && npm ci && npm run build) && node ../tools/embed_web.mjs
#   idf.py set-target esp32s3 build
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(growcontroller_hub)
