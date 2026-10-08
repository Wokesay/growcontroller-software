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

// Every key stands at the start of its own line as "key": – a key in any
// other form (single quotes, no quotes, a second key on the line) would be
// skipped and a collision missed, so it fails instead.
export function keys(text, file = "text") {
  const out = [];
  for (const line of text.split("\n")) {
    const m = line.match(/^\s*"([^"]+)"\s*:/);
    if (!m) {
      assert.doesNotMatch(line, /^\s*(?:'[^']*'|[A-Za-z_$][\w$.-]*)\s*:/, `${file}: key not written as "key": ${line.trim()}`);
      assert.doesNotMatch(line, /[{,]\s*"[^"]+"\s*:/, `${file}: one key per line: ${line.trim()}`);
      continue;
    }
    assert.doesNotMatch(line.slice(m[0].length), /^\s*"(?:[^"\\]|\\.)*"\s*,\s*"[^"]+"\s*:/, `${file}: one key per line: ${line.trim()}`);
    out.push(m[1]);
  }
  return out;
}

// German and English part of each file
function parts(file) {
  const text = readFileSync(join(dir, file), "utf8");
  if (file === "de.ts") return { de: keys(text, file), en: null };
  if (file === "en.ts") return { de: null, en: keys(text, file) };
  const m = text.match(/^export const en\b/m);
  assert.ok(m, `${file}: export const en`);
  return { de: keys(text.slice(0, m.index), file), en: keys(text.slice(m.index), file) };
}

test("the key reader refuses keys it cannot read", () => {
  assert.deepEqual(keys('  "a.b": "x",\n  "c":\n    "long value: with colon",\n  // note: y\n'), ["a.b", "c"]);
  assert.deepEqual(keys('  "a": "Bottle \\"{name}\\"",\n'), ["a"]);
  assert.throws(() => keys("  'a.b': \"x\",\n"));
  assert.throws(() => keys("  ab: \"x\",\n"));
  assert.throws(() => keys('  "a": "x", "b": "y",\n'));
  assert.throws(() => keys('export const de = { "a": "x" };\n'));
});

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
