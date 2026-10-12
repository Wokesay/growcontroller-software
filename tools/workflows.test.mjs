// SPDX-License-Identifier: AGPL-3.0-or-later
// Every action in the workflows is made by GitHub (SD-027) and pinned by
// its full commit SHA with the version as a comment (#29); Dependabot
// keeps both up to date. Node's types for the web app's tests are those of
// the Node that CI runs (#101). The web app's packages are checked against
// their registry signatures right after they are installed (#100, SD-058).
//   node --test tools/workflows.test.mjs
import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const dir = join(dirname(fileURLToPath(import.meta.url)), "..", ".github", "workflows");
const files = readdirSync(dir).filter((f) => /\.ya?ml$/.test(f));

test("actions are GitHub's own and pinned by commit SHA", () => {
  assert.ok(files.length >= 4);
  let n = 0;
  for (const f of files) {
    for (const line of readFileSync(join(dir, f), "utf8").split("\n")) {
      const m = line.match(/^\s*(?:-\s+)?uses:\s*(\S+)(.*)$/);
      if (!m) continue;
      n++;
      const [, ref, rest] = m;
      if (ref.startsWith("./")) continue; // reusable workflow of this repository
      assert.match(ref, /^actions\/[\w.-]+(\/[\w.-]+)*@[0-9a-f]{40}$/, `${f}: ${ref}`);
      assert.match(rest, /^\s+# v\d+\.\d+\.\d+\s*$/, `${f}: ${ref} needs a "# vX.Y.Z" comment`);
    }
  }
  assert.ok(n > 10, "uses: lines found");
});

test("the same action uses the same commit everywhere", () => {
  const seen = new Map();
  for (const f of files) {
    for (const m of readFileSync(join(dir, f), "utf8").matchAll(/uses:\s*(actions\/[\w.-]+)(?:\/[\w.-]+)*@([0-9a-f]{40})/g)) {
      const [, action, sha] = m;
      if (seen.has(action)) assert.equal(sha, seen.get(action), `${f}: ${action}`);
      else seen.set(action, sha);
    }
  }
});

function job(text, id) {
  const m = text.match(new RegExp(`^ {2}${id}:\\n((?: {4}.*\\n|\\s*\\n)*)`, "m"));
  assert.ok(m, `job ${id}`);
  return m[1];
}

test("every uses: is in a form the pin check reads", () => {
  // A flow mapping like "- { uses: x@main }" would slip past the check above.
  for (const f of files) {
    const text = readFileSync(join(dir, f), "utf8");
    const tokens = text.match(/\buses\b["']?\s*:/g) ?? [];
    const lines = text.split("\n").filter((l) => /^\s*(?:-\s+)?uses:\s*\S/.test(l));
    assert.equal(tokens.length, lines.length, f);
  }
});

test("only the publishing job of a release can write", () => {
  for (const f of files) {
    const text = readFileSync(join(dir, f), "utf8");
    assert.match(text, /^permissions:\n {2}contents: read\n/m, `${f}: read-only by default`);
    const writes = text.split("\n").filter((l) => /:\s*write\b/.test(l));
    if (f !== "release.yml") assert.deepEqual(writes, [], f);
  }
  const release = readFileSync(join(dir, "release.yml"), "utf8");
  const publish = job(release, "publish");
  const writesOutside = release.replace(publish, "").split("\n").filter((l) => /:\s*write\b/.test(l));
  assert.deepEqual(writesOutside, []);
  assert.match(publish, /if: github\.event_name == 'push' && startsWith\(github\.ref, 'refs\/tags\/v'\)/);
});

test("the publishing job builds nothing and the release build uses no cache", () => {
  const release = readFileSync(join(dir, "release.yml"), "utf8");
  const publish = job(release, "publish");
  assert.doesNotMatch(publish, /actions\/(checkout|setup-node|cache)@/);
  assert.doesNotMatch(publish, /\b(npm|npx|cmake|pip|pipx)\b|\bnode\s/);
  assert.doesNotMatch(release, /actions\/cache/);
  // Merged downloads would let one artifact overwrite another's files.
  assert.doesNotMatch(release, /merge-multiple/);
  for (const m of release.matchAll(/uses: actions\/setup-node@\S+.*\n((?: {8}.*\n)*)/g)) {
    assert.match(m[1], /package-manager-cache: false/);
    assert.doesNotMatch(m[1], /^\s*cache:/m);
  }
  assert.match(job(release, "packages"), /release: true/);
  const packages = readFileSync(join(dir, "packages.yml"), "utf8");
  assert.doesNotMatch(packages, /actions\/cache/);
  assert.match(packages, /cache: \$\{\{ !inputs\.release && 'npm' \|\| '' \}\}/);
});

test("the app image is built without write access and pushed to a real registry only by publish", () => {
  // SD-034: app.yml builds and smoke tests, release.yml's publish pushes.
  const app = readFileSync(join(dir, "app.yml"), "utf8");
  assert.doesNotMatch(app, /docker (login|push)|imagetools create|--push\b|type=registry|--cache-(from|to)|:\s*write\b/);
  assert.match(app, /docker build --pull /, "every build pulls its base image afresh");
  const release = readFileSync(join(dir, "release.yml"), "utf8");
  const publish = job(release, "publish");
  assert.doesNotMatch(publish, /docker (build|run)\b|buildx build/);
  assert.equal((release.match(/docker login/g) ?? []).length, (publish.match(/docker login/g) ?? []).length);
  for (const arch of ["amd64", "aarch64"]) {
    assert.match(job(release, `app-${arch}`), new RegExp(`uses: ./.github/workflows/app.yml\\n {4}with:\\n {6}arch: ${arch}\\n {6}release: true\\n`));
  }
});

test("the dry run pushes the app image with publish's own steps", () => {
  // check runs on every release run, publish only for a tag: the same
  // steps, so the first tag is not their first run.
  const release = readFileSync(join(dir, "release.yml"), "utf8");
  const step = (text, name) => {
    const m = text.match(new RegExp(`^ {6}- name: ${name}\\n((?: {8}.*\\n)*)`, "m"));
    assert.ok(m, name);
    return m[1].replace(/^ {8}id: image\n/m, "");
  };
  for (const name of ["Check the app images against their checksums and labels", "Push the app image"]) {
    assert.equal(step(job(release, "check"), name), step(job(release, "publish"), name), name);
  }
  assert.match(job(release, "check"), /IMAGE: localhost:5000\//);
  assert.match(job(release, "check"), /image: registry:2@sha256:[0-9a-f]{64}\n/);
  assert.match(job(release, "publish"), /IMAGE: ghcr\.io\//);
});

test("steps fail on a failing command in a pipe and get no secrets passed on", () => {
  for (const f of files) {
    const text = readFileSync(join(dir, f), "utf8");
    assert.doesNotMatch(text, /secrets:\s*inherit/, f);
  }
  for (const f of ["app.yml", "release.yml"]) {
    assert.match(readFileSync(join(dir, f), "utf8"), /^defaults:\n {2}run:\n(?: {4}#.*\n)* {4}shell: bash\n/m, f);
  }
});

test("tools and containers come pinned: reuse by hashes, images by digest", () => {
  // #29: nothing installed by name alone in the workflows.
  for (const f of files) {
    const text = readFileSync(join(dir, f), "utf8");
    assert.doesNotMatch(text, /\bpipx (install|run)\b|\buv (pip|tool) install\b/, `${f}: installs by name`);
    // also `"$RUNNER_TEMP/reuse/bin/pip" install`: a quote may close the path
    const installs = [...text.matchAll(/\bpip3?(?:\.\d+)?["']?\s+install\b[^\n]*/g)].map((m) => m[0]);
    for (const line of installs) assert.match(line, /--require-hashes .*--no-deps .*--only-binary :all:/, `${f}: ${line}`);
    for (const m of text.matchAll(/^\s+(?:container|image): (\S+)/gm)) assert.match(m[1], /@sha256:[0-9a-f]{64}$/, `${f}: ${m[1]}`);
  }
  // The two workflows that run reuse install it exactly this way.
  for (const f of ["ci.yml", "checks.yml"]) {
    const text = readFileSync(join(dir, f), "utf8");
    assert.match(text, /pip" install --require-hashes --no-deps --only-binary :all: -r tools\/requirements-reuse-build\.txt\n/, `${f}: the build backend`);
    assert.match(text, /pip" install --require-hashes --no-deps --no-build-isolation --only-binary :all: --no-binary reuse -r tools\/requirements-reuse\.txt\n/, `${f}: reuse`);
  }
  for (const file of ["requirements-reuse.txt", "requirements-reuse-build.txt"]) {
    const req = readFileSync(join(dir, "..", "..", "tools", file), "utf8");
    const pinned = [...req.matchAll(/^([a-z0-9._-]+)==/gim)].map((m) => m[1]);
    const hashed = [...req.matchAll(/^([a-z0-9._-]+)==\S+ \\\n {4}--hash=sha256:[0-9a-f]{64}/gim)].map((m) => m[1]);
    assert.ok(pinned.length > 0, file);
    assert.deepEqual(hashed, pinned, `${file}: every package has hashes`);
    assert.doesNotMatch(req, /^\s*(-e|--index-url|--extra-index-url|--find-links)\b|^\S+ @ /m, `${file}: no other source`);
  }
});

test("Node's types are those of the Node that CI runs (#101)", () => {
  // every setup-node names its version, and all name the same
  const versions = new Set();
  let setups = 0;
  let named = 0;
  for (const f of files) {
    const text = readFileSync(join(dir, f), "utf8");
    setups += text.match(/uses:[ \t]*actions\/setup-node@/g)?.length ?? 0;
    for (const m of text.matchAll(/node-version:[ \t]*(\S+)/g)) {
      named++;
      versions.add(m[1]);
    }
  }
  assert.ok(setups > 0, "setup-node found");
  assert.equal(named, setups, "every setup-node has a node-version");
  assert.equal(versions.size, 1, `one Node version in CI: ${[...versions].join(", ")}`);
  const web = JSON.parse(readFileSync(join(dir, "..", "..", "web", "package.json"), "utf8"));
  assert.equal(web.devDependencies["@types/node"]?.split(".")[0], [...versions][0], "@types/node in web/package.json");
  // Dependabot does not raise them to another major on its own.
  const dependabot = readFileSync(join(dir, "..", "dependabot.yml"), "utf8");
  const npm = dependabot.slice(dependabot.indexOf("package-ecosystem: npm"), dependabot.indexOf("- package-ecosystem:", dependabot.indexOf("package-ecosystem: npm")));
  assert.match(npm, /directory: \/web\n/);
  assert.match(npm, /- dependency-name: "@types\/node"\n[ \t]+update-types: \["version-update:semver-major"\]/);
});

test("the web app's packages are checked against their registry signatures (#100, SD-058)", () => {
  // Wherever they are installed, before any of them runs: each place
  // installs the builds for its own platform, and only installed ones are
  // checked.
  const root = join(dir, "..", "..");
  const places = [
    ...files.map((f) => join(".github", "workflows", f)),
    join("tools", "ci.sh"),
    join("ha", "app", "Dockerfile"),
  ];
  let installs = 0;
  for (const place of places) {
    for (const line of readFileSync(join(root, place), "utf8").split("\n")) {
      if (!/\bnpm ci\b/.test(line) || /^\s*#/.test(line)) continue;
      installs++;
      assert.doesNotMatch(line, /\|\||;|#/, `${place}: no way around the check: ${line}`);
      const steps = line.split("&&").map((x) => x.trim().replace(/\)$/, ""));
      const at = steps.findIndex((x) => /^npm ci\b/.test(x));
      assert.ok(at >= 0, `${place}: npm ci starts a step: ${line}`);
      assert.equal(steps[at + 1], "npm audit signatures", `${place}: right after npm ci: ${line}`);
    }
  }
  // tools/ci.sh, the firmware job, the packages, the release, the app image
  assert.ok(installs >= 5, `npm ci found ${installs} times`);
  assert.match(readFileSync(join(dir, "ci.yml"), "utf8"), /^ {10}tools\/ci\.sh$/m, "CI runs tools/ci.sh");
  // The check proves the registry's own record is signed; the lockfile must
  // point at that record, not at a tarball elsewhere.
  const lock = JSON.parse(readFileSync(join(root, "web", "package-lock.json"), "utf8"));
  const entries = Object.entries(lock.packages).filter(([path]) => path !== "");
  assert.ok(entries.length > 100, "packages in the lockfile");
  for (const [path, pkg] of entries) {
    const name = path.slice(path.lastIndexOf("node_modules/") + "node_modules/".length);
    const file = `${name.split("/").pop()}-${pkg.version}.tgz`;
    assert.equal(pkg.resolved, `https://registry.npmjs.org/${name}/-/${file}`, `${path} comes from the npm registry`);
    assert.match(pkg.integrity ?? "", /^sha512-/, `${path} is pinned by its sha512`);
  }
});
