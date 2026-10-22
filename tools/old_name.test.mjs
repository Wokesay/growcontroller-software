// SPDX-License-Identifier: AGPL-3.0-or-later
// The project is called Doseph (#312, SD-098). The old name growcontroller
// stays only in the history (docs/DECISIONS.md, the CHANGELOG's released
// sections) and where a line speaks of the time before the name, each kind
// only in the files that need it: the product repository, which keeps its
// name; the old address, image and folders an install from before still
// has; the old default name the core reads as the new one; and the move of
// the Home Assistant app.
//   node --test tools/old_name.test.mjs
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));

const MOVE_DOCS = [
  ".github/workflows/release.yml",
  "CHANGELOG.md",
  "SECURITY.md",
  "docs/HOME_ASSISTANT.md",
  "docs/RELEASE.md",
  "docs/TESTING.md",
  "ha/app/DOCS.md",
  "ha/app/README.md",
  "ha/app/config.yaml",
  "tests/core/test_ha.cpp",
];
const PAST = [
  { why: "the product repository", files: ["CLAUDE.md", "docs/WORKFLOW.md"], text: /Wokesay\/growcontroller(?![\w-])|product repository `growcontroller`/ },
  { why: "the old address", files: ["docs/HOME_ASSISTANT.md", "docs/RELEASE.md", "docs/WORKFLOW.md"], text: /Wokesay\/growcontroller-software\b/ },
  { why: "the old app image", files: ["docs/HOME_ASSISTANT.md", "docs/RELEASE.md"], text: /growcontroller-ha\b/ },
  { why: "the simulator's old data folder", files: ["CHANGELOG.md", "sim/README.txt", "sim/main.cpp", "tools/package.sh"], text: /growcontroller-daten/ },
  { why: "the old default name", files: ["core/src/config.cpp", "tests/core/test_api.cpp"], text: /"[Gg]rowcontroller( 2)?"/ },
  { why: "the old file names", files: ["CHANGELOG.md"], text: /growcontroller-(konfiguration|diagnose-)/ },
  {
    why: "the move",
    files: MOVE_DOCS,
    text: /\b(called|was|[Ff]rom|old|old name|Used|uninstall) growcontroller\b|growcontroller is now Doseph|\bgrowcontroller (app|Home Assistant app)\b|Apps → growcontroller →|#coming-from-growcontroller\b/,
  },
];

/** Lines naming the old name that are neither history nor about the past where they stand. */
export function strayLines(hits) {
  return hits.filter(({ file, line, text, releasedFrom }) => {
    if (file === "docs/DECISIONS.md" || file === "tools/old_name.test.mjs") return false;
    if (file === "CHANGELOG.md" && releasedFrom !== undefined && line >= releasedFrom) return false;
    return !PAST.some((p) => p.files.includes(file) && p.text.test(text));
  });
}

const grep = (args) =>
  execFileSync("git", ["grep", "--no-color", "--no-column", "-n", "-I", "-z", ...args], { cwd: root, encoding: "utf8" });

function hits() {
  let out = "";
  try {
    out = grep(["-i", "growcontroller"]);
  } catch (e) {
    if (e.status === 1) return []; // no hit at all
    throw e;
  }
  const releasedFrom = Number(grep(["-e", "^## \\[[0-9]", "--", "CHANGELOG.md"]).split("\0")[1]);
  return out
    .split("\n")
    .filter(Boolean)
    .map((row) => {
      const [file, line, text] = row.split("\0");
      return { file, line: Number(line), text, releasedFrom };
    });
}

test("the reader of the hits tells history and the past from the rest", () => {
  const at = (file, line, text) => ({ file, line, text, releasedFrom: 100 });
  const stray = strayLines([
    at("docs/DECISIONS.md", 1, "growcontroller runs"),
    at("tools/old_name.test.mjs", 2, "growcontroller"),
    at("CHANGELOG.md", 120, "growcontroller as an app"),
    at("CLAUDE.md", 3, "the product repository `Wokesay/growcontroller`"),
    at("docs/RELEASE.md", 4, "ghcr.io/wokesay/growcontroller-ha:X.Y.Z"),
    at("sim/README.txt", 5, 'the folder "growcontroller-daten"'),
    at("core/src/config.cpp", 6, 'if (c.system.name == "growcontroller")'),
    at("ha/app/DOCS.md", 7, "Up to 0.1.0-proto.4 this app was called growcontroller."),
    at("CHANGELOG.md", 8, "- **growcontroller is now Doseph**, next to the old growcontroller app"),
    // Leftovers it must find
    at("CHANGELOG.md", 50, "growcontroller as an app"),
    at("README.md", 9, "# growcontroller"),
    at("web/src/app.tsx", 10, '{config.value.system.name !== "growcontroller" ? config.value.system.name : t("app.tagline")}'),
    at("web/src/source.ts", 11, 'export const SOURCE_REPO = "https://github.com/Wokesay/growcontroller-software";'),
    at("ha/main.cpp", 12, '"and report this: https://github.com/Wokesay/growcontroller-software/issues\\n";'),
    at("ha/app/config.yaml", 13, "image: ghcr.io/wokesay/growcontroller-ha"),
    at(".github/workflows/release.yml", 14, "          pattern: growcontroller-ha-*"),
    at("web/src/pages/settings.tsx", 15, "a.download = `growcontroller-diagnose-${diag.reportId}.json`;"),
    at("web/src/lang/setup.ts", 16, '"welcome.basics.h": "Welcome from growcontroller",'),
    at("docs/RELEASE.md", 17, "ghcr.io/wokesay/growcontroller-hacks"),
    at("CLAUDE.md", 18, "https://github.com/Wokesay/growcontroller-software/issues"),
  ]);
  assert.deepEqual(
    stray.map((h) => `${h.file}:${h.line}`),
    [
      "CHANGELOG.md:50",
      "README.md:9",
      "web/src/app.tsx:10",
      "web/src/source.ts:11",
      "ha/main.cpp:12",
      "ha/app/config.yaml:13",
      ".github/workflows/release.yml:14",
      "web/src/pages/settings.tsx:15",
      "web/src/lang/setup.ts:16",
      "docs/RELEASE.md:17",
      "CLAUDE.md:18",
    ],
  );
});

test("the old name appears only in the history and where a line speaks of the past (#312)", () => {
  const found = hits();
  assert.ok(found.length > 0, "the reader found none of the places that keep the old name on purpose");
  assert.ok(found[0].releasedFrom > 0, "CHANGELOG.md has no released section");
  const stray = strayLines(found);
  assert.deepEqual(
    stray.map((h) => `${h.file}:${h.line}: ${h.text.trim()}`),
    [],
    "Say Doseph there; if the line speaks of the time before the name, add it to PAST in tools/old_name.test.mjs for that file",
  );
});
