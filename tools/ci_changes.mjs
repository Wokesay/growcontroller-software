// SPDX-License-Identifier: AGPL-3.0-or-later
// Change filter of the full CI (ci.yml, SD-030): docs-only changes skip
// the heavy jobs and run only the quick checks (PD-059).
//   node tools/ci_changes.mjs <base> <head>   prints code=true or code=false
// The changed files go to stderr for the log. Without a base, or if git
// cannot compare, it prints code=true: an unknown range runs everything.
import { execFileSync } from "node:child_process";
import { realpathSync } from "node:fs";
import { pathToFileURL } from "node:url";

// The same paths that ci.yml skipped with paths-ignore before SD-030.
const DOCS_ONLY = [/^docs\//, /\.md$/, /^LICENSES\//, /^\.claude\//, /^\.github\/ISSUE_TEMPLATE\//];

export function needsFullCi(files) {
  return files.some((f) => f !== "" && !DOCS_ONLY.some((re) => re.test(f)));
}

function main([base, head]) {
  if (!base || !head) return true;
  try {
    // --no-renames lists both paths of a move, so moving code into docs/
    // still counts as a code change. -z keeps unusual file names unquoted.
    const out = execFileSync("git", ["diff", "--name-only", "--no-renames", "-z", base, head], {
      encoding: "utf8",
    });
    const files = out.split("\0").filter((f) => f !== "");
    // JSON: control characters in a file name stay visible in the log.
    console.error(files.length ? files.map((f) => JSON.stringify(f)).join("\n") : "(no changed files)");
    return needsFullCi(files);
  } catch (e) {
    console.error(`git diff ${base} ${head} failed, running the full CI: ${e.message}`);
    return true;
  }
}

// realpath: Node resolves symlinks for import.meta.url but not in argv.
if (process.argv[1] && import.meta.url === pathToFileURL(realpathSync(process.argv[1])).href) {
  console.log(`code=${main(process.argv.slice(2))}`);
}
