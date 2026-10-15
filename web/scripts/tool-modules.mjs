// SPDX-License-Identifier: AGPL-3.0-or-later
// The built app holds our sources and the packages the web SBOM lists
// (`npm sbom --omit dev`), nothing of a build tool (PD-228, SD-073). Rolldown
// and Vite add modules of their own when the code needs them: Rolldown its
// runtime helpers (for CommonJS and some exports), Vite its preload helper
// and polyfill. A Vite plugin looks at every module of every chunk, which the
// minified output no longer shows, and fails the build on such a module or
// on a build-time package. scripts/no-tool-code.mjs still checks the text of
// the output, the page included.
import { readFileSync } from "node:fs";
import { isAbsolute, join, normalize, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HELPERS = [
  [/^\0?rolldown[:/]runtime/, "Rolldown's runtime helpers"],
  [/vite\/preload-helper/, "Vite's preload helper for dynamic imports"],
  [/vite\/modulepreload-polyfill/, "Vite's modulepreload polyfill"],
];

const shown = (id) => id.replace(/\0/g, "\\0");

// One line per module that is not ours and not a runtime package of
// web/package-lock.json; none for a clean bundle.
export function shippedProblems(ids, root, lock) {
  const problems = [];
  for (const raw of ids) {
    const id = raw.split("?")[0];
    if (id.startsWith("\0") || !isAbsolute(id)) {
      const what = HELPERS.find(([re]) => re.test(id))?.[1] ?? "code of a build tool";
      problems.push(`${shown(id)}: ${what}`);
      continue;
    }
    const rel = relative(root, normalize(id));
    const parts = rel.split(/[\\/]/);
    if (rel.startsWith("..") || isAbsolute(rel)) {
      problems.push(`${id}: neither our sources nor a package`);
      continue;
    }
    const path = parts.join("/");
    if (path === "index.html" || parts[0] === "src") continue;
    if (parts[0] !== "node_modules") {
      problems.push(`${path}: neither our sources nor a package`);
      continue;
    }
    // The package that holds the file, nested ones by their own path
    let key = "";
    let name = "";
    for (let i = 0; i < parts.length - 1; i++) {
      if (parts[i] !== "node_modules") continue;
      name = parts[i + 1].startsWith("@") ? `${parts[i + 1]}/${parts[i + 2]}` : parts[i + 1];
      key = `${parts.slice(0, i + 1).join("/")}/${name}`;
      i += name.split("/").length;
    }
    const entry = lock.packages?.[key];
    if (!entry) problems.push(`${path}: ${name} is not in web/package-lock.json`);
    else if (entry.dev) problems.push(`${path}: ${name} is a build-time package, not in the web SBOM`);
  }
  return problems;
}

// The Vite plugin: fails the build with every problem at once.
export function toolCodeGuard({ root, lock } = {}) {
  const web = root ?? resolve(fileURLToPath(new URL("..", import.meta.url)));
  return {
    name: "growcontroller:tool-code-guard",
    apply: "build",
    generateBundle(_options, bundle) {
      const pkgs = lock ?? JSON.parse(readFileSync(join(web, "package-lock.json"), "utf8"));
      const found = [];
      for (const [file, out] of Object.entries(bundle)) {
        if (out.type !== "chunk") continue;
        for (const p of shippedProblems(Object.keys(out.modules), web, pkgs)) found.push(`${file}: ${p}`);
      }
      if (found.length) this.error(`Build tool code in the app (PD-228):\n- ${found.join("\n- ")}`);
    },
  };
}
