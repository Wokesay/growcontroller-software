// SPDX-License-Identifier: AGPL-3.0-or-later
// Operation of a set-up system: overview, Stop, faults, history, functions.
import { expect, login, scenario, test } from "./helpers";

test.beforeAll(async ({ request }) => {
  await scenario(request, "demo");
});

test("Übersicht zeigt Überwachung, Messwerte und Regelzeilen", async ({ page }) => {
  await login(page);
  await expect(page.getByTestId("metric-pH")).toBeVisible();
  await expect(page.getByTestId("metric-EC")).toBeVisible();
  await expect(page.locator(".ctl")).toHaveCount(4);
  await expect(page.locator(".sim-tag").first()).toBeVisible();
  // The hub sends English with keys; a German page shows German (SD-032),
  // with numbers in the page language.
  await expect(page.getByTestId("watchdog")).toContainText(/Alles in Ordnung|Problem/);
  const lines = () => page.locator(".ctl .line").allTextContents().then((l) => l.join(" "));
  await expect.poll(lines).toMatch(/Regelt|Ruht|Wartet|Gesperrt|Freigabe nötig|Aus|Läuft|Füllt/);
  await expect.poll(lines).toMatch(/\d,\d/);
  expect(await lines()).not.toMatch(/Controlling|Resting|Waiting|Blocked|Needs release|Running|Filling/);
  // Events too: the hub stores key and values, the page shows German.
  await expect(page.getByTestId("event").filter({ hasText: "Angemeldet" }).first()).toBeVisible();
});

test("English page: monitoring and control lines come in English from the hub (SD-032)", async ({ page }) => {
  await login(page);
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  await expect(page.getByTestId("watchdog")).toContainText(/Everything OK|problem/);
  await expect(page.getByTestId("watchdog")).not.toContainText(/Prüfungen|Probleme/);
  const lines = () => page.locator(".ctl .line").allTextContents().then((l) => l.join(" "));
  await expect.poll(lines).toMatch(/Controlling|Resting|Waiting|Blocked|Needs release|Running|Filling|Switched off/);
  await expect.poll(lines).toMatch(/\d\.\d/);
  expect(await lines()).not.toMatch(/Regelt|Ruht|Wartet|Gesperrt|Freigabe nötig|Läuft|Füllt/);
  await expect(page.getByTestId("event").filter({ hasText: "Signed in" }).first()).toBeVisible();
  await expect(page.getByTestId("event").filter({ hasText: "Angemeldet" })).toHaveCount(0);
});

