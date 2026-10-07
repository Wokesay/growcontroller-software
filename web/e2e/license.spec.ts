// SPDX-License-Identifier: AGPL-3.0-or-later
// AGPL-3.0 §13: users who interact with the hub over the network are offered
// the source code of exactly this build, before and after login.
import { expect, test } from "@playwright/test";
import { login, scenario } from "./helpers";

const SOURCE = /^https:\/\/github\.com\/Wokesay\/growcontroller-software(\/tree\/[0-9A-Za-z._\-]+)?$/;

test("Source code link of the build on the login page and in settings", async ({ page, request }) => {
  await scenario(request, "demo");
  await page.goto("/");
  const onLogin = page.getByTestId("source-link");
  await expect(onLogin).toBeVisible();
  expect(await onLogin.getAttribute("href")).toMatch(SOURCE);

  await login(page);
  await page.goto("/#/einstellungen");
  const inSettings = page.getByTestId("source-link");
  await expect(inSettings).toBeVisible();
  const href = await inSettings.getAttribute("href");
  expect(href).toMatch(SOURCE);
  // CI passes the revision it builds; then the link must point to exactly it.
  const rev = process.env.GC_SOURCE_REV;
  if (rev) expect(href).toBe(`https://github.com/Wokesay/growcontroller-software/tree/${rev}`);
  else expect(href).toContain("/tree/"); // a build from a git checkout links its commit
});
