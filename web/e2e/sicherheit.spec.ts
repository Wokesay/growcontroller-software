// SPDX-License-Identifier: AGPL-3.0-or-later
// Zugangsschutz (EN 18031-1 ACM/AUM): ohne Anmeldung keine Daten, keine Aktoren.
import { expect, test } from "@playwright/test";
import { login, scenario } from "./helpers";

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

test("Damaged event messages leave the page and STOP usable (SD-032)", async ({ page, request }) => {
  await scenario(request, "demo");
  // Records as a tampered events.json could hold them: inherited keys, texts
  // that are no strings, nesting far beyond what the hub writes.
  // 10 000 levels, written as text: too deep for JSON.stringify here.
  const deep = '{"key":"ev.plain","text":"x","args":{"text":'.repeat(10000) + '"x"' + "}}".repeat(10000);
  const now = Math.floor(Date.now() / 1000);
  const ev = (id: number, title: unknown, text: unknown) => ({ id, ts: now - id, type: "system", severity: "info", title, text, data: {} });
  const amount = { key: "amount", text: "Teil A 40.0 ml", args: { name: "Teil A", ml: 40 } };
  const events = [
    ev(1, { key: "ev.contents", text: "In the tank: Teil A 40.0 ml", args: { done: [amount] } }, null),
    ev(2, { key: "constructor", text: "c" }, { key: "__proto__", text: "p" }),
    ev(3, { key: "toString", text: { toString: 1 } }, { key: "ev.plain", text: "t", args: { text: { key: 5, text: { toString: 1 } } } }),
    ev(4, { key: "ev.plain", text: "deep", args: { text: "DEEP" } }, { key: "ev.contents", text: "", args: { done: [[[[[[["x"]]]]]]] } }),
  ];
  const body = JSON.stringify({ events }).replace('"DEEP"', deep);
  await page.route(/\/api\/v1\/events\?/, (route) => route.fulfill({ body, contentType: "application/json" }));
  await login(page);
  await expect(page.getByTestId("event")).toHaveCount(4);
  await expect(page.getByText("Drin: Teil A 40,0 ml")).toBeVisible();  // a list of amounts in German
  await expect(page.getByText("Dieser Teil lässt sich nicht anzeigen")).toHaveCount(0);
  await expect(page.locator(".stop-btn")).toBeVisible();
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  await expect(page.getByTestId("event")).toHaveCount(4);
  await expect(page.getByText("In the tank: Teil A 40.0 ml")).toBeVisible();
  await expect(page.locator(".stop-btn")).toBeVisible();
});
