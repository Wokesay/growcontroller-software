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

Once devices are placed on the market (sold or lent), we report actively
exploited vulnerabilities under Art. 14 CRA (applies since 2026-09-11) to
the coordinating CSIRT and ENISA through the single reporting platform
and inform affected users; until then we do so voluntarily. A second
reporting channel besides GitHub follows before devices are offered.

We will not take action against anyone who researches and reports in good
faith: on their own installation or the simulator only, without accessing
third-party devices or data, without denial of service, and keeping the
details confidential until disclosure.

## Supported versions

| Version | Security updates |
|---|---|
| 0.x (prototype) | no – do not use with real hardware |

The support period for sold devices will be set before the first sale (at
least 5 years, CRA Art. 13).

Background: [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md),
[`docs/RELEASE.md`](docs/RELEASE.md).
