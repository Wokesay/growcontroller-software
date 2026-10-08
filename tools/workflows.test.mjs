// SPDX-License-Identifier: AGPL-3.0-or-later
// Every action in the workflows is made by GitHub (SD-027) and pinned by
// its full commit SHA with the version as a comment (#29); Dependabot
// keeps both up to date.
//   node --test tools/workflows.test.mjs
import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const dir = join(dirname(fileURLToPath(import.meta.url)), "..", ".github", "workflows");
const files = readdirSync(dir).filter((f) => /\.ya?ml$/.test(f));

test("actions are GitHub's own and pinned by commit SHA", () => {
  assert.ok(files.length >= 4);
  let n = 0;
  for (const f of files) {
    for (const line of readFileSync(join(dir, f), "utf8").split("\n")) {
      const m = line.match(/^\s*(?:-\s+)?uses:\s*(\S+)(.*)$/);
      if (!m) continue;
      n++;
      const [, ref, rest] = m;
      if (ref.startsWith("./")) continue; // reusable workflow of this repository
      assert.match(ref, /^actions\/[\w.-]+(\/[\w.-]+)*@[0-9a-f]{40}$/, `${f}: ${ref}`);
      assert.match(rest, /^\s+# v\d+\.\d+\.\d+\s*$/, `${f}: ${ref} needs a "# vX.Y.Z" comment`);
    }
  }
  assert.ok(n > 10, "uses: lines found");
});

test("the same action uses the same commit everywhere", () => {
  const seen = new Map();
  for (const f of files) {
    for (const m of readFileSync(join(dir, f), "utf8").matchAll(/uses:\s*(actions\/[\w.-]+)(?:\/[\w.-]+)*@([0-9a-f]{40})/g)) {
      const [, action, sha] = m;
      if (seen.has(action)) assert.equal(sha, seen.get(action), `${f}: ${action}`);
      else seen.set(action, sha);
    }
  }
});
