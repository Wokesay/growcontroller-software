# Contributing

Thanks for your interest. In short: small PRs, a test first, everything
in English.

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
`THIRD_PARTY_NOTICES.md`.

## Develop

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
