// SPDX-License-Identifier: AGPL-3.0-or-later
// Text keys of the web app (#18): web/src/lang/de.ts and en.ts merge the
// base texts and one file per area. A key used in two files would silently
// overwrite the other; every file has the same keys in German and English.
//   node --test tools/i18n_keys.test.mjs
import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const dir = join(dirname(fileURLToPath(import.meta.url)), "..", "web", "src", "lang");
const keys = (text) => [...text.matchAll(/^\s*"([A-Za-z0-9_.-]+)":/gm)].map((m) => m[1]);

// German and English part of each file
function parts(file) {
  const text = readFileSync(join(dir, file), "utf8");
  if (file === "de.ts") return { de: keys(text), en: null };
  if (file === "en.ts") return { de: null, en: keys(text) };
  const i = text.indexOf("export const en");
  assert.ok(i > 0, `${file}: export const en`);
  return { de: keys(text.slice(0, i)), en: keys(text.slice(i)) };
}

const files = readdirSync(dir).filter((f) => f.endsWith(".ts"));

test("no text key is defined in two files", () => {
  const seen = new Map();
  for (const f of files) {
    const p = parts(f);
    for (const k of p.de ?? p.en) {
      const where = f === "en.ts" ? "de.ts" : f;
      if (seen.has(k) && seen.get(k) !== where) assert.fail(`${k}: in ${seen.get(k)} and ${where}`);
      seen.set(k, where);
    }
  }
});

test("every file has the same keys in German and English", () => {
  for (const f of files.filter((f) => f !== "de.ts" && f !== "en.ts")) {
    const { de, en } = parts(f);
    assert.deepEqual([...en].sort(), [...de].sort(), f);
    assert.equal(new Set(de).size, de.length, `${f}: duplicate key`);
  }
  const base = parts("de.ts").de, baseEn = parts("en.ts").en;
  assert.deepEqual([...baseEn].sort(), [...base].sort(), "de.ts and en.ts");
});
