// SPDX-License-Identifier: AGPL-3.0-or-later
// tools/sbom_web.mjs: the web SBOM lists exactly the runtime packages of
// web/package-lock.json, the ones the tool-code guard lets into the app
// (#201, SD-075). `npm sbom --omit dev` left out preact, a runtime package
// that a build tool has as a peer.
//   node --test tools/sbom_web.test.mjs
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";
import { shippedProblems } from "../web/scripts/tool-code-guard.mjs";
import { runtimePackages, webSbom } from "./sbom_web.mjs";

const root = join(import.meta.dirname, "..");
const lock = {
  lockfileVersion: 3,
  packages: {
    "": { name: "growcontroller-web", version: "0.1.0" },
    "node_modules/preact": { version: "11.0.0" },
    "node_modules/@preact/signals": { version: "2.11.3" },
    "node_modules/vite": { version: "8.3.2", dev: true },
    "node_modules/x": { version: "1.0.0", dev: true },
    // a dev dependency that a runtime package has as an optional peer:
    // installed by `npm ci --omit=dev`, so runtime
    "node_modules/y": { version: "2.0.0", devOptional: true },
    // a runtime binary for another platform, not installed here
    "node_modules/z-win32": { version: "1.0.0", optional: true },
    "node_modules/@preact/signals/node_modules/preact": { version: "10.0.0" },
    "node_modules/local": { resolved: "../local", link: true },
  },
};
// As npm writes them: scope optional for optional and devOptional ones, the
// development property only for dev ones
const component = (name, version, path, { dev = false, optional = false } = {}) => ({
  "bom-ref": `${name}@${version}`,
  type: "library",
  name,
  version,
  scope: dev || optional ? "optional" : "required",
  properties: [{ name: "cdx:npm:package:path", value: path }, ...(dev ? [{ name: "cdx:npm:package:development", value: "true" }] : [])],
});
const full = {
  bomFormat: "CycloneDX",
  metadata: { component: { "bom-ref": "growcontroller-web@0.1.0", name: "web" } },
  components: [
    component("preact", "11.0.0", "node_modules/preact"),
    component("@preact/signals", "2.11.3", "node_modules/@preact/signals"),
    component("vite", "8.3.2", "node_modules/vite", { dev: true, optional: true }),
    component("x", "1.0.0", "node_modules/x", { dev: true, optional: true }),
    component("y", "2.0.0", "node_modules/y", { optional: true }),
    component("preact", "10.0.0", "node_modules/@preact/signals/node_modules/preact"),
  ],
  dependencies: [
    { ref: "growcontroller-web@0.1.0", dependsOn: ["@preact/signals@2.11.3", "preact@11.0.0", "vite@8.3.2"] },
    { ref: "@preact/signals@2.11.3", dependsOn: ["preact@10.0.0"] },
    { ref: "preact@11.0.0", dependsOn: ["y@2.0.0"] },
    { ref: "vite@8.3.2", dependsOn: ["x@1.0.0", "preact@11.0.0"] },
  ],
};
const kept = (sbom) => sbom.components.map((c) => c.properties.find((p) => p.name === "cdx:npm:package:path").value);

test("the runtime packages of the lockfile: everything not dev, devOptional included, nested ones by their own path, no links", () => {
  assert.deepEqual([...runtimePackages(lock).keys()], [
    "node_modules/preact",
    "node_modules/@preact/signals",
    "node_modules/y",
    "node_modules/z-win32",
    "node_modules/@preact/signals/node_modules/preact",
  ]);
  assert.throws(() => runtimePackages({ lockfileVersion: 1, dependencies: {} }), /lockfile version 2 or later/);
});

test("the SBOM keeps the runtime packages as npm wrote them, preact included, and drops the build tools and their edges", () => {
  const sbom = webSbom(full, lock);
  assert.deepEqual(kept(sbom), ["node_modules/preact", "node_modules/@preact/signals", "node_modules/y", "node_modules/@preact/signals/node_modules/preact"]);
  for (const c of sbom.components) assert.deepEqual(c, full.components.find((f) => f["bom-ref"] === c["bom-ref"] && f.properties[0].value === c.properties[0].value));
  assert.deepEqual(sbom.dependencies, [
    { ref: "growcontroller-web@0.1.0", dependsOn: ["@preact/signals@2.11.3", "preact@11.0.0"] },
    { ref: "@preact/signals@2.11.3", dependsOn: ["preact@10.0.0"] },
    { ref: "preact@11.0.0", dependsOn: ["y@2.0.0"] },
  ]);
  assert.deepEqual(sbom.metadata, full.metadata);
  assert.equal(full.components.length, 6, "the input is not changed");
});

