cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GC_VERSION)
string(STRIP "${GC_VERSION}" GC_VERSION)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" GC_VERSION_NUM "${GC_VERSION}")

project(growcontroller VERSION ${GC_VERSION_NUM} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GC_BUILD_SIM "Simulator mit Host-Server bauen" ON)
option(GC_BUILD_TESTS "Tests bauen" ON)
option(GC_SANITIZE "AddressSanitizer und UBSan in Tests und Simulator" OFF)
option(GC_WERROR "Warnungen als Fehler" ON)
set(GC_SIM_WEB_ASSETS "" CACHE FILEPATH "Erzeugte web_assets.cpp: Web-App in den Simulator einbetten (Download-Fassung)")

if(MSVC)
  # Umlaute in Quelltexten, große Objektdateien (JSON, httplib), Standardverhalten
  add_compile_options(/utf-8 /bigobj /EHsc /permissive- /Zc:__cplusplus)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(cmake/deps.cmake)

# Katalog und Changelog werden als Daten in den Kern eingebettet.
set(GC_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GC_GEN_DIR}/gc")
add_custom_command(
  OUTPUT "${GC_GEN_DIR}/gc/embedded.cpp"
  COMMAND ${CMAKE_COMMAND}
          -DCATALOG=${CMAKE_CURRENT_SOURCE_DIR}/catalog/catalog.json
          -DCHANGELOG=${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md
          -DVERSION=${GC_VERSION}
          -DOUT=${GC_GEN_DIR}/gc/embedded.cpp
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS catalog/catalog.json CHANGELOG.md VERSION cmake/embed.cmake
  COMMENT "Bette Katalog, Changelog und Version ein")

if(MSVC)
  set(GC_WARN /W3)
  if(GC_WERROR)
    list(APPEND GC_WARN /WX)
  endif()
else()
  set(GC_WARN -Wall -Wextra -Wpedantic -Wshadow)
  if(GC_WERROR)
    list(APPEND GC_WARN -Werror)
  endif()
endif()

add_library(gc_core
  core/src/bus.cpp
  core/src/catalog.cpp
  core/src/config.cpp
  core/src/truth.cpp
  core/src/history.cpp
  core/src/events.cpp
  core/src/sha256.cpp
  core/src/auth.cpp
  core/src/mix.cpp
  core/src/dosing.cpp
  core/src/control.cpp
  core/src/resolver.cpp
  core/src/watchdog.cpp
  core/src/hub.cpp
  core/src/api.cpp
  "${GC_GEN_DIR}/gc/embedded.cpp")
target_include_directories(gc_core PUBLIC core/include "${GC_DEPS_DIR}" "${GC_GEN_DIR}")
target_compile_options(gc_core PRIVATE ${GC_WARN})

if(GC_SANITIZE)
  set(GC_SAN -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_compile_options(gc_core PUBLIC ${GC_SAN})
  target_link_options(gc_core PUBLIC ${GC_SAN})
endif()

if(GC_BUILD_SIM)
  find_package(Threads REQUIRED)
  add_library(gc_sim STATIC sim/world.cpp sim/simbus.cpp sim/scenario.cpp sim/updater.cpp)
  target_include_directories(gc_sim PUBLIC sim)
  target_link_libraries(gc_sim PUBLIC gc_core)
  target_compile_options(gc_sim PRIVATE ${GC_WARN})

  add_executable(gc_sim_server sim/main.cpp)
  target_link_libraries(gc_sim_server PRIVATE gc_sim Threads::Threads)
  # httplib.h erzeugt mit -Wconversion/-Wshadow viele Warnungen; nur für main.cpp gelockert.
  if(NOT MSVC)
    target_compile_options(gc_sim_server PRIVATE -Wall -Wextra)
  endif()
  if(GC_SIM_WEB_ASSETS)
    target_sources(gc_sim_server PRIVATE "${GC_SIM_WEB_ASSETS}")
    target_compile_definitions(gc_sim_server PRIVATE GC_EMBEDDED_WEB=1)
  endif()
  if(WIN32)
    target_link_libraries(gc_sim_server PRIVATE ws2_32)
  endif()
endif()

if(GC_BUILD_TESTS)
  enable_testing()
  add_executable(gc_tests
    tests/core/main.cpp
    tests/core/test_catalog_config.cpp
    tests/core/test_truth.cpp
    tests/core/test_history.cpp
    tests/core/test_mix.cpp
    tests/core/test_resolver.cpp
    tests/core/test_watchdog.cpp
    tests/core/test_auth.cpp
    tests/core/test_api.cpp
    tests/core/test_scenarios.cpp)
  target_link_libraries(gc_tests PRIVATE gc_sim)
  if(NOT MSVC)
    target_compile_options(gc_tests PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME core COMMAND gc_tests)
endif()
