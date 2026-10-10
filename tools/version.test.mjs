// SPDX-License-Identifier: AGPL-3.0-or-later
// Every place that names the release names the one in VERSION: release.yml's
// verify checks VERSION, web/package.json and the app's config.yaml at the
// tag; this catches the rest before (docs/RELEASE.md, step 1).
//   node --test tools/version.test.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const read = (f) => readFileSync(join(root, f), "utf8");
const version = read("VERSION").trim();

test("VERSION is one release in the prototype scheme", () => {
  assert.equal(read("VERSION"), `${version}\n`);
  assert.match(version, /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/);
});

test("the web app, its lock file, the app, the README and the bug form name VERSION", () => {
  assert.equal(JSON.parse(read("web/package.json")).version, version, "web/package.json");
  const lock = JSON.parse(read("web/package-lock.json"));
  assert.equal(lock.version, version, "package-lock.json");
  assert.equal(lock.packages[""].version, version, 'package-lock.json packages[""]');
  assert.ok(read("ha/app/config.yaml").split("\n").includes(`version: "${version}"`), "ha/app/config.yaml");
  assert.ok(read("README.md").includes(`**Status: prototype \`${version}\`.**`), "README status line");
  assert.match(read(".github/ISSUE_TEMPLATE/bug.yml"), new RegExp(`placeholder: ${version.replace(/[.]/g, "\\.")}\\n`), "bug form");
});

test("the changelog has the release's section, or it is still unreleased", () => {
  const log = read("CHANGELOG.md");
  assert.match(log, /^## \[Unreleased\]$/m, "an [Unreleased] section stays on top");
  const released = log.split("\n").filter((l) => l.startsWith("## [") && !l.startsWith("## [Unreleased]"));
  // Each release once; the newest first. VERSION is either the newest release
  // (being cut) or newer than it (in work); never an older one.
  const names = released.map((l) => l.match(/^## \[([^\]]+)\]/)[1]);
  assert.equal(new Set(names).size, names.length, "each release once");
  if (names.includes(version)) assert.equal(names[0], version, "VERSION is the newest release");
  for (const l of released) assert.match(l, /^## \[[^\]]+\] – \d{4}-\d{2}-\d{2}$/, l);
});
