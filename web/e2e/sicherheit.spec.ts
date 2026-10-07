// SPDX-License-Identifier: AGPL-3.0-or-later
// Zugangsschutz (EN 18031-1 ACM/AUM): ohne Anmeldung keine Daten, keine Aktoren.
import { expect, test } from "@playwright/test";
import { scenario } from "./helpers";

test("API ohne Sitzung: 401 für Zustand und Aktoren", async ({ request }) => {
  await scenario(request, "demo");
  const fresh = await request.storageState();
  expect(fresh.cookies.length).toBe(0);
  expect((await request.get("/api/v1/state")).status()).toBe(401);
  expect((await request.post("/api/v1/stop")).status()).toBe(401);
  expect((await request.post("/api/v1/dose", { data: { canister: "teil-a", ml: 2 } })).status()).toBe(401);
  expect((await request.get("/api/v1/info")).status()).toBe(200);
});

test("Falsches Passwort wird abgelehnt, Sicherheitskopfzeilen sind gesetzt", async ({ page }) => {
  const res = await page.goto("/");
  expect(res?.headers()["x-frame-options"]).toBe("DENY");
  expect(res?.headers()["content-security-policy"]).toContain("default-src 'self'");
  await page.locator("input[name=password]").fill("falsch-falsch");
  await page.getByRole("button", { name: "Anmelden" }).click();
  await expect(page.getByText("Passwort falsch")).toBeVisible();
});
