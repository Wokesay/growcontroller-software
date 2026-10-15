// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/signatures_report.mjs: why `npm audit signatures` failed (#163).
//   node --test tools/signatures_report.test.mjs
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { describe } from "./signatures_report.mjs";

test("names each invalid or missing package with npm's code and message", () => {
  const lines = describe(
    JSON.stringify({
      invalid: [
        {
          name: "@babel/helper-validator-identifier",
          version: "7.29.7",
          resolved: "https://registry.npmjs.org/@babel/helper-validator-identifier/-/helper-validator-identifier-7.29.7.tgz",
          integrity: "sha512-qehx",
          predicateType: "https://slsa.dev/provenance/v1",
          code: "EATTESTATIONVERIFY",
          message: "artifact signature verification failed",
        },
      ],
      missing: [{ name: "left-pad", version: "1.3.0", code: "EMISSINGSIGNATUREKEY" }],
    }),
  );
  assert.deepEqual(lines, [
    "invalid: @babel/helper-validator-identifier@7.29.7 (attestation https://slsa.dev/provenance/v1)",
    "  EATTESTATIONVERIFY: artifact signature verification failed",
    "  from https://registry.npmjs.org/@babel/helper-validator-identifier/-/helper-validator-identifier-7.29.7.tgz",
    "  integrity sha512-qehx",
    "missing: left-pad@1.3.0",
    "  EMISSINGSIGNATUREKEY",
  ]);
});

test("says so when npm now finds nothing, or answers with no JSON", () => {
  assert.deepEqual(describe(JSON.stringify({ invalid: [], missing: [] })), [
    "npm finds no invalid or missing signature now; the failure did not come back.",
  ]);
  assert.deepEqual(describe("npm error Failed to download"), ["npm gave no JSON:", "npm error Failed to download"]);
  assert.deepEqual(describe(""), ["npm gave no JSON."]);
});

test("npm that cannot check at all is not taken for a check that passed", () => {
  // What npm answers when it cannot reach Sigstore's TUF repository
  assert.deepEqual(describe(JSON.stringify({ error: { summary: "Failed to download", detail: "" } })), [
    "npm could not check the signatures: Failed to download",
  ]);
  assert.deepEqual(describe(JSON.stringify({ error: { code: "ENOTFOUND", summary: "getaddrinfo", detail: "tuf-repo-cdn.sigstore.dev" } })), [
    "npm could not check the signatures: ENOTFOUND: getaddrinfo",
    "  tuf-repo-cdn.sigstore.dev",
  ]);
  for (const other of ["null", "{}", JSON.stringify({ invalid: "x" })])
    assert.deepEqual(describe(other), [`npm gave no report: ${other}`], other);
});

test("a job that failed before npm ci gets a note, not a failure of its own", () => {
  const empty = mkdtempSync(join(tmpdir(), "sig-"));
  const r = spawnSync(process.execPath, [join(import.meta.dirname, "signatures_report.mjs"), empty], { encoding: "utf8" });
  assert.equal(r.status, 0);
  assert.match(r.stdout, /nothing to report/);
});
