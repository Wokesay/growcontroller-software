// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests for the license check of the npm packages: the SPDX expressions
// (tools/spdx_expr.mjs), the allow lists (tools/license_policy.mjs) and the
// check itself (tools/check_licenses.mjs).
//   node --test tools/check_licenses.test.mjs
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { licenseProblems } from "./license_policy.mjs";
import { allowed } from "./spdx_expr.mjs";

const OK = new Set(["MIT", "Apache-2.0", "ISC"]);

test("single licenses", () => {
  assert.equal(allowed("MIT", OK), true);
  assert.equal(allowed("GPL-3.0-only", OK), false);
  assert.equal(allowed("", OK), false);
});

test("OR needs one allowed side, AND needs both", () => {
  assert.equal(allowed("(MIT OR GPL-3.0-only)", OK), true);
  assert.equal(allowed("MIT AND GPL-3.0-only", OK), false);
  assert.equal(allowed("MIT AND Apache-2.0", OK), true);
  assert.equal(allowed("(GPL-2.0-only OR (MIT AND Apache-2.0))", OK), true);
});

test("malformed expressions are never allowed", () => {
  assert.equal(allowed("MIT GPL-3.0-only", OK), false); // missing operator
  assert.equal(allowed("MIT and GPL-3.0-only", OK), false); // lower-case operator
  assert.equal(allowed("(MIT and GPL-2.0-only)", OK), false);
  assert.equal(allowed("(MIT OR ISC", OK), false); // missing bracket
  assert.equal(allowed("MIT OR ISC)", OK), false); // stray bracket
  assert.equal(allowed("MIT WITH Classpath-exception-2.0", OK), false); // WITH is not supported
  assert.equal(allowed("OR MIT", OK), false);
});

// One package in a lockfile; runtime packages are listed in the notices.
function problems(name, pkg) {
  const lock = { lockfileVersion: 3, packages: { "": {}, [`node_modules/${name}`]: pkg } };
  return licenseProblems(lock, `| \`${name}\` |`).problems;
}

test("MPL-2.0 only for lightningcss as a build tool (SD-046)", () => {
  assert.deepEqual(problems("lightningcss", { license: "MPL-2.0", dev: true }), []);
  assert.deepEqual(problems("lightningcss-linux-x64-musl", { license: "MPL-2.0", dev: true, optional: true }), []);
  // shipped, or only partly a build tool: never
  assert.equal(problems("lightningcss", { license: "MPL-2.0" }).length, 1);
  assert.equal(problems("lightningcss", { license: "MPL-2.0", devOptional: true }).length, 1);
  // any other build tool under MPL-2.0 needs a decision of its own
  assert.equal(problems("some-bundler", { license: "MPL-2.0", dev: true }).length, 1);
  assert.equal(problems("lightningcss-plugin", { license: "MPL-2.0", dev: true }).length, 1, "only its platform builds");
  assert.equal(problems("not-lightningcss", { license: "MPL-2.0", dev: true }).length, 1);
  // lightningcss gets no other license than MPL-2.0 on top
  for (const lic of ["GPL-3.0-only", "AGPL-3.0-only", "LGPL-3.0-only", "MPL-1.1", "EPL-2.0"]) {
    assert.equal(problems("lightningcss", { license: lic, dev: true }).length, 1, lic);
    assert.equal(problems("some-tool", { license: lic, dev: true }).length, 1, lic);
  }
});

test("runtime packages need a notice, every package a license", () => {
  assert.deepEqual(problems("preact", { license: "MIT" }), []);
  assert.deepEqual(licenseProblems({ lockfileVersion: 3, packages: { "": {}, "node_modules/x": { license: "MIT" } } }, "").problems, [
    "x: runtime package missing in THIRD_PARTY_NOTICES.md",
  ]);
  assert.deepEqual(problems("x", { dev: true }), ["x: no license in package-lock.json"]);
  assert.equal(licenseProblems({ lockfileVersion: 1, dependencies: {} }, "").problems.length, 1);
  assert.equal(licenseProblems({ lockfileVersion: 3, packages: { "": {} } }, "").problems.length, 1);
});

test("the check runs on the real lockfile and says so", () => {
  const script = fileURLToPath(new URL("./check_licenses.mjs", import.meta.url));
  assert.match(execFileSync(process.execPath, [script], { encoding: "utf8" }), /^License check: \d+ npm packages OK\.$/m);
});
