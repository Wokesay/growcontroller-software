// Bettet die gebaute Web-App (web/dist) gzip-komprimiert ein. Aufruf nach
// `npm run build`:
//   node tools/embed_web.mjs              → firmware/main/web_assets.cpp
//   node tools/embed_web.mjs <datei.cpp>  → z. B. für den Simulator-Download
import { mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { gzipSync } from "node:zlib";

const root = fileURLToPath(new URL("..", import.meta.url));
const dist = join(root, "web", "dist");
const out = process.argv[2] ? resolve(process.argv[2]) : join(root, "firmware", "main", "web_assets.cpp");
const types = { html: "text/html; charset=utf-8", js: "application/javascript", css: "text/css", svg: "image/svg+xml", json: "application/json", png: "image/png", ico: "image/x-icon" };

const files = [];
const walk = (d) => {
  for (const f of readdirSync(d)) {
    const p = join(d, f);
    if (statSync(p).isDirectory()) walk(p);
    else files.push(p);
  }
};
walk(dist);

let src = "// Erzeugt von tools/embed_web.mjs – nicht von Hand bearbeiten.\n#include \"gc/web_assets.hpp\"\n\nnamespace gc {\n\n";
const entries = [];
let total = 0;
files.forEach((p, i) => {
  const gz = gzipSync(readFileSync(p), { level: 9 });
  total += gz.length;
  const ext = p.split(".").pop();
  src += `static const unsigned char kF${i}[] = {`;
  for (let k = 0; k < gz.length; k++) src += (k % 24 === 0 ? "\n  " : "") + gz[k] + ",";
  src += "\n};\n";
  entries.push(`  {"/${relative(dist, p).split("\\").join("/")}", "${types[ext] ?? "application/octet-stream"}", kF${i}, sizeof(kF${i})}`);
});
src += `\nconst WebAsset kWebAssets[] = {\n${entries.join(",\n")}\n};\nconst size_t kWebAssetCount = ${entries.length};\n\n}  // namespace gc\n`;
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, src);
console.log(`${entries.length} Dateien, ${(total / 1024).toFixed(1)} KB gzip → ${relative(root, out)}`);
