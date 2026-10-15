// SPDX-License-Identifier: AGPL-3.0-or-later
// Says why `npm audit signatures` failed (#163). The check itself stays
// where it is, right after `npm ci` (SD-058), and its plain output names a
// package but not the reason. After a failed job this asks npm again with
// --json and prints, for each package that is invalid or missing, npm's code
// and message, with the Node and npm that checked it. It changes nothing and
// never fails the job itself: the check already did.
//   node tools/signatures_report.mjs [web folder]
import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

// The lines to print for npm's JSON answer (an object with "invalid" and
// "missing"), or for whatever else came back.
export function describe(stdout) {
  let report;
  try {
    report = JSON.parse(stdout);
  } catch {
    const text = String(stdout ?? "").trim();
    return [`npm gave no JSON${text ? ":" : "."}`, ...(text ? [text.slice(0, 4000)] : [])];
  }
  // npm that could not check at all (no registry, no Sigstore) answers with
  // an error, not with a report; that is no pass.
  if (report?.error) {
    const e = report.error;
    const lines = [`npm could not check the signatures: ${[e.code, e.summary].filter(Boolean).join(": ") || JSON.stringify(e)}`];
    if (e.detail) lines.push(`  ${String(e.detail).slice(0, 4000)}`);
    return lines;
  }
  if (!Array.isArray(report?.invalid) || !Array.isArray(report?.missing)) return [`npm gave no report: ${String(stdout).slice(0, 4000)}`];
  const lines = [];
  for (const kind of ["invalid", "missing"]) {
    for (const p of report[kind]) {
      const what = [p.predicateType && `attestation ${p.predicateType}`, p.keyid && `key ${p.keyid}`].filter(Boolean).join(", ");
      lines.push(`${kind}: ${p.name ?? "?"}@${p.version ?? "?"}${what ? ` (${what})` : ""}`);
      if (p.code || p.message) lines.push(`  ${[p.code, p.message].filter(Boolean).join(": ")}`);
      if (p.resolved) lines.push(`  from ${p.resolved}`);
      if (p.integrity) lines.push(`  integrity ${p.integrity}`);
    }
  }
  if (lines.length === 0) lines.push("npm finds no invalid or missing signature now; the failure did not come back.");
  return lines;
}

function run(cmd, args, cwd) {
  const r = spawnSync(cmd, args, { cwd, encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  return { out: r.stdout ?? "", err: r.stderr ?? "", status: r.status };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const web = resolve(process.argv[2] ?? "web");
  if (!existsSync(join(web, "node_modules"))) {
    console.log(`No packages installed in ${web}: the job failed before npm ci, nothing to report.`);
  } else {
    console.log(`Node ${process.version}, npm ${run("npm", ["-v"], web).out.trim()}`);
    const r = run("npm", ["audit", "signatures", "--json"], web);
    for (const line of describe(r.out)) console.log(line);
    if (r.err.trim()) console.log(r.err.trim().slice(0, 4000));
  }
}
