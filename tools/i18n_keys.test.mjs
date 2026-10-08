// SPDX-License-Identifier: AGPL-3.0-or-later
// Text keys of the web app (#18): web/src/lang/de.ts and en.ts merge the
// base texts and one file per area. A key used in two files would silently
// overwrite the other; every file has the same keys in German and English.
// The hub's messages (SD-032): core/src/messages.cpp (English) and
// web/src/lang/msg.ts (German) have the same keys and placeholders, and
// every say("…") in the core names a key of the table.
//   node --test tools/i18n_keys.test.mjs
import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const dir = join(root, "web", "src", "lang");
const coreSrc = join(root, "core", "src");

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

// Placeholders per key, e.g. "Pair {p}" → "p" and "{ph:2}" → "ph:2"; a
// value may span lines.
export function placeholders(text) {
  const out = new Map();
  let key = null;
  for (const line of text.split("\n")) {
    const m = line.match(/^\s*"([^"]+)"\s*:/);
    if (m) {
      key = m[1];
      out.set(key, new Set());
    } else if (/^\s*(\/\/|[}\]]|\.\.\.|export\b)/.test(line)) {
      key = null;
      continue;
    }
    if (key === null) continue;
    for (const v of (m ? line.slice(m[0].length) : line).matchAll(/\{(\w+(?::\d)?)\}/g)) out.get(key).add(v[1]);
  }
  return new Map([...out].map(([k, v]) => [k, [...v].sort().join(",")]));
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

// msg.ts holds only German (the hub sends English); it has its own tests.
const files = readdirSync(dir).filter((f) => f.endsWith(".ts") && f !== "msg.ts");

// The core's table: one {"key", "template"} per line.
export function coreMessages(text) {
  const out = new Map();
  for (const m of text.matchAll(/^\s*\{"([^"]+)",\s*"((?:[^"\\]|\\.)*)"\},?\s*$/gm)) {
    assert.ok(!out.has(m[1]), `messages.cpp: ${m[1]} twice`);
    out.set(m[1], m[2]);
  }
  return out;
}

const sorted = (text) => [...text.matchAll(/\{(\w+(?::\d)?)\}/g)].map((v) => v[1]).sort().join(",");

test("the hub's messages have a German text with the same placeholders", () => {
  const core = coreMessages(readFileSync(join(coreSrc, "messages.cpp"), "utf8"));
  assert.ok(core.size > 50, "messages.cpp read");
  const text = readFileSync(join(dir, "msg.ts"), "utf8");
  const german = keys(text, "msg.ts");
  assert.equal(new Set(german).size, german.length, "msg.ts: duplicate key");
  assert.deepEqual([...german].sort(), [...core.keys()].sort(), "same keys in messages.cpp and msg.ts");
  const de = placeholders(text);
  for (const [k, tmpl] of core) assert.equal(de.get(k), sorted(tmpl), `${k}: placeholders differ`);
});

test("every say() in the core names a key of the table", () => {
  const core = coreMessages(readFileSync(join(coreSrc, "messages.cpp"), "utf8"));
  let n = 0;
  for (const f of readdirSync(coreSrc).filter((f) => f.endsWith(".cpp") && f !== "messages.cpp")) {
    const text = readFileSync(join(coreSrc, f), "utf8");
    for (const m of text.matchAll(/\bsay\(\s*("([^"]*)"|[^)\s][^,)]*)/g)) {
      n++;
      // A key chosen at run time must be one of several literals in a ternary.
      const literals = m[2] !== undefined ? [m[2]] : [...m[1].matchAll(/"([^"]+)"/g)].map((x) => x[1]);
      assert.ok(literals.length > 0, `${f}: say(${m[1]}) without a literal key`);
      for (const k of literals) assert.ok(core.has(k), `${f}: say("${k}") is not in messages.cpp`);
    }
  }
  assert.ok(n > 50, "say() calls found");
  assert.equal(sorted("{a:2} {b}"), "a:2,b");
});

test("German and English use the same placeholders", () => {
  const read = (file) => {
    const text = readFileSync(join(dir, file), "utf8");
    if (file === "de.ts" || file === "en.ts") return placeholders(text);
    const m = text.match(/^export const en\b/m);
    return { de: placeholders(text.slice(0, m.index)), en: placeholders(text.slice(m.index)) };
  };
  const pairs = files.filter((f) => f !== "de.ts" && f !== "en.ts").map((f) => [f, read(f)]);
  pairs.push(["de.ts/en.ts", { de: read("de.ts"), en: read("en.ts") }]);
  for (const [f, { de, en }] of pairs) {
    for (const [k, v] of de) assert.equal(en.get(k), v, `${f}: ${k}: placeholders differ`);
  }
  assert.deepEqual(placeholders('  "a": "Pair {p} of {n}",\n  "b":\n    "x {y}",\n  // {z}\n'), new Map([["a", "n,p"], ["b", "y"]]));
});

test("the key reader refuses keys it cannot read", () => {
  assert.deepEqual(keys('  "a.b": "x",\n  "c":\n    "long value: with colon",\n  // note: y\n'), ["a.b", "c"]);
  assert.deepEqual(keys('  "a": "Bottle \\"{name}\\"",\n'), ["a"]);
  assert.throws(() => keys("  'a.b': \"x\",\n"));
  assert.throws(() => keys("  ab: \"x\",\n"));
  assert.throws(() => keys('  "a": "x", "b": "y",\n'));
  assert.throws(() => keys('export const de = { "a": "x" };\n'));
});

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
