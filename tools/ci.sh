#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# All checks as CI runs them. Locally: tools/ci.sh  (E2E: E2E=1 tools/ci.sh)
# Needs reuse (pipx install reuse==6.2.0; CI installs tools/requirements-reuse.txt
# with its hashes); REUSE=<path> selects another binary.
set -euo pipefail
cd "$(dirname "$0")/.."

group() { echo "::group::$1"; }
end() { echo "::endgroup::"; }

group "Architecture rules"
tools/arch_check.sh
end

group "CI and release scripts, workflow pins"
node --test tools/ci_changes.test.mjs tools/workflows.test.mjs tools/sbom_cpp.test.mjs tools/app.test.mjs tools/version.test.mjs
end

group "Text keys of the web app"
node --test tools/i18n_keys.test.mjs
end

group "Licenses (REUSE, npm dependencies)"
"${REUSE:-reuse}" lint
node --test tools/check_licenses.test.mjs
node tools/check_licenses.mjs
end

group "Core, simulator and tests (Debug, AddressSanitizer, UBSan)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGC_SANITIZE=ON
cmake --build build
./build/gc_tests
node --test tools/ha_server.test.mjs
end

group "Web app (type check, build, size budget)"
(cd web && npm ci --ignore-scripts --no-audit --no-fund && npm run build)
end

if [ "${E2E:-0}" = "1" ]; then
  group "End-to-end in the browser against the simulator"
  (cd web && npx playwright test)
  end
fi
echo "All checks passed."
