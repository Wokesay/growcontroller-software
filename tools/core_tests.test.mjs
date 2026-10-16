// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/core_tests.sh runs every test case once, in a process of its own with
// a temporary folder of its own, several at once, the scenarios first; a case
// that fails, crashes, does not end or runs other than one case fails the run
// and its output is shown with the command to run it again (#260). It runs here
// against a stand-in that answers as doctest does.
//   node --test tools/core_tests.test.mjs
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { chmodSync, existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const script = fileURLToPath(new URL("core_tests.sh", import.meta.url));

// The stand-in: FAKE_S and FAKE_O scenario and other cases, run from --first to
// --last as doctest does, each taking 0.k s (scenarios) or 0.0k s (others) and
// saying so only with --duration=true; FAKE_FAIL fails one case, FAKE_CRASH ends
// one before it reports, FAKE_NONE runs no case and FAKE_TWO two at one place,
// FAKE_HANG sleeps at one, FAKE_SLEEP makes each take a second
const fake = `#!/usr/bin/env bash
part=o; first=1; last=; count=0; duration=0
for a in "$@"; do
  case "$a" in
    --count) count=1 ;;
    --duration=true) duration=1 ;;
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
echo "$part$first $TMPDIR $([ -d "$TMPDIR" ] && echo there) $(date +%s.%N)" >> "$FAKE_CALLS"
[ "$part$first" = "$FAKE_HANG" ] && { echo "started $part$first"; sleep 30; }
[ "$part$first" = "$FAKE_CRASH" ] && { echo "AddressSanitizer: boom"; exit 134; }
[ -n "$FAKE_SLEEP" ] && sleep 1
[ "$part$first" = "$FAKE_TWO" ] && last=$((first + 1))
if [ "$part$first" != "$FAKE_NONE" ] && [ "$duration" = 1 ]; then
  for i in $(seq "$first" "\${last:-$n}"); do
    if [ "$part" = s ]; then echo "0.\${i}00000 s: $part case $i"; else echo "0.0\${i}0000 s: $part case $i"; fi
  done
fi
[ -n "$FAKE_SLEEP" ] && echo "$part$first end $(date +%s.%N)" >> "$FAKE_CALLS"
echo "[doctest] assertions: 2 | 2 passed | 0 failed |"
if [ "$part$first" = "$FAKE_FAIL" ]; then echo "ERROR: boom in $part$first"; exit 1; fi
exit 0
`;

function setup(binary = true) {
  const dir = mkdtempSync(join(tmpdir(), "gc-core-tests-"));
  const bin = join(dir, "gc_tests");
  if (binary) {
    writeFileSync(bin, fake);
    chmodSync(bin, 0o755);
  }
  const calls = join(dir, "calls");
  writeFileSync(calls, "");
  return { dir, bin, calls };
}

function lines(calls) {
  return readFileSync(calls, "utf8").split("\n").filter(Boolean).map((l) => l.split(" "));
}

function run(env, { binary = true } = {}) {
  const { dir, bin, calls } = setup(binary);
  try {
    const r = spawnSync("bash", [script, bin], {
      encoding: "utf8",
      timeout: 60_000,
      env: { ...process.env, JOBS: "3", FAKE_S: "3", FAKE_O: "4", FAKE_CALLS: calls, ...env },
    });
    return { ...r, calls: lines(calls) };
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

test("every test case runs once, in a process and a temporary folder of its own, and a passing run sums up", () => {
  const r = run({});
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.deepEqual(r.calls.map((c) => c[0]).sort(), ["o1", "o2", "o3", "o4", "s1", "s2", "s3"]);
  assert.equal(new Set(r.calls.map((c) => c[1])).size, 7);
  for (const [place, folder, there] of r.calls) {
    assert.equal(there, "there", place);
    assert.equal(existsSync(folder), false, `${place}'s folder is removed`);
  }
  assert.match(r.stdout, /^Core tests: 7 test cases, 14 assertions, all passed \(3 at once, \d+ s\)$/m);
  // The five slowest, slowest first
  assert.match(r.stdout, /^Slowest:\n {2}0\.300000 s: s case 3\n {2}0\.200000 s: s case 2\n {2}0\.100000 s: s case 1\n {2}0\.040000 s: o case 4\n {2}0\.030000 s: o case 3\n?$/m);
});

test("JOBS cases run at once, by default as many as there are cores", () => {
  let r = run({ FAKE_SLEEP: "1" });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  const span = {};
  for (const [place, a, b, c] of r.calls) {
    if (a === "end") span[place].end = Number(b);
    else span[place] = { start: Number(c) };
  }
  // The most running at any one moment, counted when each starts
  const spans = Object.values(span);
  const most = Math.max(...spans.map((x) => spans.filter((y) => y.start <= x.start && x.start < y.end).length));
  assert.equal(most, 3);
  const cores = spawnSync("getconf", ["_NPROCESSORS_ONLN"], { encoding: "utf8" }).stdout.trim();
  r = run({ JOBS: "" });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout, new RegExp(`all passed \\(${cores} at once`));
});

test("the scenarios start first", () => {
  const r = run({ JOBS: "1" });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.deepEqual(r.calls.map((c) => c[0]), ["s1", "s2", "s3", "o1", "o2", "o3", "o4"]);
});

test("a failing test case fails the run; its output and how to run it again are shown after all have ended, the others' output is not", () => {
  const r = run({ FAKE_FAIL: "o2" });
  assert.equal(r.status, 1);
  assert.equal(r.calls.length, 7);
  assert.match(r.stdout, /^=== o2: failed \(exit 1\) ===\nAgain: \S+gc_tests '--source-file-exclude=\*test_scenarios\.cpp' --first=2 --last=2\n[\s\S]*ERROR: boom in o2/m);
  assert.doesNotMatch(r.stdout, /s case 1/);
  assert.match(r.stdout, /^Core tests: 1 of 7 test cases failed \(3 at once, \d+ s\)$/m);
});

test("a crash, or a place that runs no test case, fails the run and says which", () => {
  let r = run({ FAKE_CRASH: "s2" });
  assert.equal(r.status, 1);
  assert.match(r.stdout, /^=== s2: ended early \(exit 134\) ===\nAgain: \S+ '--source-file=\*test_scenarios\.cpp' --first=2 --last=2\nAddressSanitizer: boom$/m);
  r = run({ FAKE_NONE: "o4" });
  assert.equal(r.status, 1);
  assert.match(r.stdout, /^=== o4: ran 0 test cases instead of one ===$/m);
  r = run({ FAKE_TWO: "o2" });
  assert.equal(r.status, 1);
  assert.match(r.stdout, /^=== o2: ran 2 test cases instead of one ===$/m);
});

test("a binary that lists no test cases, no binary, or JOBS that is no whole number of at least 1 fails the run", () => {
  let r = run({ FAKE_S: "0", FAKE_O: "0" });
  assert.equal(r.status, 1);
  assert.match(r.stdout, /lists no test cases/);
  r = run({}, { binary: false });
  assert.equal(r.status, 1);
  assert.match(r.stdout, /is not there or cannot be run/);
  for (const jobs of ["0", "00", "-1", "two", "1.5", " 2"]) {
    r = run({ JOBS: jobs });
    assert.equal(r.status, 1, jobs);
    assert.match(r.stdout, /JOBS must be a whole number of at least 1/, jobs);
    assert.deepEqual(r.calls, [], jobs);
  }
});

test("a run stopped from outside names the cases that had not ended, with what they printed", async () => {
  const { dir, bin, calls } = setup();
  try {
    const child = spawn("bash", [script, bin], {
      detached: true,
      env: { ...process.env, JOBS: "2", FAKE_S: "1", FAKE_O: "2", FAKE_HANG: "s1", FAKE_CALLS: calls },
    });
    let stdout = "";
    child.stdout.on("data", (d) => (stdout += d));
    const ended = new Promise((resolve) => child.on("close", resolve));
    // Once the other cases are done, stop the whole run as a CI time limit does
    for (let i = 0; i < 100 && lines(calls).length < 3; i++) await new Promise((r) => setTimeout(r, 100));
    await new Promise((r) => setTimeout(r, 500));
    process.kill(-child.pid, "SIGTERM");
    const status = await ended;
    assert.notEqual(status, 0);
    assert.match(stdout, /^Core tests: stopped; these had not ended:\n=== s1 ===\nstarted s1$/m);
    assert.doesNotMatch(stdout, /=== o1 ===/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("tools/ci.sh runs the core tests through this script", () => {
  const ci = readFileSync(fileURLToPath(new URL("ci.sh", import.meta.url)), "utf8");
  assert.match(ci, /^tools\/core_tests\.sh build\/gc_tests$/m);
  assert.doesNotMatch(ci, /^\.\/build\/gc_tests/m);
});
