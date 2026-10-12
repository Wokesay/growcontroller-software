// SPDX-License-Identifier: AGPL-3.0-or-later
// License check of all npm dependencies of the web app (CI, tools/ci.sh).
// Reads web/package-lock.json and fails if a package has no license or one
// that is not on the allow list (tools/license_policy.mjs).
//   node tools/check_licenses.mjs
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { licenseProblems } from "./license_policy.mjs";

const root = fileURLToPath(new URL("..", import.meta.url));
const lock = JSON.parse(readFileSync(join(root, "web", "package-lock.json"), "utf8"));
const notices = readFileSync(join(root, "THIRD_PARTY_NOTICES.md"), "utf8");
const { count, problems } = licenseProblems(lock, notices);
if (problems.length) {
  console.error("License check failed:\n- " + problems.join("\n- "));
  process.exit(1);
}
console.log(`License check: ${count} npm packages OK.`);