test("Stop switches everything off and can be resumed", async ({ page }) => {
  await login(page);
  await page.getByTitle("Stopp: alle Pumpen und Ausgänge aus").click();
  await page.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(page.getByText("Stopp aktiv.", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Fortsetzen", exact: true }).click();
  await expect(page.getByText("Stopp aktiv.", { exact: true })).toHaveCount(0);
  await expect(page.locator(".toast").last()).toHaveText("Automatik fortgesetzt");
});

test("Resume while the inlet stays latched says so, not only that automation resumed (SD-087)", async ({ page }) => {
  await login(page);
  // as after a start whose saved state was unknown: the inlet waits for a person
  await page.route(/\/api\/v1\/state(\?|$)/, async (route) => {
    const res = await route.fetch();
    const st = await res.json();
    st.latches = { ...st.latches, "inlet.fault": { at: st.now, why: { key: "why.state_unknown", text: "Saved state unknown" } } };
    await route.fulfill({ response: res, json: st });
  });
  await page.getByTitle("Stopp: alle Pumpen und Ausgänge aus").click();
  await page.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(page.getByText("Stopp aktiv.", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Fortsetzen", exact: true }).click();
  await expect(page.locator(".toast.info").last()).toHaveText("Automatik fortgesetzt. Der Zulauf bleibt zu, bis du ihn unter Tank & Regelung freigibst.");
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("A Stop reaches the hub from a browser without AbortSignal.timeout (before iOS 16)", async ({ page }) => {
  await page.addInitScript(() => {
    delete (AbortSignal as { timeout?: unknown }).timeout;
  });
  await login(page);
  expect(await page.evaluate(() => "timeout" in AbortSignal)).toBe(false);
  await page.getByTitle("Stopp: alle Pumpen und Ausgänge aus").click();
  await page.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(page.getByText("Stopp aktiv.", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Fortsetzen", exact: true }).click();
  await expect(page.getByText("Stopp aktiv.", { exact: true })).toHaveCount(0);
});

test("A Stop that does not reach the hub says so, and the dialog stays open to try again", async ({ page }) => {
  await login(page);
  await page.route(/\/api\/v1\/stop$/, (route) => route.abort());
  await page.getByTitle("Stopp: alle Pumpen und Ausgänge aus").click();
  const dialog = page.getByRole("dialog");
  await dialog.getByRole("button", { name: "Alles stoppen" }).click();
  // In the dialog, with what to do in an emergency, and a button to try again
  // The demo has smart plugs: unplugging the hub leaves them as they are
  await expect(dialog.getByRole("alert")).toContainText("Stopp nicht bestätigt – keine Antwort vom Hub. Zieh im Notfall den Netzstecker des Hubs und schalte Geräte an Steckdosen direkt aus.");
  await expect(dialog.getByRole("button", { name: "Noch einmal versuchen" })).toBeFocused();
  // A hub that does not answer in time is no confirmation either: the button says it is sending, then the same
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await page.route(/\/api\/v1\/stop$/, () => undefined);  // never answered
  await dialog.getByRole("button", { name: "Noch einmal versuchen" }).click();
  await expect(dialog.getByRole("button", { name: "Stopp wird gesendet …" })).toBeDisabled();
  await expect(dialog.getByRole("alert")).toContainText("Stopp nicht bestätigt", { timeout: 10_000 });
  await expect(dialog.getByRole("button", { name: "Noch einmal versuchen" })).toBeEnabled();
  // Opened again, the dialog starts fresh
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await dialog.getByRole("button", { name: "Abbrechen" }).click();
  await page.getByTitle("Stopp: alle Pumpen und Ausgänge aus").click();
  await expect(dialog.getByRole("button", { name: "Alles stoppen" })).toBeVisible();
  await expect(dialog.getByRole("alert")).toBeEmpty();
  // A switch that gets no answer is not silent either
  await dialog.getByRole("button", { name: "Abbrechen" }).click();
  await page.goto("/#/klima");
  await page.route(/\/api\/v1\/roles\/[^/]+\/switch$/, (route) => route.abort());
  await page.locator('[data-testid^="switch-"]').first().click();
  await expect(page.getByText("Der Hub antwortet nicht. Ist er eingeschaltet und im selben Netz?")).toBeVisible();
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("A Stop that arrives after all closes its dialog for good; a Resume that gets no answer says so", async ({ page }) => {
  await login(page);
  await page.route(/\/api\/v1\/stop$/, (route) => route.abort());
  await page.getByTitle("Stopp: alle Pumpen und Ausgänge aus").click();
  const dialog = page.getByRole("dialog");
  await dialog.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(dialog.getByRole("alert")).toContainText("Stopp nicht bestätigt");
  // The Stop reaches the hub after all (late, or from another device): the dialog goes
  const origin = new URL(page.url()).origin;
  expect((await page.request.post("/api/v1/stop", { data: {}, headers: { Origin: origin } })).ok()).toBe(true);
  await expect(page.getByText("Stopp aktiv.", { exact: true })).toBeVisible();
  await expect(dialog).toHaveCount(0);
  // A Resume without an answer says so, and the hub stays stopped
  await page.route(/\/api\/v1\/resume$/, (route) => route.abort());
  await page.getByRole("button", { name: "Fortsetzen", exact: true }).click();
  await expect(page.getByText("Der Hub antwortet nicht. Ist er eingeschaltet und im selben Netz?")).toBeVisible();
  await expect(page.getByText("Stopp aktiv.", { exact: true })).toBeVisible();
  // After a Resume that arrives, the old dialog does not come back
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await page.getByRole("button", { name: "Fortsetzen", exact: true }).click();
  await expect(page.getByText("Stopp aktiv.", { exact: true })).toHaveCount(0);
  await expect(dialog).toHaveCount(0);
});

test("A Stop closed while it is being sent still says when it failed; a refusal starts with the Stop's text", async ({ page }) => {
  await login(page);
  await page.route(/\/api\/v1\/stop$/, () => undefined);  // never answered
  await page.getByTitle("Stopp: alle Pumpen und Ausgänge aus").click();
  const dialog = page.getByRole("dialog");
  await dialog.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(dialog.getByRole("button", { name: "Stopp wird gesendet …" })).toBeDisabled();
  await page.keyboard.press("Escape");
  await expect(dialog).toHaveCount(0);
  await expect(page.locator(".toast.error")).toContainText("Stopp nicht bestätigt – keine Antwort vom Hub. Zieh im Notfall den Netzstecker des Hubs", { timeout: 10_000 });
  // Opened again, nothing old shows; a hub that refuses names its reason after the Stop's text
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await page.route(/\/api\/v1\/stop$/, (route) => route.fulfill({ status: 500 }));
  await page.getByTitle("Stopp: alle Pumpen und Ausgänge aus").click();
  await expect(dialog.getByRole("alert")).toBeEmpty();
  await dialog.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(dialog.getByRole("alert")).toContainText("Stopp nicht bestätigt – Fehler 500. Zieh im Notfall");
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Settings refuse a time zone that does not exist and save one in its usual spelling", async ({ page }) => {
  await login(page);
  await page.goto("/#/einstellungen");
  const tz = page.locator("input[name=timezone]");
  const card = page.locator(".card", { has: tz });
  let saves = 0;
  await page.route(/\/api\/v1\/system$/, (route) => {
    saves++;
    return route.continue();
  });
  await tz.fill("Mars/Olympus");
  await tz.blur();
  await expect(card.getByText("Diese Zeitzone gibt es nicht. Schreib sie wie Europe/Berlin.")).toBeVisible();
  // An offset that newer browsers know is no zone name for the hub: refused at the field too
  await tz.fill("+01:00");
  await expect(card.getByText("Diese Zeitzone gibt es nicht. Schreib sie wie Europe/Berlin.")).toBeVisible();
  await card.getByRole("button", { name: "Speichern" }).click();
  await card.getByRole("button", { name: "Speichern" }).click();
  await tz.fill("asia/tokyo");
  await card.getByRole("button", { name: "Speichern" }).click();
  await expect(page.getByText("Gespeichert").first()).toBeVisible();
  await expect(tz).toHaveValue("Asia/Tokyo");
  expect((await (await page.request.get("/api/v1/config")).json()).system.timezone).toBe("Asia/Tokyo");
  expect(saves, "the zone that does not exist was not sent").toBe(1);
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await tz.fill("Europe/Berlin");
  await card.getByRole("button", { name: "Speichern" }).click();
  await expect(tz).toHaveValue("Europe/Berlin");
});

test("A switch or choice in Settings that gets no answer says so", async ({ page }) => {
  await login(page);
  await page.goto("/#/einstellungen");
  await page.route(/\/api\/v1\/system$/, (route) => route.abort());
  await page.locator('label.toggle[title="Automatisch nach Updates suchen"]').click();
  await expect(page.getByText("Der Hub antwortet nicht. Ist er eingeschaltet und im selben Netz?")).toBeVisible();
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("The badge on a wide screen goes by the age of the values, as the phone's bar", async ({ page }) => {
  await login(page);
  const badge = page.locator(".topbar .pill").first();
  // Without the live stream the polls stand in for it: still live after more than 10 s
  await page.route(/\/api\/v1\/events\/stream/, (route) => route.abort());
  await page.reload();
  await expect(badge).toHaveText("live");
  await page.waitForTimeout(12_000);
  await expect(badge).toHaveText("live");
  // No values at all any more: disconnected
  await page.route(/\/api\/v1\/state(\?|$)/, (route) => route.abort());
  await expect(badge).toHaveText("getrennt", { timeout: 20_000 });
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await expect(badge).toHaveText("live", { timeout: 20_000 });
});

test("History: the time axis follows the page's language and the 24-hour clock", async ({ page }) => {
  // The axis is drawn on a canvas: what it writes is collected here
  await page.addInitScript(() => {
    const seen: string[] = [];
    (window as unknown as { axisTexts: string[] }).axisTexts = seen;
    const fill = CanvasRenderingContext2D.prototype.fillText;
    CanvasRenderingContext2D.prototype.fillText = function (...args: Parameters<typeof fill>) {
      seen.push(String(args[0]));
      return fill.apply(this, args);
    };
  });
  await login(page);
  await page.goto("/#/verlauf");
  await expect(page.locator(".chart").first()).toBeVisible();
  const texts = () => page.evaluate(() => (window as unknown as { axisTexts: string[] }).axisTexts);
  await expect.poll(async () => (await texts()).filter((s) => /^\d{2}:\d{2}$/.test(s)).length).toBeGreaterThan(0);
  expect((await texts()).filter((s) => /\b(am|pm|AM|PM)\b|\d+\/\d+/.test(s))).toEqual([]);
});

test("A Stop after the session has run out: the sign-in page says it was not carried out", async ({ page }) => {
  await login(page);
  await page.route(/\/api\/v1\/stop$/, (route) => route.fulfill({ status: 401, json: { error: { key: "auth.required", text: "Bitte anmelden." } } }));
  await page.getByTitle("Stopp: alle Pumpen und Ausgänge aus").click();
  await page.getByRole("dialog").getByRole("button", { name: "Alles stoppen" }).click();
  await expect(page.getByTestId("session-expired")).toContainText("Hast du gerade etwas angeklickt, hat der Hub es nicht ausgeführt");
  await expect(page.getByTestId("session-expired")).toContainText("Zieh im Notfall den Netzstecker des Hubs und schalte Geräte an Steckdosen direkt aus.");
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("The German mix page shows the hub's notes in German", async ({ page }) => {
  await login(page);
  await page.goto("/#/mischen");
  await page.locator("input[name=water]").fill("40");
  const preview = page.getByTestId("mix-preview");
  await expect(preview.locator("table")).toBeVisible();
  await expect(preview).toContainText("pH zuletzt");
  await expect(preview).not.toContainText("pH comes last");
});

test("Sprungsperre der pH-Sonde wird sichtbar mit Grund", async ({ page }) => {
  await login(page);
  const st = await (await page.request.get("/api/v1/sim")).json();
  const head = st.world.ports.find((p: any) => p.class === "head_ph_ec");
  await page.request.post("/api/v1/sim/fault", { data: { device: head.id, fault: "jump" } });
  await expect(page.getByTestId("metric-pH").getByText("Sprungsperre")).toBeVisible({ timeout: 30_000 });
  await page.goto("/#/tank");
  // The simulator's jump is a rise, never the hub's own dose, which only lowers pH (RAT-041, #162)
  await expect(page.getByText(/Der Wert stieg von .+ auf .+\. Hat jemand ohne Pflegemodus am Tank gearbeitet\? Wenn nicht, prüf die Sonde mit einer Handmessung/).first()).toBeVisible();
  await expect(page.getByText(/Der Wert fiel von/)).toHaveCount(0);
  // the lock list names the reading in words, never the internal key (#143)
  await expect(page.getByText(/pH im Tank sprang unerwartet – die Sperre löst sich nach 15/).first()).toBeVisible();
  await expect(page.getByText(/jump\.tank\./)).toHaveCount(0);
  await page.request.post("/api/v1/sim/fault", { data: { device: head.id, fault: "none" } });
});

test("Funktionen zeigen, was fehlt, und Verlauf zeichnet Kurven", async ({ page }) => {
  await login(page);
  await page.goto("/#/funktionen");
  await expect(page.getByTestId("fn-ph_control")).toBeVisible();
  await page.getByTestId("fn-climate_watch").click();
  await expect(page.getByTestId("fn-climate_watch")).not.toContainText("Dafür brauchst du");  // Demo hat einen Klima-Kopf
  await expect(page.getByTestId("fn-climate_watch")).toContainText("Lufttemperatur zugeordnet");  // the checklist and the catalog's names in the page's language (SD-083)
  await expect(page.locator("body")).not.toContainText(/Next:|You need|Ready, switched off|Switched on/);  // the summaries too
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  await page.getByTestId("fn-climate_watch").click();
  await expect(page.getByTestId("fn-climate_watch")).toContainText("Air temperature assigned");
  await expect(page.getByTestId("fn-climate_watch")).not.toContainText("zugeordnet");
  await expect(page.locator("body")).not.toContainText(/Als Nächstes|Dafür brauchst du|Bereit, ausgeschaltet|Eingeschaltet/);
  await page.evaluate(() => localStorage.setItem("gc.lang", "de"));
  await page.reload();
  await page.goto("/#/verlauf");
  await expect(page.locator(".chart canvas").first()).toBeVisible();
  await expect(page.locator(".u-legend").first()).toContainText("Zeit");
  await expect(page.getByTestId("event").first()).toBeVisible();
});

test("Numbers follow the page language: range with a comma, field re-formats on a language switch (PD-035)", async ({ page }) => {
  await login(page);
  await page.goto("/#/funktionen");
  await page.getByTestId("fn-ph_control").click();
  await expect(page.getByTestId("fn-ph_control")).toContainText("0,3–3");
  // No thousands separator: "1.000" typed back would be 1 L.
  await page.goto("/#/funktionen?f=refill");
  await expect(page.getByTestId("fn-refill")).toContainText("1–1000");
  await page.goto("/#/einstellungen");
  await page.getByLabel("Grenze je manueller Gabe").fill("2,5");
  try {
    const saved = page.waitForResponse((r) => r.url().endsWith("/api/v1/system") && r.request().method() === "PUT");
    await page.getByRole("tab", { name: "English" }).click();
    await saved;
    await expect(page.getByLabel("Limit per manual dose")).toHaveValue("2.5");
    await page.goto("/#/funktionen?f=refill");
    await expect(page.getByTestId("fn-refill")).toContainText("1–1000");
  } finally {
    // The switch also sets the hub's language; later tests expect German.
    const res = await page.request.put("/api/v1/system", { data: { language: "de" } });
    expect(res.ok()).toBeTruthy();
  }
});

test("A device without a name of its own is named by its class, and roles by their names, in the page's language (SD-083)", async ({ page }) => {
  await login(page);
  const named = await page.request.patch("/api/v1/devices/PHEC-3F2A91", { data: { name: "" } });
  expect(named.ok()).toBeTruthy();
  try {
    await page.goto("/#/geraete");
    await expect(page.getByTestId("device-PHEC-3F2A91").getByText("Sensorkopf pH/EC").first()).toBeVisible();
    await page.goto("/#/geraete?tab=zuordnung");
    await expect(page.getByRole("cell", { name: "pH im Tank", exact: true })).toBeVisible();
    await expect(page.locator("option", { hasText: "Sensorkopf pH/EC" }).first()).toBeAttached();
    await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
    await page.goto("/#/geraete");
    await page.reload();
    await expect(page.getByTestId("device-PHEC-3F2A91").getByText("pH/EC sensor head").first()).toBeVisible();
    await expect(page.locator("main")).not.toContainText("Sensorkopf");
    await page.goto("/#/geraete?tab=zuordnung");
    await expect(page.getByRole("cell", { name: "pH in the tank", exact: true })).toBeVisible();
    await expect(page.locator("option", { hasText: "pH/EC sensor head" }).first()).toBeAttached();
    await expect(page.locator("main")).not.toContainText("pH im Tank");
  } finally {
    await page.evaluate(() => localStorage.setItem("gc.lang", "de"));
    await page.request.patch("/api/v1/devices/PHEC-3F2A91", { data: { name: "pH/EC Tank" } });
  }
});

test("Fehlsteckung: Pumpe am Hub-Anschluss wird mit Klartext gemeldet", async ({ page }) => {
  await login(page);
  await page.request.post("/api/v1/sim/plug", { data: { port: 2, class: "pump_cap" } });
  await page.goto("/#/geraete");
  // In the page's language, the tooltip too (SD-083)
  const line = "Pumpe direkt am Hub – steck sie auf den Dosierblock";
  await expect(page.getByTestId("ports").getByText(line)).toBeVisible();
  await expect(page.getByTestId("ports").locator(`[title="${line}"]`)).toHaveCount(1);
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  await expect(page.getByTestId("ports").getByText("Pump plugged straight into the hub – plug it onto the dosing block")).toBeVisible();
  await expect(page.getByTestId("ports").getByText(line)).toHaveCount(0);
  await page.evaluate(() => localStorage.setItem("gc.lang", "de"));
  await page.request.post("/api/v1/sim/unplug", { data: { port: 2 } });
});

test("Problem melden erzeugt ein Diagnosepaket ohne Geheimnisse", async ({ page }) => {
  await login(page);
  await page.goto("/#/einstellungen");
  await page.getByRole("button", { name: "Diagnosepaket erstellen" }).click();
  await expect(page.getByText(/Vorgang GC-/)).toBeVisible();
  // The files the browser saves carry the name Doseph (#312)
  await expect(page.locator(".side .brand")).toContainText("Doseph");
  const [diagFile] = await Promise.all([page.waitForEvent("download"), page.getByRole("button", { name: "Herunterladen" }).click()]);
  expect(diagFile.suggestedFilename()).toMatch(/^doseph-diagnostics-GC-[^/]+\.json$/);
  const [backup] = await Promise.all([page.waitForEvent("download"), page.getByRole("link", { name: "Einstellungen sichern" }).click()]);
  expect(backup.suggestedFilename()).toBe("doseph-config.json");
  const diag = await (await page.request.get("/api/v1/diagnostics")).text();
  expect(diag).not.toContain("salt");
  expect(diag).not.toContain("demo-passwort");
  // The public issue is English even from a German page (PD-034) and carries
  // only title, labels and the short body – no diagnostic data.
  const href = await page.getByRole("link", { name: "Öffentlich auf GitHub melden" }).getAttribute("href");
  const url = new URL(href!);
  expect([...url.searchParams.keys()].sort()).toEqual(["body", "labels", "title"]);
  expect(url.searchParams.get("body")).toContain("**Description**");
  expect(url.searchParams.get("body")).not.toContain("Beschreibung");
  expect(url.searchParams.get("body")).not.toContain("events");
});

test("English page: sign-in, recipes and settings speak English (#18)", async ({ page }) => {
  await page.goto("/");
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  await page.locator("input[name=password]").fill("demo-passwort");
  await page.getByRole("button", { name: "Sign in" }).click();
  await expect(page.getByTestId("watchdog")).toBeVisible();
  await page.goto("/#/rezepte");
  await expect(page.getByRole("heading", { name: "Bottles", exact: true })).toBeVisible();
  // Numbers use a decimal point on an English page (PD-035): CalMag 0.6 ml/L
  await page.locator(".card.flat", { has: page.getByRole("heading", { name: "Wachstum", exact: true }) }).getByTitle("Edit").click();
  const values = await page.getByRole("dialog").locator("input").evaluateAll((els) => els.map((e) => (e as HTMLInputElement).value));
  expect(values).toContain("0.6");
  expect(values).not.toContain("0,6");
  await page.getByRole("dialog").getByRole("button", { name: "Cancel" }).click();
  await page.goto("/#/einstellungen");
  await page.getByRole("button", { name: "Create diagnostic package" }).click();
  await expect(page.getByText(/Report no\. GC-/)).toBeVisible();
});

test("English page: overview, mixing, tank, functions and history speak English (#18)", async ({ page }) => {
  await login(page);
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  await expect(page.getByRole("heading", { name: "Control", exact: true })).toBeVisible();
  await page.goto("/#/mischen");
  await expect(page.getByRole("heading", { name: "Mix nutrient solution" })).toBeVisible();
  await expect(page.getByRole("heading", { name: "Manual dose" })).toBeVisible();
  await page.goto("/#/tank");
  await expect(page.getByRole("heading", { name: "Grow cycle and phases" })).toBeVisible();
  await page.goto("/#/funktionen");
  await expect(page.getByText("Stage 0 – Mixing")).toBeVisible();
  await page.goto("/#/verlauf");
  await expect(page.getByText("Dashed lines show doses.", { exact: false })).toBeVisible();
});

test("English page: devices and the simulator panel speak English (#18)", async ({ page }) => {
  await login(page);
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.goto("/#/geraete");
  await page.reload();
  await expect(page.getByRole("tab", { name: "Expand" })).toBeVisible();
  await page.getByRole("button", { name: "Simulator" }).click();
  await expect(page.getByText("Speed", { exact: true })).toBeVisible();
  await expect(page.getByText("Faults", { exact: true })).toBeVisible();
});

test("Rezept-Vorlage: Vorschau, Kanister zuordnen, Rezept anlegen", async ({ page }) => {
  await login(page);
  await page.goto("/#/rezepte");
  await page.getByTestId("templates").getByRole("button", { name: /Wachstum Wo\. 1–4 \(nach Athena A01\.004\)/ }).click();
  await expect(page.getByRole("dialog")).toContainText("Grow B");
  // Manufacturer data with edition and date, not binding (PD-030)
  await expect(page.getByRole("dialog")).toContainText("Herstellerangabe, unverbindlich");
  await expect(page.getByRole("dialog")).toContainText("Ausgabe A01.004");
  await expect(page.getByRole("button", { name: "Rezept anlegen" })).toBeDisabled();
  await page.locator("select[name=map-b]").selectOption({ label: "Teil B" });
  await page.locator("select[name=map-a]").selectOption({ label: "Teil A" });
  await page.locator("select[name=map-calmag]").selectOption({ label: "CalMag" });
  await page.getByRole("button", { name: "Rezept anlegen" }).click();
  await expect(page.getByText("Rezept „Wachstum Wo. 1–4 (nach Athena A01.004)“ angelegt")).toBeVisible();
  await expect(page.getByRole("dialog")).toHaveCount(0);
});

test("The CSV links ask for the page's format (RAT-016, SD-083)", async ({ page }) => {
  await login(page);
  await page.goto("/#/verlauf");
  const de = page.getByTestId("history-csv");
  await expect(de).toHaveAttribute("href", /&lang=de$/);
  const deRes = await page.request.get((await de.getAttribute("href"))!);
  expect((await deRes.text()).startsWith("zeit_utc;")).toBe(true);
  expect(deRes.headers()["content-disposition"]).toContain("doseph-verlauf.csv");
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.goto("/#/einstellungen");
  await page.reload();
  const link = page.getByTestId("settings-csv");
  await expect(link).toHaveAttribute("href", /lang=en/);
  const res = await page.request.get((await link.getAttribute("href"))!);
  const body = await res.text();
  expect(body.startsWith("time_utc,")).toBe(true);
  expect(body.split("\n")[1]).toMatch(/^\d+,(\d+\.\d{3})?(,|$)/);  // a time, then a number with a decimal point or a gap
  expect(res.headers()["content-disposition"]).toContain("doseph-history.csv");
});

test("Recipe template: an English page stores the English name, even on a German hub (#32)", async ({ page }) => {
  await login(page);
  // This browser chooses English; the hub stays German.
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.goto("/#/rezepte");
  await page.reload();
  await page.getByTestId("templates").getByRole("button", { name: /Vegetative wk 1–4 \(per Athena A01\.004\)/ }).click();
  await page.locator("select[name=map-b]").selectOption({ label: "Teil B" });
  await page.locator("select[name=map-a]").selectOption({ label: "Teil A" });
  await page.locator("select[name=map-calmag]").selectOption({ label: "CalMag" });
  await page.getByRole("button", { name: "Create recipe" }).click();
  await expect(page.getByText('Recipe "Vegetative wk 1–4 (per Athena A01.004)" created')).toBeVisible();
  const cfg = await (await page.request.get("/api/v1/config")).json();
  expect(cfg.system.language).toBe("de");
  const made = cfg.recipes.find((r: { name: string }) => r.name === "Vegetative wk 1–4 (per Athena A01.004)");
  expect(made?.note).toContain("Manufacturer data, not binding");
});

test("Messwert-Kacheln bei Sensorausfall: nichts ragt aus der Kachel", async ({ page }) => {
  await login(page);
  for (const device of ["LVL-77B210", "PHEC-3F2A91"]) await page.request.post("/api/v1/sim/fault", { data: { device, fault: "offline" } });
  await page.request.post("/api/v1/sim/speed", { data: { speed: 60 } });
  const outside = () =>
    page.evaluate(() =>
      [...document.querySelectorAll(".metric")].filter((el) => {
        const box = el.getBoundingClientRect();
        return [...el.querySelectorAll("*")].some((c) => {
          const r = c.getBoundingClientRect();
          return r.width > 0 && (r.right > box.right + 1 || r.bottom > box.bottom + 1 || r.left < box.left - 1);
        });
      }).length,
    );
  for (const hash of ["/"]) {
    await page.goto(hash);
    await expect(page.getByText("Sensor liefert nicht").first()).toBeVisible({ timeout: 30_000 });
    expect(await outside()).toBe(0);
  }
  for (const device of ["LVL-77B210", "PHEC-3F2A91"]) await page.request.post("/api/v1/sim/fault", { data: { device, fault: "none" } });
  await page.request.post("/api/v1/sim/speed", { data: { speed: 1 } });
});

test("Menu by topic: headings group the pages in the side menu (SD-056)", async ({ page }) => {
  await login(page);
  const side = page.locator("aside.side nav");
  const groups: [string, string[]][] = [
    ["Nährlösung", ["Mischen", "Tank & Regelung", "Rezepte & Kanister", "Bewässerung"]],
    ["Raum", ["Klima", "Licht"]],
    ["System", ["Geräte", "Funktionen", "Einstellungen"]],
  ];
  for (const [head, links] of groups) await expect(side.getByRole("group", { name: head }).getByRole("link")).toHaveText(links);
  await expect(side.getByRole("link")).toHaveText(["Übersicht", "Verlauf", ...groups.flatMap(([, links]) => links)]);
  await side.getByRole("link", { name: "Bewässerung" }).click();
  await expect(page).toHaveURL(/#\/bewaesserung/);
  await expect(side.getByRole("link", { name: "Bewässerung" })).toHaveAttribute("aria-current", "page");
  // An unknown address shows the Overview, and the menu marks it
  await page.goto("/#/gibtsnicht");
  await expect(side.getByRole("link", { name: "Übersicht" })).toHaveAttribute("aria-current", "page");
});

test("Climate, light and watering say they work by hand and what to keep running; with nothing assigned, what to connect (SD-056)", async ({ page }) => {
  await login(page);
  const keep = {
    climate: ["Klimaregler", "Befeuchter und Entfeuchter am Hub gehen bei jedem Neustart aus"],
    light: ["Zeitschaltuhr", "nicht in eine WLAN-Steckdose des Hubs"],  // a hub plug is off after every restart (R6)
    irrigation: ["Bewässerungssteuerung", "weder auf Trockenlauf noch auf pH und EC"],  // the dry-run lock and the pH/EC check cover only a pump the hub switches (RAT-068, SD-093)
  };
  for (const [path, area] of [["klima", "climate"], ["licht", "light"], ["bewaesserung", "irrigation"]] as const) {
    await page.goto(`/#/${path}`);
    const note = page.getByTestId(`manual-${area}`);
    await expect(note).toContainText("Handbetrieb:");
    for (const text of keep[area]) await expect(note).toContainText(text);
  }
  // The demo has no watering pump: the page says what to connect, and where; on the plug it can be chosen
  await expect(page.locator('[data-testid^="output-"]')).toHaveCount(0);
  const empty = page.getByTestId("area-empty-irrigation");
  await expect(empty).toContainText("Keine Gießpumpe zugeordnet");
  await expect(empty).toContainText("schaltbare WLAN-Steckdose");
  await expect(empty).not.toContainText("Noch keine WLAN-Steckdose");
  await empty.getByRole("link", { name: "Zuordnen" }).click();
  await expect(page).toHaveURL(/#\/geraete\?tab=geraete$/);
  await expect(page.locator('[data-testid^="outlets-"] select').first().locator("option", { hasText: "Gießpumpe" })).toHaveCount(1);
  // Climate and light have devices: their switches, no empty state; a role not assigned shows as such
  await page.goto("/#/klima");
  await expect(page.getByTestId("output-zone.exhaust")).toBeVisible();
  await expect(page.getByTestId("output-zone.dehumidifier")).toContainText("Nicht zugeordnet");
  await expect(page.getByTestId("area-empty-climate")).toHaveCount(0);
  await page.goto("/#/licht");
  await expect(page.getByTestId("output-zone.light")).toBeVisible();
  await expect(page.getByTestId("area-empty-light")).toHaveCount(0);
});

test("Bereiche: Klima zeigt Messwerte und VPD, Geräte lassen sich manuell schalten", async ({ page }) => {
  await login(page);
  await page.goto("/#/klima");
  await expect(page.getByTestId("metric-VPD")).toBeVisible();
  // Assume no state (the Stop test before switches everything off): toggle twice
  const row = page.getByTestId("output-zone.exhaust");
  const btn = page.getByTestId("switch-zone.exhaust");
  const before = (await btn.textContent())?.trim();
  const after = before === "Einschalten" ? "Ausschalten" : "Einschalten";
  await btn.click();
  await expect(btn).toHaveText(after);
  await btn.click();
  await expect(btn).toHaveText(before ?? "");
  await expect(row).toContainText("Abluft");
  await page.goto("/#/licht");
  await expect(page.getByTestId("output-zone.light")).toContainText("Leiste");
  await page.goto("/#/verlauf");
  await expect(page.getByText("VPD (Luft)")).toBeVisible();
  await page.goto("/");
  await expect(page.getByText("Schaltausgänge")).toBeVisible();
});

test("Calibration: a failed run is cancelled however the window is closed, Escape included", async ({ page }) => {
  const pump = "CAP-1F02C1";
  const api = (path: string, body?: unknown) =>
    page.evaluate(
      async ([p, b]) => (await fetch(`/api/v1${p}`, b === undefined ? {} : { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(b) })).json(),
      [path, body] as const,
    );
  await login(page);
  await page.goto(`/#/geraete?pump=${pump}`);
  await page.reload();
  const dialog = page.getByRole("dialog");
  await dialog.getByRole("button", { name: "Pumpe starten" }).click();
  await api("/sim/fault", { device: pump, fault: "blocked" });
  for (let i = 0; i < 10; i++) await api("/sim/advance", { hours: 0.0013 });
  await expect(dialog.locator(".banner.bad")).toBeVisible();  // the run failed
  await api("/sim/fault", { device: pump, fault: "none" });
  await page.keyboard.press("Escape");
  await expect(dialog).toHaveCount(0);
  await expect.poll(async () => (await api("/state")).lastJob?.state).toBe("aborted");
});

test("A value calibrated outside the hub shows for display only, with its age, not as a fault (RAT-025)", async ({ page }) => {
  // The Home Assistant trial's state, made from the simulator's: pH calibrated elsewhere
  await page.route(/\/api\/v1\/events\/stream/, (route) => route.abort());
  await page.route(/\/api\/v1\/state(\?|$)/, async (route) => {
    const res = await route.fetch();
    const st = await res.json();
    st.readings["tank.ph"] = {
      ...st.readings["tank.ph"],
      quality: "uncalibrated",
      usable: false,
      ageS: 20,
      reason: { key: "truth.external", text: "Außerhalb des Hubs kalibriert – vom Hub nicht geprüft", args: {} },
    };
    await route.fulfill({ response: res, json: st });
  });
  await login(page);
  const tile = page.getByTestId("metric-pH");
  await expect(tile.getByText("anderswo kalibriert")).toBeVisible();
  await expect(tile.getByText("Außerhalb des Hubs kalibriert – vom Hub nicht geprüft")).toBeVisible();
  await expect(tile.getByText("nicht kalibriert")).toHaveCount(0);
  await expect(tile).not.toHaveClass(/\bbad\b/);
  await expect(tile.locator(".metric-foot")).toContainText("vor 20 s");
  expect(await tile.locator(".metric-foot").textContent()).toContain("20\u00a0s");  // number and unit stay on one line (#106)
  await expect(page.getByTestId("metric-EC").locator(".spark")).toHaveCount(1);  // the history has arrived
  await expect(tile.locator(".spark")).toHaveCount(1);  // its trend too: the history records it (SD-039)
  await page.unrouteAll({ behavior: "ignoreErrors" });  // a poll may still be in flight
});

test("Calibration: a window closed half way ends the calibration on the hub, and opened again it starts at the first buffer (RAT-042)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  const cancels: unknown[] = [];
  page.on("request", (r) => {
    if (r.url().includes("/api/v1/probe") && r.method() === "POST" && r.postDataJSON()?.action === "cancel") cancels.push(r.postDataJSON());
  });
  await page.goto("/#/geraete");
  const open = () => page.getByRole("button", { name: "pH kalibrieren" }).first().click();
  await open();
  const dialog = page.getByRole("dialog");
  await dialog.getByTestId("probe-take").click();
  await expect(dialog.locator(".ws.cur")).toContainText("Puffer 4,00");
  await page.keyboard.press("Escape");
  await expect.poll(() => cancels.length).toBe(1);
  await expect.poll(async () => Object.keys((await (await page.request.get("/api/v1/state")).json()).probeCalibration ?? {}).length).toBe(0);
  // Opened again: from the first buffer, as the hub has it, and it saves once the probe has settled in each buffer
  await open();
  await expect(dialog.locator(".ws.cur")).toContainText("Puffer 7,00");
  await expect(dialog.locator("strong").first()).toHaveText(/^(6,9|7,0)/);
  await dialog.getByTestId("probe-take").click();
  await expect(dialog.locator(".ws.cur")).toContainText("Puffer 4,00");
  await expect(dialog.locator("strong").first()).toHaveText(/^(3,9|4,0)/);
  await dialog.getByTestId("probe-take").click();
  await dialog.getByRole("button", { name: "Kalibrierung speichern" }).click();
  await expect(page.getByText("Kalibrierung gespeichert")).toBeVisible();
  expect(cancels.length).toBe(1);  // a saved one is not cancelled
});

for (const lang of ["de", "en"] as const) {
  test(`Recipes: removing a bottle asks first and names the consequence; cancelling keeps it (${lang}, #120)`, async ({ page, request }) => {
    await scenario(request, "demo");
    await login(page);
    await page.evaluate((l) => localStorage.setItem("gc.lang", l), lang);
    const name = lang === "de" ? "Reiniger" : "Cleaner";
    const made = await page.evaluate(async (n) => {
      const r = await fetch("/api/v1/canisters", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ name: n, capacityMl: 500 }) });
      return r.ok;
    }, name);
    expect(made).toBe(true);
    await page.goto("/#/rezepte");
    await page.reload();
    const row = page.locator(".rtable tbody tr").filter({ hasText: name });
    await expect(row).toHaveCount(1);
    const texts =
      lang === "de"
        ? { button: `„${name}“ entfernen`, remove: "Entfernen", title: "Kanister entfernen?", text: `Damit entfernst du „${name}“ samt Vorrat. Das kannst du nicht rückgängig machen.`, cancel: "Abbrechen", done: `Kanister „${name}“ entfernt` }
        : { button: `Remove "${name}"`, remove: "Remove", title: "Remove bottle?", text: `This removes "${name}" and its stock. You can't undo this.`, cancel: "Cancel", done: `Bottle "${name}" removed` };
    await row.getByRole("button", { name: texts.button }).click();
    // A confirmation: a screen reader reads the consequence with the title
    const dialog = page.getByRole("alertdialog", { name: texts.title });
    await expect(dialog).toBeVisible();
    await expect(dialog).toHaveAccessibleDescription(texts.text);
    await dialog.getByRole("button", { name: texts.cancel }).click();
    await expect(dialog).toHaveCount(0);
    await expect(row).toHaveCount(1);  // cancelling keeps it
    await expect(row.getByRole("button", { name: texts.button })).toBeFocused();
    await row.getByRole("button", { name: texts.button }).click();
    await dialog.getByRole("button", { name: texts.remove, exact: true }).click();
    await expect(row).toHaveCount(0);
    await expect(page.getByText(texts.done)).toBeVisible();
    await expect(page.locator("#add-bottle")).toBeFocused();  // the row is gone: on to adding a bottle
  });
}

test("Recipes: saving a bottle keeps a dose booked while its editor was open; a stock below zero shows in red (SD-066)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  const state = async () => (await page.request.get("/api/v1/state")).json();
  expect((await page.request.post("/api/v1/canisters/calmag/stock", { data: { ml: 3 } })).ok()).toBeTruthy();
  await page.goto("/#/rezepte");
  const row = (name: string) => page.locator(".rtable tbody tr").filter({ hasText: name });
  await row("CalMag").getByTitle("Bearbeiten").click();
  const dialog = page.getByRole("dialog");
  await expect(dialog.locator("input[name=canister-name]")).toHaveValue("CalMag");
  // while the editor is open, a hand dose books 5 ml from the 3 ml left
  await expect.poll(async () => { const s = await state(); return s.job === null && s.dosing === null; }, { timeout: 60_000 }).toBe(true);
  expect((await page.request.post("/api/v1/dose", { data: { canister: "calmag", ml: 5 } })).ok()).toBeTruthy();
  await expect.poll(async () => { const s = await state(); return s.job === null && s.dosing === null; }, { timeout: 60_000 }).toBe(true);
  expect((await state()).stock.calmag).toBeLessThan(-1);
  // only the name changes: the amount left it opened with is not sent back
  await dialog.locator("input[name=canister-name]").fill("CalMag neu");
  await dialog.getByRole("button", { name: "Speichern" }).click();
  await expect(dialog).toHaveCount(0);
  expect((await state()).stock.calmag).toBeLessThan(-1);
  await expect(row("CalMag neu").locator("span.num", { hasText: /^-\d+ ml$/ })).toHaveAttribute("style", /--bad/);
  // opened again: the field is empty and says what the hub counts
  await row("CalMag neu").getByTitle("Bearbeiten").click();
  await expect(dialog.locator("input[name=canister-stock]")).toHaveValue("");
  await expect(dialog).toContainText(/Rechnerisch -\d+ ml\. Trag ein, was wirklich im Kanister ist\./);
});

test("Recipes: a bottle a recipe uses says where and how to take it out, a pair as a whole, and the way works (#120)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  await page.goto("/#/rezepte");
  const row = (name: string) => page.locator(".rtable tbody tr").filter({ hasText: name });
  const blocked = page.getByRole("alertdialog", { name: "Kanister wird noch gebraucht" });
  // Part A of a pair in two recipes: both parts, both recipes, no removal on offer
  await row("Teil A").getByRole("button", { name: "„Teil A“ entfernen" }).click();
  await expect(blocked).toHaveAccessibleDescription(
    "„Teil A“ bildet mit „Teil B“ ein Paar und steckt in den Rezepten „Wachstum“ und „Blüte“. Nimm dort beide heraus (Mengen leeren, speichern) oder lösch die Rezepte. Danach kannst du den Kanister entfernen.",
  );
  await expect(blocked.getByRole("button", { name: "Entfernen", exact: true })).toHaveCount(0);
  await blocked.getByRole("button", { name: "Verstanden" }).click();
  await expect(blocked).toHaveCount(0);
  // With one recipe left, the dialog opens it
  const gone = await page.evaluate(async () => {
    const cfg = await (await fetch("/api/v1/config")).json();
    const bloom = cfg.recipes.find((r: { name: string }) => r.name === "Blüte");
    return (await fetch(`/api/v1/recipes/${bloom.id}`, { method: "DELETE" })).ok;
  });
  expect(gone).toBe(true);
  await page.reload();
  await row("CalMag").getByRole("button", { name: "„CalMag“ entfernen" }).click();
  await expect(blocked).toHaveAccessibleDescription(
    "„CalMag“ steckt im Rezept „Wachstum“. Nimm den Kanister dort heraus (Menge leeren, speichern) oder lösch das Rezept. Danach kannst du ihn entfernen.",
  );
  await blocked.getByRole("button", { name: "Rezept bearbeiten" }).click();
  const editor = page.getByRole("dialog", { name: "Rezept „Wachstum“" });
  await expect(editor).toBeVisible();
  // Following the advice works: the amount cleared and saved, the bottle can go
  await editor.locator(".step").filter({ hasText: "CalMag" }).getByRole("textbox").fill("");
  await editor.getByRole("button", { name: "Speichern" }).click();
  await expect(editor).toHaveCount(0);
  await expect(row("CalMag").getByRole("button", { name: "„CalMag“ entfernen" })).toBeFocused();  // the next step of the advice
  await page.keyboard.press("Enter");
  await page.getByRole("alertdialog", { name: "Kanister entfernen?" }).getByRole("button", { name: "Entfernen", exact: true }).click();
  await expect(row("CalMag")).toHaveCount(0);
  await expect(page.locator("#add-bottle")).toBeFocused();  // not the next row's button, which took the removed row's place
  // A pair in one recipe: both parts out, and the recipe still has a step
  await row("Teil A").getByRole("button", { name: "„Teil A“ entfernen" }).click();
  await expect(blocked).toHaveAccessibleDescription(
    "„Teil A“ bildet mit „Teil B“ ein Paar und steckt im Rezept „Wachstum“. Nimm dort beide heraus (Mengen leeren, speichern) oder lösch das Rezept. Danach kannst du den Kanister entfernen.",
  );
  await blocked.getByRole("button", { name: "Abbrechen" }).click();
  await expect(blocked).toHaveCount(0);
});

test("Recipes: the removal dialog's text follows the pump, the recipes and the language (#120)", async ({ page, request }) => {
  await scenario(request, "demo");
  // pH−'s pump reads as not calibrated once the page asks again (state without the live stream)
  let uncalibrated = false;
  await page.route(/\/api\/v1\/events\/stream/, (route) => route.abort());
  await page.route(/\/api\/v1\/state(\?|$)/, async (route) => {
    const res = await route.fetch();
    const st = await res.json();
    if (uncalibrated) for (const d of st.devices) if (d.id === "CAP-1F02D9") d.info = { ...d.info, flowMlPerMin: null };
    await route.fulfill({ response: res, json: st });
  });
  await login(page);
  await page.goto("/#/rezepte");
  const row = (name: string) => page.locator(".rtable tbody tr").filter({ hasText: name });
  const ask = page.getByRole("alertdialog", { name: "Kanister entfernen?" });
  const blocked = page.getByRole("alertdialog", { name: "Kanister wird noch gebraucht" });
  // A calibrated pump stays calibrated
  await row("pH−").getByRole("button", { name: "„pH−“ entfernen" }).click();
  await expect(ask).toHaveAccessibleDescription(
    "Damit entfernst du „pH−“ samt Vorrat. Die Pumpe darauf bleibt eingemessen und ist frei für einen anderen Kanister. Das kannst du nicht rückgängig machen.",
  );
  await ask.getByRole("button", { name: "Abbrechen" }).click();
  // A bottle without a pair in two recipes: both named, no recipe to open
  await row("CalMag").getByRole("button", { name: "„CalMag“ entfernen" }).click();
  await expect(blocked).toHaveAccessibleDescription(
    "„CalMag“ steckt in den Rezepten „Wachstum“ und „Blüte“. Nimm den Kanister dort heraus (Menge leeren, speichern) oder lösch die Rezepte. Danach kannst du ihn entfernen.",
  );
  await expect(blocked.getByRole("button", { name: "Rezept bearbeiten" })).toHaveCount(0);
  await blocked.getByRole("button", { name: "Verstanden" }).click();
  // A pump without a calibration is only freed
  uncalibrated = true;
  await page.reload();
  await expect(row("pH−").getByRole("link", { name: "einmessen" })).toBeVisible();
  await row("pH−").getByRole("button", { name: "„pH−“ entfernen" }).click();
  await expect(ask).toHaveAccessibleDescription(
    "Damit entfernst du „pH−“ samt Vorrat. Die Pumpe darauf ist dann frei für einen anderen Kanister. Das kannst du nicht rückgängig machen.",
  );
  await ask.getByRole("button", { name: "Abbrechen" }).click();
  // In English, the pair as the bottle's
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  await row("Teil A").getByRole("button", { name: 'Remove "Teil A"' }).click();
  await expect(page.getByRole("alertdialog", { name: "Bottle still in use" })).toHaveAccessibleDescription(
    '"Teil A" forms a pair with "Teil B" and is in the recipes "Wachstum" and "Blüte". Take both out of each (clear their amounts, save) or delete the recipes. Then you can remove the bottle.',
  );
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Recipes: removing one bottle of a pair says the other is then no longer paired, and saving still works (#147)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  const freed = await page.evaluate(async () => {
    const cfg = await (await fetch("/api/v1/config")).json();
    const gone = await Promise.all(cfg.recipes.map((r: { id: string }) => fetch(`/api/v1/recipes/${r.id}`, { method: "DELETE" })));
    return gone.every((r) => r.ok);
  });
  expect(freed).toBe(true);
  await page.goto("/#/rezepte");
  await page.reload();
  const row = (name: string) => page.locator(".rtable tbody tr").filter({ hasText: name });
  await expect(row("Teil B")).toContainText("Paar AB");
  await row("Teil A").getByRole("button", { name: "„Teil A“ entfernen" }).click();
  const dialog = page.getByRole("alertdialog", { name: "Kanister entfernen?" });
  await expect(dialog).toHaveAccessibleDescription(
    "Damit entfernst du „Teil A“ samt Vorrat. Die Pumpe darauf bleibt eingemessen und ist frei für einen anderen Kanister. „Teil B“ ist danach ohne Paar. Das kannst du nicht rückgängig machen.",
  );
  await dialog.getByRole("button", { name: "Entfernen", exact: true }).click();
  await expect(row("Teil A")).toHaveCount(0);
  await expect(row("Teil B")).not.toContainText("Paar AB");
  const saved = await page.evaluate(async () => {
    const r = await fetch("/api/v1/recipes", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ name: "Nur B", steps: [{ canister: "teil-b", mlPerL: 1 }] }) });
    return r.status;
  });
  expect(saved).toBe(200);
});

test("Recipes: taking a bottle out of its pair says the other is then no longer paired, and both stay usable (#152)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  await page.goto("/#/rezepte");
  const row = (name: string) => page.locator(".rtable tbody tr").filter({ hasText: name });
  await row("Teil A").getByTitle("Bearbeiten").click();
  const editor = page.getByRole("dialog", { name: "Kanister „Teil A“" });
  const save = editor.getByRole("button", { name: "Speichern" });
  await expect(editor.locator("#bottle-unpairs")).toHaveCount(0);
  // A pH bottle has no pair: the type alone takes it out
  const kind = editor.locator("label.field", { hasText: "Typ" }).locator("select");
  await kind.selectOption("ph_down");
  await expect(editor.locator("#bottle-unpairs")).toHaveText("Nach dem Speichern ist „Teil B“ ohne Paar.");
  await kind.selectOption("nutrient");
  await expect(editor.locator("#bottle-unpairs")).toHaveCount(0);
  await editor.locator("input[name=canister-pair]").fill("");
  await expect(editor.locator("#bottle-unpairs")).toHaveText("Nach dem Speichern ist „Teil B“ ohne Paar.");
  await expect(save).toHaveAccessibleDescription("Nach dem Speichern ist „Teil B“ ohne Paar.");
  await save.click();
  await expect(editor).toHaveCount(0);
  await expect(row("Teil A")).not.toContainText("Paar AB");
  await expect(row("Teil B")).not.toContainText("Paar AB");
  // A save the hub checks as a whole still works
  const saved = await page.evaluate(async () => {
    const r = await fetch("/api/v1/tank", { method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify({}) });
    return r.status;
  });
  expect(saved).toBe(200);
});

test("Recipes: taking one bottle out of a pair of three leaves the other two a pair, and the editor says nothing (#152)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  const paired = await page.evaluate(async () => {
    const r = await fetch("/api/v1/canisters", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ id: "calmag", pair: "AB" }) });
    return r.status;
  });
  expect(paired).toBe(200);
  await page.goto("/#/rezepte");
  await page.reload();
  const row = (name: string) => page.locator(".rtable tbody tr").filter({ hasText: name });
  await expect(row("CalMag")).toContainText("Paar AB");
  await row("Teil A").getByTitle("Bearbeiten").click();
  const editor = page.getByRole("dialog", { name: "Kanister „Teil A“" });
  const save = editor.getByRole("button", { name: "Speichern" });
  await editor.locator("input[name=canister-pair]").fill("");
  await expect(editor.locator("input[name=canister-pair]")).toHaveValue("");
  await page.evaluate(() => new Promise(requestAnimationFrame));
  await expect(editor.locator("#bottle-unpairs")).toHaveCount(0);
  await expect(save).toHaveAccessibleDescription("");
  await save.click();
  await expect(editor).toHaveCount(0);
  await expect(row("Teil A")).not.toContainText("Paar AB");
  await expect(row("Teil B")).toContainText("Paar AB");
  await expect(row("CalMag")).toContainText("Paar AB");
});

