# Fremdbibliotheken als einzelne Header, Version und SHA256 gepinnt.
# Sie werden beim Konfigurieren nach build/_deps geladen und nicht
# eingecheckt. Lizenzen: alle MIT (siehe README.md, Abschnitt Lizenz).
#
# Offline: GC_DEPS_DIR auf einen Ordner mit den drei Dateien setzen.

set(GC_DEPS_DIR "${CMAKE_BINARY_DIR}/_deps/include" CACHE PATH "Ordner der Header-Abhängigkeiten")

function(gc_fetch_header name url sha256 dest_rel)
  set(dest "${GC_DEPS_DIR}/${dest_rel}")
  if(EXISTS "${dest}")
    file(SHA256 "${dest}" have)
    if(have STREQUAL "${sha256}")
      return()
    endif()
    message(STATUS "${name}: Prüfsumme passt nicht, lade neu")
  endif()
  message(STATUS "Lade ${name}")
  file(DOWNLOAD "${url}" "${dest}" EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON STATUS st)
  list(GET st 0 code)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "${name} konnte nicht geladen werden: ${st}")
  endif()
endfunction()

gc_fetch_header(nlohmann_json
  "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
  9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6
  "nlohmann/json.hpp")

gc_fetch_header(cpp_httplib
  "https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.54.1/httplib.h"
  5933c14b2d0f45212925ed18ca579841f5fce717f431fc20cec712423e905b10
  "httplib.h")

gc_fetch_header(doctest
  "https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h"
  44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb
  "doctest/doctest.h")
