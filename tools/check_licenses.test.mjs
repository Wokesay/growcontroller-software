// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests for the SPDX expression check used by tools/check_licenses.mjs.
//   node --test tools/check_licenses.test.mjs
import assert from "node:assert/strict";
import { test } from "node:test";
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