// Bottles added through the API, and the recipe editor opened from its card (#159)
async function addNutrients(page: import("@playwright/test").Page, names: string[]) {
  return page.evaluate(async (list) => {
    const out: number[] = [];
    for (const name of list)
      out.push((await fetch("/api/v1/canisters", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ name, kind: "nutrient", capacityMl: 1000 }) })).status);
    return out.every((s) => s === 200);
  }, names);
}
async function openRecipe(page: import("@playwright/test").Page, name: string, title = `Rezept „${name}“`) {
  await page.locator(".card.flat").filter({ has: page.getByRole("heading", { name, exact: true }) }).getByRole("button", { name: /^(Bearbeiten|Edit)$/ }).click();
  const editor = page.getByRole("dialog", { name: title });
  await expect(editor).toBeVisible();
  return editor;
}
async function recipeId(page: import("@playwright/test").Page, name: string): Promise<string> {
  return page.evaluate(async (n) => (await (await fetch("/api/v1/config")).json()).recipes.find((r: { name: string }) => r.name === n).id, name);
}
async function apiCall(page: import("@playwright/test").Page, method: string, path: string, body?: unknown) {
  return page.evaluate(
    async ([m, p, b]) => (await fetch(p as string, { method: m as string, headers: { "Content-Type": "application/json" }, body: b === undefined ? undefined : JSON.stringify(b) })).status,
    [method, path, body] as const,
  );
}

