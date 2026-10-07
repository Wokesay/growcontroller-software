// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests for the change filter of the full CI (tools/ci_changes.mjs).
//   node --test tools/ci_changes.test.mjs
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, mkdtempSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { needsFullCi } from "./ci_changes.mjs";

const script = join(dirname(fileURLToPath(import.meta.url)), "ci_changes.mjs");

test("docs-only changes skip the full CI", () => {
  assert.equal(needsFullCi(["docs/DECISIONS.md", "CHANGELOG.md", "web/README.md"]), false);
  assert.equal(needsFullCi(["docs/templates/claude-triage.yml"]), false);
  assert.equal(needsFullCi(["LICENSES/AGPL-3.0-or-later.txt", ".claude/agents/qa.md"]), false);
  assert.equal(needsFullCi([".github/ISSUE_TEMPLATE/device.yml"]), false);
  assert.equal(needsFullCi([]), false);
  assert.equal(needsFullCi([""]), false); // trailing newline of git diff
});

test("any other file needs the full CI", () => {
  assert.equal(needsFullCi(["core/src/dosing.cpp"]), true);
  assert.equal(needsFullCi(["docs/API.md", "web/src/app.tsx"]), true);
  assert.equal(needsFullCi([".github/workflows/ci.yml"]), true);
  assert.equal(needsFullCi(["catalog/catalog.json", "tools/ci.sh"]), true);
  assert.equal(needsFullCi(["docsx/notes.txt", "README.md.in"]), true); // prefix and suffix only
});

function git(dir, ...args) {
  return execFileSync("git", ["-C", dir, ...args], { encoding: "utf8" }).trim();
}

function run(dir, ...args) {
  return execFileSync(process.execPath, [script, ...args], { cwd: dir, encoding: "utf8" }).trim();
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
    writeFileSync(join(dir, "docs", "a.md"), "a\n");
    git(dir, "add", ".");
    git(dir, "commit", "-q", "-m", "base");
    const base = git(dir, "rev-parse", "HEAD");

    writeFileSync(join(dir, "docs", "a.md"), "b\n");
    git(dir, "commit", "-q", "-am", "docs");
    assert.equal(run(dir, base, "HEAD"), "code=false");

    // Moving a code file into docs/ removes code: without --no-renames
    // git would list only the new path.
    renameSync(join(dir, "core", "a.cpp"), join(dir, "docs", "a.cpp"));
    git(dir, "add", "-A");
    git(dir, "commit", "-q", "-m", "move");
    assert.equal(run(dir, "HEAD^1", "HEAD"), "code=true");

    assert.equal(run(dir, "", "HEAD"), "code=true");
    assert.equal(run(dir), "code=true");
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
