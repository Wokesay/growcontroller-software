// SPDX-License-Identifier: AGPL-3.0-or-later
// Every place that names the release names the one in VERSION: release.yml's
// verify checks VERSION, web/package.json and the app's config.yaml at the
// tag; this catches the rest before (docs/RELEASE.md, step 1).
//   node --test tools/version.test.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const read = (f) => readFileSync(join(root, f), "utf8");
const version = read("VERSION").trim();

test("VERSION is one release in the prototype scheme", () => {
  assert.equal(read("VERSION"), `${version}\n`);
  assert.match(version, /^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/);
});

test("the web app, its lock file, the app, the README and the bug form name VERSION", () => {
  assert.equal(JSON.parse(read("web/package.json")).version, version, "web/package.json");
  const lock = JSON.parse(read("web/package-lock.json"));
  assert.equal(lock.version, version, "package-lock.json");
  assert.equal(lock.packages[""].version, version, 'package-lock.json packages[""]');
  assert.ok(read("ha/app/config.yaml").split("\n").includes(`version: "${version}"`), "ha/app/config.yaml");
  assert.ok(read("README.md").includes(`**Status: prototype \`${version}\`.**`), "README status line");
  assert.match(read(".github/ISSUE_TEMPLATE/bug.yml"), new RegExp(`placeholder: ${version.replace(/[.]/g, "\\.")}\\n`), "bug form");
});

test("the README says a pre-release is for testing only, and a Stable release does not (SD-096)", () => {
  const notice = "> **For testing only:** this version does not meet the requirements of\n> the EU Cyber Resilience Act (CRA).";
  if (version.includes("-")) assert.ok(read("README.md").includes(notice), "README: the testing notice");
  else assert.ok(!read("README.md").includes("For testing only"), "README: no testing notice at a Stable release");
});

test("the changelog has the release's section, or it is still unreleased", () => {
  const log = read("CHANGELOG.md");
  assert.match(log, /^## \[Unreleased\]$/m, "an [Unreleased] section stays on top");
  const released = log.split("\n").filter((l) => l.startsWith("## [") && !l.startsWith("## [Unreleased]"));
  // Each release once; the newest first. VERSION is either the newest release
  // (being cut) or newer than it (in work); never an older one.
  const names = released.map((l) => l.match(/^## \[([^\]]+)\]/)[1]);
  assert.equal(new Set(names).size, names.length, "each release once");
  if (names.includes(version)) assert.equal(names[0], version, "VERSION is the newest release");
  for (const l of released) assert.match(l, /^## \[[^\]]+\] – \d{4}-\d{2}-\d{2}$/, l);
});

// The Security section of the release in work (SD-079, SD-097): every block
// has one of the known headings; a vulnerability has a CVSS and an advisory;
// no other block has an advisory; a safety or robustness fix that no
// attacker can trigger has no CVSS. It checks VERSION's section once that
// has one while [Unreleased] has none (the release pull request), else
// [Unreleased].
test("the Security section's blocks follow SD-079 and SD-097", () => {
  const log = read("CHANGELOG.md");
  assert.doesNotMatch(log, /Safety and robustness fixes, no vulnerability/, "the old label (SD-097)");
  const sectionOf = (heading) => {
    const start = log.indexOf(heading);
    if (start < 0) return "";
    const next = log.indexOf("\n## [", start + 1);
    return log.slice(start, next < 0 ? undefined : next);
  };
  // [Unreleased] while it has a Security section, else the release named in VERSION
  let section = sectionOf("\n## [Unreleased]");
  if (!section.includes("\n### Security\n")) section = sectionOf(`\n## [${version}] – `);
  if (!section.includes("\n### Security\n")) return;  // a release with no security item
  const sec = section.slice(section.indexOf("\n### Security\n"));
  const vuln = "Vulnerabilities, each with an advisory:";
  const safety = "Safety and robustness fixes, not exploitable by an attacker (CRA Art. 3(41)); disclosed under Annex I, Part II (4):";
  const known = [vuln, safety, "A protective cut-off added:", "A protective cut-off relaxed on purpose:", "Hardening, without a vulnerability behind it:"];
  const blocks = sec.split(/\n(?=[A-Z][^\n]*:\n\n- )/).slice(1);
  assert.ok(blocks.length >= 2, "the Security section has its blocks");
  const items = (b) => b.split(/\n(?=- \*\*)/).slice(1);
  for (const b of blocks) {
    const head = b.split("\n")[0];
    assert.ok(known.includes(head), `a known block heading: ${head}`);
    for (const i of items(b)) {
      const name = i.split("\n")[0];
      if (head === vuln) {
        assert.match(i, /\n  - Advisory: a GHSA/, `an advisory for ${name}`);
        assert.match(i, /\n  - Severity: \w+, CVSS 3\.1 /, `a CVSS for ${name}`);
      } else assert.doesNotMatch(i, /\n  - Advisory:/, `no advisory outside the vulnerabilities: ${name}`);
      if (head === safety) assert.match(i, /\n  - Severity: \w+; no CVSS, as it is not exploitable by an attacker\./, name);
    }
  }
});