test("Recipes: an existing recipe lists every nutrient bottle; an amount puts it in, the order counts only bottles with one (#159)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  expect(await addNutrients(page, ["Booster", "Enzym"])).toBe(true);
  await page.goto("/#/rezepte");
  await page.reload();
  const editor = await openRecipe(page, "Wachstum");
  const names = editor.locator(".step strong");
  const amount = (name: string) => editor.getByRole("textbox", { name: `Menge für ${name} in ml pro Liter` });
  await expect(names).toHaveText(["Teil A", "Teil B", "CalMag", "Booster", "Enzym"]);
  await expect(editor.locator(".step .n")).toHaveText(["1", "2", "3", "", ""]);
  await expect(editor.getByRole("button", { name: "Booster nach oben" })).toHaveCount(0);
  await expect(editor.getByText("Ein Kanister ohne Menge kommt nicht ins Rezept.")).toBeVisible();
  // An amount puts a bottle in, numbered in its row's place; up and down pass over bottles without one
  await amount("Enzym").fill("0,3");
  await expect(editor.locator(".step .n")).toHaveText(["1", "2", "3", "", "4"]);
  await editor.getByRole("button", { name: "CalMag nach unten" }).click();
  await expect(names).toHaveText(["Teil A", "Teil B", "Booster", "Enzym", "CalMag"]);
  await expect(editor.locator(".step .n")).toHaveText(["1", "2", "", "3", "4"]);
  await expect(editor.getByRole("button", { name: "CalMag nach oben" })).toBeFocused();  // down is off at the bottom
  await page.keyboard.press("Enter");
  await expect(names).toHaveText(["Teil A", "Teil B", "Booster", "CalMag", "Enzym"]);
  await editor.getByRole("button", { name: "Enzym nach oben" }).click();
  await expect(names).toHaveText(["Teil A", "Teil B", "Booster", "Enzym", "CalMag"]);
  // Each value stays with its bottle after a move
  await expect(amount("Booster")).toHaveValue("");
  await expect(amount("Enzym")).toHaveValue("0,3");
  await expect(amount("CalMag")).toHaveValue("0,6");
  // The focus moves with the row, and to its other button at the top
  await expect(editor.getByRole("button", { name: "Enzym nach oben" })).toBeFocused();
  await page.keyboard.press("Enter");
  await page.keyboard.press("Enter");
  await expect(names).toHaveText(["Enzym", "Teil A", "Teil B", "Booster", "CalMag"]);
  await expect(editor.getByRole("button", { name: "Enzym nach unten" })).toBeFocused();
  // 0 leaves a bottle out, as an empty field does, and is no problem
  await amount("Booster").fill("0");
  await expect(editor.locator(".step .n")).toHaveText(["1", "2", "3", "", "4"]);
  await expect(editor.getByRole("button", { name: "Booster nach oben" })).toHaveCount(0);
  await expect(editor.locator("#recipe-problems")).toHaveCount(0);
  await editor.getByRole("button", { name: "Speichern" }).click();
  await expect(editor).toHaveCount(0);
  const steps = await page.evaluate(async () => {
    const cfg = await (await fetch("/api/v1/config")).json();
    return cfg.recipes.find((r: { name: string }) => r.name === "Wachstum").steps.map((s: { canister: string; mlPerL: number }) => `${s.canister} ${s.mlPerL}`);
  });
  expect(steps).toEqual(["enzym 0.3", "teil-a 2", "teil-b 2", "calmag 0.6"]);
  const again = await openRecipe(page, "Wachstum");
  await expect(again.locator(".step strong")).toHaveText(["Enzym", "Teil A", "Teil B", "CalMag", "Booster"]);
  await expect(again.getByRole("textbox", { name: "Menge für Enzym in ml pro Liter" })).toHaveValue("0,3");
});

