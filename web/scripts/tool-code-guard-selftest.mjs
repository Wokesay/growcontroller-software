// SPDX-License-Identifier: AGPL-3.0-or-later
// The tool-code guard against the real bundler (SD-075): the unit tests feed
// it made-up bundles, so this builds two throwaway apps with the pinned Vite
// and Rolldown. One with a CommonJS and a dynamic import must fail, naming
// Rolldown's runtime and Vite's preload helper; one in plain modules must
// pass. Fails when either does not, for example after an update that renames
// or stops listing a helper module. Writes nothing into the repository.
//   cd web && node scripts/tool-code-guard-selftest.mjs
import { mkdirSync, mkdtempSync, realpathSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { build } from "vite";
import { toolCodeGuard } from "./tool-code-guard.mjs";

async function attempt(files) {
  const root = realpathSync(mkdtempSync(join(tmpdir(), "tool-code-guard-")));
  try {
    mkdirSync(join(root, "src"));
    for (const [name, text] of Object.entries(files)) writeFileSync(join(root, "src", name), text);
    const entry = join(root, "src", "main.js");
    await build({
      root,
      configFile: false,
      logLevel: "silent",
      plugins: [toolCodeGuard({ root, lock: { packages: {} }, entry })],
      build: { write: false, minify: true, modulePreload: { polyfill: false }, rollupOptions: { input: entry } },
    });
    return "";
  } catch (e) {
    return String(e?.message ?? e);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
}

const failures = [];
const helpers = await attempt({
  "main.js": 'import c from "./c.cjs";\nconsole.log(c.x);\nimport("./d.js").then((m) => console.log(m.d));\n',
  "c.cjs": "module.exports = { x: 1 };\n",
  "d.js": "export const d = 2;\n",
});
for (const name of ["\\0rolldown/runtime.js", "\\0vite/preload-helper.js"]) {
  if (!helpers.includes(name)) failures.push(`a CommonJS and a dynamic import: the guard did not name ${name}${helpers ? `, but said:\n${helpers}` : " (the build passed)"}`);
}
const clean = await attempt({ "main.js": 'import { d } from "./d.js";\nconsole.log(d);\n', "d.js": "export const d = 2;\n" });
if (clean) failures.push(`plain modules: the guard failed the build:\n${clean}`);
if (failures.length) {
  console.error(`The tool-code guard does not work with this bundler (SD-075):\n- ${failures.join("\n- ")}`);
  process.exit(1);
}
console.log("The tool-code guard names Rolldown's and Vite's helpers in a real build, and passes plain modules.");
