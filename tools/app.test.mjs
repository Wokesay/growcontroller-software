// SPDX-License-Identifier: AGPL-3.0-or-later
// The Home Assistant app (SD-034): its config.yaml installs the image that
// release.yml publishes for this version, asks for nothing but Home
// Assistant's API through the Supervisor and one port, and matches the
// server's --app mode and the Dockerfile.
//   node --test tools/app.test.mjs
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const read = (f) => readFileSync(join(root, f), "utf8");

// The flat YAML of an app: "key: value", lists "  - item", maps "  k: v".
// Anything else fails, so a construct this reader does not know cannot
// slip past the checks below.
export function parseFlatYaml(text) {
  const out = {};
  let key = null;
  for (const raw of text.split("\n")) {
    const line = raw.replace(/\s+$/, "");
    if (line === "" || line.startsWith("#")) continue;
    let m;
    if ((m = line.match(/^([a-z_]+):(?: (.+))?$/))) {
      key = m[1];
      assert.ok(!(key in out), `${key} twice`);
      out[key] = m[2] === undefined ? null : scalar(m[2]);
    } else if ((m = line.match(/^ {2}- (.+)$/)) && key && (out[key] === null || Array.isArray(out[key]))) {
      (out[key] ??= []).push(scalar(m[1]));
    } else if ((m = line.match(/^ {2}([^\s:]+): (.+)$/)) && key && (out[key] === null || isMap(out[key]))) {
      (out[key] ??= {})[m[1]] = scalar(m[2]);
    } else {
      assert.fail(`line not understood: ${raw}`);
    }
  }
  return out;
}
const isMap = (v) => v !== null && typeof v === "object" && !Array.isArray(v);
function scalar(s) {
  if (/^"[^"\\]*"$/.test(s)) return s.slice(1, -1);
  if (s === "true" || s === "false") return s === "true";
  if (/^\d+$/.test(s)) return Number(s);
  assert.doesNotMatch(s, /^["'[{&*!|>]|\s#/, `quote or simplify: ${s}`);
  return s;
}

const config = parseFlatYaml(read("ha/app/config.yaml"));
const version = read("VERSION").trim();
const IMAGE = "ghcr.io/wokesay/growcontroller-ha";

test("the reader of the app's YAML refuses what it does not know", () => {
  assert.deepEqual(parseFlatYaml('a: "1.0"\nb:\n  - x\nc:\n  8099/tcp: 8099\nd: true\n'), {
    a: "1.0",
    b: ["x"],
    c: { "8099/tcp": 8099 },
    d: true,
  });
  assert.throws(() => parseFlatYaml("a: 1\na: 2\n"));
  assert.throws(() => parseFlatYaml("a:\n    - deeper\n"));
  assert.throws(() => parseFlatYaml("a: [x, y]\n"));
  assert.throws(() => parseFlatYaml("a: x # comment\n"));
  assert.throws(() => parseFlatYaml("a:\n  - x\n  k: v\n"));
});

test("the app installs the image of this release", () => {
  assert.equal(config.version, version, "config.yaml version = VERSION");
  // As release.yml's verify reads it: quoted, on a line of its own
  assert.ok(read("ha/app/config.yaml").split("\n").includes(`version: "${version}"`));
  assert.equal(config.image, IMAGE);
  const release = read(".github/workflows/release.yml");
  assert.match(release, new RegExp(`^ {6}IMAGE: ${IMAGE.replace(/[.]/g, "\\.")}$`, "m"), "release.yml pushes the same image");
  assert.deepEqual(config.arch, ["aarch64", "amd64"]);
  for (const arch of config.arch) {
    assert.match(read(".github/workflows/release.yml"), new RegExp(`arch: ${arch}\\n`), `release.yml builds ${arch}`);
    assert.match(read(".github/workflows/ci.yml"), new RegExp(`arch: ${arch}\\n`), `ci.yml builds ${arch}`);
  }
});

test("the app asks for Home Assistant's API through the Supervisor and one port, nothing more", () => {
  const allowed = [
    "name", "version", "slug", "description", "url", "stage", "arch", "image", "startup", "boot",
    "homeassistant_api", "ports", "ports_description", "webui", "watchdog",
  ];
  assert.deepEqual(Object.keys(config).filter((k) => !allowed.includes(k)), [], "a new key needs a review (SD-034)");
  assert.equal(config.homeassistant_api, true);
  assert.equal(config.stage, "experimental");
  assert.deepEqual(config.ports, { "8099/tcp": 8099 });
  assert.equal(config.webui, "http://[HOST]:[PORT:8099]/");
  assert.equal(config.watchdog, "http://[HOST]:[PORT:8099]/api/v1/info");
});

test("the server's --app mode and the image match the app", () => {
  // tools/ha_server.test.mjs runs --app itself; here only the values the
  // app's config.yaml depends on.
  const main = read("ha/main.cpp");
  assert.match(main, /if \(data\.empty\(\)\) data = app \? "\/data\/hub"/);
  assert.match(main, /if \(port == 0\) port = app \? 8099 :/);
  assert.match(main, /app \? gc::json\{\{"url", "http:\/\/supervisor\/core"\}\}/);
  assert.match(main, /"SUPERVISOR_TOKEN"/);
  const docker = read("ha/app/Dockerfile");
  assert.match(docker, /^EXPOSE 8099$/m);
  assert.match(docker, /^ENTRYPOINT \["\/gc_ha_server", "--app"\]$/m);
  assert.doesNotMatch(docker, /^CMD\b/m, "no further arguments to --app");
  for (const label of ['io.hass.version="\\$BUILD_VERSION"', 'io.hass.type="app"', 'io.hass.arch="\\$BUILD_ARCH"']) {
    assert.match(docker, new RegExp(label), label);
  }
  // Stages: the web app, the server without node, the license texts, and
  // the image with nothing but their results.
  const stages = [...docker.matchAll(/^FROM (\S+)(?: AS (\w+))?$/gm)].map((m) => [m[1], m[2]]);
  assert.deepEqual(stages.map(([, name]) => name), ["web", "server", "licenses", undefined]);
  for (const [from, name] of stages.slice(0, 3)) assert.match(from, /^alpine:[\d.]+@sha256:[0-9a-f]{64}$/, `${name}: pinned by digest`);
  assert.equal(stages[3][0], "scratch");
  const server = docker.slice(docker.indexOf(" AS server"), docker.indexOf(" AS licenses"));
  assert.doesNotMatch(server, /nodejs|npm|COPY (\.|web|tools)\b|--from=web/, "the server stage gets no node and no web packages");
  const image = docker.slice(docker.indexOf("FROM scratch"));
  assert.deepEqual([...image.matchAll(/^COPY (.*)$/gm)].map((m) => m[1]), [
    "--from=server /src/build/gc_ha_server /gc_ha_server",
    "--from=web /src/web/dist /web",
    "--from=licenses /out /licenses",
  ]);
});

test("Home Assistant finds exactly this one app in the repository", () => {
  // The Supervisor looks for config.yaml/json files in its clone,
  // skipping folders that start with a dot.
  const files = execFileSync("git", ["ls-files", "-z"], { cwd: root, encoding: "utf8" }).split("\0");
  const found = files.filter((f) => /(^|\/)config\.(ya?ml|json)$/.test(f) && !f.split("/").some((part) => part.startsWith(".")));
  assert.deepEqual(found, ["ha/app/config.yaml"]);
});

test("the repository is a Home Assistant app repository", () => {
  const repo = parseFlatYaml(read("repository.yaml"));
  assert.equal(repo.url, "https://github.com/Wokesay/growcontroller-software");
  assert.deepEqual(Object.keys(repo).sort(), ["maintainer", "name", "url"]);
});
