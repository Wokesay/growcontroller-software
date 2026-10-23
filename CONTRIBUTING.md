# Contributing

Thanks for your interest.

## Ways to help right now

The project is a prototype, so the most useful help needs no code:

- **Try the demo and give feedback** ([`docs/DEMO.md`](docs/DEMO.md)):
  where you got stuck, which texts you did not understand, what your own
  setup would need. Post it in
  [Discussions](https://github.com/Wokesay/growcontroller-software/discussions).
- **Report bugs** with the
  [bug report form](https://github.com/Wokesay/growcontroller-software/issues/new?template=bug.yml);
  "simulator only" is a fine answer for the hardware.
- **Tell us about your devices** with the
  [device form](https://github.com/Wokesay/growcontroller-software/issues/new?template=device.yml): probes, pumps,
  smart plugs, sensors in Home Assistant. It helps decide what gets supported
  ([`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md)).
- **Improve texts and docs:** an unclear sentence in the app or the docs
  is a bug too. English and German are the app's languages.
- **Code:** read on. In short: small PRs, a test first, everything in
  English. For anything larger than a fix, open a discussion first, so
  that we agree on the way before you spend the time. A fix for a
  vulnerability never goes into a public PR: mention it in the private
  report ([`SECURITY.md`](SECURITY.md)).

**How this repository works:** it is built by one maintainer with an AI
coding assistant (Claude Code), which writes most of the code, tests and
docs, and every decision is recorded (`docs/DECISIONS.md`; SD-, PD- and
RAT- numbers are explained in
[`docs/FAQ.md`](docs/FAQ.md#what-do-sd--pd--rat--and-m1-mean)). The
review steps below name the maintainer's AI review agents
(`.claude/agents/`, `docs/WORKFLOW.md`); as an outside contributor you
do not run them, the maintainer does. You need CI green, a test for your
change and a CHANGELOG entry.

## License of contributions

This project is licensed under AGPL-3.0-or-later (`LICENSE`) with the
additional permission in `ADDITIONAL_PERMISSION.md`. By opening a pull
request you agree that your contribution is licensed under the same terms,
including that additional permission (inbound = outbound). There is no CLA
and no sign-off (DCO). The project offers no other license for the
software: no dual licensing, no commercial license.

Every new source file starts with an SPDX header:

<!-- REUSE-IgnoreStart -->
```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
```
<!-- REUSE-IgnoreEnd -->

(`#` for shell, CMake and YAML; `<!-- -->` is not needed for Markdown and
JSON, which `REUSE.toml` covers.) CI runs `reuse lint` and a license check
of all npm dependencies (`tools/check_licenses.mjs`). A new dependency
needs a license compatible with the AGPL and an entry in
`THIRD_PARTY_NOTICES.md`. Build tools that are never shipped may also be
under CC-BY-4.0 (data); lightningcss as a build tool also under MPL-2.0
(SD-046). A package the web app imports goes under `dependencies` in
`web/package.json`, never under `devDependencies`: it ends up in what we
ship, and the check refuses it otherwise (#102).

## Develop

What to install on Linux, macOS or Windows:
[`docs/DEMO.md`](docs/DEMO.md#8-build-it-yourself-instead); `tools/ci.sh`
also needs [reuse](https://reuse.software/) (`pipx install reuse==6.2.0`).

```bash
tools/dev.sh                    # simulator + web app on :8080
cd web && npm run dev           # UI with hot reload on :5173 (API → :8080)
tools/ci.sh                     # all checks before the PR
```

## Code rules

- **The core stays platform-neutral:** no platform headers in `core/`.
- **Actuators only through the gateway** (`Actuators` in
  `core/src/dosing.cpp`).
- **The watchdog only evaluates.** It includes only `readmodel.hpp` and
  `config.hpp`.
- **A missing value stays missing** (`nullopt`/`NaN`, JSON `null`), never
  0.
- **Logic reads parameters, never phase names.**

`tools/arch_check.sh` checks these rules; CI fails on a violation.
Background: `docs/CONCEPT.md` §4.

## Domain rules

Every domain rule cites its entry in the rationale register
(`docs/RATIONALE.md`, "Rationale: RAT-xxx") and has a test. New rules get
an entry there and a row in `docs/INVARIANTS.md`. Simulator numbers are
either measured (with source) or marked as an assumption.

## Pull requests

1. **Branch** from `main`, one topic per PR.
2. **Test first:** a bug gets a test that shows it.
3. **CHANGELOG:** an entry under `[Unreleased]` (Added / Changed / Fixed /
   Security).
4. **Language:** code, comments, docs, commits and the PR in English.
5. **Approval:** CI green, review by `reviewer` and `qa`; additionally
   `security` for gateway, sensor truth, login or updates, and `ux` for UI
   changes.
6. **Merge:** squash only; the PR title and description become the commit
   message on `main`, so write them for someone reading the history
   (SD-027).

## Ideas and questions

Please use Discussions, not issues. An issue is created once an idea is
accepted.

## Conduct

Be kind, criticise the work and not the person: [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md).
