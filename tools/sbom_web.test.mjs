// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/sbom_web.mjs: the web SBOM lists exactly the runtime packages of
// web/package-lock.json (#201, SD-075). `npm sbom --omit dev` left out
// preact, a runtime package that a build tool has as a peer.
//   node --test tools/sbom_web.test.mjs
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";
import { runtimePackages, webSbom } from "./sbom_web.mjs";

const root = join(import.meta.dirname, "..");
const lock = {
  packages: {
    "": { name: "growcontroller-web", version: "0.1.0" },
    "node_modules/preact": { version: "11.0.0", peer: true },
    "node_modules/@preact/signals": { version: "2.11.3" },
    "node_modules/vite": { version: "8.3.2", dev: true },
    "node_modules/x": { version: "1.0.0", devOptional: true },
    "node_modules/@preact/signals/node_modules/preact": { version: "10.0.0" },
  },
};
const component = (name, version, path, dev) => ({
  "bom-ref": `${name}@${version}`,
  type: "library",
  name,
  version,
  scope: dev ? "optional" : "required",
  properties: [{ name: "cdx:npm:package:path", value: path }, ...(dev ? [{ name: "cdx:npm:package:development", value: "true" }] : [])],
});
// What npm writes without --omit dev: preact is marked development because
// a build tool has it as a peer
const full = {
  bomFormat: "CycloneDX",
  metadata: { component: { "bom-ref": "growcontroller-web@0.1.0", name: "web" } },
  components: [
    component("preact", "11.0.0", "node_modules/preact", true),
    component("@preact/signals", "2.11.3", "node_modules/@preact/signals", false),
    component("vite", "8.3.2", "node_modules/vite", true),
    component("x", "1.0.0", "node_modules/x", true),
    component("preact", "10.0.0", "node_modules/@preact/signals/node_modules/preact", false),
  ],
  dependencies: [
    { ref: "growcontroller-web@0.1.0", dependsOn: ["@preact/signals@2.11.3", "preact@11.0.0", "vite@8.3.2"] },
    { ref: "@preact/signals@2.11.3", dependsOn: ["preact@10.0.0"] },
    { ref: "vite@8.3.2", dependsOn: ["x@1.0.0", "preact@11.0.0"] },
  ],
};

test("the runtime packages of the lockfile: not dev, not devOptional, nested ones by their own path", () => {
  assert.deepEqual([...runtimePackages(lock).entries()], [
    ["node_modules/preact", "preact@11.0.0"],
    ["node_modules/@preact/signals", "@preact/signals@2.11.3"],
    ["node_modules/@preact/signals/node_modules/preact", "preact@10.0.0"],
  ]);
});

test("the SBOM keeps the runtime packages, preact included, and drops the build tools and their edges", () => {
  const sbom = webSbom(full, lock);
  assert.deepEqual(sbom.components.map((c) => c["bom-ref"]), ["preact@11.0.0", "@preact/signals@2.11.3", "preact@10.0.0"]);
  for (const c of sbom.components) {
    assert.equal(c.scope, "required", c["bom-ref"]);
    assert.ok(!c.properties.some((p) => p.name === "cdx:npm:package:development"), c["bom-ref"]);
  }
  assert.deepEqual(sbom.dependencies, [
    { ref: "growcontroller-web@0.1.0", dependsOn: ["@preact/signals@2.11.3", "preact@11.0.0"] },
    { ref: "@preact/signals@2.11.3", dependsOn: ["preact@10.0.0"] },
  ]);
  assert.deepEqual(sbom.metadata, full.metadata);
  assert.equal(full.components.length, 5, "the input is not changed");
});

test("a runtime package npm did not list fails, as does a component without a path", () => {
  const missing = { ...full, components: full.components.filter((c) => c["bom-ref"] !== "preact@11.0.0") };
  assert.throws(() => webSbom(missing, lock), /preact@11\.0\.0 \(node_modules\/preact\) is a runtime package of web\/package-lock\.json but not in npm's SBOM/);
  const pathless = { ...full, components: [...full.components, { "bom-ref": "y@1.0.0", name: "y", version: "1.0.0" }] };
  assert.throws(() => webSbom(pathless, lock), /y@1\.0\.0 has no cdx:npm:package:path/);
});

test("on the real lockfile: exactly its runtime packages, preact among them", () => {
  const r = spawnSync("npm", ["sbom", "--sbom-format", "cyclonedx", "--package-lock-only"], { cwd: join(root, "web"), encoding: "utf8", maxBuffer: 64 * 1024 * 1024, shell: process.platform === "win32" });
  assert.equal(r.status, 0, r.stderr);
  const realLock = JSON.parse(readFileSync(join(root, "web", "package-lock.json"), "utf8"));
  const sbom = webSbom(JSON.parse(r.stdout), realLock);
  const listed = sbom.components.map((c) => `${c.name}@${c.version}`).sort();
  assert.deepEqual(listed, [...runtimePackages(realLock).values()].sort());
  assert.ok(listed.some((p) => p.startsWith("preact@")), listed.join(", "));
});

test("the release and `npm run sbom` build the web SBOM with it, not with --omit dev", () => {
  const release = readFileSync(join(root, ".github", "workflows", "release.yml"), "utf8");
  assert.match(release, /\(cd web && npm sbom --sbom-format cyclonedx --package-lock-only\) \| node tools\/sbom_web\.mjs web\/package-lock\.json > out\/dist\/sbom-web\.cdx\.json/);
  assert.doesNotMatch(release, /--omit dev/);
  assert.match(release, /^ {6}- tools\/sbom_web\.mjs$/m, "a change to it runs the release dry run");
  const scripts = JSON.parse(readFileSync(join(root, "web", "package.json"), "utf8")).scripts;
  assert.equal(scripts.sbom, "npm sbom --sbom-format cyclonedx --package-lock-only | node ../tools/sbom_web.mjs package-lock.json > ../build/sbom-web.cdx.json");
});
