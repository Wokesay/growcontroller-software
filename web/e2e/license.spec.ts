// SPDX-License-Identifier: AGPL-3.0-or-later
// AGPL-3.0 §13: users who interact with the hub over the network are offered
// the source code of exactly this build, before and after login.
import { expect, login, scenario, test } from "./helpers";

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

test("A prototype says it is for testing only, on the sign-in and first-password page and in Settings; a Stable release does not (SD-096)", async ({ page, request }) => {
  await scenario(request, "demo");
  // The hub's version is a pre-release (0.1.0-proto.N)
  await page.goto("/");
  await page.evaluate(() => localStorage.setItem("gc.lang", "de"));
  await page.reload();
  await expect(page.getByTestId("testing-notice")).toHaveText("Nur zum Testen: Diese Version erfüllt nicht die Anforderungen der EU-Cyberresilienz-Verordnung (CRA).");
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  await expect(page.getByTestId("testing-notice")).toHaveText("For testing only: this version does not meet the requirements of the EU Cyber Resilience Act (CRA).");
  await page.locator("input[name=password]").fill("demo-passwort");
  await page.getByRole("button", { name: "Sign in" }).click();
  await expect(page.getByTestId("watchdog")).toBeVisible();
  await page.goto("/#/einstellungen");
  await expect(page.getByTestId("testing-notice")).toBeVisible();

  // A Stable version shows nothing; one with only build metadata neither; a missing one does
  const withVersion = async (version: string | null) => {
    await page.unroute(/\/api\/v1\/info(\?|$)/);
    await page.route(/\/api\/v1\/info(\?|$)/, async (route) => {
      const res = await route.fetch();
      const info = await res.json();
      if (version === null) delete info.version;
      else info.version = version;
      await route.fulfill({ response: res, json: info });
    });
    await page.reload();
    await expect(page.getByRole("heading", { name: "Display and info" })).toBeVisible();
  };
  await withVersion("0.2.0");
  await expect(page.getByTestId("testing-notice")).toHaveCount(0);
  await withVersion("0.2.0+20261022");
  await expect(page.getByTestId("testing-notice")).toHaveCount(0);
  await withVersion("0.2.0+build-7");  // a hyphen in the build part is no pre-release
  await expect(page.getByTestId("testing-notice")).toHaveCount(0);
  await withVersion("v0.2.0");  // not a version this rule can read
  await expect(page.getByTestId("testing-notice")).toBeVisible();
  await withVersion("0.2.0-beta.1");
  await expect(page.getByTestId("testing-notice")).toBeVisible();
  await withVersion(null);
  await expect(page.getByTestId("testing-notice")).toBeVisible();
  await page.unroute(/\/api\/v1\/info(\?|$)/);

  // Setting the first password: the language comes first (SD-014), then the notice
  await scenario(request, "neu");
  await page.evaluate(() => localStorage.removeItem("gc.lang"));
  await page.goto("/");
  await expect(page.getByTestId("first-lang")).toBeVisible();
  await expect(page.getByTestId("testing-notice")).toBeVisible();
  const order = await page.evaluate(() => {
    const lang = document.querySelector('[data-testid="first-lang"]')!;
    const notice = document.querySelector('[data-testid="testing-notice"]')!;
    return lang.compareDocumentPosition(notice) & Node.DOCUMENT_POSITION_FOLLOWING;
  });
  expect(order).toBeTruthy();
  await scenario(request, "demo");
});
