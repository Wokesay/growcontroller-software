// SPDX-License-Identifier: AGPL-3.0-or-later
// Every action in the workflows is made by GitHub (SD-027) and pinned by
// its full commit SHA with the version as a comment (#29); Dependabot
// keeps both up to date.
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

test("the app image is built without write access and only pushed by publish", () => {
  // SD-034: app.yml builds and smoke tests, release.yml's publish pushes.
  const app = readFileSync(join(dir, "app.yml"), "utf8");
  assert.doesNotMatch(app, /docker (login|push)|imagetools create|:\s*write\b/);
  assert.match(app, /docker build --pull /, "every build pulls its base image afresh");
  const release = readFileSync(join(dir, "release.yml"), "utf8");
  const publish = job(release, "publish");
  assert.doesNotMatch(publish, /docker (build|run)\b|buildx build/);
  assert.equal((release.match(/docker push|docker login/g) ?? []).length, (publish.match(/docker push|docker login/g) ?? []).length);
  for (const arch of ["amd64", "aarch64"]) {
    assert.match(job(release, `app-${arch}`), new RegExp(`uses: ./.github/workflows/app.yml\\n {4}with:\\n {6}arch: ${arch}\\n {6}release: true\\n`));
  }
});
