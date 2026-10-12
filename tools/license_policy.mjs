// SPDX-License-Identifier: AGPL-3.0-or-later
// Which licenses the npm packages of the web app may have, checked by
// tools/check_licenses.mjs and tested in tools/check_licenses.test.mjs.
// Runtime packages end up in the hub, the simulator and the Home Assistant
// app, so they must be AGPL-compatible and listed in THIRD_PARTY_NOTICES.md.
// Build tools (dev) are not installed anywhere we ship; for them a few more
// licenses are fine.
import { allowed } from "./spdx_expr.mjs";

export const RUNTIME = new Set(["MIT", "ISC", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "0BSD", "Zlib", "CC0-1.0", "Unlicense"]);
// CC-BY-4.0: browser usage data (caniuse-lite), read by the build only.
export const DEV = new Set([...RUNTIME, "CC-BY-4.0"]);
// MPL-2.0 only for lightningcss and its platform builds (SD-046): Vite runs
// it on our CSS and none of its files end up in the app. Other build tools
// such as Vite or Rolldown put code of their own into the app, so a dev
// package is not by itself never shipped.
const MPL_TOOLS = /^lightningcss(?:-(?:android|darwin|freebsd|linux|win32)-[a-z0-9-]+)?$/;
const DEV_MPL = new Set([...DEV, "MPL-2.0"]);

/** Problems of all packages in a package-lock.json (version 2 or later); none means OK. */
export function licenseProblems(lock, notices) {
  if (!(lock?.lockfileVersion >= 2) || !lock.packages) {
    return { count: 0, problems: ["web/package-lock.json must be lockfile version 2 or later"] };
  }
  const problems = [];
  let count = 0;
  for (const [path, pkg] of Object.entries(lock.packages)) {
    if (!path) continue;
    count++;
    const name = path.replace(/^.*node_modules\//, "");
    const lic = typeof pkg.license === "string" ? pkg.license : "";
    const ok = !pkg.dev ? RUNTIME : MPL_TOOLS.test(name) ? DEV_MPL : DEV;
    if (!lic) problems.push(`${name}: no license in package-lock.json`);
    else if (!allowed(lic, ok)) problems.push(`${name}: license ${lic} not allowed${pkg.dev ? " (build tool)" : ""}`);
    if (!pkg.dev && !notices.includes(`\`${name}\``)) problems.push(`${name}: runtime package missing in THIRD_PARTY_NOTICES.md`);
  }
  if (count === 0) problems.push("no packages found in web/package-lock.json");
  return { count, problems };
}
