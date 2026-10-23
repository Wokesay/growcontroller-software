#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# All checks as CI runs them. Locally: tools/ci.sh  (E2E: E2E=1 tools/ci.sh)
# Needs reuse (pipx install reuse==6.2.0; CI installs tools/requirements-reuse.txt
# with its hashes); REUSE=<path> selects another binary. Checking the npm
# packages' signatures needs the npm registry and Sigstore's TUF repository
# (tuf-repo-cdn.sigstore.dev).
set -euo pipefail
cd "$(dirname "$0")/.."

group() { echo "::group::$1"; }
end() { echo "::endgroup::"; }

group "Architecture rules"
tools/arch_check.sh
end

group "CI and release scripts, workflow pins"
node --test tools/ci_changes.test.mjs tools/workflows.test.mjs tools/signatures_report.test.mjs tools/sbom_cpp.test.mjs tools/app.test.mjs tools/version.test.mjs tools/flash_manifest.test.mjs tools/arch_check.test.mjs tools/tool_code_guard.test.mjs tools/sbom_web.test.mjs tools/core_tests.test.mjs tools/forms.test.mjs
end

group "Text keys of the web app"
node --test tools/i18n_keys.test.mjs
end

group "Licenses (REUSE, npm dependencies)"
"${REUSE:-reuse}" lint
node --test tools/check_licenses.test.mjs tools/check_provenance.test.mjs
node tools/check_licenses.mjs
end

group "Core, simulator and tests (Debug, AddressSanitizer, UBSan)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGC_SANITIZE=ON
cmake --build build
# Each test case in a process of its own, on every core (#260)
tools/core_tests.sh build/gc_tests
node --test tools/ha_server.test.mjs tools/sim_server.test.mjs
end

# No package in the lockfile may have lost the provenance a lower version
# had, or have it from another source repository (#128); the installed
# packages' registry signatures and
# provenance attestations are checked right after they are installed,
# before any of them runs (SD-058).
group "Web app (provenance, registry signatures, type check, build, size budget, no build tool code; type check of the end-to-end tests and the build configs)"
node tools/check_provenance.mjs
echo "Node $(node -v), npm $(npm -v) check the signatures (CI: .nvmrc, #163)"
(cd web && npm ci --ignore-scripts --no-audit --no-fund && npm audit signatures && npm run build && npm run typecheck:node)
(cd web && node scripts/tool-code-guard-selftest.mjs)
end

if [ "${E2E:-0}" = "1" ]; then
  group "End-to-end in the browser against the simulator"
  (cd web && npx playwright test)
  end
fi
echo "All checks passed."
