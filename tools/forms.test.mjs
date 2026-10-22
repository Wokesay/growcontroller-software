// SPDX-License-Identifier: AGPL-3.0-or-later
// What testers use (SD-094): "Report a problem" in the web app fills in the
// bug form's text fields by their ids, and the link carries no labels, which
// GitHub answers with 404 for anyone without the right to set them (#305);
// every release's notes point to the testers' pages.
//   node --test tools/forms.test.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const read = (p) => readFileSync(join(root, p), "utf8");

test("the report link opens the bug form and fills only fields it has", () => {
  const page = read("web/src/pages/settings.tsx");
  const link = page.match(/new URLSearchParams\(\{([\s\S]*?)\}\)/)?.[1];
  assert.ok(link, "settings.tsx builds the link with URLSearchParams");
  const keys = [...link.matchAll(/^\s*(\w+):/gm)].map((m) => m[1]);
  assert.deepEqual(keys, ["template", "title", "what", "version", "report"]);
  assert.match(link, /template: "bug\.yml"/);
  const form = read(".github/ISSUE_TEMPLATE/bug.yml");
  for (const id of keys.filter((k) => k !== "template" && k !== "title")) {
    const field = form.match(new RegExp(`- type: (\\w+)\\n    id: ${id}\\n`));
    assert.ok(field, `bug.yml has a field "${id}"`);
    assert.ok(["input", "textarea"].includes(field[1]), `"${id}" is a text field, which a query can fill`);
  }
  assert.doesNotMatch(page, /issues\/new\?labels=/);
});

test("every release's notes send testers to the demo guide and the support page (SD-094)", () => {
  const release = read(".github/workflows/release.yml");
  const line = release.split("\n").find((l) => l.includes("**Testers: start here:**"));
  assert.ok(line, "release.yml adds the testers' line");
  assert.match(line, />> out\/NOTES\.md$/);
  // the pages come with #304
  for (const page of ["docs/DEMO.md", "SUPPORT.md"]) assert.ok(line.includes(`https://github.com/Wokesay/growcontroller-software/blob/main/${page}`), page);
  assert.ok(release.indexOf("**Testers: start here:**") < release.indexOf('echo "notes='), "before the notes' checksum");
});
