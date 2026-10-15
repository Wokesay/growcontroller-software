# Security

## Reporting vulnerabilities – not in public

Please **never as a public issue**. Report vulnerabilities only through
GitHub **Private Vulnerability Reporting** (you need a GitHub account;
there is no e-mail address for reports):
`https://github.com/Wokesay/growcontroller-software/security/advisories/new`.

Helpful: the affected version, steps to reproduce (the simulator is often
enough), possible consequences. Especially important: anything that can
make a pump or valve switch unintentionally, and anything that bypasses
login.

## What you can expect from us

| Step | Target |
|---|---|
| Acknowledgement of receipt | 7 days |
| First assessment | 30 days |
| Fix | as soon as possible; for 0.x the fix goes to `main` and into the next pre-release |
| Disclosure | after the fix, coordinated with you; at the latest 90 days after the report |

At the latest from the first offer of a device or the first device lent,
we report under Art. 14 CRA (applies since 2026-09-11) every actively
exploited vulnerability and every severe incident that affects the
security of the product. The report goes to the coordinating CSIRT and
ENISA through the single reporting platform: an early warning within 24
hours of learning of it, a notification within 72 hours, then a final
report. We inform affected users and tell them what they can do. Until
then we do so voluntarily. A second reporting channel besides GitHub follows before
devices are offered.

We will not take action against anyone who researches and reports in good
faith: on their own installation or the simulator only, without accessing
third-party devices or data, without denial of service, and keeping the
details confidential until disclosure.

## Advisories

Every fixed vulnerability gets a GitHub Security Advisory in this
repository
(`https://github.com/Wokesay/growcontroller-software/security/advisories`),
with a CVE that we request from GitHub, or the CVE of the component it
comes from. A vulnerability here is a flaw that someone without the needed
rights can trigger: a device in the network, a crafted file or another
input. The advisory is published with the release that contains the
fix, once that release has passed its acceptance. If that release takes
longer than 30 days after the fix is on `main`, the advisory is
published anyway, with a workaround.

The release notes list every security item under "Security", with what
it is, the affected versions, the impact, the severity and the remedy;
safety and robustness fixes that are not a vulnerability are listed
there too, without an advisory (SD-077).

## Supported versions

| Version | Security updates |
|---|---|
| 0.x (prototype) | only the latest pre-release, including the Home Assistant app: a fix comes in the next pre-release; do not dose or switch real hardware with it |

**Support period for devices:** every board with our firmware gets
security updates for 7 years from the delivery of that board, also boards
delivered before 2027-12-11. The end is stated per delivery year and falls
in December: a board delivered in 2028 gets security updates until at
least 12/2035. The web app will show the end date for your device and a
notice once it has passed (CRA Art. 13(19)).

Background: [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md),
[`docs/RELEASE.md`](docs/RELEASE.md).
