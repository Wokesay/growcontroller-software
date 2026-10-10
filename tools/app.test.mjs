// SPDX-License-Identifier: AGPL-3.0-or-later
// The Home Assistant app (SD-034): its config.yaml installs the image that
// release.yml publishes for this version, asks for no more than read
// access to Home Assistant and one port, and matches the server's --app
// mode and the Dockerfile.
//   node --test tools/app.test.mjs
import assert from "node:assert/strict";
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
  assert.equal(config.image, IMAGE);
  const release = read(".github/workflows/release.yml");
  assert.match(release, new RegExp(`^ {6}IMAGE: ${IMAGE.replace(/[.]/g, "\\.")}$`, "m"), "release.yml pushes the same image");
  assert.deepEqual(config.arch, ["aarch64", "amd64"]);
  for (const arch of config.arch) {
    assert.match(read(".github/workflows/release.yml"), new RegExp(`arch: ${arch}\\n`), `release.yml builds ${arch}`);
    assert.match(read(".github/workflows/ci.yml"), new RegExp(`arch: ${arch}\\n`), `ci.yml builds ${arch}`);
  }
});

test("the app asks for read access to Home Assistant and one port, nothing more", () => {
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
  const main = read("ha/main.cpp");
  assert.match(main, /if \(app\) data = "\/data\/hub", host = "0\.0\.0\.0", web = "\/web", port = 8099;/);
  assert.match(main, /app \? gc::json\{\{"url", "http:\/\/supervisor\/core"\}\}/);
  assert.match(main, /"SUPERVISOR_TOKEN"/);
  const docker = read("ha/app/Dockerfile");
  assert.match(docker, /^EXPOSE 8099$/m);
  assert.match(docker, /^ENTRYPOINT \["\/gc_ha_server", "--app"\]$/m);
  assert.match(docker, /^FROM scratch$/m, "nothing but the server, the web app and the licenses");
  for (const label of ['io.hass.version="\\$BUILD_VERSION"', 'io.hass.type="app"', 'io.hass.arch="\\$BUILD_ARCH"']) {
    assert.match(docker, new RegExp(label), label);
  }
  assert.match(docker, /^FROM alpine:[\d.]+@sha256:[0-9a-f]{64} AS build$/m, "base image pinned by digest");
});

test("the repository is a Home Assistant app repository", () => {
  const repo = parseFlatYaml(read("repository.yaml"));
  assert.equal(repo.url, "https://github.com/Wokesay/growcontroller-software");
  assert.deepEqual(Object.keys(repo).sort(), ["maintainer", "name", "url"]);
});
