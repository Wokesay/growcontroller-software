# Security

## Reporting vulnerabilities – not in public

Please **never as a public issue**. While the repository is private:
e-mail the project owner (the address will be added here when the
repository goes public; until then, use direct contact). After that, use
GitHub **Private Vulnerability Reporting**:
`https://github.com/Wokesay/growcontroller-software/security/advisories/new`.

Helpful: the affected version, steps to reproduce (the simulator is often
enough), possible consequences. Especially important: anything that can
make a pump or valve switch unintentionally, and anything that bypasses
login.

## What you can expect from us

> **Draft.** The deadlines and commitments in this section apply only once
> the project owner approves them before the repository goes public.

| Step | Target |
|---|---|
| Acknowledgement of receipt | 3 working days |
| First assessment | 10 working days |
| Fix | as soon as possible; security fixes come as a separate PATCH release |
| Disclosure | after the fix, coordinated with you; at the latest 90 days after the report |

We report actively exploited vulnerabilities to the authorities under
Art. 14 CRA (in force since 2026-09-11) and inform affected users. We will
not take action against anyone who researches and reports in good faith.

## Supported versions

| Version | Security updates |
|---|---|
| 0.x (prototype) | no – do not use with real hardware |

The support period for sold devices will be set before the first sale (at
least 5 years, CRA Art. 13).

Background: [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md),
[`docs/RELEASE.md`](docs/RELEASE.md).
