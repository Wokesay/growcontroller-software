// SPDX-License-Identifier: AGPL-3.0-or-later
// Change filter of the full CI (ci.yml, SD-030): docs-only changes skip
// the heavy jobs and run only the quick checks (PD-059).
//   node tools/ci_changes.mjs <base> <head>   prints code=true or code=false
// Without a base, or if git cannot compare, it prints code=true: an
// unknown range runs everything.
import { execFileSync } from "node:child_process";
import { pathToFileURL } from "node:url";

// The same paths that ci.yml skipped with paths-ignore before SD-030.
const DOCS_ONLY = [/^docs\//, /\.md$/, /^LICENSES\//, /^\.claude\//, /^\.github\/ISSUE_TEMPLATE\//];

export function needsFullCi(files) {
  return files.some((f) => f !== "" && !DOCS_ONLY.some((re) => re.test(f)));
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [base, head] = process.argv.slice(2);
  let code = true;
  if (base && head) {
    try {
      // --no-renames lists both paths of a move, so moving code into docs/
      // still counts as a code change.
      const out = execFileSync("git", ["diff", "--name-only", "--no-renames", base, head], {
        encoding: "utf8",
      });
      code = needsFullCi(out.split("\n"));
    } catch (e) {
      console.error(`git diff ${base} ${head} failed, running the full CI: ${e.message}`);
    }
  }
  console.log(`code=${code}`);
}
