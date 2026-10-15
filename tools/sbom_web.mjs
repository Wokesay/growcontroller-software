// SPDX-License-Identifier: AGPL-3.0-or-later
// The web SBOM: exactly the runtime packages of web/package-lock.json, the
// packages whose code the app may hold (web/scripts/tool-code-guard.mjs;
// #201, SD-075). `npm sbom --omit dev` is not that: it drops every package a
// build tool depends on, peers included, and so left out preact. This takes
// npm's SBOM of the whole lockfile and keeps the components whose
// `cdx:npm:package:path` is a runtime entry of the lockfile, as required, and
// the dependency edges between them. A runtime package npm did not list
// fails.
//   (cd web && npm sbom --sbom-format cyclonedx --package-lock-only) \
//     | node tools/sbom_web.mjs web/package-lock.json > sbom-web.cdx.json
import { readFileSync, realpathSync } from "node:fs";
import { fileURLToPath } from "node:url";

const PATH = "cdx:npm:package:path";
const DEVELOPMENT = "cdx:npm:package:development";

// Lockfile path → name@version of every runtime package
export function runtimePackages(lock) {
  const out = new Map();
  for (const [path, entry] of Object.entries(lock.packages ?? {})) {
    if (!path || entry.dev || entry.devOptional) continue;
    const name = entry.name ?? path.slice(path.lastIndexOf("node_modules/") + "node_modules/".length);
    out.set(path, `${name}@${entry.version}`);
  }
  return out;
}

export function webSbom(full, lock) {
  const runtime = runtimePackages(lock);
  const kept = [];
  const seen = new Set();
  for (const c of full.components ?? []) {
    const path = c.properties?.find((p) => p.name === PATH)?.value;
    if (path === undefined) throw new Error(`${c["bom-ref"]} has no ${PATH}: npm's SBOM changed, check tools/sbom_web.mjs`);
    if (!runtime.has(path)) continue;
    seen.add(path);
    kept.push({ ...c, scope: "required", properties: c.properties.filter((p) => p.name !== DEVELOPMENT) });
  }
  const missing = [...runtime].filter(([path]) => !seen.has(path));
  if (missing.length) throw new Error(missing.map(([path, id]) => `${id} (${path}) is a runtime package of web/package-lock.json but not in npm's SBOM`).join("\n"));
  const refs = new Set([full.metadata?.component?.["bom-ref"], ...kept.map((c) => c["bom-ref"])]);
  const dependencies = (full.dependencies ?? [])
    .filter((d) => refs.has(d.ref))
    .map((d) => ({ ...d, dependsOn: (d.dependsOn ?? []).filter((r) => refs.has(r)) }));
  return { ...full, components: kept, dependencies };
}

function isMain() {
  try {
    return process.argv[1] && realpathSync(process.argv[1]) === realpathSync(fileURLToPath(import.meta.url));
  } catch {
    return false;
  }
}

if (isMain()) {
  const lockPath = process.argv[2];
  if (!lockPath) {
    console.error("usage: npm sbom --sbom-format cyclonedx --package-lock-only | node tools/sbom_web.mjs <package-lock.json>");
    process.exit(2);
  }
  const full = JSON.parse(readFileSync(0, "utf8"));
  const lock = JSON.parse(readFileSync(lockPath, "utf8"));
  process.stdout.write(`${JSON.stringify(webSbom(full, lock), null, 2)}\n`);
}
