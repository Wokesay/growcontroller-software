// SPDX-License-Identifier: AGPL-3.0-or-later
// License check of all npm dependencies of the web app (CI, tools/ci.sh).
// Reads web/package-lock.json and fails if a package has no license or one
// that is not on the allow list. Runtime packages end up in the hub and the
// simulator, so they must be AGPL-compatible and listed in
// THIRD_PARTY_NOTICES.md. Build tools (dev) are never shipped; for them a few
// more permissive licenses are fine.
//   node tools/check_licenses.mjs
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { allowed } from "./spdx_expr.mjs";

const root = fileURLToPath(new URL("..", import.meta.url));
const RUNTIME = new Set(["MIT", "ISC", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "0BSD", "Zlib", "CC0-1.0", "Unlicense"]);
// CC-BY-4.0: browser usage data (caniuse-lite), read by the build only.
const DEV = new Set([...RUNTIME, "CC-BY-4.0"]);

function main() {
  const lock = JSON.parse(readFileSync(join(root, "web", "package-lock.json"), "utf8"));
  const notices = readFileSync(join(root, "THIRD_PARTY_NOTICES.md"), "utf8");
  if (!(lock.lockfileVersion >= 2) || !lock.packages) {
    console.error("License check failed: web/package-lock.json must be lockfile version 2 or later");
    process.exit(1);
  }
  const problems = [];
  let count = 0;
  for (const [path, pkg] of Object.entries(lock.packages ?? {})) {
    if (!path) continue;
    count++;
    const name = path.replace(/^.*node_modules\//, "");
    const lic = typeof pkg.license === "string" ? pkg.license : "";
    if (!lic) problems.push(`${name}: no license in package-lock.json`);
    else if (!allowed(lic, pkg.dev ? DEV : RUNTIME)) problems.push(`${name}: license ${lic} not allowed${pkg.dev ? " (build tool)" : ""}`);
    if (!pkg.dev && !notices.includes(`\`${name}\``)) problems.push(`${name}: runtime package missing in THIRD_PARTY_NOTICES.md`);
  }
  if (count === 0) problems.push("no packages found in web/package-lock.json");
  if (problems.length) {
    console.error("License check failed:\n- " + problems.join("\n- "));
    process.exit(1);
  }
  console.log(`License check: ${count} npm packages OK.`);
}

main();
