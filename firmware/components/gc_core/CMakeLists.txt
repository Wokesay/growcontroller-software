# Der plattformneutrale Kern als ESP-IDF-Komponente – dieselben Quellen wie im Simulator.
set(GC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
file(GLOB GC_SRCS "${GC_ROOT}/core/src/*.cpp")
set(GC_GEN "${CMAKE_BINARY_DIR}/gc_generated")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  file(STRINGS "${GC_ROOT}/VERSION" GC_VERSION LIMIT_COUNT 1)
  file(MAKE_DIRECTORY "${GC_GEN}/gc")
  execute_process(COMMAND ${CMAKE_COMMAND}
    -DCATALOG=${GC_ROOT}/catalog/catalog.json
    -DCHANGELOG=${GC_ROOT}/CHANGELOG.md
    -DVERSION=${GC_VERSION}
    -DOUT=${GC_GEN}/gc/embedded.cpp
    -P ${GC_ROOT}/cmake/embed.cmake
    RESULT_VARIABLE embed_rc)
  if(NOT embed_rc EQUAL 0)
    message(FATAL_ERROR "Einbetten von Katalog/Changelog fehlgeschlagen")
  endif()
  # Als Cache-Eintrag setzen: ESP-IDF verlangt nur CMake 3.16 (CMP0126 alt),
  # dort würde das set(CACHE) in deps.cmake eine normale Variable verdrängen.
  set(GC_DEPS_DIR "${CMAKE_BINARY_DIR}/gc_deps" CACHE PATH "Header-Abhängigkeiten der Firmware" FORCE)
  include("${GC_ROOT}/cmake/deps.cmake")
endif()

idf_component_register(
  SRCS ${GC_SRCS} "${GC_GEN}/gc/embedded.cpp"
  INCLUDE_DIRS "${GC_ROOT}/core/include" "${CMAKE_BINARY_DIR}/gc_deps" "${GC_GEN}")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-unused-parameter)
