#!/usr/bin/env bash
# Entwicklung: Simulator bauen und starten (Demo mit 48 h Verlauf), Web-App bauen.
#   tools/dev.sh              → http://127.0.0.1:8080, Passwort „demo-passwort“
#   SCENARIO=neu tools/dev.sh → leerer Hub mit Ersteinrichtung
# Für UI-Arbeit mit Hot-Reload zusätzlich: cd web && npm run dev (Port 5173, API → 8080)
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build build --target gc_sim_server
[ -d web/node_modules ] || (cd web && npm ci --no-audit --no-fund)
(cd web && npx vite build >/dev/null)
exec ./build/gc_sim_server --port "${PORT:-8080}" --scenario "${SCENARIO:-demo}" --web web/dist --password "${PASSWORD:-demo-passwort}" --data "${DATA:-sim-data}" "$@"
