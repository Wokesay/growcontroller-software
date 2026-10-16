// SPDX-License-Identifier: AGPL-3.0-or-later
// Text keys of the web app (#18): web/src/lang/de.ts and en.ts merge the
// base texts and one file per area. A key used in two files would silently
// overwrite the other; every file has the same keys in German and English.
// The hub's messages (SD-032): core/src/messages.cpp (English) and
// web/src/lang/msg.ts (German) have the same keys and placeholders, and
// every say("…") in the core, the servers and the firmware names a key of the table.
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

// msg.ts and cat.ts hold only German (the hub and the catalog are English);
// they have their own tests.
const files = readdirSync(dir).filter((f) => f.endsWith(".ts") && f !== "msg.ts" && f !== "cat.ts");

// The core's table: one {"key", "template"} per line; any other line in
// the table must be blank or a comment, so no entry is skipped unseen.
export function coreMessages(text) {
  const out = new Map();
  const start = text.indexOf("kMessages[] = {");
  assert.ok(start > 0, "messages.cpp: kMessages");
  const body = text.slice(text.indexOf("\n", start) + 1, text.indexOf("\n};", start));
  for (const line of body.split("\n")) {
    const m = line.match(/^\s*\{"([^"]+)",\s*"((?:[^"\\]|\\.)*)"\},?\s*$/);
    if (!m) {
      assert.match(line, /^\s*(\/\/.*)?$/, `messages.cpp: line not read: ${line.trim()}`);
      continue;
    }
    assert.ok(!out.has(m[1]), `messages.cpp: ${m[1]} twice`);
    out.set(m[1], m[2]);
  }
  return out;
}

const sorted = (text) => [...new Set([...text.matchAll(/\{(\w+(?::\d)?)\}/g)].map((v) => v[1]))].sort().join(",");

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

test("every say() in the core, the servers and the firmware names a key of the table", () => {
  const core = coreMessages(readFileSync(join(coreSrc, "messages.cpp"), "utf8"));
  let n = 0;
  const cpp = (dir) => readdirSync(join(root, dir)).filter((f) => f.endsWith(".cpp")).map((f) => join(root, dir, f));
  const sources = [
    ...readdirSync(coreSrc).filter((f) => f.endsWith(".cpp") && f !== "messages.cpp").map((f) => join(coreSrc, f)),
    ...readdirSync(join(root, "core", "include", "gc")).map((f) => join(root, "core", "include", "gc", f)),
    ...cpp("sim"),
    ...cpp("ha"),
    ...cpp(join("firmware", "main")),
  ];
  for (const path of sources) {
    const f = path.slice(root.length + 1);
    const text = readFileSync(path, "utf8").replace(/^\s*\/\/.*$/gm, "");
    for (const m of text.matchAll(/\bsay\(\s*("([^"]*)"|[^)\s][^,)]*)/g)) {
      if (f.endsWith("messages.hpp")) continue;
      n++;
      // A key chosen at run time: only literals in a ternary, one per branch.
      const literals = m[2] !== undefined ? [m[2]] : [...m[1].matchAll(/"([^"]+)"/g)].map((x) => x[1]);
      const branches = m[2] !== undefined ? 1 : (m[1].match(/\?/g) ?? []).length + 1;
      assert.ok(literals.length > 0 && literals.length === branches, `${f}: say(${m[1]}) without a literal key per branch`);
      for (const k of literals) assert.ok(core.has(k), `${f}: say("${k}") is not in messages.cpp`);
    }
  }
  assert.ok(n > 50, "say() calls found");
  assert.equal(sorted("{a:2} {b}"), "a:2,b");
});

test("no other core text uses a key of the table", () => {
  // The web app fills a known key with the table's template; a different
  // text under the same key would show that template with missing values.
  const core = coreMessages(readFileSync(join(coreSrc, "messages.cpp"), "utf8"));
  for (const f of readdirSync(coreSrc).filter((f) => f.endsWith(".cpp") && f !== "messages.cpp")) {
    const text = readFileSync(join(coreSrc, f), "utf8");
    // A key compared with another one is no text of its own
    const outside = text.replace(/\bsay\(\s*(?:"[^"]*"|[^,)]*)/g, "say(").replace(/\bkey\s*[!=]=\s*"[^"]*"/g, "key ==");
    for (const k of core.keys()) assert.ok(!outside.includes(`"${k}"`), `${f}: "${k}" used outside say()`);
  }
});

// A refusal written by hand where the table is not linked (the firmware's
// transport, a raw string) has the table's English text for its key.
test("a hand-written refusal has its key's English text", () => {
  const core = coreMessages(readFileSync(join(coreSrc, "messages.cpp"), "utf8"));
  let n = 0;
  for (const dir of ["sim", "ha", join("firmware", "main")])
    for (const f of readdirSync(join(root, dir)).filter((x) => x.endsWith(".cpp"))) {
      const text = readFileSync(join(root, dir, f), "utf8");
      for (const m of text.matchAll(/key\\?":\\?"([a-z_.]+)\\?",\\?"text\\?":\\?"([^"\\]*)/g)) {
        n++;
        assert.ok(core.has(m[1]), `${dir}/${f}: ${m[1]} is not in messages.cpp`);
        assert.equal(m[2], core.get(m[1]), `${dir}/${f}: ${m[1]}`);
      }
    }
  assert.ok(n >= 3, "hand-written refusals found");  // the firmware's; the servers take theirs from the table
});

// The hub sends English and the web app translates by key (SD-083): the
// refusals of the API, the sign-in and the configuration check, a reading's
// reasons, the checklists and the simulator's port lines hold no German text
// of their own.
test("the API, the sign-in, the configuration check, a reading's reasons and the checklists send no German text", () => {
  const german = /"(?:[^"\\\n]|\\.)*[äöüßÄÖÜ„“](?:[^"\\\n]|\\.)*"/;
  const files = ["api.cpp", "auth.cpp", "config.cpp", "hub.cpp", "truth.cpp", "resolver.cpp"].map((f) => join(coreSrc, f));
  for (const f of [...files, join(coreSrc, "../../sim/simbus.cpp")]) {
    const lines = readFileSync(f, "utf8").split("\n");
    for (const [i, line] of lines.entries()) {
      const code = line.replace(/\/\/.*$/, "");
      assert.doesNotMatch(code, german, `${f}:${i + 1}: ${line.trim()}`);
    }
  }
  assert.match('x = "Gerät";', german);
});

// The catalog is English; its German comes from lang/cat.ts by the entry's
// key (SD-083 point 3): every name and text shown has one, and the table
// holds nothing else. The Home Assistant app adds its classes in code.
test("every catalog name and text has its German, and the catalog is English", () => {
  const cat = JSON.parse(readFileSync(join(root, "catalog", "catalog.json"), "utf8"));
  const want = [];
  for (const id of Object.keys(cat.capabilities)) want.push(`cat.cap.${id}`);
  for (const id of Object.keys(cat.deviceClasses)) want.push(`cat.class.${id}`, `cat.class.${id}.text`);
  for (const id of Object.keys(cat.roles)) want.push(`cat.role.${id}`);
  for (const f of cat.functions) {
    want.push(`cat.fn.${f.id}`, `cat.fn.${f.id}.text`);
    for (const p of f.params ?? []) {
      want.push(`cat.param.${f.id}.${p.key}`);
      for (const [v] of p.options ?? []) want.push(`cat.option.${f.id}.${p.key}.${v}`);
    }
  }
  const ha = readFileSync(join(root, "ha", "ha_bus.cpp"), "utf8");
  const list = (name) => {
    const m = ha.match(new RegExp(`${name}\\[\\] = \\{([^}]*)\\}`));
    assert.ok(m, `ha_bus.cpp: ${name}`);
    return [...m[1].matchAll(/"(\w+)"/g)].map((x) => x[1]);
  };
  for (const m of list("kMeasures")) want.push(`cat.class.ha_${m}`, `cat.class.ha_${m}.text`);
  for (const m of list("kSignalMeasures")) want.push(`cat.class.ha_${m}_raw`, `cat.class.ha_${m}_raw.text`);
  const have = keys(readFileSync(join(dir, "cat.ts"), "utf8"), "cat.ts");
  assert.equal(new Set(have).size, have.length, "cat.ts: duplicate key");
  assert.deepEqual([...have].sort(), [...want].sort(), "cat.ts has exactly the catalog's names and texts");
  // The former German class names a device stored as its name (core/src/catalog.cpp ownName) are the German table's
  const catalogCpp = readFileSync(join(coreSrc, "catalog.cpp"), "utf8");
  const former = [...catalogCpp.matchAll(/^\s*\{"(\w+)", "([^"]+)"\},$/gm)];
  const catDe = new Map([...readFileSync(join(dir, "cat.ts"), "utf8").matchAll(/^\s*"(cat\.class\.\w+)": "([^"]*)",$/gm)].map((m) => [m[1], m[2]]));
  const classes = want.filter((k) => /^cat\.class\.\w+$/.test(k));
  assert.equal(former.length, classes.length, "a former name for every class");
  for (const [, cls, name] of former) assert.equal(name, catDe.get(`cat.class.${cls}`), `former name of ${cls}`);
  // A soft requirement's reason has a key for its function and role (core/src/resolver.cpp softWhy)
  const resolver = readFileSync(join(coreSrc, "resolver.cpp"), "utf8");
  const mapped = new Set([...resolver.matchAll(/fn == "(\w+)" && r\.role == "([\w.]+)"\) return say\("req\.why\./g)].map((m) => `${m[1]}|${m[2]}`));
  for (const f of cat.functions) for (const r of f.soft ?? []) assert.ok(mapped.has(`${f.id}|${r.role}`), `${f.id}: the reason for ${r.role} has no key`);
  // No German left in what the catalog shows (the recipe templates have both languages, nameEn/noteEn)
  const { templates, ...shown } = cat;
  assert.ok(templates);
  assert.doesNotMatch(JSON.stringify(shown), /[äöüßÄÖÜ]/);
  assert.doesNotMatch(ha.replace(/\/\/.*$/gm, ""), /"[^"\n]*[äöüßÄÖÜ][^"\n]*"/);
});

// A jump lock's reason is the text of its alarm, later also of a
// notification: at most 200 characters with the longest values (RAT-045)
test("a jump lock's reason fits a notification in both languages", () => {
  const core = coreMessages(readFileSync(join(coreSrc, "messages.cpp"), "utf8"));
  const german = new Map();
  for (const m of readFileSync(join(dir, "msg.ts"), "utf8").matchAll(/^\s*"([^"]+)": ("(?:[^"\\]|\\.)*"),$/gm)) german.set(m[1], JSON.parse(m[2]));
  const longest = { from: "999.5", to: "999.5", unit: "\u00a0mS/cm" };
  const keys = [...core.keys()].filter((k) => k.startsWith("truth.jump"));
  assert.ok(keys.length >= 5, "the jump lock reasons read");
  for (const k of keys)
    for (const [lang, tmpl] of [["en", JSON.parse(`"${core.get(k)}"`)], ["de", german.get(k)]]) {
      const text = tmpl.replace(/\{(\w+)\}/g, (_, name) => longest[name] ?? "");
      assert.ok(text.length <= 200, `${k} (${lang}): ${text.length} characters`);
    }
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

// Durations and ages keep the number and its unit on one line (#106): the
// ages the app writes and the hub's spans join them with a no-break space.
test("durations and ages join the number and its unit with a no-break space", () => {
  const value = (text, key) => {
    const line = text.split("\n").find((l) => l.trimStart().startsWith(`"${key}":`));
    assert.ok(line, `${key} not found`);
    return line;
  };
  const ages = ["common.secondsAgo", "common.minutesAgo", "common.hoursAgo", "common.daysAgo"];
  for (const file of ["de.ts", "en.ts"]) {
    const text = readFileSync(join(dir, file), "utf8");
    for (const k of ages) assert.match(value(text, k), /\{n\}\\u00a0\S/, `${file}: ${k}`);
  }
  const msg = readFileSync(join(dir, "msg.ts"), "utf8");
  const core = coreMessages(readFileSync(join(coreSrc, "messages.cpp"), "utf8"));
  for (const k of ["span.s", "span.min", "span.h"]) {
    assert.match(value(msg, k), /\}\\u00a0\S/, `msg.ts: ${k}`);
    assert.match(core.get(k), /\}\\u00a0\S/, `messages.cpp: ${k}`);
  }
  // No other text joins a placeholder and a unit of time with a plain space
  const plain = /\}[ \t]+(s|min|h|Tage|Tagen|days)\b/;
  for (const [k, tmpl] of core) assert.doesNotMatch(tmpl, plain, `messages.cpp: ${k}`);
  // The hub's messages: a written number and its unit of time too (SD-083)
  const written = /\d[ \t]+(s|min|h)\b/;
  for (const [k, tmpl] of core) assert.doesNotMatch(tmpl, written, `messages.cpp: ${k}`);
  for (const line of msg.split("\n")) assert.doesNotMatch(line, written, `msg.ts: ${line.trim()}`);
  for (const file of [...files, "msg.ts"])
    for (const line of readFileSync(join(dir, file), "utf8").split("\n")) assert.doesNotMatch(line, plain, `${file}: ${line.trim()}`);
});
