# SPDX-License-Identifier: AGPL-3.0-or-later
# Erzeugt embedded.cpp mit Katalog, Changelog und Version. Die Texte stehen als
# Byte-Felder im Code: MSVC begrenzt Zeichenketten-Literale auf rund 16 KB.
function(gc_embed_bytes var file out)
  file(READ "${file}" hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  # Zeilen umbrechen (CMake-Regex kennt kein {n}): je 24 Bytes eine Zeile
  string(REPEAT "0x[0-9a-f][0-9a-f]," 24 row)
  string(REGEX REPLACE "(${row})" "\\1\n  " bytes "${bytes}")
  file(APPEND "${out}" "static const unsigned char ${var}Data[] = {\n  ${bytes}0x00};\n")
  file(APPEND "${out}" "const char* const ${var} = reinterpret_cast<const char*>(${var}Data);\n")
endfunction()

file(WRITE "${OUT}" "// Erzeugt von cmake/embed.cmake. Nicht von Hand bearbeiten.\n")
file(APPEND "${OUT}" "#include \"gc/embedded.hpp\"\nnamespace gc::embedded {\n")
file(APPEND "${OUT}" "const char* const kVersion = \"${VERSION}\";\n")
gc_embed_bytes(kCatalogJson "${CATALOG}" "${OUT}")
gc_embed_bytes(kChangelogMd "${CHANGELOG}" "${OUT}")
file(APPEND "${OUT}" "}  // namespace gc::embedded\n")
