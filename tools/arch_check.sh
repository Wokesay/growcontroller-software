#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Architekturregeln, maschinell geprüft (docs/CONCEPT.md §4).
set -u
cd "$(dirname "$0")/.."
fail=0
err() { echo "ARCH: $*"; fail=1; }

# R0: Der Kern ist plattformneutral – keine Plattform-, Netz- oder Thread-Header.
if grep -rnE '#include <(esp_|freertos/|driver/|sys/|unistd|windows|httplib|filesystem|thread|fstream|iostream)' core/; then
  err "Plattform-Header im Kern"
fi

# R1: Nur das Aktor-Gateway (dosing.cpp) ruft Aktor-Methoden des Busses,
# auch des Netz-Busses (schaltbare Steckdosen).
if grep -rnE '(bus_?|net_?)(\.|->)(startRun|setSwitch|stopAllPumps)\(' core/src | grep -v '^core/src/dosing.cpp'; then
  err "Aktor-Aufruf außerhalb des Gateways"
fi

# R2: Der Watchdog sieht nur Lesemodell und Konfiguration, keinen Bus, kein Gateway, keinen Regler.
if grep -nE '#include "gc/(bus|dosing|control|hub|truth|api)\.hpp"' core/include/gc/watchdog.hpp core/src/watchdog.cpp; then
  err "Watchdog hat einen Pfad zu Aktoren"
fi

# R4: Logik liest Phasenparameter, nie Phasennamen (Regler, Planung, Bewertung).
if grep -niE '"(blüte|bluete|wachstum|veg|vegi|bloom|flower|flush|ernte|harvest|drying|trocknung|keimling|seedling)"' \
    core/src/control.cpp core/src/mix.cpp core/src/resolver.cpp core/src/watchdog.cpp core/src/truth.cpp core/src/dosing.cpp; then
  err "Phasenname als Literal im Kern"
fi

# R5: Ein fehlender Wert ist nie 0 (Quelle: RAT-006).
if grep -rnE 'value_or\(0(\.0)?\)' core/; then
  err "fehlender Wert wird zu 0"
fi

# SD-051: the lockout knows a client by the socket's peer address. A trusted
# proxy would let cpp-httplib take it from X-Forwarded-For instead.
if grep -rn 'set_trusted_proxies' sim/ ha/ firmware/; then
  err "client address from a header (set_trusted_proxies)"
fi

# #79, #234: session tokens, salts and the setup code take their random
# numbers from the operating system (sim::randomBytes: getentropy(); the
# firmware: esp_fill_random()). On Linux x86-64 std::random_device tries
# RDSEED first, which AMD Zen 5 without AMD's fix answers with zeros
# (AMD-SB-7055). Our sources use neither it nor the RDSEED and RDRAND
# instructions, except for the one declaration right after `#ifdef _WIN32`
# in sim/scenario.cpp, where MSVC's, which the simulator package is built
# with, calls rand_s (an old mingw's would not be safe). Anything else
# fails, also a branch this check cannot read.
rd=$(find core sim ha firmware/main firmware/components -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 2>/dev/null | sort -z |
  xargs -0 awk '
    FNR == 1 { prev = "" }
    { code = $0; sub(/\/\/.*/, "", code) }  # a comment may name it
    code ~ /random_device|rdseed|rdrand/ && !(FILENAME == "sim/scenario.cpp" && prev ~ /^#ifdef[ \t]+_WIN32[ \t\r]*$/) { print FILENAME ":" FNR ":" $0 }
    { prev = code }')
if [ -n "$rd" ]; then
  echo "$rd"
  err "std::random_device, RDSEED or RDRAND outside the Windows branch of sim::randomBytes (#79, #234)"
fi

# SD-039: a value calibrated elsewhere goes into the history, never into
# control: Reading::forHistory() is read only where the history is sampled.
if grep -rnE 'forHistory\(' core/ | grep -vE '^core/include/gc/readmodel\.hpp:|^core/src/hub\.cpp:[0-9]+: +samples\.emplace_back'; then
  err "forHistory() outside the history sampling"
fi

# PD-022, PD-043, PD-047 (SD-010, SD-021, SD-024): devices do not lock out
# third-party firmware; the only eFuse allowed is the HMAC key for encrypted
# NVS. No sdkconfig option that burns eFuses for Secure Boot, flash
# encryption, a disabled ROM download mode or anti-rollback, also in a
# board profile (firmware/boards/*.defaults), and no direct eFuse writes in
# the firmware code.
if grep -rnE --include='sdkconfig*' --include='*.defaults' '^CONFIG_(SECURE_BOOT|SECURE_FLASH_ENC_ENABLED|SECURE_FLASH_ENCRYPTION_MODE_RELEASE|SECURE_DISABLE_ROM_DL_MODE|SECURE_ENABLE_SECURE_ROM_DL_MODE|BOOTLOADER_APP_ANTI_ROLLBACK)=y' firmware/; then
  err "eFuse-burning security option in firmware sdkconfig or board profile (PD-022, SD-021, SD-024)"
fi
if grep -rnE 'esp_efuse_(write|set_write_protect|set_read_protect|disable_rom_download_mode|enable_rom_secure_download_mode)' firmware/main firmware/components 2>/dev/null; then
  err "direct eFuse write in the firmware (PD-022, SD-024)"
fi

[ $fail -eq 0 ] && echo "Architekturregeln: ok"
exit $fail
