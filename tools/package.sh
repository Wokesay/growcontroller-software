#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Packs the simulator as a download and tests the package:
#   1. start with arguments (API and embedded web app answer),
#   2. plain start without arguments as by double-click (port, data folder
#      next to the program; a data folder from before the name Doseph is
#      used as it is while there is no new one, #312),
#   3. plain start with a data folder it cannot read (SD-074, #214),
#   4. plain start that finds neither its own folder nor the working folder
#      (#213);
#   3 and 4 run in memory only and do not name a data folder; both are left
#   out on Windows (no links to themselves there, and the working folder
#   cannot be removed while in use).
# Run from the repository root after the build in build-pkg/:
#   tools/package.sh <program name> <platform>
# In GitHub Actions PKG_DIR and PKG_ZIP go to $GITHUB_ENV.
set -Eeuo pipefail  # -E: the ERR trap shows the logs also when a function fails
bin="$1"
name="$2"
cd "$(dirname "$0")/.."

v=$(tr -d '\r\n' < VERSION)
d="doseph-simulator-$v-$name"
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
logs() {
  for f in sim einfach before both unreadable unreadable-before no-folder; do echo "--- $f.log"; cat "build-pkg/$f.log" 2> /dev/null || true; done
}
# On a failure: the logs, and no simulator left running
trap 'logs; kill "${pid:-}" 2> /dev/null || true' ERR

# The plain start's address from its log, once it answers there.
answers() {
  url=""
  for _ in $(seq 1 60); do
    url=$(grep -o 'http://127\.0\.0\.1:[0-9]*' "$1" | head -n 1 || true)
    [ -n "$url" ] && curl -sf "$url/api/v1/info" > /dev/null && return 0
    sleep 1
  done
  curl -sf "$url/api/v1/info" > /dev/null
}
# Memory only, said so, and the folder not named as its data folder.
memory_only() {
  grep -q "Running in memory only" "$1"
  if grep -q "^Daten:" "$1"; then
    echo "::error::$1 names a data folder"
    return 1
  fi
}

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
answers build-pkg/einfach.log
grep -q "Demo-Passwort" build-pkg/einfach.log
for _ in $(seq 1 30); do [ -f "$e/doseph-data/config.json" ] && break; sleep 1; done
test -f "$e/doseph-data/config.json"
stop "$pid"

# 2b. A data folder from before the name Doseph, and no new one: it is used as
#     it is and named, and no new folder is made (#312)
mv "$e/doseph-data" "$e/growcontroller-daten"
"./$e/$bin" > build-pkg/before.log 2>&1 &
pid=$!
answers build-pkg/before.log
grep -q "^Daten: .*growcontroller-daten" build-pkg/before.log
stop "$pid"
test -f "$e/growcontroller-daten/config.json"
test ! -e "$e/doseph-data"

# 2c. Both folders: the new one is used, the old one is left as it is
mkdir "$e/doseph-data"
"./$e/$bin" > build-pkg/both.log 2>&1 &
pid=$!
answers build-pkg/both.log
grep -q "^Daten: .*doseph-data" build-pkg/both.log
for _ in $(seq 1 30); do [ -f "$e/doseph-data/config.json" ] && break; sleep 1; done
stop "$pid"
test -f "$e/doseph-data/config.json"
test -f "$e/growcontroller-daten/config.json"
rm -rf "$e/growcontroller-daten" "$e/doseph-data"

if [ "$windows" = 0 ]; then
  # 3. The data folder next to the program cannot be read (here a link to
  #    itself, as after a start with sudo it may not be entered): it is left
  #    as it is (SD-074, #214)
  rm -rf "$e/doseph-data"
  ln -s doseph-data "$e/doseph-data"
  "./$e/$bin" > build-pkg/unreadable.log 2>&1 &
  pid=$!
  answers build-pkg/unreadable.log
  grep -q "Data cannot be read" build-pkg/unreadable.log
  memory_only build-pkg/unreadable.log
  stop "$pid"
  test -L "$e/doseph-data"

  # 3b. An old folder from before the name Doseph that cannot be read, and no
  #     new one: it is used as it is, so memory only, and no new folder is
  #     made (SD-074, #312)
  rm -f "$e/doseph-data"
  ln -s growcontroller-daten "$e/growcontroller-daten"
  "./$e/$bin" > build-pkg/unreadable-before.log 2>&1 &
  pid=$!
  answers build-pkg/unreadable-before.log
  grep -q "Data cannot be read" build-pkg/unreadable-before.log
  memory_only build-pkg/unreadable-before.log
  stop "$pid"
  test -L "$e/growcontroller-daten"
  test ! -e "$e/doseph-data"
  rm -f "$e/growcontroller-daten"

  # 4. Started by a relative name in a working folder that is gone: no folder
  #    for the data can be found (#213)
  rm -f "$e/doseph-data"
  mkdir "$e/weg"
  bash -c 'cd "$1" && rmdir "$1" && exec -a "$2" "$3"' _ "$PWD/$e/weg" "$bin" "$PWD/$e/$bin" > build-pkg/no-folder.log 2>&1 &
  pid=$!
  answers build-pkg/no-folder.log
  grep -q "No folder for the data" build-pkg/no-folder.log
  memory_only build-pkg/no-folder.log
  stop "$pid"
fi
logs

echo "Paket dist/$d geprüft"
if [ -n "${GITHUB_ENV:-}" ]; then
  echo "PKG_DIR=dist/$d" >> "$GITHUB_ENV"
  echo "PKG_ZIP=$d.zip" >> "$GITHUB_ENV"
fi
