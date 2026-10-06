#!/usr/bin/env bash
# Alle Prüfungen, wie sie die CI ausführt. Lokal: tools/ci.sh  (E2E: E2E=1 tools/ci.sh)
set -euo pipefail
cd "$(dirname "$0")/.."

group() { echo "::group::$1"; }
end() { echo "::endgroup::"; }

group "Architekturregeln"
tools/arch_check.sh
end

group "Kern, Simulator und Tests (Debug, AddressSanitizer, UBSan)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGC_SANITIZE=ON
cmake --build build
./build/gc_tests
end

group "Web-App (Typprüfung, Build, Größenbudget)"
(cd web && npm ci --ignore-scripts --no-audit --no-fund && npm run build)
end

if [ "${E2E:-0}" = "1" ]; then
  group "Ende-zu-Ende im Browser gegen den Simulator"
  (cd web && npx playwright test)
  end
fi
echo "Alle Prüfungen bestanden."
