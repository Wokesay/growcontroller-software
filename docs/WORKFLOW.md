# Workflow: own repository, issues, agents, decisions

As of 2026-10-07. Proposal, unless a PD decided it; drafts in
`docs/DECISIONS.md`.

## 1. Repository split

| Software repository (this repository) | stays in the product repository `growcontroller` |
|---|---|
| Core, simulator, web app, firmware of the hub; later firmware of dosing block and heads | Boards (KiCad), until the hardware license is decided |
| Interfaces: catalog, API, later bus register map | Purchasing, suppliers, prices, margins, business case |
| Docs, software decisions (SD), CHANGELOG, SECURITY, CONTRIBUTING | Regulatory file, test reports |
| Agents for the software roles | Internal reference material, customer data |

Signing keys are in no repository. Source: `produkt`.

**Transparency of product decisions:** If a PD affects the software, this
repository gets an SD entry "Product requirement (from PD-0xx)". It states
what applies and the public reason, without figures or suppliers. A one-way
mirror `ref/software/` leads back into the product repository (PD-017);
only the sync workflow may write there.

## 2. Move to this repository (status)

**Done:**

1. The project owner created the private repository
   `Wokesay/growcontroller-software` (PD-017). The Claude GitHub app is
   approved for it (PD-017).
2. `software/` moved from the product repository with its Git history
   (PD-023). CI, agents and `CLAUDE.md` moved along; the workflows run
   from the repository root.
3. Licensing in place (PD-022, PD-032, PD-033): `LICENSE`, `LICENSES/`,
   `REUSE.toml`, SPDX headers, `ADDITIONAL_PERMISSION.md` (AGPL §7),
   `THIRD_PARTY_NOTICES.md`; `reuse lint` and the npm license check run
   in CI; the web app links to its source code (AGPL §13).
4. Docs, templates, agents and `CLAUDE.md` are in English (PD-034); the
   rationale register `docs/RATIONALE.md` replaces references to private
   sources (PD-039). Issue templates, `SECURITY.md` and the "Report a
   problem" link point to this repository.
5. The product repository no longer carries `software/`; it mirrors this
   repository into `ref/software/` (PD-054).

**Remaining:**

1. **Translate code comments, test names and UI defaults** into English in
   a separate PR (PD-034, PD-035).
2. **Turn on secret scanning and push protection**, if not on yet.
3. **Optional:** activate the triage action from
   `docs/templates/claude-triage.yml` (§4).
4. **Going public** (PD-055 to PD-062, SD-026): the history moves into a
   new repository without the last private references (PD-056). Before
   the switch: Actions and ruleset settings; if `main` gets a required
   status check, require only "Quick checks / quick", because the full
   CI skips docs-only PRs. Right after the switch (these settings exist
   only for public repositories): private vulnerability reporting,
   approval for workflows from all outside contributors, secret scanning
   and push protection, Dependabot alerts, Discussions.
5. **Before the first public release tag:** build release assets in a
   read-only job and publish them from a job without a build; pin actions
   by commit SHA; protect `v*` tags with a ruleset.
6. **Later:** a trademark search before a trademark of our own or the
   first sale.

## 3. Roles and agents

Claude is product owner and developer, as in the product repository. The
domain roles are subagents in `.claude/agents/`. They change no files. The
agents with a shell (`triage`, `qa`, `release`) are used read-only; Claude
builds and tests.

| Agent | Role in a software company | Responsible for | Tools |
|---|---|---|---|
| `triage` | support / issue triage | classify new issues, reproduce in the simulator, duplicates, severity, draft reply | read, shell to build and start the simulator, read GitHub |
| `reviewer` | code review | correctness, architecture rules R1–R8, invariants, readability, test coverage per diff | read |
| `qa` | quality assurance | derive test cases, run tests, gaps in `INVARIANTS.md`, acceptance before a release | read, shell for tests |
| `security` | product security | threat model, EN 18031/CRA in practice, dependencies and SBOM, auth, OTA, diagnostic data | read, web |
| `release` | release and build | version, changelog, "What's new" summary, artifacts, channels, rollback plan | read, shell to build |
| `ux` | UX/UI design and texts | information architecture, texts following the principles in `UX.md`, mobile, accessibility | read, web |
| `domain` | fertigation domain expert | control and dosing logic against `INVARIANTS.md` and `RATIONALE.md`, simulator numbers, placing new domain rules | read, web |