test("Recipes: the recipe editor says before saving that a pair is only partly in, with the way out (#159)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  await page.goto("/#/rezepte");
  const editor = await openRecipe(page, "Wachstum");
  const amount = (name: string) => editor.getByRole("textbox", { name: `Menge für ${name} in ml pro Liter` });
  const save = editor.getByRole("button", { name: "Speichern" });
  const problems = editor.locator("#recipe-problems");
  await expect(problems).toHaveCount(0);
  await amount("Teil B").fill("");
  const half = "Paar AB steht nur zum Teil im Rezept, es fehlt „Teil B“. Trag dafür eine Menge ein oder leere alle Mengen des Paars.";
  await expect(problems).toHaveText(half);
  await expect(editor.getByRole("status")).toHaveText(half);  // read out from a region there from the start
  await expect(save).toBeDisabled();
  await expect(save).toHaveAccessibleDescription(half);
  // A negative amount is said to be invalid, not left out without a word
  await amount("Teil B").fill("-2");
  await expect(problems).toContainText("Die Menge für „Teil B“ ist ungültig: Trag eine Zahl über 0 ein oder leere das Feld.");
  await amount("Teil B").fill("2");
  await expect(problems).toHaveCount(0);
  await expect(save).toBeEnabled();
  // The whole pair out is fine, and so is saving it
  await amount("Teil A").fill("");
  await amount("Teil B").fill("");
  await expect(problems).toHaveCount(0);
  await save.click();
  await expect(editor).toHaveCount(0);
  // A pair of three names every bottle missing; nothing in it says so too
  expect(await apiCall(page, "POST", "/api/v1/canisters", { id: "calmag", pair: "AB" })).toBe(422);  // Wachstum now holds CalMag without A and B
  expect(await apiCall(page, "DELETE", `/api/v1/recipes/${await recipeId(page, "Wachstum")}`)).toBe(200);
  expect(await apiCall(page, "POST", "/api/v1/canisters", { id: "calmag", pair: "AB" })).toBe(200);
  await page.reload();
  const bloom = await openRecipe(page, "Blüte");
  await bloom.getByRole("textbox", { name: "Menge für Teil B in ml pro Liter" }).fill("");
  await bloom.getByRole("textbox", { name: "Menge für CalMag in ml pro Liter" }).fill("");
  await expect(bloom.locator("#recipe-problems")).toHaveText(
    "Paar AB steht nur zum Teil im Rezept, es fehlen „Teil B“ und „CalMag“. Trag dafür Mengen ein oder leere alle Mengen des Paars.",
  );
  await bloom.getByRole("textbox", { name: "Menge für Teil A in ml pro Liter" }).fill("");
  await expect(bloom.locator("#recipe-problems")).toHaveText("Trag für mindestens einen Kanister eine Menge ein.");
  await bloom.getByRole("textbox", { name: "Name" }).fill("");
  await expect(bloom.locator("#recipe-problems")).toHaveText("Gib dem Rezept einen Namen. Trag für mindestens einen Kanister eine Menge ein.");
  await bloom.getByRole("button", { name: "Abbrechen" }).click();
  // A new recipe says what it still needs, without an alarm colour, and saves once it has it
  await page.getByRole("button", { name: "Rezept", exact: true }).click();
  const fresh = page.getByRole("dialog", { name: "Rezept anlegen" });
  await expect(fresh.locator("#recipe-problems")).toHaveText("Gib dem Rezept einen Namen. Trag für mindestens einen Kanister eine Menge ein.");
  await expect(fresh.locator("#recipe-problems")).toHaveClass(/muted/);
  await expect(fresh.getByRole("button", { name: "Speichern" })).toBeDisabled();
  await fresh.getByRole("textbox", { name: "Name" }).fill("Neu");
  await fresh.getByRole("textbox", { name: "Menge für Teil B in ml pro Liter" }).fill("1");
  await expect(fresh.locator("#recipe-problems")).toHaveClass(/warn-text/);  // half a pair is a conflict
  await fresh.getByRole("textbox", { name: "Menge für Teil A in ml pro Liter" }).fill("1");
  await fresh.getByRole("textbox", { name: "Menge für CalMag in ml pro Liter" }).fill("0,5");  // CalMag is in pair AB here
  await expect(fresh.locator("#recipe-problems")).toHaveCount(0);
  await fresh.getByRole("button", { name: "Speichern" }).click();
  await expect(fresh).toHaveCount(0);
  // In English, and the hub still refuses half a pair on its own
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  const en = await openRecipe(page, "Blüte", "Recipe \"Blüte\"");
  await en.getByRole("textbox", { name: "Amount of Teil B in ml per litre" }).fill("");
  await expect(en.locator("#recipe-problems")).toHaveText(
    "Pair AB is only partly in the recipe: \"Teil B\" is missing. Enter an amount for it or clear all amounts of the pair.",
  );
  expect(await apiCall(page, "POST", "/api/v1/recipes", { name: "Halb", steps: [{ canister: "teil-a", mlPerL: 1 }] })).toBe(422);
});

