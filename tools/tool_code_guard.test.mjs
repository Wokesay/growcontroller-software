// SPDX-License-Identifier: AGPL-3.0-or-later
// web/scripts/tool-code-guard.mjs: the modules the bundler puts into the app
// are our sources and the runtime packages of web/package-lock.json, none
// of a build tool (PD-228, SD-073). The guard against the real bundler: tools/ci.sh runs
// web/scripts/tool-code-guard-selftest.mjs.
//   node --test tools/tool_code_guard.test.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join, win32 } from "node:path";
import { test } from "node:test";
import { entryListed, shippedProblems, toolCodeGuard } from "../web/scripts/tool-code-guard.mjs";

const root = "/w/web";
const lock = {
  packages: {
    "": { name: "growcontroller-web" },
    "node_modules/preact": { version: "11.0.0" },
    "node_modules/@preact/signals-core": { version: "1.14.4" },
    "node_modules/uplot": { version: "1.6.32" },
    "node_modules/vite": { version: "8.3.2", dev: true },
    "node_modules/@babel/core": { version: "7.29.0", dev: true },
  },
};
const advice = (p) => p.replace(/: (?:Rolldown|Oxc|Vite)[^:]*: .*$/, (m) => m.slice(0, m.indexOf(":", 2)));

test("our sources, the page and runtime packages pass, with or without a query", () => {
  assert.deepEqual(
    shippedProblems(
      [
        `${root}/index.html`,
        `${root}/index.html?html-proxy&index=0.js`,
        `${root}/src/main.tsx`,
        `${root}/src/styles.css?inline`,
        `${root}/node_modules/preact/dist/preact.mjs`,
        `${root}/node_modules/preact/jsx-runtime/dist/jsxRuntime.mjs#x`,
        `${root}/node_modules/@preact/signals-core/dist/signals-core.module.js`,
        `${root}/node_modules/uplot/dist/uPlot.min.css?used`,
      ],
      root,
      lock,
    ),
    [],
  );
});

test("Rolldown's, Oxc's and Vite's own helpers fail, named, with what to do", () => {
  const p = shippedProblems(
    ["\0rolldown/runtime.js", "rolldown:runtime", "\0@oxc-project+runtime@0.99.0/helpers/esm/defineProperty.js", "\0vite/preload-helper.js", "\0vite/modulepreload-polyfill.js"],
    root,
    lock,
  );
  assert.deepEqual(p.map(advice), [
    "\\0rolldown/runtime.js: Rolldown's runtime helpers",
    "rolldown:runtime: Rolldown's runtime helpers",
    "\\0@oxc-project+runtime@0.99.0/helpers/esm/defineProperty.js: Oxc's syntax-lowering helpers",
    "\\0vite/preload-helper.js: Vite's preload helper for dynamic imports",
    "\\0vite/modulepreload-polyfill.js: Vite's modulepreload polyfill",
  ]);
  assert.match(p[0], /if they cannot be avoided, Rolldown is listed as shipped \(SD-073\)$/);
  assert.deepEqual(shippedProblems(["\0commonjsHelpers.js"], root, lock), [
    "\\0commonjsHelpers.js: code of a build tool: find what brings it in and keep it out of the app",
  ]);
});

test("a build-time or unknown package in the app fails", () => {
  assert.deepEqual(
    shippedProblems([`${root}/node_modules/vite/dist/client/env.mjs`, `${root}/node_modules/@babel/core/lib/index.js`, `${root}/node_modules/left-pad/index.js`, `${root}/node_modules`], root, lock),
    [
      "node_modules/vite/dist/client/env.mjs: vite is a build-time package: keep it out of the app, or make it a runtime dependency",
      "node_modules/@babel/core/lib/index.js: @babel/core is a build-time package: keep it out of the app, or make it a runtime dependency",
      "node_modules/left-pad/index.js: left-pad is not in web/package-lock.json",
      "node_modules: ? is not in web/package-lock.json",
    ],
  );
});

test("a package installed inside another is looked up by its own path; a folder named node_modules inside a package is not one", () => {
  const nested = {
    packages: {
      ...lock.packages,
      "node_modules/preact/node_modules/x": { version: "1.0.0", dev: true },
      "node_modules/@preact/signals-core/node_modules/@a/b": { version: "1.0.0", dev: true },
    },
  };
  assert.deepEqual(
    shippedProblems(
      [
        `${root}/node_modules/preact/node_modules/x/i.js`,
        `${root}/node_modules/@preact/signals-core/node_modules/@a/b/i.js`,
        `${root}/node_modules/uplot/dist/node_modules/y/i.js`,
      ],
      root,
      nested,
    ).map((l) => l.slice(0, l.indexOf(" is "))),
    ["node_modules/preact/node_modules/x/i.js: x", "node_modules/@preact/signals-core/node_modules/@a/b/i.js: @a/b"],
  );
});

