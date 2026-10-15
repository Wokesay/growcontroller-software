// SPDX-License-Identifier: AGPL-3.0-or-later
// SBOM (CycloneDX) of the web app: exactly the runtime packages of
// web/package-lock.json, the entries without "dev": true, which ship in the
// app (SD-075). npm's own `--omit dev` drops every package that a build tool
// has as a peer, preact among them (#201), so npm lists the whole lockfile
// and this keeps only the runtime packages and their dependencies.
//   node tools/sbom_web.mjs > sbom-web.cdx.json
import { execFileSync } from "node:child_process";
import { readFileSync, realpathSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

// name@version of every runtime package in the lockfile
export function runtimePackages(lock) {
  const refs = new Set();
  for (const [path, entry] of Object.entries(lock.packages ?? {})) {
    if (path === "" || entry.dev) continue;
    const name = entry.name ?? path.slice(path.lastIndexOf("node_modules/") + "node_modules/".length);
    refs.add(`${name}@${entry.version}`);
  }
  return refs;
}

export function runtimeSbom(full, lock) {
  const keep = runtimePackages(lock);
  const root = full.metadata?.component?.["bom-ref"];
  if (!root) throw new Error("the SBOM names no component of its own");
  const components = (full.components ?? []).filter((c) => keep.has(c["bom-ref"]));
  const listed = new Set(components.map((c) => c["bom-ref"]));
  const missing = [...keep].filter((ref) => !listed.has(ref));
  if (missing.length) throw new Error(`runtime packages missing from npm's SBOM: ${missing.join(", ")}`);
  if (listed.size !== components.length) throw new Error("a runtime package is listed twice in npm's SBOM");
  const dependencies = (full.dependencies ?? [])
    .filter((d) => d.ref === root || keep.has(d.ref))
    .map((d) => ({ ...d, dependsOn: (d.dependsOn ?? []).filter((ref) => keep.has(ref)) }));
  return { ...full, components, dependencies };
}

const isMain = process.argv[1] && pathToFileURL(realpathSync(process.argv[1])).href === import.meta.url;
if (isMain) {
  const web = join(dirname(fileURLToPath(import.meta.url)), "..", "web");
  // the lockfile only: no installed packages needed
  const npm = process.platform === "win32" ? "npm.cmd" : "npm";
  const full = JSON.parse(execFileSync(npm, ["sbom", "--sbom-format", "cyclonedx", "--package-lock-only"], { cwd: web, encoding: "utf8", maxBuffer: 64 << 20 }));
  const lock = JSON.parse(readFileSync(join(web, "package-lock.json"), "utf8"));
  console.log(JSON.stringify(runtimeSbom(full, lock), null, 2));
}
