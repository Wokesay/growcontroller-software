// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests for the SBOM of the simulator's C++ dependencies (tools/sbom_cpp.mjs).
//   node --test tools/sbom_cpp.test.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { parseDeps, sbom } from "./sbom_cpp.mjs";

const deps = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "cmake", "deps.cmake"), "utf8");

test("every pinned header of cmake/deps.cmake is found with version and hash", () => {
  const found = parseDeps(deps);
  assert.deepEqual(
    found.map((d) => d.name),
    ["nlohmann_json", "cpp_httplib", "doctest"],
  );
  for (const d of found) {
    assert.match(d.version, /^\d+\.\d+\.\d+$/, d.name);
    assert.match(d.sha256, /^[0-9a-f]{64}$/, d.name);
    assert.ok(deps.includes(d.sha256), d.name);
  }
});

test("the SBOM lists the shipped libraries, not the test framework", () => {
  const bom = sbom(deps, "1.2.3-beta.1");
  assert.equal(bom.bomFormat, "CycloneDX");
  assert.equal(bom.metadata.component.version, "1.2.3-beta.1");
  const byName = Object.fromEntries(bom.components.map((c) => [c.name, c]));
  assert.deepEqual(Object.keys(byName).sort(), ["cpp-httplib", "nlohmann-json"]);
  const json = byName["nlohmann-json"];
  assert.equal(json.version, "3.11.3");
  assert.equal(json.purl, "pkg:github/nlohmann/json@v3.11.3");
  assert.equal(json.hashes[0].alg, "SHA-256");
  assert.ok(deps.includes(json.hashes[0].content));
  assert.equal(json.licenses[0].license.id, "MIT");
});

test("an unknown dependency stops the build instead of an incomplete SBOM", () => {
  const extra = `${deps}\ngc_fetch_header(newlib\n  "https://raw.githubusercontent.com/x/newlib/v1.0.0/newlib.h"\n  ${"a".repeat(64)}\n  "newlib.h")\n`;
  assert.throws(() => sbom(extra, "1.0.0"), /newlib/);
});
