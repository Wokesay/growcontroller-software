# Versions, changelog, releases, updates

As of 2026-10-08. Basis: `produkt`, `regulatorik`, `software`, `kunde`
(agents of the product repository). An assessment, not legal advice.

## Versions

- **Self-describing (PD-037):** The name and description say what a
  version is and what it contains. No code names. The exact schema is open;
  proposal: SemVer with the channel in the name and a title that names the
  content.
- **SemVer** `MAJOR.MINOR.PATCH[-beta.N]` (draft E12). The version is in
  `VERSION`, `web/package.json`, `ha/app/config.yaml` and in the tag
  `vX.Y.Z`.
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

1. **Prepare:** `[Unreleased]` → `[X.Y.Z] – date`, bump `VERSION`,
   `web/package.json` and `ha/app/config.yaml`, PR, review (`reviewer`,
   `release`, and `security` for security topics), CI green. The app's
   documentation (`ha/app/DOCS.md`) and the release notes ask users to
   make a backup of the app before updating. With the
   app, the project owner runs part one of the acceptance test before
   "mergen" (SD-065): the release branch's server against a real Home
   Assistant (`docs/HOME_ASSISTANT.md`) for the password, the sensors, a
   restart with the same data folder and the checks the pull request
   lists for what the release brings; automatic updates of the app stay
   off on that Home Assistant (off unless switched on). On Windows the
   project owner also starts the release branch's simulator package (an
   artifact of its CI run) once by double click with a
   `growcontroller-daten` next to it that it may not read (Properties ›
   Security › deny Read): it opens, says "Data cannot be read" and leaves
   the folder as it is (SD-074). The package test does this on macOS and
   Linux (#214). Compare `.nvmrc`
   with the newest Node 22 security release
   (`https://nodejs.org/dist/index.json`, `"security": true`) and raise it
   in a pull request of its own if it is lower, together with the app
   image's `node` image (SD-070, SD-071). Tag right after
   the merge: Home
   Assistant's app store reads `config.yaml` from `main` and offers the
   new version before its image exists.
2. **Tag:** the project owner tags `vX.Y.Z` on `main` (only the
   repository admin can set `v*` tags, SD-027). The workflow
   `release.yml` (#29):
   - `verify`: the tag matches `VERSION` (and `web/package.json` and
     `ha/app/config.yaml` match it) and points at a commit on `main`.
   - `packages` and `build`, without write access and without a cache:
     the simulator for three platforms, the web app, the license notices,
     two SBOMs (CycloneDX: web app from npm, C++ libraries from
     `cmake/deps.cmake`), `SHA256SUMS` and the release notes from the
     changelog section.
   - `app-amd64` and `app-aarch64` (`app.yml`, SD-034), without write
     access and without a cache: the Home Assistant app image, each on a
     runner of its architecture, started and checked next to a stand-in
     for the Supervisor, saved with its checksum.
   - `check`, read-only and in every dry run too: the files and app
     images match their checksums, and the app image goes through
     `publish`'s own push steps into a registry on the runner, so the
     first tag is not their first run.
   - `publish`, the only job that can write: checks the files against
     `SHA256SUMS` and the app images against their checksums and labels,
     checks that the tag still points at the built commit, attests the
     files' build provenance, pushes the app image
     (`ghcr.io/wokesay/growcontroller-ha:X.Y.Z`, one name made from the
     two pushed images by digest, plus `X.Y.Z-amd64` and
     `X.Y.Z-aarch64`), attests it, checks the tag once more and creates
     the GitHub release. It runs no npm and builds nothing.
   - Pre-releases (`-beta`, `-proto`) are marked as pre-release.
   - A pull request that changes the release machinery runs everything
     except `publish` as a dry run.
   - All actions are GitHub's own, pinned by commit SHA
     (`tools/workflows.test.mjs`); Dependabot proposes updates after a
     cooldown of 7 days.
   - The web app's npm packages are pinned by their sha512 in
     `web/package-lock.json`, each resolving to its own record on the npm
     registry; Dependabot proposes updates after a cooldown of 7 days.
     Wherever they are installed (`tools/ci.sh`, the firmware job, the
     simulator packages, the release build, the app image),
     `npm audit signatures` runs right after `npm ci`: a missing or
     invalid registry signature or an invalid provenance attestation
     fails the build, a package without provenance passes (SD-058). It
     proves the registry's signed record of each installed version, not
     the safety of a release from a hijacked maintainer account; against
     that stand the cooldown and the lockfile pin.
     `tools/workflows.test.mjs` keeps the step and the registry addresses
     in place.
   - The npm that checks them comes with Node, so every job that sets up
     Node runs the exact one named in `.nvmrc`, not whatever the runner
     image brings (SD-070). It is raised by hand in a pull request of its
     own, with a `security` review; step 1 "Prepare" compares it with the
     newest Node 22 security release. The app image checks with the same
     Node, from the official `node` image pinned by digest, and prints the
     versions: `tools/app.test.mjs` fails when the image's tag names
     another Node than `.nvmrc`, the build when the Node it pulled is
     another (SD-071).
   - When a job with the check fails, a step that runs only then
     (`tools/signatures_report.mjs`) shows the end of npm's own log of
     the check, the only record of the failed run (or that the check
     passed and the job failed elsewhere), and asks npm again for each
     invalid or missing package with npm's reason. A check that passes
     when asked again is no clearance. Text from npm cannot act as a
     workflow command there. The step changes nothing and never replaces
     the check.
   - In `tools/ci.sh` (CI's `checks` job), before the packages are
     installed, `tools/check_provenance.mjs` finds for every package in the
     lockfile, every platform build included, the highest lower version
     that was published with provenance (SD-063). If the locked version has
     none, or has it from another source repository or repository id, the
     check fails, since a hijacked account typically publishes by hand.
     The release, the packages and the app image install the same lockfile
     after it passed there on `main`. A case accepted after review goes
     into `tools/provenance-exceptions.json` with its exact problem and the
     reason; an entry no locked package has any more fails too. Changes to
     that file or to the check need a `security` review.
   - Anyone can check a download; the check must name the release
     workflow and the tag, otherwise any workflow of the repository would
     pass:

     ```bash
     gh attestation verify <file> --repo Wokesay/growcontroller-software \
       --signer-workflow Wokesay/growcontroller-software/.github/workflows/release.yml \
       --source-ref refs/tags/vX.Y.Z --deny-self-hosted-runners
     ```

   - The app image is checked the same way:

     ```bash
     gh attestation verify oci://ghcr.io/wokesay/growcontroller-ha:X.Y.Z \
       --repo Wokesay/growcontroller-software \
       --signer-workflow Wokesay/growcontroller-software/.github/workflows/release.yml \
       --source-ref refs/tags/vX.Y.Z --deny-self-hosted-runners
     ```

   - **Once, after the first release with the app:** the project owner
     makes the package public (GitHub profile → Packages →
     `growcontroller-ha` → Package settings → Change visibility →
     Public). Home Assistant downloads it without a login. A public
     package cannot be made private again. Under Package settings →
     Manage Actions access, only this repository should be listed.
   - **Accepting a release with the app, part two** (part one is in step
     1; SD-065), right after `publish`:
     - `docker buildx imagetools inspect
       ghcr.io/wokesay/growcontroller-ha:X.Y.Z` lists linux/amd64 and
       linux/arm64;
     - on the owner's Home Assistant, with a backup of the previous
       version made first, the update from the store keeps password,
       sensors and history, and a restart keeps the data;
     - on a second host (a Raspberry Pi and, where possible, a PC) the
       app shows in the store, installs, opens its web UI, sets a
       password and reads sensors;
     - another device guessing the password six times does not lock out
       a phone that signed in (SD-051);
     - after the update from a version before #80 (the server ran as
       root), the app starts and reads values, so it finds the Supervisor
       in `/etc/hosts`; the host's log (Settings › System › Logs, Host)
       has no `apparmor="DENIED"` for `*_growcontroller`; restoring the
       app's backup and starting it again works;
     - a new backup holds the new version, and its entry for the app is
       not empty.

     If part two fails, a fixing release follows at once; the release
     notes and an issue tell users to keep automatic updates off until
     then, and users who made a backup before updating restore it. A
     reinstall is no way back: `main` carries the failed version.
   - **Never delete a published app image** (`ghcr.io/wokesay/growcontroller-ha:X.Y.Z`):
     restoring a backup of the app pulls that version's image again.
   - Merge a version change to `ha/app/config.yaml` only together with
     the release: `config.yaml` on `main` is what every installed app
     compares itself with (SD-034).
   - The SBOMs list what ships, not the build tools (SD-075). The web
     build fails when a module the bundler puts into the app is neither
     our source, the page nor a runtime package of the lockfile, and the
     web SBOM lists exactly those packages (`tools/sbom_web.mjs`, #201;
     `npm sbom --omit dev` left out `preact`, which a build tool has as a
     peer). It is built from the packages `npm ci` installed, and fails
     the step when npm and the lockfile disagree; as `npm sbom` checks
     the edges of every package, a broken peer of a build tool fails it
     too. Code the build tools write into our own modules or the page
     is ours as built by them. The C++ SBOM
     lists the header libraries from `cmake/deps.cmake`, not yet the
     statically linked compiler runtimes; the web app embedded in the
     simulator is in the web SBOM. The app image has no SBOM of its own
     yet (#81): it holds the same server code, the web app, and musl and the
     GCC runtime from Alpine 3.24, whose exact package versions the image
     names in `/licenses/BUILD_PACKAGES.txt`; image scanners see none of
     them in a static binary. The Alpine packages of the build are not
     pinned: each build takes the current ones of Alpine 3.24 (supported
     until 2028-06). Security fixes for musl, the GCC runtime,
     cpp-httplib and nlohmann/json are watched by hand. Dependabot updates
     the app's base images (pinned by digest; `tools/app.test.mjs` keeps
     the smoke test's stand-in on the Alpine digest and the web stage's
     `node` image at `.nvmrc`'s Node on the same Alpine, SD-071: a new
     digest of the `node` tag passes, a new Alpine digest fails until the
     stand-in follows, a newer Node until `.nvmrc` follows, a newer
     Alpine until the `node` tag and the stand-in follow; a new `node`
     digest is merged only after a review like a raise of `.nvmrc`, as
     what changed is not in the diff) and `reuse`
     with its dependencies and build backend
     (`tools/requirements-reuse*.txt`, installed with `--require-hashes`,
     wheels only except `reuse` itself, which is built from its hashed
     source without fetching anything else);
     it does not cover `cmake/deps.cmake`, the `espressif/idf` container
     (pinned by digest in `ci.yml`) and the `registry:2` service of the
     `check` job; they are updated by hand. `tools/workflows.test.mjs`
     refuses a tool installed by name alone or an image without a
     digest.
   - If `publish` fails after the release was created, delete the
     unfinished release (not the tag) and run the job again. A second run
     pushes the same app image again.
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
  `tools/arch_check.sh` rejects the sdkconfig options, also in the board
  profiles, that would burn
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

- **Support period (PD-182 to PD-185, SD-076):** security updates for 7
  years from the delivery of each board with our firmware, also for boards
  delivered before 2027-12-11. Stated per delivery year, ending in
  December ("delivered 2028: security updates until at least 12/2035");
  the web app shows the end date and a notice once it has passed (Art.
  13(19)). Every security update stays available for at least 10 years
  (Art. 13).
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
