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

# PD-022, PD-043, PD-047 (SD-010, SD-021, SD-024): devices do not lock out
# third-party firmware; the only eFuse allowed is the HMAC key for encrypted
# NVS. No sdkconfig option that burns eFuses for Secure Boot, flash
# encryption, a disabled ROM download mode or anti-rollback, and no direct
# eFuse writes in the firmware code.
if grep -rnE --include='sdkconfig*' '^CONFIG_(SECURE_BOOT|SECURE_FLASH_ENC_ENABLED|SECURE_FLASH_ENCRYPTION_MODE_RELEASE|SECURE_DISABLE_ROM_DL_MODE|SECURE_ENABLE_SECURE_ROM_DL_MODE|BOOTLOADER_APP_ANTI_ROLLBACK)=y' firmware/; then
  err "eFuse-burning security option in firmware sdkconfig (PD-022, SD-021, SD-024)"
fi
if grep -rnE 'esp_efuse_(write|set_write_protect|set_read_protect|disable_rom_download_mode|enable_rom_secure_download_mode)' firmware/main firmware/components 2>/dev/null; then
  err "direct eFuse write in the firmware (PD-022, SD-024)"
fi

[ $fail -eq 0 ] && echo "Architekturregeln: ok"
exit $fail
