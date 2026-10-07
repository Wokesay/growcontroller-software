// SPDX-License-Identifier: AGPL-3.0-or-later
import { expect, type Page, type APIRequestContext } from "@playwright/test";

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
