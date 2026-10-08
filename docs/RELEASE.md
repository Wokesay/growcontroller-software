# Versions, changelog, releases, updates

As of 2026-10-08. Basis: `produkt`, `regulatorik`, `software`, `kunde`
(agents of the product repository). An assessment, not legal advice.

## Versions

- **Self-describing (PD-037):** The name and description say what a
  version is and what it contains. No code names. The exact schema is open;
  proposal: SemVer with the channel in the name and a title that names the
  content.
- **SemVer** `MAJOR.MINOR.PATCH[-beta.N]` (draft E12). The version is in
  `VERSION`, `web/package.json` and in the tag `vX.Y.Z`.
- **MAJOR:** a break in configuration, API or bus protocol. Integrators and
  bus devices need this signal.
- **PATCH:** only bug and security fixes. This way security updates come
  separately from feature updates where feasible (CRA Annex I Part II).
- **Prototype phase:** `0.x.y-proto.N`. No compatibility promises.

## Channels (PD-036)

- **Stable (live):** firmware for the hub without any simulation code. It
  is excluded at build time, and the web app has no simulator panel. Live
  operation shows only real data.
- **Beta:** a pre-release for real devices, for testing, plus a simulator
  download to try things out (demo, time-lapse, faults).
- **Development:** a build per commit with all switches, e.g. 300x
  time-lapse, scenarios and fault injection.
- Ready-made simulator downloads for Windows, macOS and Linux exist in Beta
  and Development, not in Stable.
- A Raspberry Pi runs only the simulator and the optional companion, never
  the control (PD-038).

## Changelog

[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) with the
sections **Added, Changed, Fixed, Removed, Security**. Every PR adds an
entry under `[Unreleased]`.

- **"Security"** names the affected versions, severity, fix version and
  GHSA or CVE ID.
- **For the app**, every release gets a three-line summary:
  "New / Fixed / Please note" (`kunde`). The full changelog is embedded in
  the firmware (`GET /api/v1/changelog`) and readable in the app.

## Release process

1. **Prepare:** `[Unreleased]` → `[X.Y.Z] – date`, bump `VERSION` and
   `web/package.json`, PR, review (`reviewer`, `release`, and `security`
   for security topics), CI green.
2. **Tag:** the project owner tags `vX.Y.Z` on `main` (only the
   repository admin can set `v*` tags, SD-027). The workflow
   `release.yml` (#29):
   - `verify`: the tag matches `VERSION` and points at a commit on
     `main`.
   - `packages` and `build`, without write access and without a cache:
     the simulator for three platforms, the web app, the license notices,
     two SBOMs (CycloneDX: web app from npm, C++ libraries from
     `cmake/deps.cmake`), `SHA256SUMS` and the release notes from the
     changelog section.
   - `publish`, the only job that can write: checks the files against
     `SHA256SUMS`, attests their build provenance and creates the GitHub
     release. It runs no npm and builds nothing.
   - Pre-releases (`-beta`, `-proto`) are marked as pre-release.
   - A pull request that changes the release machinery runs everything
     except `publish` as a dry run.
   - All actions are GitHub's own, pinned by commit SHA
     (`tools/workflows.test.mjs`); Dependabot proposes updates after a
     cooldown of 7 days.
   - Anyone can check a download: `gh attestation verify <file> --repo
     Wokesay/growcontroller-software`.
   - The C++ SBOM lists the header libraries from `cmake/deps.cmake`, not
     the statically linked compiler runtimes; the web app embedded in the
     simulator is in the web SBOM. Dependabot does not cover
     `cmake/deps.cmake`, the `espressif/idf` container and `reuse`; they
     are updated by hand.
   - If `publish` fails after the release was created, delete the
     unfinished release (not the tag) and run the job again.
   - Simulator downloads belong only to Beta releases, not to Stable
     (PD-036); `release.yml` does not yet tell the channels apart (#19).
3. **Firmware** (once `firmware/` builds):
   - CI builds the ESP-IDF image and the SBOM (`idf.py sbom-create`).
     The Stable image is built without simulation code (PD-036).
   - **Signing happens offline** after manual approval by the project
     owner. The key is never in the repository and never unprotected in
     CI.
   - The manifest (version, channel, size, hash, signature, summary) goes
     into the release.
4. **Channels:** Beta right away, Stable after at least 7 days without
   findings (V). Development needs no release step; it is built per commit
   (PD-036).

## Updates on the hub (concept, a mock-up in the simulator)

- **Two app partitions** (A/B) with `otadata`. The new version starts as
  "pending verify".
- **Self-test** (bus, ports, storage, web server) confirms the new
  version. Otherwise the hub rolls back to the old one automatically.
- **Signature before installation:** at least signature verification
  without hardware Secure Boot (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`,
  to be tested on the S3). Signed updates and any Secure Boot must let the
  owner install their own firmware; no Secure Boot against the user
  (PD-022). Decided for the first devices (PD-043, SD-021): signature
  check in the firmware, no eFuses burned except the HMAC key for
  encrypted NVS (PD-047, SD-024), downgrade protection in software.
  `tools/arch_check.sh` rejects the sdkconfig options that would burn
  eFuses for Secure Boot, flash encryption, a disabled download mode or
  anti-rollback.
- **Web UI in the app image:** An update is atomic; a rollback takes the
  UI along.
- **Only in a safe state:** no job, no dosing (API: 409). Recipes,
  calibration values and settings are kept (customer view).
- **Ways:**
  - From the app, against the manifest of the GitHub releases.
  - Offline, by uploading a signed file.
  - For self-builders, a web installer (ESP Web Tools, Chrome/Edge on the
    desktop).
- **Update check:**
  - It can be turned off and sends no device ID.
  - A privacy notice is needed: the IP address goes to GitHub (§ 25
    TDDDG).
- **Security updates:**
  - Per CRA Annex I 2(c), "where applicable" automatic by default, with an
    easy opt-out, a notice and postponement.
  - For a dosing device only when idle.
  - Whether this is "applicable" for this device is for the project owner
    to decide (open).

## Support and obligations (CRA, from 2027-12-11; reporting obligation since 2026-09-11)

- **Support period:** at least 5 years (lower bound). It is stated at
  purchase as month and year. Every security update stays available for at
  least 10 years (Art. 13).
- **Reporting actively exploited vulnerabilities:** 24 h / 72 h / 14 days
  via the ENISA platform (Art. 14). This needs a deputy who can meet the
  24-hour deadline.
- **SBOM** belongs in the technical documentation; publishing it is not
  mandatory.
- **Exit plan at project end:** release the keys or offer an owner unlock;
  inform market surveillance and users in advance (Art. 13(23), PD-005).

Sources: report of `regulatorik` from 2026-10-06, search results only,
retrieved 2026-10-06:

- CRA, Regulation (EU) 2024/2847:
  https://eur-lex.europa.eu/legal-content/EN/TXT/HTML/?uri=OJ%3AL_202402847
- Commission guidance C(2026) 5252, summary:
  https://www.cyberresilienceact.eu/commission-guidance.html
- ENISA Single Reporting Platform: no URL in the report; look it up before
  use.