Other views stay in the product repository: `kunde` (customer), `anwender`
(grower), `architekt` (architecture), `firmware`, `hardware`,
`regulatorik` (regulatory), `produkt` (product and business). This
repository asks them through the project owner or in a joint session.

**Flow of a change:**

```
Issue/idea ─► triage ─► project owner decides (priority, whether at all)
   ─► Claude: branch, test first, then code ─► tools/ci.sh green
   ─► reviewer + qa (+ security for gateway/sensor truth/auth/update, + ux for UI)
   ─► PR with CHANGELOG entry ─► CI green ─► approval ─► merge ─► release collects
```

## 4. Making issues visible in Claude Code and working through them

Three ways, can be combined (source: Claude Code docs, retrieved
2026-10-06: code.claude.com/docs/en/github-actions, …/routines):

1. **In every session via GitHub MCP:**
   - At start, Claude lists the open issues with the label `triage` and
     assesses them with `triage`.
   - This step is in `CLAUDE.md` under "At session start".
2. **Routine:** a scheduled session in Claude Code on the web, e.g. on
   weekday mornings.
   - It reads new issues, reproduces them in the simulator and writes a
     draft comment for the project owner.
   - It posts nothing publicly without his approval.
3. **GitHub Action with `@claude`:**
   - Template in `docs/templates/claude-triage.yml`.
   - Needs the Claude GitHub app and a secret (`ANTHROPIC_API_KEY` or
     `CLAUDE_CODE_OAUTH_TOKEN`).

**Protection** (issue texts are outside input). In June 2026 a crafted
issue gained write access through the Claude Code Action; fixed from
v1.0.94. Sources (retrieved 2026-10-06):
https://thehackernews.com/2026/06/claude-code-github-action-flaw-let-one.html,
https://flatt.tech/research/posts/poisoning-claude-code-one-github-issue-to-break-the-supply-chain/.
Therefore:

- Triage workflows run read-only, with no secrets except the API key and
  with the permissions `issues: write`, `contents: read`.
- No merge and no release from issue workflows.
- Signing happens only offline after manual approval.
- Security reports never come as issues (`SECURITY.md`).

**For hobby customers without GitHub:**

- The app offers "Report a problem" with a case number and a diagnostic
  package that the customer sees first.
- Later there is also a form or an email address. Claude anonymizes the
  report and opens the issue.

**Effort (assumption of `produkt`):** 2–8 reports per week at 50–300
devices, so 2–5 h per week with Claude triage. Rules:

- reply within 7 days;
- stale label after 30/60 days;
- FAQ from frequent issues;
- feature requests in Discussions "Ideas".

## 5. Branches, commits, reviews

- **Branches:** `main` is always releasable. Every change on its own
  branch with a PR. Squash merge.
- **Commits:** in English (PD-034). No sign-off, no DCO, no CLA (PD-022).
- **PR:** template with safety items (gateway, missing value, rationale
  RAT ID, changelog).
- **Merge** only through a pull request, after green CI and the reviews
  above. Claude merges, except releases, license and security rules and
  changes to the actuator gateway or protective cut-offs, which wait for
  the project owner's "mergen" (PD-045, SD-023).

## 6. Decisions

- **SD log:** `docs/DECISIONS.md`, format `## SD-XXX: Title`, sequential,
  chronological. No status field; a later SD replaces an earlier one.
- **Drafts** are kept separate and apply only after the project owner says
  "entschieden" (decided).
- **Product decisions** stay in the product repository (PD). In this
  repository they appear as "Product requirement (from PD-0xx)".
