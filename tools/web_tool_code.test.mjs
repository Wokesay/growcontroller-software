// SPDX-License-Identifier: AGPL-3.0-or-later
// web/scripts/tool-modules.mjs: the built app holds our sources and the
// packages the web SBOM lists, no code of a build tool (PD-228).
//   node --test tools/web_tool_code.test.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";
import { shippedProblems, toolCodeGuard } from "../web/scripts/tool-modules.mjs";

const root = "/w/web";
const lock = {
  packages: {
    "": { name: "growcontroller-web" },
    "node_modules/preact": { version: "11.0.0" },
    "node_modules/@preact/signals-core": { version: "1.14.4" },
    "node_modules/vite": { version: "8.3.2", dev: true },
    "node_modules/@babel/core": { version: "7.29.0", dev: true },
  },
};

test("our sources, the page and runtime packages pass", () => {
  assert.deepEqual(
    shippedProblems(
      [
        `${root}/index.html`,
        `${root}/src/main.tsx`,
        `${root}/src/styles.css?inline`,
        `${root}/node_modules/preact/dist/preact.mjs`,
        `${root}/node_modules/@preact/signals-core/dist/signals-core.module.js`,
      ],
      root,
      lock,
    ),
    [],
  );
});

test("Rolldown's and Vite's own helpers fail, named", () => {
  assert.deepEqual(shippedProblems(["\0rolldown/runtime.js", "\0vite/preload-helper.js", "vite/modulepreload-polyfill.js"], root, lock), [
    "\\0rolldown/runtime.js: Rolldown's runtime helpers",
    "\\0vite/preload-helper.js: Vite's preload helper for dynamic imports",
    "vite/modulepreload-polyfill.js: Vite's modulepreload polyfill",
  ]);
  assert.deepEqual(shippedProblems(["\0commonjsHelpers.js"], root, lock), ["\\0commonjsHelpers.js: code of a build tool"]);
});

test("a build-time package in the app fails: it is not in the web SBOM", () => {
  assert.deepEqual(
    shippedProblems([`${root}/node_modules/vite/dist/client/env.mjs`, `${root}/node_modules/@babel/core/lib/index.js`, `${root}/node_modules/left-pad/index.js`], root, lock),
    [
      "node_modules/vite/dist/client/env.mjs: vite is a build-time package, not in the web SBOM",
      "node_modules/@babel/core/lib/index.js: @babel/core is a build-time package, not in the web SBOM",
      "node_modules/left-pad/index.js: left-pad is not in web/package-lock.json",
    ],
  );
  // A package nested under another one is looked up by its own path
  const nested = { packages: { ...lock.packages, "node_modules/preact/node_modules/x": { version: "1.0.0", dev: true } } };
  assert.deepEqual(shippedProblems([`${root}/node_modules/preact/node_modules/x/i.js`], root, nested), [
    "node_modules/preact/node_modules/x/i.js: x is a build-time package, not in the web SBOM",
  ]);
});

test("files outside the sources and the packages fail", () => {
  assert.deepEqual(shippedProblems([`${root}/scripts/x.mjs`, "/elsewhere/y.js", `${root}/../tools/z.mjs`], root, lock), [
    "scripts/x.mjs: neither our sources nor a package",
    "/elsewhere/y.js: neither our sources nor a package",
    `${root}/../tools/z.mjs: neither our sources nor a package`,
  ]);
});

test("the plugin fails the build with every problem, and passes a clean bundle", () => {
  const guard = toolCodeGuard({ root, lock });
  const errors = [];
  const ctx = { error: (m) => errors.push(m) };
  const chunk = (ids) => ({ type: "chunk", modules: Object.fromEntries(ids.map((id) => [id, {}])) });
  guard.generateBundle.call(ctx, {}, { "a.js": chunk([`${root}/src/main.tsx`]), "s.css": { type: "asset" } });
  assert.deepEqual(errors, []);
  guard.generateBundle.call(ctx, {}, { "a.js": chunk([`${root}/src/main.tsx`, "\0rolldown/runtime.js"]), "b.js": chunk([`${root}/node_modules/vite/x.js`]) });
  assert.equal(errors.length, 1);
  assert.match(errors[0], /^Build tool code in the app \(PD-228\):\n- a\.js: \\0rolldown\/runtime\.js: Rolldown's runtime helpers\n- b\.js: node_modules\/vite\/x\.js: vite is a build-time package/);
  assert.equal(guard.apply, "build");
});

test("vite.config.ts runs the guard, and the build still runs the text check", () => {
  const web = join(import.meta.dirname, "..", "web");
  assert.match(readFileSync(join(web, "vite.config.ts"), "utf8"), /^ {2}plugins: \[preact\(\), toolCodeGuard\(\)\],$/m);
  assert.match(JSON.parse(readFileSync(join(web, "package.json"), "utf8")).scripts.build, /&& node scripts\/no-tool-code\.mjs$/);
});
