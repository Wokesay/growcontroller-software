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

# PD-022: devices do not lock out third-party firmware. No sdkconfig option
# that burns eFuses for Secure Boot, release-mode flash encryption, a
# disabled ROM download mode or anti-rollback until a decision allows it.
if grep -nE '^CONFIG_(SECURE_BOOT|SECURE_FLASH_ENC_ENABLED|SECURE_FLASH_ENCRYPTION_MODE_RELEASE|SECURE_DISABLE_ROM_DL_MODE|SECURE_ENABLE_SECURE_ROM_DL_MODE|BOOTLOADER_APP_ANTI_ROLLBACK)=y' firmware/sdkconfig*; then
  err "eFuse-burning security option in firmware/sdkconfig* (PD-022)"
fi

[ $fail -eq 0 ] && echo "Architekturregeln: ok"
exit $fail
