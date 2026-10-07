// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests for the change filter of the full CI (tools/ci_changes.mjs) and
// for the summary job `ci-ok` in ci.yml (SD-030).
//   node --test tools/ci_changes.test.mjs
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { needsFullCi } from "./ci_changes.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const script = join(here, "ci_changes.mjs");

test("docs-only changes skip the full CI", () => {
  assert.equal(needsFullCi(["docs/DECISIONS.md", "CHANGELOG.md", "web/README.md"]), false);
  assert.equal(needsFullCi(["docs/templates/claude-triage.yml"]), false);
  assert.equal(needsFullCi(["LICENSES/AGPL-3.0-or-later.txt", ".claude/agents/qa.md"]), false);
  assert.equal(needsFullCi([".github/ISSUE_TEMPLATE/device.yml"]), false);
  assert.equal(needsFullCi(["docs/Über uns.md", "docs/my file.md"]), false);
  assert.equal(needsFullCi([]), false);
  assert.equal(needsFullCi([""]), false);
});

test("any other file needs the full CI", () => {
  assert.equal(needsFullCi(["core/src/dosing.cpp"]), true);
  assert.equal(needsFullCi(["docs/API.md", "web/src/app.tsx"]), true);
  assert.equal(needsFullCi([".github/workflows/ci.yml"]), true);
  assert.equal(needsFullCi(["catalog/catalog.json", "tools/ci.sh"]), true);
  assert.equal(needsFullCi(["docsx/notes.txt", "README.md.in"]), true); // prefix and suffix only
  // Files named like the docs folders, not inside them
  assert.equal(needsFullCi(["docs"]), true);
  assert.equal(needsFullCi([".claude", "LICENSE", ".github/ISSUE_TEMPLATE"]), true);
  assert.equal(needsFullCi(["core/my file.cpp", "README.MD"]), true);
});

const yml = readFileSync(join(here, "..", ".github", "workflows", "ci.yml"), "utf8");
const jobs = yml.slice(yml.indexOf("\njobs:\n"));

test("ci.yml starts always and gates the heavy jobs on the filter", () => {
  // paths-ignore would leave the required check ci-ok pending forever;
  // continue-on-error could hide a failed job from ci-ok.
  assert.doesNotMatch(yml, /^\s*paths(-ignore)?:/m);
  assert.doesNotMatch(yml, /continue-on-error/);
  for (const id of ["checks", "firmware", "packages"]) {
    const block = jobs.match(new RegExp(`^ {2}${id}:\\n((?: {4}.*\\n|\\s*\\n)*)`, "m"));
    assert.ok(block, id);
    assert.match(block[1], /^ {4}needs: changes$/m, id);
    assert.match(block[1], /^ {4}if: needs\.changes\.outputs\.code == 'true'$/m, id);
  }
});

test("ci-ok waits for every other job of ci.yml", () => {
  // A job missing from the needs of ci-ok could fail without blocking a
  // merge.
  const ids = [...jobs.matchAll(/^ {2}([A-Za-z0-9_-]+):\s*$/gm)].map((m) => m[1]);
  const needs = jobs.match(/^ {2}ci-ok:\n(?: {4}.*\n|\s*\n)*? {4}needs: \[([^\]]*)\]/m);
  assert.ok(ids.includes("ci-ok") && needs, "ci-ok with a needs list");
  assert.deepEqual(
    needs[1].split(",").map((s) => s.trim()).sort(),
    ids.filter((id) => id !== "ci-ok").sort(),
  );
});

// The scratch repository must not inherit the developer's git settings or
// a GIT_DIR of a running hook.
const env = { ...process.env, GIT_DIR: undefined, GIT_INDEX_FILE: undefined, GIT_WORK_TREE: undefined };

function git(dir, ...args) {
  const cfg = ["-c", "commit.gpgsign=false", "-c", "core.hooksPath=/dev/null"];
  return execFileSync("git", ["-C", dir, ...cfg, ...args], { encoding: "utf8", env }).trim();
}

function run(dir, ...args) {
  return execFileSync(process.execPath, [script, ...args], {
    cwd: dir,
    encoding: "utf8",
    env,
    stdio: ["ignore", "pipe", "ignore"],
  }).trim();
}

test("command line: compares two commits, unknown range runs everything", () => {
  const dir = mkdtempSync(join(tmpdir(), "ci-changes-"));
  try {
    git(dir, "init", "-q");
    git(dir, "config", "user.email", "test@example.invalid");
    git(dir, "config", "user.name", "test");
    mkdirSync(join(dir, "core"));
    mkdirSync(join(dir, "docs"));
    writeFileSync(join(dir, "core", "a.cpp"), "int a;\n");
    writeFileSync(join(dir, "core", "c.cpp"), "int c;\n");
    writeFileSync(join(dir, "docs", "a.md"), "a\n");
    git(dir, "add", ".");
    git(dir, "commit", "-q", "-m", "base");
    const base = git(dir, "rev-parse", "HEAD");

    writeFileSync(join(dir, "docs", "a.md"), "b\n");
    writeFileSync(join(dir, "docs", "Ü b.md"), "c\n"); // quoted by git without -z
    git(dir, "add", ".");
    git(dir, "commit", "-q", "-m", "docs");
    assert.equal(run(dir, base, "HEAD"), "code=false");

    // Moving a code file into docs/ removes code: without --no-renames
    // git would list only the new path.
    renameSync(join(dir, "core", "a.cpp"), join(dir, "docs", "a.cpp"));
    git(dir, "add", "-A");
    git(dir, "commit", "-q", "-m", "move");
    assert.equal(run(dir, "HEAD^1", "HEAD"), "code=true");

    git(dir, "rm", "-q", "docs/a.md");
    git(dir, "commit", "-q", "-m", "delete docs");
    assert.equal(run(dir, "HEAD^1", "HEAD"), "code=false");
    git(dir, "rm", "-q", "core/c.cpp");
    git(dir, "commit", "-q", "-m", "delete code");
    assert.equal(run(dir, "HEAD^1", "HEAD"), "code=true");
    git(dir, "commit", "-q", "--allow-empty", "-m", "empty");
    assert.equal(run(dir, "HEAD^1", "HEAD"), "code=false");

    // Like GitHub's PR merge commit: a docs-only branch merged into a main
    // that moved on with code compares against main, not the branch base.
    const fork = git(dir, "rev-parse", "HEAD");
    mkdirSync(join(dir, "core"), { recursive: true }); // git rm removed it
    writeFileSync(join(dir, "core", "b.cpp"), "int b;\n");
    git(dir, "add", ".");
    git(dir, "commit", "-q", "-m", "main moves on");
    const main = git(dir, "rev-parse", "HEAD");
    git(dir, "checkout", "-q", "-b", "pr", fork);
    writeFileSync(join(dir, "docs", "b.md"), "b\n");
    git(dir, "add", ".");
    git(dir, "commit", "-q", "-m", "pr docs");
    git(dir, "checkout", "-q", "--detach", main);
    git(dir, "merge", "-q", "--no-ff", "-m", "merge", "pr");
    assert.equal(run(dir, "HEAD^1", "HEAD"), "code=false");

    assert.equal(run(dir, "0000000000000000000000000000000000000000", "HEAD"), "code=true");
    assert.equal(run(dir, "", "HEAD"), "code=true");
    assert.equal(run(dir), "code=true");
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
