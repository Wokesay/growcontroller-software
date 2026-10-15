// SPDX-License-Identifier: AGPL-3.0-or-later
// The built app holds no code of the build tools: THIRD_PARTY_NOTICES.md
// lists Vite as used for building only, not shipped. Vite adds code of its
// own unless told not to: the modulepreload polyfill, and for dynamic
// imports its preload helper. Breaks the build if either shows up, in a
// script or inline in a page. scripts/tool-code-guard.mjs checks the modules
// of every chunk during the build, Rolldown's helpers included (SD-075).
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative } from "node:path";
import { fileURLToPath } from "node:url";

const MARKERS = [
  ["modulepreload", "Vite's modulepreload polyfill (build.modulePreload.polyfill)"],
  ["vite:preloadError", "Vite's preload helper for dynamic imports"],
  ["__vitePreload", "Vite's preload helper for dynamic imports"],
];
const dir = fileURLToPath(new URL("../dist", import.meta.url));
const found = [];
let scanned = 0;
const walk = (d) => {
  for (const f of readdirSync(d)) {
    const p = join(d, f);
    if (statSync(p).isDirectory()) walk(p);
    else if (/\.(?:m?js|html)$/.test(p)) {
      scanned++;
      const text = readFileSync(p, "utf8");
      for (const [marker, what] of MARKERS) if (text.includes(marker)) found.push(`${relative(dir, p)}: ${what}`);
    }
  }
};
walk(dir);
if (scanned === 0) found.push("no .js or .html file in dist: nothing was checked");
if (found.length) {
  console.error("Build tool code in the app:\n- " + found.join("\n- "));
  process.exit(1);
}
console.log("No build tool code in the app.");