test("a web worker fails, also inlined: it is built apart, so the guard cannot see its modules", () => {
  assert.deepEqual(
    shippedProblems([`${root}/src/w.ts?worker&inline`, `${root}/src/w.ts?worker`, `${root}/src/s.ts?sharedworker`, `${root}/src/w.ts?worker_file&type=module`], root, lock).map((p) => p.slice(p.indexOf(": ") + 2)),
    Array(4).fill("a web worker, built apart: the guard cannot see its modules"),
  );
  assert.deepEqual(shippedProblems([`${root}/src/networker.ts`, `${root}/src/x.ts?url`], root, lock), []);
});

test("files outside the sources and the packages fail", () => {
  assert.deepEqual(shippedProblems([`${root}/scripts/x.mjs`, "/elsewhere/y.js", `${root}/../tools/z.mjs`, root], root, lock), [
    "scripts/x.mjs: neither our sources nor a package",
    "/elsewhere/y.js: neither our sources nor a package",
    `${root}/../tools/z.mjs: neither our sources nor a package`,
    `${root}: neither our sources nor a package`,
  ]);
});

test("the plugin fails the build with every problem, when it checked nothing, and on a script outside its chunks", () => {
  const guard = toolCodeGuard({ root, lock });
  assert.equal(guard.apply, "build");
  const errors = [];
  const ctx = { error: (m) => errors.push(m) };
  const chunk = (ids) => ({ type: "chunk", modules: Object.fromEntries(ids.map((id) => [id, {}])) });
  const main = `${root}/src/main.tsx`;
  guard.generateBundle.call(ctx, {}, { "a.js": chunk([main]), "s.css": { type: "asset" } });
  assert.deepEqual(errors, []);
  guard.generateBundle.call(ctx, {}, { "a.js": chunk([main, "\0rolldown/runtime.js"]), "b.js": chunk([`${root}/node_modules/vite/x.js`]) });
  assert.equal(errors.length, 1);
  assert.match(errors[0], /^Modules in the app that are neither our sources nor its runtime packages \(PD-228, SD-073\):\n- a\.js: \\0rolldown\/runtime\.js: Rolldown's runtime helpers.*\n- b\.js: node_modules\/vite\/x\.js: vite is a build-time package/);
  // Nothing to check is no pass: no chunk, an empty modules field, or a
  // bundle without the entry
  for (const bundle of [{}, { "a.js": { type: "chunk" } }, { "a.js": chunk([`${root}/src/other.ts`]) }]) {
    errors.length = 0;
    guard.generateBundle.call(ctx, {}, bundle);
    assert.match(errors[0] ?? "", /nothing checked: no chunk lists src\/main\.tsx among its modules/, JSON.stringify(bundle));
  }
  errors.length = 0;
  guard.generateBundle.call(ctx, {}, { "a.js": chunk([main]), "assets/worker-x.js": { type: "asset" } });
  assert.match(errors[0] ?? "", /assets\/worker-x\.js: a script that is not a chunk of the app's build/);
});

test("on Windows, Vite's ids with forward slashes match the web folder and the entry", () => {
  // Vite names modules D:/a/web/src/main.tsx; Node's paths there use
  // backslashes (CI's Windows build failed on this)
  const web = "D:\\a\\web";
  const ids = ["D:/a/web/src/main.tsx", "D:/a/web/node_modules/preact/dist/preact.mjs", "d:/a/web/index.html?html-proxy", "\0rolldown/runtime.js"];
  assert.deepEqual(shippedProblems(ids, web, lock, win32).map((p) => p.slice(0, p.indexOf(":", 3))), ["\\0rolldown/runtime.js"]);
  assert.equal(entryListed(ids, win32.join(web, "src", "main.tsx"), win32), true);
  assert.equal(entryListed(["D:/a/web/src/other.ts"], win32.join(web, "src", "main.tsx"), win32), false);
  assert.deepEqual(shippedProblems(["E:/x/y.js"], web, lock, win32), ["E:/x/y.js: neither our sources nor a package"]);
});

test("vite.config.ts runs the guard, the build still runs the text check, and tools/ci.sh the self-test", () => {
  const top = join(import.meta.dirname, "..");
  assert.match(readFileSync(join(top, "web", "vite.config.ts"), "utf8"), /^ {2}plugins: \[preact\(\), toolCodeGuard\(\)\],$/m);
  assert.match(JSON.parse(readFileSync(join(top, "web", "package.json"), "utf8")).scripts.build, /&& node scripts\/no-tool-code\.mjs$/);
  assert.match(readFileSync(join(top, "tools", "ci.sh"), "utf8"), /\n\(cd web && node scripts\/tool-code-guard-selftest\.mjs\)\n/);
});
