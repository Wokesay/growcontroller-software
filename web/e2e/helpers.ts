// SPDX-License-Identifier: AGPL-3.0-or-later
import { test as base, expect, type Page, type APIRequestContext } from "@playwright/test";

export { expect };

// Every test also fails on an uncaught exception in the page and on a part
// the error boundary had to replace (SD-032): either could otherwise pass
// unnoticed where no step looks at that part of the page.
export const test = base.extend<{ appErrorGuard: void }>({
  appErrorGuard: [
    async ({ page }, use) => {
      const errors: string[] = [];
      page.on("pageerror", (e) => errors.push(e.message));
      await use();
      expect(errors, "uncaught exceptions in the page").toEqual([]);
      if (!page.isClosed()) await expect(page.getByText(/Dieser Teil lässt sich nicht anzeigen|This part can't be shown/)).toHaveCount(0);
    },
    { auto: true },
  ],
});

export async function scenario(request: APIRequestContext, name: "neu" | "stufe1" | "demo") {
  const r = await request.post("/api/v1/sim/scenario", { data: { name } });
  expect(r.ok()).toBeTruthy();
}

export async function login(page: Page, password = "demo-passwort") {
  await page.goto("/");
  await page.locator("input[name=password]").fill(password);
  await page.getByRole("button", { name: "Anmelden" }).click();
  await expect(page.getByTestId("watchdog")).toBeVisible();
}

export async function simSpeed(page: Page, speed: number) {
  await page.evaluate((s) => fetch("/api/v1/sim/speed", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ speed: s }) }), speed);
}
