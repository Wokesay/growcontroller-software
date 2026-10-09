## What changes?

<!-- Short, from the user's point of view. Link the issue or decision (SD-xxx). -->

## Safety and invariants

- [ ] Actuators only through the gateway, watchdog without an actuator path (`tools/arch_check.sh`)
- [ ] A missing value stays missing, never 0
- [ ] New domain rule? RAT entry in `docs/RATIONALE.md`, row in `docs/INVARIANTS.md` and test case named
- [ ] Safety relevant (pumps, inlet, login, updates)? Then a "Security" section in the CHANGELOG
- [ ] Gateway or cut-offs touched? Logic change → waits for "mergen"; no logic change → `security` confirms unchanged behaviour, tests named (SD-033)

## Tests

- [ ] `tools/ci.sh` green (needs `reuse`), for UI changes also `E2E=1 tools/ci.sh`
- [ ] Reviews: `reviewer` and `qa`; `security` for gateway, sensor truth, login or updates; `ux` for UI
- [ ] New logic has a unit or scenario test

## Licensing and language

<!-- REUSE-IgnoreStart -->
- [ ] New source files carry `SPDX-License-Identifier: AGPL-3.0-or-later`
<!-- REUSE-IgnoreEnd -->
- [ ] New dependencies are AGPL-compatible and listed in `THIRD_PARTY_NOTICES.md`
- [ ] Everything in English

## Changelog

- [ ] Entry under `[Unreleased]` (Added / Changed / Fixed / Security)
