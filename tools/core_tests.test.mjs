// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/core_tests.sh runs every test case once, in a process of its own,
// several at once, the scenarios first; a case that fails or crashes, or a
// place in the list that runs no case, fails the run and its output is shown
// (#260). It runs here against
// a stand-in that answers as doctest does.
//   node --test tools/core_tests.test.mjs
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { chmodSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const script = fileURLToPath(new URL("core_tests.sh", import.meta.url));

// The stand-in: FAKE_S and FAKE_O scenario and other cases, run from --first to
// --last as doctest does; FAKE_FAIL fails one case, FAKE_CRASH ends one without
// output, FAKE_NONE runs no case at one place
const fake = `#!/usr/bin/env bash
part=o; first=1; last=; count=0
for a in "$@"; do
  case "$a" in
    --count) count=1 ;;
    --source-file=*) part=s ;;
    --first=*) first=\${a#--first=} ;;
    --last=*) last=\${a#--last=} ;;
  esac
done
n=$FAKE_O; [ "$part" = s ] && n=$FAKE_S
if [ "$count" = 1 ]; then
  echo "[doctest] unskipped test cases passing the current filters: $n"
  exit 0
fi
echo "$part$first" >> "$FAKE_CALLS"
[ "$part$first" = "$FAKE_CRASH" ] && exit 134
if [ "$part$first" != "$FAKE_NONE" ]; then
  for i in $(seq "$first" "\${last:-$n}"); do echo "0.01 s: $part case $i"; done
fi
echo "[doctest] assertions: 2 | 2 passed | 0 failed |"
if [ "$part$first" = "$FAKE_FAIL" ]; then echo "ERROR: boom in $part$first"; exit 1; fi
exit 0
`;

function run(env, { binary = true } = {}) {
  const dir = mkdtempSync(join(tmpdir(), "gc-core-tests-"));
  try {
    const bin = join(dir, "gc_tests");
    if (binary) {
      writeFileSync(bin, fake);
      chmodSync(bin, 0o755);
    }
    const calls = join(dir, "calls");
    writeFileSync(calls, "");
    const r = spawnSync("bash", [script, bin], {
      encoding: "utf8",
      env: { ...process.env, JOBS: "3", FAKE_S: "3", FAKE_O: "4", FAKE_CALLS: calls, ...env },
    });
    return { ...r, calls: readFileSync(calls, "utf8").split("\n").filter(Boolean) };
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

test("every test case runs once, in a process of its own, and a passing run sums up", () => {
  const r = run({});
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.deepEqual([...r.calls].sort(), ["o1", "o2", "o3", "o4", "s1", "s2", "s3"]);
  assert.match(r.stdout, /^Core tests: 7 test cases, 14 assertions, all passed \(3 at once, \d+ s\)$/m);
  assert.match(r.stdout, /^Slowest:\n {2}0\.01 s: /m);
});

test("the scenarios start first", () => {
  const r = run({ JOBS: "1" });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.deepEqual(r.calls, ["s1", "s2", "s3", "o1", "o2", "o3", "o4"]);
});

test("a failing test case fails the run; its output is shown after all have ended, the others' is not", () => {
  const r = run({ FAKE_FAIL: "o2" });
  assert.equal(r.status, 1);
  assert.equal(r.calls.length, 7);
  assert.match(r.stdout, /^=== o2: failed ===\n[\s\S]*ERROR: boom in o2/m);
  assert.doesNotMatch(r.stdout, /s case 1/);
  assert.match(r.stdout, /^Core tests: 1 of 7 test cases failed \(3 at once, \d+ s\)$/m);
});

test("a crash, or a place that runs no test case, fails the run", () => {
  for (const [env, part] of [[{ FAKE_CRASH: "s2" }, "s2"], [{ FAKE_NONE: "o4" }, "o4"]]) {
    const r = run(env);
    assert.equal(r.status, 1, part);
    assert.match(r.stdout, new RegExp(`^=== ${part}: ran 0 test cases instead of one ===$`, "m"), part);
  }
});

test("a binary that lists no test cases, or none at all, fails the run", () => {
  let r = run({ FAKE_S: "0", FAKE_O: "0" });
  assert.equal(r.status, 1);
  assert.match(r.stdout, /lists no test cases/);
  r = run({}, { binary: false });
  assert.equal(r.status, 1);
  assert.match(r.stdout, /is not there or cannot be run/);
});