test("Recipes: the bottle editor says before saving that a pair label would leave a recipe with part of a pair, and the way out works (#159)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  expect(await addNutrients(page, ["Booster", "Cee"])).toBe(true);
  expect(await apiCall(page, "POST", "/api/v1/canisters", { id: "cee", pair: "CD" })).toBe(200);
  await page.goto("/#/rezepte");
  await page.reload();
  const row = (name: string) => page.locator(".rtable tbody tr").filter({ hasText: name });
  const bottle = async (name: string) => {
    await row(name).getByTitle("Bearbeiten").click();
    const editor = page.getByRole("dialog", { name: `Kanister „${name}“` });
    await expect(editor).toBeVisible();
    return editor;
  };
  // Leaving one pair and joining another: both said, and both describe Save
  let editor = await bottle("Teil A");
  await editor.locator("input[name=canister-pair]").fill("CD");
  await expect(editor.getByRole("button", { name: "Speichern" })).toHaveAccessibleDescription(
    "Nach dem Speichern ist „Teil B“ ohne Paar. So stünde Paar CD nur zum Teil in den Rezepten „Wachstum“ und „Blüte“. Trag dort erst für „Cee“ und „Teil A“ je eine Menge ein oder leere ihre Mengen, dann speichere hier.",
  );
  await expect(editor.getByRole("status")).toHaveText(
    "Nach dem Speichern ist „Teil B“ ohne Paar. So stünde Paar CD nur zum Teil in den Rezepten „Wachstum“ und „Blüte“. Trag dort erst für „Cee“ und „Teil A“ je eine Menge ein oder leere ihre Mengen, dann speichere hier.",
  );
  await editor.getByRole("button", { name: "Abbrechen" }).click();
  editor = await bottle("Booster");
  let save = editor.getByRole("button", { name: "Speichern" });
  await editor.locator("input[name=canister-pair]").fill("AB");
  const many = "So stünde Paar AB nur zum Teil in den Rezepten „Wachstum“ und „Blüte“. Trag dort erst für „Teil A“, „Teil B“ und „Booster“ je eine Menge ein oder leere ihre Mengen, dann speichere hier.";
  await expect(editor.locator("#bottle-problems")).toHaveText(many);
  await expect(editor.getByRole("status")).toHaveText(many);  // read out from a region there from the start
  await expect(save).toBeDisabled();
  await expect(save).toHaveAccessibleDescription(many);
  // The field that causes it points to the reason
  const pairField = editor.locator("input[name=canister-pair]");
  await expect(pairField).toHaveAttribute("aria-invalid", "true");
  await expect(pairField).toHaveAccessibleDescription(many);
  await pairField.fill("XY");
  await expect(editor.locator("#bottle-problems")).toHaveCount(0);
  await expect(editor.getByRole("status")).toHaveText("");
  await expect(pairField).not.toHaveAttribute("aria-invalid", /.*/);
  await expect(save).toBeEnabled();
  await editor.getByRole("button", { name: "Abbrechen" }).click();
  // One recipe: the singular, and its way out works
  expect(await apiCall(page, "DELETE", `/api/v1/recipes/${await recipeId(page, "Blüte")}`)).toBe(200);
  await page.reload();
  editor = await bottle("Booster");
  await editor.locator("input[name=canister-pair]").fill("AB");
  await expect(editor.locator("#bottle-problems")).toHaveText(
    "So stünde Paar AB nur zum Teil im Rezept „Wachstum“. Trag dort erst für „Teil A“, „Teil B“ und „Booster“ eine Menge ein oder leere ihre Mengen, dann speichere hier.",
  );
  await editor.getByRole("button", { name: "Abbrechen" }).click();
  const recipe = await openRecipe(page, "Wachstum");
  await recipe.getByRole("textbox", { name: "Menge für Booster in ml pro Liter" }).fill("0,5");
  await recipe.getByRole("button", { name: "Speichern" }).click();
  await expect(recipe).toHaveCount(0);
  editor = await bottle("Booster");
  await editor.locator("input[name=canister-pair]").fill("AB");
  await expect(editor.locator("#bottle-problems")).toHaveCount(0);
  await editor.getByRole("button", { name: "Speichern" }).click();
  await expect(editor).toHaveCount(0);
  await expect(row("Booster")).toContainText("Paar AB");
  // A new bottle cannot be in a recipe yet: save it without the label first
  await page.locator("#add-bottle").click();
  editor = page.getByRole("dialog", { name: "Kanister anlegen" });
  await editor.locator("input[name=canister-name]").fill("");
  await expect(editor.locator("#bottle-problems")).toHaveText("Gib dem Kanister einen Namen.");
  await expect(editor.getByRole("button", { name: "Speichern" })).toBeDisabled();
  await editor.locator("input[name=canister-name]").fill("Neu");
  await editor.locator("input[name=canister-pair]").fill("AB");
  await expect(editor.locator("#bottle-problems")).toHaveText(
    "So stünde Paar AB nur zum Teil im Rezept „Wachstum“. Speichere den Kanister erst ohne Paar, trag dort eine Menge für ihn ein und dann hier das Paar.",
  );
  await editor.getByRole("button", { name: "Abbrechen" }).click();
  // A pH bottle a recipe holds
  editor = await bottle("CalMag");
  save = editor.getByRole("button", { name: "Speichern" });
  await editor.locator("label.field", { hasText: "Typ" }).locator("select").selectOption("ph_down");
  const ph = "Ein pH-Kanister gehört in kein Rezept, „CalMag“ steht aber im Rezept „Wachstum“. Nimm ihn dort erst heraus (Menge leeren, speichern).";
  const kind = editor.locator("label.field", { hasText: "Typ" }).locator("select");
  await expect(editor.locator("#bottle-problems")).toHaveText(ph);
  await expect(save).toBeDisabled();
  await expect(kind).toHaveAttribute("aria-invalid", "true");
  await expect(kind).toHaveAccessibleDescription(ph);
  await kind.selectOption("nutrient");
  await expect(editor.locator("#bottle-problems")).toHaveCount(0);
  await expect(save).toBeEnabled();
});

