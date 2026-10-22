<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Feedback for testers

Thank you for trying growcontroller. Your view as a first-time user is
what we need most: where it is unclear, slow or wrong. Not tried it yet?
Start with [GETTING_STARTED.md](GETTING_STARTED.md).

## What we would like to know

1. Where did you get stuck or have to guess?
2. Which words did you not understand?
3. Did the app ever show something you knew was wrong?
4. What would you need before letting it run your tank?
5. Phone or computer, which browser and system?

Short answers are fine. One sentence helps more than none.

## Where each kind of report goes

| You have … | Send it to |
|---|---|
| Answers to the questions, ideas, opinions | [GitHub Discussions](https://github.com/Wokesay/growcontroller-software/discussions), category "Tester feedback" |
| A bug you can make happen again | the [bug form](https://github.com/Wokesay/growcontroller-software/issues/new?template=bug.yml), or "Report a problem" in the app (below) |
| A device or sensor you tested | the [device form](https://github.com/Wokesay/growcontroller-software/issues/new?template=device.yml); see [COMPATIBILITY.md](COMPATIBILITY.md) |
| A security vulnerability | never in public: a [private report](https://github.com/Wokesay/growcontroller-software/security/advisories/new), as [SECURITY.md](../SECURITY.md) says |

To start a post in the category directly:
[new tester feedback](https://github.com/Wokesay/growcontroller-software/discussions/new?category=tester-feedback).
Not sure where something belongs? Post it there; we move it.

All of these need a GitHub account. Issues and Discussions are public.

## Reporting a bug from the app

1. Open **Settings**, card **Report a problem**.
2. Under **What happened?**, describe briefly what went wrong.
3. Choose **Create diagnostic package**. You should see **Report no.
   GC-…**: that is your report ID.
4. Choose **Report publicly on GitHub**. The bug form opens with your
   description, the version and the report ID filled in.
5. Pick where it happened (the simulator or the Home Assistant app), add
   the steps, and submit.

Did a pump run although it should not have, or not switch off? Tick the
safety box in the form and report it right away.

## What to include

- **Version:** Settings › Display and info, row **Software**.
- **Where:** the simulator or the Home Assistant app.
- **Steps:** what you did, one step per line, so we can repeat it.
- **Expected and seen:** what you expected, and what happened instead.
- **Device:** phone or computer, browser and system, with versions if
  you know them.
- **Report ID** (GC-…), if you used "Report a problem".
- A screenshot helps. Crop the browser's address bar.

## What never to post

Issues and Discussions are public. Never post:

- passwords, including your Home Assistant password;
- tokens or keys (also not from ESPHome YAML: the `api` key, the `ota`
  password);
- Wi-Fi details;
- the address of your Home Assistant;
- your location.

## The diagnostic package

"Create diagnostic package" puts together version, settings, states and
the last 72 hours of events: no passwords, sessions or Wi-Fi
credentials. It stays on your device.

- Send it only when we ask for it in the issue.
- Look at it first: **View contents** shows its beginning in the app;
  the file from **Download** has all of it, as plain text (JSON) for
  any text editor.
- Then attach that file to the issue.

There is no private way to send it yet. If you see something in it
that should not be public, do not attach it; tell us in the issue.

## What happens next

- We read everything and reply in the thread.
- Accepted ideas become issues; you can follow them there.
- A fixed bug is named in the release notes.
- We confirm a private report within 7 days
  ([SECURITY.md](../SECURITY.md)).
- We do not promise dates. A fix comes with a later pre-release.
