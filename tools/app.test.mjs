// SPDX-License-Identifier: AGPL-3.0-or-later
// The Home Assistant app (SD-034): its config.yaml installs the image that
// release.yml publishes for this version, asks for nothing but Home
// Assistant's API through the Supervisor, opens through Ingress and keeps
// its one port closed until a user opens it (SD-077), and matches the
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
  if (s === "null") return null;  // a quoted "null" stays text, which the Supervisor takes for no port
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

test("the app asks for Home Assistant's API through the Supervisor, Ingress and one closed port, nothing more", () => {
  const allowed = [
    "name", "version", "slug", "description", "url", "stage", "arch", "image", "startup", "boot",
    "homeassistant_api", "ingress", "ingress_port", "panel_icon", "panel_title", "ports", "ports_description", "watchdog",
  ];
  assert.deepEqual(Object.keys(config).filter((k) => !allowed.includes(k)), [], "a new key needs a review (SD-034)");
  assert.equal(config.homeassistant_api, true);
  assert.equal(config.stage, "experimental");
  // Opened through Ingress with Home Assistant's login, by its administrators
  // only (panel_admin left at its default); its port never mapped (SD-077)
  assert.equal(config.ingress, true);
  assert.equal(config.ingress_port, 8100);
  assert.ok(!("panel_admin" in config), "only administrators open it");
  assert.ok(!("ingress_stream" in config));
  // The direct port, closed unless a user opens it; no link to it
  assert.deepEqual(config.ports, { "8099/tcp": null });
  assert.match(read("ha/app/config.yaml"), /^ {2}8099\/tcp: null$/m, "null, not a quoted text");
  assert.ok(!("webui" in config), "no link to a port that is closed");
  // The Supervisor's watchdog reads /info through the Ingress port, which
  // answers it without a user; the direct port has no API without a password
  assert.equal(config.watchdog, "http://[HOST]:[PORT:8100]/api/v1/info");
});