test("Recipes: a bottle whose stock is unknown stays unknown when edited, also when it gets a size; a bottle change gives it one (RAT-015, #184)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  expect(await apiCall(page, "POST", "/api/v1/canisters", { name: "Booster", kind: "nutrient", capacityMl: null })).toBe(200);
  await page.goto("/#/rezepte");
  await page.reload();
  const row = (name: string) => page.locator(".rtable tbody tr").filter({ has: page.getByText(name, { exact: true }) });
  const stock = () => page.evaluate(async () => (await (await fetch("/api/v1/state")).json()).stock.booster);
  await expect(row("Booster")).toContainText("unbekannt");
  await expect(page.getByText("Beim Kanister „Booster“ kennt der Hub den Inhalt nicht. Tipp auf den Stift (Bearbeiten) und trag ein, was drin ist.")).toBeVisible();
  await expect(row("Booster").getByTitle("Kanister gewechselt (voll): erst die Kanistergröße eintragen")).toBeDisabled();
  await row("Booster").getByTitle("Bearbeiten").click();
  const editor = page.getByRole("dialog", { name: "Kanister „Booster“" });
  // No size of its own is filled in, and a size is not an amount left
  await expect(editor.locator("input[name=canister-size]")).toHaveValue("");
  await expect(editor.locator("input[name=canister-stock]")).toHaveValue("");
  await expect(editor.getByText("Der Hub kennt den Inhalt nicht. Trag ein, was im Kanister ist, bei einem vollen die Kanistergröße.")).toBeVisible();
  await editor.locator("input[name=canister-size]").fill("1000");
  await expect(editor.locator("input[name=canister-stock]")).toHaveValue("");
  // Typed key by key, an existing bottle's amount keeps its focus too; left empty again, nothing is entered
  const amount = editor.locator("input[name=canister-stock]");
  await amount.click();
  await amount.pressSequentially("250", { delay: 30 });
  await expect(amount).toBeFocused();
  await expect(amount).toHaveValue("250");
  await amount.fill("");
  await editor.locator("input[name=canister-name]").fill("Booster Plus");
  await editor.getByRole("button", { name: "Speichern" }).click();
  await expect(editor).toHaveCount(0);
  await expect(row("Booster Plus")).toContainText("unbekannt");
  expect(await stock()).toBeNull();
  // The overview does not count it as enough, and says how to give it one
  await page.goto("/");
  await expect(page.getByText("Vorrat in allen Kanistern ausreichend")).toHaveCount(0);
  await expect(page.locator(".list .item").filter({ hasText: "Booster Plus" })).toContainText("unbekannt");
  await expect(page.getByText("Trag den Inhalt unter Rezepte & Kanister ein, dann warnt der Hub, bevor ein Kanister leer wird.")).toBeVisible();
  // A bottle change gives it a number
  await page.goto("/#/rezepte");
  await row("Booster Plus").getByTitle("Kanister gewechselt (voll)").click();
  await expect(row("Booster Plus")).toContainText("1.000 ml");
  await expect(page.getByText("Beim Kanister „Booster Plus“ kennt der Hub den Inhalt nicht.", { exact: false })).toHaveCount(0);
  await row("Booster Plus").getByTitle("Bearbeiten").click();
  await expect(page.getByRole("dialog").getByText("Der Hub zieht jede Gabe ab. Ändere den Wert nur, wenn er nicht stimmt.")).toBeVisible();
  await page.getByRole("dialog").getByRole("button", { name: "Abbrechen" }).click();
  await page.goto("/");
  await expect(page.getByText("Vorrat in allen Kanistern ausreichend")).toBeVisible();
});