test("it fails when npm and the lockfile disagree, rather than write a wrong SBOM", () => {
  const without = (ref) => ({ ...full, components: full.components.filter((c) => c["bom-ref"] !== ref) });
  assert.throws(() => webSbom(without("preact@11.0.0"), lock), /preact@11\.0\.0 \(node_modules\/preact\) is a runtime package of web\/package-lock\.json but not in npm's SBOM/);
  const devMarked = { ...full, components: full.components.map((c) => (c["bom-ref"] === "preact@11.0.0" ? component("preact", "11.0.0", "node_modules/preact", { dev: true }) : c)) };
  assert.throws(() => webSbom(devMarked, lock), /npm marks preact@11\.0\.0 \(node_modules\/preact\) as development, the lockfile does not/);
  const pathless = { ...full, components: [...full.components, { "bom-ref": "q@1.0.0", name: "q", version: "1.0.0" }] };
  assert.throws(() => webSbom(pathless, lock), /q@1\.0\.0 has no cdx:npm:package:path/);
  const twice = { ...full, components: [...full.components, component("preact", "11.0.0", "node_modules/@preact/signals/node_modules/preact")] };
  assert.throws(() => webSbom(twice, { ...lock, packages: { ...lock.packages, "node_modules/@preact/signals/node_modules/preact": { version: "11.0.0" } } }), /preact@11\.0\.0 twice/);
});

test("on the real lockfile: npm's SBOM filtered is exactly the packages the tool-code guard lets into the app, preact among them", () => {
  const r = spawnSync("npm", ["sbom", "--sbom-format", "cyclonedx", "--package-lock-only"], { cwd: join(root, "web"), encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  assert.equal(r.status, 0, r.stderr);
  const realLock = JSON.parse(readFileSync(join(root, "web", "package-lock.json"), "utf8"));
  const sbom = webSbom(JSON.parse(r.stdout), realLock);
  assert.deepEqual(kept(sbom).sort(), [...runtimePackages(realLock).keys()].filter((p) => !realLock.packages[p].optional).sort());
  assert.ok(kept(sbom).includes("node_modules/preact"));
  // One notion of runtime package: the guard passes a file of a package
  // exactly when the SBOM lists it
  const web = "/w/web";
  for (const [path, entry] of Object.entries(realLock.packages)) {
    if (!path || entry.link) continue;
    const passes = shippedProblems([`${web}/${path}/index.js`], web, realLock).length === 0;
    assert.equal(passes, runtimePackages(realLock).has(path), path);
  }
});

test("run as a step, it reads npm's SBOM from stdin and writes the web SBOM", () => {
  const npm = spawnSync("npm", ["sbom", "--sbom-format", "cyclonedx", "--package-lock-only"], { cwd: join(root, "web"), encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  assert.equal(npm.status, 0, npm.stderr);
  const r = spawnSync(process.execPath, [join(root, "tools", "sbom_web.mjs"), join(root, "web", "package-lock.json")], { input: npm.stdout, encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  assert.equal(r.status, 0, r.stderr);
  assert.ok(JSON.parse(r.stdout).components.some((c) => c.name === "preact"));
  const bad = spawnSync(process.execPath, [join(root, "tools", "sbom_web.mjs"), join(root, "web", "package-lock.json")], { input: "", encoding: "utf8" });
  assert.notEqual(bad.status, 0, "no input is no SBOM");
  assert.equal(bad.stdout, "");
});

test("the release and `npm run sbom` build the web SBOM with it, from the installed packages, not with --omit", () => {
  const release = readFileSync(join(root, ".github", "workflows", "release.yml"), "utf8");
  assert.match(release, /\n {10}\(cd web && npm sbom --sbom-format cyclonedx\) \| node tools\/sbom_web\.mjs web\/package-lock\.json > out\/dist\/sbom-web\.cdx\.json\n/);
  assert.doesNotMatch(release, /--omit\b/);
  assert.match(release, /^ {6}- tools\/sbom_web\.mjs$/m, "a change to it runs the release dry run");
  const scripts = JSON.parse(readFileSync(join(root, "web", "package.json"), "utf8")).scripts;
  assert.equal(scripts.sbom, "npm sbom --sbom-format cyclonedx | node ../tools/sbom_web.mjs package-lock.json > ../build/sbom-web.cdx.json");
});
