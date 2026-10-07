#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Packs the simulator as a download and tests the package twice:
#   1. start with arguments (API and embedded web app answer),
#   2. plain start without arguments as by double-click (port, data folder
#      next to the program).
# Run from the repository root after the build in build-pkg/:
#   tools/package.sh <program name> <platform>
# In GitHub Actions PKG_DIR and PKG_ZIP go to $GITHUB_ENV.
set -euo pipefail
bin="$1"
name="$2"
cd "$(dirname "$0")/.."

v=$(tr -d '\r\n' < VERSION)
d="growcontroller-simulator-$v-$name"
rm -rf "dist/$d"
mkdir -p "dist/$d"
if [ -f build-pkg/Release/gc_sim_server.exe ]; then src=build-pkg/Release/gc_sim_server.exe; else src=build-pkg/gc_sim_server; fi
cp "$src" "dist/$d/$bin"
cp sim/README.txt "dist/$d/"
cp LICENSE "dist/$d/LICENSE.txt"
node tools/third_party.mjs "dist/$d/THIRD_PARTY_LICENSES.txt"

# Unter Windows (Git-Bash, auch lokal) beendet taskkill das Programm
# zuverlässig; es trifft alle Simulatoren gleichen Namens.
case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) windows=1 ;; *) windows=0 ;; esac
stop() {
  if [ "$windows" = 1 ]; then taskkill //F //IM "$bin" > /dev/null 2>&1 || true; else kill "$1" 2> /dev/null || true; fi
  wait "$1" 2> /dev/null || true
}
logs() { echo "--- sim.log"; cat build-pkg/sim.log 2> /dev/null || true; echo "--- einfach.log"; cat build-pkg/einfach.log 2> /dev/null || true; }
trap 'logs' ERR

# 1. Mit Argumenten
"./dist/$d/$bin" --port 18080 --scenario neu --prefill 0 > build-pkg/sim.log 2>&1 &
pid=$!
for _ in $(seq 1 60); do curl -sf http://127.0.0.1:18080/api/v1/info > /dev/null && break; sleep 1; done
curl -sf http://127.0.0.1:18080/api/v1/info
echo
curl -sf --compressed http://127.0.0.1:18080/ | grep -q '<div id="app">'
stop "$pid"

# 2. Einfachstart: Kopie in eigenem Ordner, ohne Argumente
e=build-pkg/einfach
rm -rf "$e"
mkdir -p "$e"
cp "dist/$d/$bin" "$e/"
"./$e/$bin" > build-pkg/einfach.log 2>&1 &
pid=$!
url=""
for _ in $(seq 1 60); do
  url=$(grep -o 'http://127\.0\.0\.1:[0-9]*' build-pkg/einfach.log | head -n 1 || true)
  [ -n "$url" ] && curl -sf "$url/api/v1/info" > /dev/null && break
  sleep 1
done
curl -sf "$url/api/v1/info" > /dev/null
grep -q "Demo-Passwort" build-pkg/einfach.log
for _ in $(seq 1 30); do [ -f "$e/growcontroller-daten/config.json" ] && break; sleep 1; done
test -f "$e/growcontroller-daten/config.json"
stop "$pid"
logs

echo "Paket dist/$d geprüft"
if [ -n "${GITHUB_ENV:-}" ]; then
  echo "PKG_DIR=dist/$d" >> "$GITHUB_ENV"
  echo "PKG_ZIP=$d.zip" >> "$GITHUB_ENV"
fi
