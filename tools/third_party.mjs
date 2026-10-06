// Lizenzhinweise der mitgelieferten Fremdbibliotheken für Download-Pakete
// (MIT/ISC verlangen sie auch in Kopien). Web-Bibliotheken aus
// web/node_modules (nach `npm ci`), C++-Bibliotheken aus cmake/deps.cmake.
//   node tools/third_party.mjs <ziel.txt>
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const out = resolve(process.argv[2] ?? "THIRD_PARTY_LICENSES.txt");
const mods = join(root, "web", "node_modules");

// Laufzeit-Abhängigkeiten der Web-App samt ihren eigenen (ohne Entwicklung)
const seen = new Set();
const walk = (name) => {
  if (seen.has(name)) return;
  const dir = join(mods, name);
  if (!existsSync(join(dir, "package.json"))) throw new Error(`${name} fehlt – vorher npm ci in web/`);
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
  throw new Error(`Keine Lizenzdatei in ${dir}`);
};

// MIT-Wortlaut aus der Lizenz von preact (gleich für alle MIT-Pakete)
const preact = licenseText(join(mods, "preact"));
const mitBody = preact.slice(preact.indexOf("Permission is hereby granted"));

const parts = [];
for (const name of [...seen].sort()) {
  const dir = join(mods, name);
  const pkg = JSON.parse(readFileSync(join(dir, "package.json"), "utf8"));
  parts.push(`${name} ${pkg.version} (${pkg.license})\n\n${licenseText(dir)}`);
}
// C++-Bibliotheken im Programm (Versionen wie in cmake/deps.cmake)
parts.push(`nlohmann/json 3.11.3 (MIT)\n\nCopyright (c) 2013-2023 Niels Lohmann <https://nlohmann.me>\n\n${mitBody}`);
parts.push(`cpp-httplib 0.54.1 (MIT)\n\nCopyright (c) 2026 Yuji Hirose. All rights reserved.\n\n${mitBody}`);

const head = "growcontroller – Lizenzhinweise der enthaltenen Fremdbibliotheken\n" + "Third-party licenses of included libraries\n";
const sep = "\n\n" + "=".repeat(72) + "\n\n";
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, head + sep + parts.join(sep) + "\n");
console.log(`${parts.length} Bibliotheken → ${out}`);
