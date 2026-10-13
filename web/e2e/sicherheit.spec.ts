// SPDX-License-Identifier: AGPL-3.0-or-later
// Zugangsschutz (EN 18031-1 ACM/AUM): ohne Anmeldung keine Daten, keine Aktoren.
import { request as httpRequest } from "node:http";
import { expect, login, scenario, test } from "./helpers";

// A sign-in from another local address: Linux has all of 127.0.0.0/8 on its
// loopback, so 127.0.0.2 is another device to the simulator.
function signInFrom(baseURL: string, localAddress: string, password: string): Promise<number> {
  const { hostname, port } = new URL(baseURL);
  const data = JSON.stringify({ password });
  return new Promise((resolve, reject) => {
    const req = httpRequest(
      { host: hostname, port, path: "/api/v1/auth/login", method: "POST", localAddress, headers: { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(data) } },
      (res) => {
        res.resume();
        res.on("end", () => resolve(res.statusCode ?? 0));
      },
    );
    req.on("error", reject);
    req.end(data);
  });
}

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

test("A part that fails to draw shows a notice; the page around it and STOP stay usable (SD-032)", async ({ page, request }) => {
  await scenario(request, "demo");
  // A record that is no object at all: the event list itself fails to draw.
  await page.route(/\/api\/v1\/events\?/, (route) => route.fulfill({ body: '{"events":[null]}', contentType: "application/json" }));
  await login(page);
  await expect(page.getByText("Dieser Teil lässt sich nicht anzeigen")).toBeVisible();
  await expect(page.getByRole("button", { name: "Seite neu laden" })).toBeVisible();
  await expect(page.getByTestId("watchdog")).toBeVisible();  // the rest of the overview
  await page.locator(".stop-btn").click();
  await expect(page.getByText("Not-Halt auslösen?")).toBeVisible();
  await page.getByRole("button", { name: "Abbrechen" }).click();
  await page.unroute(/\/api\/v1\/events\?/);
  await page.getByRole("button", { name: "Seite neu laden" }).click();
  await expect(page.getByTestId("event").first()).toBeVisible();
  await expect(page.getByText("Dieser Teil lässt sich nicht anzeigen")).toHaveCount(0);
});

test("Someone guessing from another device does not lock out this browser (SD-051)", async ({ page, request, baseURL }) => {
  test.skip(process.platform !== "linux", "needs 127.0.0.2 on the loopback (Linux)");
  await scenario(request, "demo");
  await login(page);  // this browser has signed in now
  await page.context().clearCookies();  // signed out on this browser
  const tries: number[] = [];
  for (let i = 0; i < 6; i++) tries.push(await signInFrom(baseURL!, "127.0.0.2", "falsch"));
  expect(tries).toEqual([401, 401, 401, 401, 401, 429]);
  expect(await signInFrom(baseURL!, "127.0.0.3", "demo-passwort")).toBe(429);  // never signed in: shares the lockout
  await login(page);
  await scenario(request, "demo");  // no lockout left for the next cases
});

test("A page left open renews its session, so a Stop still has one; a session the hub no longer knows is named on the sign-in page (SD-063)", async ({ page, request }) => {
  await scenario(request, "demo");
  await page.clock.install();
  await login(page);
  // Every 10 minutes the page asks, and the hub sends the cookie for another 12 hours
  const renewed = page.waitForResponse((r) => r.url().endsWith("/api/v1/auth/session") && r.request().method() === "GET");
  await page.clock.fastForward("10:01");
  const res = await renewed;
  expect(await res.headerValue("set-cookie")).toMatch(/^gc_session=[0-9a-f]+; Path=\/; HttpOnly; SameSite=Strict; Max-Age=43200$/);
  expect(await res.headerValue("cache-control")).toBe("no-store");  // the answer carries the session key
  // No answer (Wi-Fi not back yet): still signed in, nothing said
  await page.route(/\/api\/v1\/auth\/session$/, (route) => route.abort());
  await page.clock.fastForward("10:01");
  await expect(page.getByTestId("watchdog")).toBeVisible();
  await expect(page.getByTestId("session-expired")).toHaveCount(0);
  // The session is gone (a restart of the hub, a password change elsewhere): a phone that wakes up
  // asks at once, as its timers slept
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await page.route(/\/api\/v1\/auth\/session$/, (route) => route.fulfill({ json: { authenticated: false, hasPassword: true } }));
  await page.evaluate(() => document.dispatchEvent(new Event("visibilitychange")));
  await expect(page.getByTestId("session-expired")).toBeVisible();
  await page.unrouteAll({ behavior: "ignoreErrors" });
});
