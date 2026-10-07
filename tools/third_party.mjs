// SPDX-License-Identifier: AGPL-3.0-or-later
// Lizenzhinweise der mitgelieferten Fremdbibliotheken (MIT/ISC verlangen sie
// auch in Kopien).
//   node tools/third_party.mjs <ziel.txt>         Simulator: Web-App und C++
//   node tools/third_party.mjs <ziel.txt> --web   nur die Web-App
// Web-Bibliotheken aus web/node_modules (nach `npm ci`), C++-Bibliotheken aus
// den von cmake/deps.cmake geladenen Headern (Version und Copyright werden
// dort gelesen, damit sie nicht auseinanderlaufen).
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const out = resolve(process.argv[2] ?? "THIRD_PARTY_LICENSES.txt");
const webOnly = process.argv.includes("--web");
const mods = join(root, "web", "node_modules");
const fail = (m) => {
  console.error(m);
  process.exit(1);
};

// Laufzeit-Abhängigkeiten der Web-App samt ihren eigenen (ohne Entwicklung)
const seen = new Set();
const walk = (name) => {
  if (seen.has(name)) return;
  const dir = join(mods, name);
  if (!existsSync(join(dir, "package.json"))) fail(`${name} fehlt – vorher npm ci in web/`);
  seen.add(name);
  const pkg = JSON.parse(readFileSync(join(dir, "package.json"), "utf8"));
  for (const d of Object.keys(pkg.dependencies ?? {})) walk(d);
};
const web = JSON.parse(readFileSync(join(root, "web", "package.json"), "utf8"));
for (const d of Object.keys(web.dependencies ?? {})) walk(d);

const licenseText = (dir) => {
  for (const f of ["LICENSE", "LICENSE.md", "LICENSE.txt", "license", "LICENCE"]) {
    if (existsSync(join(dir, f))) return readFileSync(join(dir, f), "utf8").trim();
  }
  return fail(`Keine Lizenzdatei in ${dir}`);
};

const parts = [];
for (const name of [...seen].sort()) {
  const dir = join(mods, name);
  const pkg = JSON.parse(readFileSync(join(dir, "package.json"), "utf8"));
  parts.push(`${name} ${pkg.version} (${pkg.license})\n\n${licenseText(dir)}`);
}

if (!webOnly) {
  // MIT-Wortlaut aus der Lizenz von preact (gleich für alle MIT-Pakete)
  const preact = licenseText(join(mods, "preact"));
  const at = preact.indexOf("Permission is hereby granted");
  if (at < 0) fail("MIT-Wortlaut in der Lizenz von preact nicht gefunden");
  const mit = preact.slice(at);

  const inc = ["build-pkg", "build"].map((b) => join(root, b, "_deps", "include")).find((d) => existsSync(join(d, "httplib.h")));
  if (!inc) fail("C++-Header nicht gefunden (build-pkg/_deps/include) – vorher cmake ausführen");
  const json = readFileSync(join(inc, "nlohmann", "json.hpp"), "utf8");
  const jsonVersion = json.match(/version (\d+\.\d+\.\d+)/)?.[1] ?? fail("Version von nlohmann/json nicht gefunden");
  const jsonCopy = [...new Set([...json.matchAll(/SPDX-FileCopyrightText: (.+)/g)].map((m) => `Copyright (c) ${m[1].trim()}`))];
  parts.push(
    `nlohmann/json ${jsonVersion} (MIT)\n\n${jsonCopy.join("\n")}\n\n${mit}\n\n` +
      "Enthält Hedley von Evan Nemerson (CC0-1.0) und Teile von Google Abseil (Apache-2.0, Wortlaut unten).",
  );
  const httplib = readFileSync(join(inc, "httplib.h"), "utf8");
  const httpVersion = httplib.match(/CPPHTTPLIB_VERSION "([^"]+)"/)?.[1] ?? fail("Version von cpp-httplib nicht gefunden");
  const httpCopy = httplib.match(/Copyright \(c\) [^\n]+/)?.[0] ?? fail("Copyright von cpp-httplib nicht gefunden");
  parts.push(`cpp-httplib ${httpVersion} (MIT)\n\n${httpCopy}\n\n${mit}`);
  parts.push(`Apache License 2.0 (für die Abseil-Teile in nlohmann/json)\n\n${readFileSync(join(root, "tools", "licenses", "Apache-2.0.txt"), "utf8").trim()}`);
}

const head = "growcontroller – Lizenzhinweise der enthaltenen Fremdbibliotheken\n" + "Third-party licenses of included libraries\n";
const sep = "\n\n" + "=".repeat(72) + "\n\n";
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, head + sep + parts.join(sep) + "\n");
console.log(`${parts.length} Einträge → ${out}`);
