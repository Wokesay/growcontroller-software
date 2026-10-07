// Größenbudget der Web-App: Sie liegt gzip-komprimiert im Flash des Hubs
// (Vorschlag software: höchstens ca. 250 KB gzip). Bricht den Build, wenn
// das Budget überschritten wird.
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { gzipSync } from "node:zlib";

const BUDGET = 250 * 1024;
const dir = fileURLToPath(new URL("../dist", import.meta.url));
let total = 0;
const rows = [];
const walk = (d) => {
  for (const f of readdirSync(d)) {
    const p = join(d, f);
    if (statSync(p).isDirectory()) walk(p);
    else {
      const gz = gzipSync(readFileSync(p), { level: 9 }).length;
      total += gz;
      rows.push([p.slice(dir.length + 1), gz]);
    }
  }
};
walk(dir);
for (const [f, s] of rows) console.log(`${(s / 1024).toFixed(1).padStart(7)} KB  ${f}`);
console.log(`${(total / 1024).toFixed(1).padStart(7)} KB  gesamt (gzip), Budget ${BUDGET / 1024} KB`);
if (total > BUDGET) {
  console.error("Größenbudget überschritten");
  process.exit(1);
}
