// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests for the web app's SBOM (tools/sbom_web.mjs, #201).
//   node --test tools/sbom_web.test.mjs
import { test } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { runtimePackages, runtimeSbom } from "./sbom_web.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const lock = JSON.parse(readFileSync(join(root, "web", "package-lock.json"), "utf8"));

// A lockfile and npm's full SBOM in small: preact is a runtime package that
// a build tool also has as a peer; vite is a build tool.
const lockSmall = {
  packages: {
    "": { name: "web", version: "1.0.0" },
    "node_modules/preact": { version: "11.0.1" },
    "node_modules/@preact/signals": { version: "2.11.3" },
    "node_modules/vite": { version: "8.3.3", dev: true },
    "node_modules/@preact/preset-vite": { version: "2.10.6", dev: true },
  },
};
const fullSmall = {
  bomFormat: "CycloneDX",
  metadata: { component: { "bom-ref": "web@1.0.0" } },
  components: [
    { "bom-ref": "preact@11.0.1", name: "preact", version: "11.0.1" },
    { "bom-ref": "@preact/signals@2.11.3", name: "@preact/signals", version: "2.11.3" },
    { "bom-ref": "vite@8.3.3", name: "vite", version: "8.3.3" },
    { "bom-ref": "@preact/preset-vite@2.10.6", name: "@preact/preset-vite", version: "2.10.6" },
  ],
  dependencies: [
    { ref: "web@1.0.0", dependsOn: ["preact@11.0.1", "@preact/signals@2.11.3", "vite@8.3.3", "@preact/preset-vite@2.10.6"] },
    { ref: "@preact/signals@2.11.3", dependsOn: ["preact@11.0.1"] },
    { ref: "@preact/preset-vite@2.10.6", dependsOn: ["preact@11.0.1", "vite@8.3.3"] },
    { ref: "preact@11.0.1", dependsOn: [] },
    { ref: "vite@8.3.3", dependsOn: [] },
  ],
};

test("keeps the runtime packages, also one a build tool has as a peer, and drops the build tools", () => {
  const bom = runtimeSbom(fullSmall, lockSmall);
  assert.deepEqual(bom.components.map((c) => c["bom-ref"]), ["preact@11.0.1", "@preact/signals@2.11.3"]);
  assert.deepEqual(bom.dependencies, [
    { ref: "web@1.0.0", dependsOn: ["preact@11.0.1", "@preact/signals@2.11.3"] },
    { ref: "@preact/signals@2.11.3", dependsOn: ["preact@11.0.1"] },
    { ref: "preact@11.0.1", dependsOn: [] },
  ]);
  assert.equal(bom.metadata, fullSmall.metadata);
});

test("fails when npm's SBOM lacks a runtime package of the lockfile", () => {
  const full = { ...fullSmall, components: fullSmall.components.filter((c) => c.name !== "preact") };
  assert.throws(() => runtimeSbom(full, lockSmall), /missing from npm's SBOM: preact@11\.0\.1/);
});

test("the web SBOM lists exactly the lockfile's runtime packages", () => {
  const bom = JSON.parse(execFileSync(process.execPath, [join(root, "tools", "sbom_web.mjs")], { encoding: "utf8", maxBuffer: 64 << 20 }));
  const want = runtimePackages(lock);
  assert.ok([...want].some((ref) => ref.startsWith("preact@")), "preact is a runtime package of the lockfile");
  assert.deepEqual(new Set(bom.components.map((c) => `${c.name}@${c.version}`)), want);
  const refs = new Set([bom.metadata.component["bom-ref"], ...want]);
  for (const d of bom.dependencies) {
    assert.ok(refs.has(d.ref), d.ref);
    for (const on of d.dependsOn) assert.ok(want.has(on), `${d.ref} depends on ${on}`);
  }
});

test("the release and `npm run sbom` build the web SBOM with tools/sbom_web.mjs", () => {
  const release = readFileSync(join(root, ".github", "workflows", "release.yml"), "utf8");
  assert.match(release, /node tools\/sbom_web\.mjs > out\/dist\/sbom-web\.cdx\.json/);
  assert.match(release, /- tools\/sbom_web\.mjs$/m, "a change to it runs the release's dry run");
  const pkg = JSON.parse(readFileSync(join(root, "web", "package.json"), "utf8"));
  assert.match(pkg.scripts.sbom, /^node \.\.\/tools\/sbom_web\.mjs /);
  for (const text of [release, pkg.scripts.sbom]) assert.doesNotMatch(text, /--omit dev/);
});
