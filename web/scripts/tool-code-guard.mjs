// SPDX-License-Identifier: AGPL-3.0-or-later
// The modules the bundler puts into the app are our sources and the runtime
// packages of web/package-lock.json, which the web SBOM lists, none of a
// build tool (PD-228, SD-075). Rolldown, Oxc and Vite add modules of their own when the code
// needs them: Rolldown its runtime helpers (CommonJS interop and namespace
// objects), Oxc its syntax-lowering helpers, Vite its preload helper and
// polyfill. A Vite plugin looks at every module of every chunk, which the
// minified output no longer shows, and fails the build on such a module or
// on a build-time package. scripts/no-tool-code.mjs still checks the text
// of the output, the page included.
// It keeps the SBOM accurate against honest tools; it is no defence against
// a malicious build tool, which runs in the same process (SECURITY_MODEL.md).
// The types are in tool-code-guard.d.mts: change both together.
import { readFileSync, realpathSync } from "node:fs";
import nodePath from "node:path";
import { fileURLToPath } from "node:url";

const HELPERS = [
  [/^\0?rolldown[:/]runtime/, "Rolldown's runtime helpers: avoid CommonJS imports and namespace objects; if they cannot be avoided, Rolldown is listed as shipped (SD-075)"],
  [/^\0?@oxc-project\+runtime/, "Oxc's syntax-lowering helpers: keep the code within build.target"],
  [/vite\/preload-helper/, "Vite's preload helper for dynamic imports: the app imports statically"],
  [/vite\/modulepreload-polyfill/, "Vite's modulepreload polyfill: keep build.modulePreload.polyfill off"],
];
const TOOL = "code of a build tool: find what brings it in and keep it out of the app";
const PACKAGE = "is a build-time package: keep it out of the app, or make it a runtime dependency";

const shown = (id) => id.replace(/\0/g, "\\0");
const clean = (id) => id.split(/[?#]/)[0];

// One line per module that is not ours and not a runtime package of
// web/package-lock.json; none for a clean bundle. `path` is the platform's:
// on Windows Vite names modules with forward slashes, Node with backslashes.
export function shippedProblems(ids, root, lock, path = nodePath) {
  const { isAbsolute, relative } = path;
  const problems = [];
  for (const raw of ids) {
    // A web worker is built apart, even when inlined as a string in a
    // chunk, so its modules never reach this check
    if (/[?&](?:shared)?worker(?:_file)?(?:[&=]|$)/.test(raw)) {
      problems.push(`${shown(raw)}: a web worker, built apart: the guard cannot see its modules`);
      continue;
    }
    const id = clean(raw);
    if (id.startsWith("\0") || !isAbsolute(id)) {
      const what = HELPERS.find(([re]) => re.test(id))?.[1] ?? TOOL;
      problems.push(`${shown(id)}: ${what}`);
      continue;
    }
    const rel = relative(root, id);
    if (!rel || rel.startsWith("..") || isAbsolute(rel)) {
      problems.push(`${id}: neither our sources nor a package`);
      continue;
    }
    const parts = rel.split(/[\\/]/);
    const shownPath = parts.join("/");
    if (shownPath === "index.html" || parts[0] === "src") continue;
    if (parts[0] !== "node_modules") {
      problems.push(`${shownPath}: neither our sources nor a package`);
      continue;
    }
    // The innermost installed package that holds the file: the longest
    // leading path that web/package-lock.json knows
    let key = "";
    for (let n = parts.length - 1; n >= 2 && !key; n--) {
      const candidate = parts.slice(0, n).join("/");
      if (lock.packages?.[candidate]) key = candidate;
    }
    if (!key) {
      const name = parts[1]?.startsWith("@") ? `${parts[1]}/${parts[2]}` : parts[1];
      problems.push(`${shownPath}: ${name ?? "?"} is not in web/package-lock.json`);
      continue;
    }
    const name = key.slice(key.lastIndexOf("node_modules/") + "node_modules/".length);
    if (lock.packages[key].dev) problems.push(`${shownPath}: ${name} ${PACKAGE}`);
  }
  return problems;
}

// Whether one of the modules is the entry, compared as paths.
export function entryListed(ids, entry, path = nodePath) {
  return ids.some((raw) => {
    const id = clean(raw);
    return !id.startsWith("\0") && path.isAbsolute(id) && path.relative(entry, id) === "";
  });
}

// The Vite plugin: fails the build with every problem at once, and when it
// checked nothing (no chunk, or the entry not among its modules), so that a
// change of the bundler's output does not leave it passing silently.
export function toolCodeGuard({ root, lock, entry } = {}) {
  const web = root ?? realpathSync(nodePath.resolve(fileURLToPath(new URL("..", import.meta.url))));
  const main = entry ?? nodePath.join(web, "src", "main.tsx");
  return {
    name: "doseph:tool-code-guard",
    apply: "build",
    generateBundle(_options, bundle) {
      const pkgs = lock ?? JSON.parse(readFileSync(nodePath.join(web, "package-lock.json"), "utf8"));
      const found = [];
      let entrySeen = false;
      for (const [file, out] of Object.entries(bundle)) {
        // A script that is not a chunk of this build (a web worker is built
        // apart) escapes the module check
        if (out.type !== "chunk") {
          if (/\.(?:m?js|cjs)$/.test(file)) found.push(`${file}: a script that is not a chunk of the app's build`);
          continue;
        }
        const ids = Object.keys(out.modules ?? {});
        if (entryListed(ids, main)) entrySeen = true;
        for (const p of shippedProblems(ids, web, pkgs)) found.push(`${file}: ${p}`);
      }
      if (!entrySeen) found.push(`nothing checked: no chunk lists ${nodePath.relative(web, main).split(nodePath.sep).join("/")} among its modules`);
      if (found.length) this.error(`Modules in the app that are neither our sources nor its runtime packages (PD-228, SD-075):\n- ${found.join("\n- ")}`);
    },
  };
}
