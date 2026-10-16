#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# The core tests on every core (#260): each test case runs in a process of its
# own, as many at once as there are cores, the scenarios first since they take
# longest. A failing test case's output is shown after all have ended, and the
# run then ends with exit 1. A passing run prints one summary line and the
# slowest cases. build/gc_tests alone still runs everything in one process.
#   tools/core_tests.sh [binary]      JOBS=<n> sets how many run at once
set -euo pipefail
bin=${1:-build/gc_tests}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
[ -x "$bin" ] || { echo "Core tests: $bin is not there or cannot be run"; exit 1; }
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

# Two parts by source file; each test case is then picked by its place in its
# part (doctest's --first/--last count the cases that pass the filters, from 1)
filter() { if [ "$1" = s ]; then echo "--source-file=*test_scenarios.cpp"; else echo "--source-file-exclude=*test_scenarios.cpp"; fi; }
count() { "$bin" --no-version --count "$(filter "$1")" | sed -n 's/.*passing the current filters: *\([0-9][0-9]*\).*/\1/p'; }
ns=$(count s)
no=$(count o)
if [ -z "$ns" ] || [ -z "$no" ] || [ $((ns + no)) -eq 0 ]; then
  echo "Core tests: $bin lists no test cases"
  exit 1
fi

run() {
  local log="$out/$1$2"
  "$bin" --no-version --duration=true "$(filter "$1")" --first="$2" --last="$2" > "$log.log" 2>&1 && touch "$log.ok" || true
}
export -f run filter
export bin out
start=$SECONDS
{ seq 1 "$ns" | sed 's/^/s /'; seq 1 "$no" | sed 's/^/o /'; } | xargs -P "$jobs" -n 2 bash -c 'run "$1" "$2"' _

# A case passed only if it ended with exit 0 after running exactly one test case
failed=0
asserts=0
for log in "$out"/*.log; do
  ran=$(grep -c '^[0-9.]* s: ' "$log" || true)
  if [ -e "${log%.log}.ok" ] && [ "$ran" = 1 ]; then
    n=$(sed -n 's/^\[doctest\] assertions: *\([0-9][0-9]*\) .*/\1/p' "$log")
    asserts=$((asserts + ${n:-0}))
  else
    failed=$((failed + 1))
    echo "=== $(basename "$log" .log): $( [ "$ran" = 1 ] && echo failed || echo "ran $ran test cases instead of one") ==="
    cat "$log"
  fi
done
total=$((ns + no))
took=$((SECONDS - start))
if [ "$failed" -gt 0 ]; then
  echo "Core tests: $failed of $total test cases failed ($jobs at once, ${took} s)"
  exit 1
fi
echo "Core tests: $total test cases, $asserts assertions, all passed ($jobs at once, ${took} s)"
echo "Slowest:"
sed -n 's/^\([0-9.]*\) s: \(.*\)/\1 s: \2/p' "$out"/*.log | sort -rn | head -5 | sed 's/^/  /'