test("the server's --app mode and the image match the app", () => {
  // tools/ha_server.test.mjs runs --app itself; here only the values the
  // app's config.yaml depends on.
  const main = read("ha/main.cpp");
  assert.match(main, /if \(data\.empty\(\)\) data = app \? "\/data\/hub"/);
  assert.match(main, /if \(port == 0\) port = app \? 8099 :/);
  assert.match(main, /if \(ingressPort == 0\) ingressPort = 8100;/, "the Ingress port of config.yaml");
  assert.match(main, /constexpr const char\* kSupervisor = "172\.30\.32\.2";/);
  assert.match(main, /app \? gc::json\{\{"url", "http:\/\/supervisor\/core"\}\}/);
  assert.match(main, /"SUPERVISOR_TOKEN"/);
  const docker = read("ha/app/Dockerfile");
  assert.match(docker, /^EXPOSE 8099 8100$/m);
  assert.match(docker, /^ENTRYPOINT \["\/gc_ha_server", "--app"\]$/m);
  assert.doesNotMatch(docker, /^CMD\b/m, "no further arguments to --app");
  for (const label of ['io.hass.version="\\$BUILD_VERSION"', 'io.hass.type="app"', 'io.hass.arch="\\$BUILD_ARCH"']) {
    assert.match(docker, new RegExp(label), label);
  }
  // Stages: the web app, the server without node, the license texts, and
  // the image with nothing but their results.
  const stages = [...docker.matchAll(/^FROM (\S+)(?: AS (\w+))?$/gm)].map((m) => [m[1], m[2]]);
  assert.deepEqual(stages.map(([, name]) => name), ["web", "server", "licenses", undefined]);
  const base = stages[1][0];
  assert.match(base, /^alpine:[\d.]+@sha256:[0-9a-f]{64}$/, "pinned by digest");
  assert.equal(stages[2][0], base, "licenses: the same base image");
  assert.equal(stages[3][0], "scratch");
  // The web stage's npm checks the packages' signatures (SD-058): the Node
  // CI runs (.nvmrc), on the same Alpine, pinned by digest (SD-071).
  const literal = (s) => s.replace(/[.]/g, "\\.");
  const pin = read(".nvmrc").trim();
  const alpine = base.match(/^alpine:([\d.]+)@/)[1];
  assert.match(stages[0][0], new RegExp(`^node:${literal(pin)}-alpine${literal(alpine)}@sha256:[0-9a-f]{64}$`), "web: the Node of .nvmrc on the same Alpine");
  const webStage = docker.slice(docker.indexOf(" AS web"), docker.indexOf(" AS server"));
  assert.doesNotMatch(webStage, /^RUN\b.*\bapk\b/m, "web: nothing from Alpine's packages; everything comes with the pinned image");
  assert.doesNotMatch(webStage, /\bcorepack\b|\bnpm (?:i|install|up|update)\b[^\n]*(?:\s-g\b|--global)|^(?:ADD|COPY --from)/m, "web: nothing replaces the image's Node or npm");
  // The digest, not the tag, decides what is pulled: before npm runs, the
  // stage checks that its Node is the one of .nvmrc.
  assert.match(read(".dockerignore"), /^!\.nvmrc$/m, ".nvmrc goes into the build");
  const check = webStage.search(/^RUN v="\$\(tr -d '\\r\\n' < \.nvmrc\)"; test "\$\(node -v\)" = "v\$v" \|\| \{ .*; exit 1; \}$/m);
  assert.ok(check > webStage.indexOf("COPY .nvmrc ./") && webStage.indexOf("COPY .nvmrc ./") > 0, "web: copies .nvmrc and checks node -v against it");
  assert.ok(check < webStage.indexOf("npm ci"), "web: the check comes before npm ci");
  // The Supervisor's stand-in in the smoke test is the server stage's
  // Alpine base image, so Dependabot's update of the Dockerfile shows up here.
  assert.match(read(".github/workflows/app.yml"), new RegExp(`^ {6}STANDIN: ${literal(base)}$`, "m"));
  const server = docker.slice(docker.indexOf(" AS server"), docker.indexOf(" AS licenses"));
  // The core tests in the smoke test are built as this stage builds the
  // server: the same packages, the same options but the tests (#112).
  const workflow = read(".github/workflows/app.yml");
  const step = workflow.slice(workflow.indexOf("name: Core tests with the image's toolchain"), workflow.indexOf("name: The image's server against"));
  const packages = server.match(/^RUN apk add --no-cache (.+)$/m)?.[1];
  assert.ok(packages, "the server stage installs its packages in one line");
  assert.ok(step.includes(`apk add --no-cache ${packages} `), `the core tests get the server's packages: ${packages}`);
  const configure = server.slice(server.indexOf("RUN cmake -S"), server.indexOf("cmake --build"));
  const options = configure.match(/"-D[^"]*"|-[DG] ?\S+/g).filter((o) => !o.startsWith("-DGC_BUILD_TESTS"));
  assert.ok(options.some((o) => o.includes("CMAKE_EXE_LINKER_FLAGS")), configure);
  for (const o of options) assert.ok(step.includes(o), `the core tests are configured with ${o}`);
  assert.doesNotMatch(server, /nodejs|npm|--from=web/, "the server stage gets no node and no web packages");
  assert.doesNotMatch(server, /^COPY (?:--\S+ )*(?:\.|web\/?|tools\/?)\s/m, "nor the whole tree, web/ or tools/");
  const image = docker.slice(docker.indexOf("FROM scratch"));
  assert.deepEqual([...image.matchAll(/^COPY (.*)$/gm)].map((m) => m[1]), [
    "--from=server /src/build/gc_ha_server /gc_ha_server",
    "--from=web /src/web/dist /web",
    "--from=licenses /out /licenses",
  ]);
});

