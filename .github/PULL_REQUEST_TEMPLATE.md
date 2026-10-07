## What changes?

<!-- Short, from the user's point of view. Link the issue or decision (SD-xxx). -->

## Safety and invariants

- [ ] Actuators only through the gateway, watchdog without an actuator path (`tools/arch_check.sh`)
- [ ] A missing value stays missing, never 0
- [ ] New domain rule? RAT entry in `docs/RATIONALE.md` and test case named
- [ ] Safety relevant (pumps, inlet, login, updates)? Then a "Security" section in the CHANGELOG

## Tests

- [ ] `tools/ci.sh` green, for UI changes also `E2E=1 tools/ci.sh`
- [ ] New logic has a unit or scenario test

## Licensing and language

<!-- REUSE-IgnoreStart -->
- [ ] New source files carry `SPDX-License-Identifier: AGPL-3.0-or-later`
<!-- REUSE-IgnoreEnd -->
- [ ] New dependencies are AGPL-compatible and listed in `THIRD_PARTY_NOTICES.md`
- [ ] Everything in English

## Changelog

- [ ] Entry under `[Unreleased]` (Added / Changed / Fixed / Security)