test("Recipes: a new bottle starts full: its amount follows its size until the amount itself is changed (#184)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  await page.goto("/#/rezepte");
  await page.locator("#add-bottle").click();
  const editor = page.getByRole("dialog", { name: "Kanister anlegen" });
  const size = editor.locator("input[name=canister-size]");
  const amount = editor.locator("input[name=canister-stock]");
  await expect(amount).toHaveValue("1000");
  // Typed key by key, as on a phone: each field keeps its focus and every digit
  await size.click();
  await size.press("ControlOrMeta+a");
  await size.pressSequentially("500", { delay: 30 });
  await expect(size).toBeFocused();
  await expect(size).toHaveValue("500");
  await expect(amount).toHaveValue("500");
  await amount.click();
  await amount.press("ControlOrMeta+a");
  await amount.pressSequentially("300", { delay: 30 });
  await expect(amount).toBeFocused();
  await expect(amount).toHaveValue("300");
  await size.fill("750");
  await expect(amount).toHaveValue("300");
  await editor.locator("input[name=canister-name]").fill("Bloom");
  await editor.getByRole("button", { name: "Speichern" }).click();
  await expect(editor).toHaveCount(0);
  expect(await page.evaluate(async () => (await (await fetch("/api/v1/state")).json()).stock.bloom)).toBe(300);
  // A size cleared clears the amount that follows it; an amount left empty on purpose stays unknown
  await page.locator("#add-bottle").click();
  await size.fill("");
  await expect(amount).toHaveValue("");
  await size.fill("500");
  await expect(amount).toHaveValue("500");
  await amount.fill("");
  await editor.locator("input[name=canister-name]").fill("Enzym");
  await editor.getByRole("button", { name: "Speichern" }).click();
  await expect(editor).toHaveCount(0);
  expect(await page.evaluate(async () => (await (await fetch("/api/v1/state")).json()).stock.enzym)).toBeNull();
});

test("Setup run again over a bottle whose stock is unknown keeps it unknown and adds no size (RAT-015, #184)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  const bottle = (id: string) =>
    page.evaluate(async (i) => {
      const k = (await (await fetch("/api/v1/config")).json()).canisters.find((c: { id: string }) => c.id === i);
      const stock = (await (await fetch("/api/v1/state")).json()).stock;
      return { found: !!k, size: k?.capacityMl ?? null, stock: stock[i] ?? null };
    }, id);
  for (const size of [null, 500]) {
    expect(await apiCall(page, "DELETE", "/api/v1/canisters/ph")).toBe(200);
    expect(await apiCall(page, "POST", "/api/v1/canisters", { name: "pH−", kind: "ph_down", pump: "CAP-1F02D9", capacityMl: size, stockMl: null })).toBe(200);
    expect(await bottle("ph")).toEqual({ found: true, size, stock: null });
    await page.goto("/#/einrichtung?s=3");
    await page.reload();
    await expect(page.getByTestId("existing-recipes")).toBeVisible();
    await page.getByTestId("wizard-next").click();
    await expect(page.getByTestId("existing-recipes")).toHaveCount(0);
    expect(await bottle("ph"), `size ${size}`).toEqual({ found: true, size, stock: null });
  }
  expect((await bottle("teil-a")).stock).toBeGreaterThan(0);
});

test("Overview and recipes: a low stock and unknown ones are listed together, the note counts the bottles, also in English (RAT-015, #184)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  for (const name of ["Booster", "Enzym"]) expect(await apiCall(page, "POST", "/api/v1/canisters", { name, kind: "nutrient", capacityMl: null })).toBe(200);
  expect(await apiCall(page, "POST", "/api/v1/canisters/teil-a/stock", { ml: 100 })).toBe(200);
  await page.goto("/#/rezepte");
  await page.reload();
  await expect(page.getByText("Bei 2 Kanistern kennt der Hub den Inhalt nicht, etwa nach dem Laden einer Sicherung.", { exact: false })).toBeVisible();
  await expect(page.getByText("Beim Kanister", { exact: false })).toHaveCount(0);
  await page.goto("/");
  const items = page.locator(".list .item");
  await expect(items.filter({ hasText: "Teil A" })).toContainText("100 ml");
  await expect(items.filter({ hasText: "Booster" })).toContainText("unbekannt");
  await expect(items.filter({ hasText: "Enzym" })).toContainText("unbekannt");
  await expect(items.filter({ hasText: "Teil B" })).toHaveCount(0);  // enough: not listed
  await expect(page.getByText("Trag den Inhalt unter Rezepte & Kanister ein", { exact: false })).toHaveCount(1);
  await expect(page.getByText("Vorrat in allen Kanistern ausreichend")).toHaveCount(0);
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  await expect(items.filter({ hasText: "Booster" })).toContainText("unknown");
  await expect(page.getByText("Enter the amount under Recipes & bottles, so the hub warns you before a bottle runs out.")).toBeVisible();
  await page.goto("/#/rezepte");
  await expect(page.getByText("The hub doesn't know the amount in 2 bottles, for example after restoring a backup.", { exact: false })).toBeVisible();
  const row = page.locator(".rtable tbody tr").filter({ has: page.getByText("Booster", { exact: true }) });
  await expect(row).toContainText("unknown");
  await expect(row.getByTitle("Bottle changed (full): enter the bottle size first")).toBeDisabled();
  await row.getByTitle("Edit").click();
  await expect(page.getByRole("dialog").getByText("The hub doesn't know the amount. Enter what is in the bottle; for a full one, its size.")).toBeVisible();
});

test("Dialogs: focus moves into a dialog, Tab stays inside, focus returns to the opener, the title names it (#105)", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  await page.goto("/#/rezepte");
  const opener = page.locator(".rtable tbody tr").first().getByTitle("Bearbeiten");
  const name = (await page.locator(".rtable tbody tr").first().locator("strong").textContent()) ?? "";
  const inside = (d: import("@playwright/test").Locator) => d.evaluate((el) => el.contains(document.activeElement));
  for (const close of ["Escape", "button"] as const) {
    await opener.click();
    const dialog = page.getByRole("dialog", { name: `Kanister „${name}“` });
    await expect(dialog).toBeVisible();
    await expect.poll(() => inside(dialog)).toBe(true);  // focus on open
    for (let i = 0; i < 25; i++) {
      await page.keyboard.press("Tab");
      expect(await inside(dialog), `Tab ${i + 1}`).toBe(true);
    }
    for (let i = 0; i < 25; i++) {
      await page.keyboard.press("Shift+Tab");
      expect(await inside(dialog), `Shift+Tab ${i + 1}`).toBe(true);
    }
    // At the ends Tab wraps: from the dialog back to the last control, from there to the first
    await dialog.focus();
    await page.keyboard.press("Shift+Tab");
    const last = await page.evaluate(() => document.activeElement?.outerHTML ?? "");
    await page.keyboard.press("Tab");
    await expect(dialog.getByRole("button", { name: "Schließen" })).toBeFocused();
    await page.keyboard.press("Shift+Tab");
    expect(await page.evaluate(() => document.activeElement?.outerHTML ?? "")).toBe(last);
    // Focus that has left the dialog (a control that disabled itself drops it to the page) comes back with Tab
    await page.evaluate(() => (document.activeElement as HTMLElement).blur());
    expect(await inside(dialog)).toBe(false);
    await page.keyboard.press("Tab");
    await expect(dialog.getByRole("button", { name: "Schließen" })).toBeFocused();
    if (close === "Escape") await page.keyboard.press("Escape");
    else await dialog.getByRole("button", { name: "Schließen" }).click();
    await expect(dialog).toHaveCount(0);
    await expect(opener, close).toBeFocused();  // focus returns
  }
  // Safari and Firefox on a Mac do not focus a clicked button: the dialog does not
  // then send the focus to whatever had it last
  const link = page.locator("a[href]:visible").first();
  await link.focus();
  await expect(link).toBeFocused();
  await page.evaluate(() => (document.activeElement as HTMLElement).blur());
  await opener.dispatchEvent("click");
  await expect(page.getByRole("dialog", { name: `Kanister „${name}“` })).toBeVisible();
  await page.keyboard.press("Escape");
  await expect(page.getByRole("dialog")).toHaveCount(0);
  await expect(link).not.toBeFocused();
  // A button that disables itself while it waits for the hub (Settings › Changelog) still
  // gets the focus back, though the page had moved it to <body> meanwhile
  let answer!: () => void;
  const held = new Promise<void>((r) => (answer = r));
  await page.route(/\/api\/v1\/changelog$/, async (route) => {
    await held;
    await route.fulfill({ contentType: "text/plain", body: "# Changelog" });
  });
  await page.goto("/#/einstellungen");
  const changelog = page.getByRole("button", { name: "Changelog" });
  await changelog.click();
  await expect(changelog).toBeDisabled();
  await expect.poll(() => page.evaluate(() => document.activeElement === document.body)).toBe(true);
  answer();
  const log = page.getByRole("dialog", { name: "Changelog" });
  await expect(log).toBeVisible();
  await page.keyboard.press("Escape");
  await expect(log).toHaveCount(0);
  await expect(changelog).toBeFocused();
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("A refusal from the hub shows in the page's language: a backup from a newer version (SD-083)", async ({ page }) => {
  await login(page);
  const newer = { name: "doseph-config.json", mimeType: "application/json", buffer: Buffer.from(JSON.stringify({ schemaVersion: 99 })) };
  const restore = async () => {
    await page.goto("/#/einstellungen");
    await page.locator('input[type=file][accept="application/json"]').setInputFiles(newer);
    return page.locator(".toast.error").last();
  };
  await expect(await restore()).toHaveText("Diese Sicherung stammt aus einer neueren Version – aktualisiere zuerst den Hub, dann lade sie");
  // The same refusal on an English page: the hub's English text, no German
  await page.evaluate(() => localStorage.setItem("gc.lang", "en"));
  await page.reload();
  const en = await restore();
  await expect(en).toHaveText("This backup comes from a newer version – update the hub first, then restore it");
  await expect(en).not.toContainText("Sicherung");
});