test("the app's AppArmor profile allows only what the server needs, and the smoke test runs the image in it (#80)", () => {
  const text = read("ha/app/apparmor.txt");
  // The Supervisor takes the name from the one line that starts with
  // "profile" and renames it to the app's full slug (<hash>_growcontroller).
  assert.deepEqual([...text.matchAll(/^profile ([^ ]+).*$/gm)].map((m) => m[1]), [config.slug]);
  assert.match(text, /^#include <tunables\/global>\n+profile growcontroller flags=\(attach_disconnected,mediate_deleted\) \{$/m);
  const profile = text.slice(text.search(/^profile /m));
  const rules = profile.slice(profile.indexOf("{") + 1, profile.lastIndexOf("}")).split("\n").map((l) => l.trim()).filter(Boolean);
  // A new rule needs a review: it widens what a flaw in the web server can reach.
  assert.deepEqual(rules, [
    "#include <abstractions/base>",
    // As root only to give /data/hub away and to leave root (ha/main.cpp)
    "capability chown,",
    "capability dac_override,",
    // docker-init, still root, passes the Supervisor's SIGTERM on to 65532
    "capability kill,",
    "capability setgid,",
    "capability setuid,",
    // TCP over IPv4 (it listens on 0.0.0.0) to the Supervisor and from the
    // browsers; no UDP: the Supervisor's name is in /etc/hosts
    "network inet stream,",
    "signal (receive) peer=unconfined,",
    "signal (receive) peer=runc,",
    "signal (receive) peer=crun,",
    "signal (receive) peer=dockerd,",
    "signal (send,receive) peer=*_growcontroller,",
    // docker-init starts the server (the Supervisor's init: true)
    "/sbin/docker-init ix,",
    "/gc_ha_server ix,",
    "/etc/hosts r,",
    "/web/{,**} r,",
    "/data/hub/{,**} rw,",
  ]);
  const main = read("ha/main.cpp");
  assert.match(main, /if \(data\.empty\(\)\) data = app \? "\/data\/hub"/, "the folder the profile lets it write");
  assert.match(main, /constexpr uid_t kAppUid = 65532;/);
  assert.match(main, /constexpr gid_t kAppGid = 65532;/);
  // The smoke test loads it renamed as the Supervisor does, starts the image
  // as the Supervisor does (docker-init, the Supervisor's address in
  // /etc/hosts) and fails on anything the profile refused.
  const workflow = read(".github/workflows/app.yml");
  const smoke = workflow.slice(workflow.indexOf("name: Smoke test"), workflow.indexOf("name: Core tests with the image's toolchain"));
  assert.ok(smoke.includes(`sed 's/^profile growcontroller /profile 0000ci00_growcontroller /' ha/app/apparmor.txt > "$RUNNER_TEMP/apparmor.txt"`));
  assert.ok(smoke.includes(`sudo apparmor_parser -r -W "$RUNNER_TEMP/apparmor.txt"`));
  assert.match(smoke, /docker run -d --name "\$1" --init --security-opt apparmor=0000ci00_growcontroller .*--add-host "supervisor:\$supervisor"/);
  for (const run of ["start app gc-data", "start fresh gc-fresh", "confined app", "confined fresh"]) assert.ok(smoke.includes(run), run);
  assert.match(main, /if \(host\.empty\(\)\) host = app \? "0\.0\.0\.0"/, "IPv4, as the profile allows");
  assert.doesNotMatch(smoke, /--network-alias supervisor/, "the app finds the Supervisor in /etc/hosts, not by DNS");
  assert.ok(smoke.includes(`grep -F 'profile="0000ci00_growcontroller"' > denied.txt`));
  assert.ok(smoke.includes(`apparmor_parser -Q -K /apparmor.txt`), "the oldest parser reads it too");
  assert.match(workflow, /^ {6}DEBIAN: debian:12@sha256:[0-9a-f]{64}$/m);
  assert.match(workflow, /-e GC_HA_ROOT_TEST=1 "\$STANDIN"/, "the image's server runs the tests as root, never skipping them");
  for (const want of ["Uid: 65532 65532 65532 65532", "Gid: 65532 65532 65532 65532", "CapEff: 0000000000000000", "NoNewPrivs: 1"]) {
    assert.ok(smoke.includes(`'${want}'`), want);
  }
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
