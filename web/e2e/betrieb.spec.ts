// SPDX-License-Identifier: AGPL-3.0-or-later
// Betrieb im eingerichteten System: Übersicht, Not-Halt, Störungen, Verlauf, Funktionen.
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
  await expect.poll(lines).toMatch(/Regelt|Ruht|Wartet|Gesperrt|Gerastet|Aus|Läuft|Füllt/);
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
  expect(await lines()).not.toMatch(/Regelt|Ruht|Wartet|Gesperrt|Gerastet|Läuft|Füllt/);
  await expect(page.getByTestId("event").filter({ hasText: "Signed in" }).first()).toBeVisible();
  await expect(page.getByTestId("event").filter({ hasText: "Angemeldet" })).toHaveCount(0);
});

test("Not-Halt stoppt alles und lässt sich fortsetzen", async ({ page }) => {
  await login(page);
  await page.getByTitle("Not-Halt: alle Pumpen und Ausgänge aus").click();
  await page.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(page.getByText("Not-Halt aktiv.")).toBeVisible();
  await page.getByRole("button", { name: "Fortsetzen" }).click();
  await expect(page.getByText("Not-Halt aktiv.")).toHaveCount(0);
});

test("A Stop reaches the hub from a browser without AbortSignal.timeout (before iOS 16)", async ({ page }) => {
  await page.addInitScript(() => {
    delete (AbortSignal as { timeout?: unknown }).timeout;
  });
  await login(page);
  expect(await page.evaluate(() => "timeout" in AbortSignal)).toBe(false);
  await page.getByTitle("Not-Halt: alle Pumpen und Ausgänge aus").click();
  await page.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(page.getByText("Not-Halt aktiv.")).toBeVisible();
  await page.getByRole("button", { name: "Fortsetzen" }).click();
  await expect(page.getByText("Not-Halt aktiv.")).toHaveCount(0);
});

test("A Stop that does not reach the hub says so, and the dialog stays open to try again", async ({ page }) => {
  await login(page);
  await page.route(/\/api\/v1\/stop$/, (route) => route.abort());
  await page.getByTitle("Not-Halt: alle Pumpen und Ausgänge aus").click();
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
  await page.getByTitle("Not-Halt: alle Pumpen und Ausgänge aus").click();
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
  await page.getByTitle("Not-Halt: alle Pumpen und Ausgänge aus").click();
  const dialog = page.getByRole("dialog");
  await dialog.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(dialog.getByRole("alert")).toContainText("Stopp nicht bestätigt");
  // The Stop reaches the hub after all (late, or from another device): the dialog goes
  const origin = new URL(page.url()).origin;
  expect((await page.request.post("/api/v1/stop", { data: {}, headers: { Origin: origin } })).ok()).toBe(true);
  await expect(page.getByText("Not-Halt aktiv.")).toBeVisible();
  await expect(dialog).toHaveCount(0);
  // A Resume without an answer says so, and the hub stays stopped
  await page.route(/\/api\/v1\/resume$/, (route) => route.abort());
  await page.getByRole("button", { name: "Fortsetzen" }).click();
  await expect(page.getByText("Der Hub antwortet nicht. Ist er eingeschaltet und im selben Netz?")).toBeVisible();
  await expect(page.getByText("Not-Halt aktiv.")).toBeVisible();
  // After a Resume that arrives, the old dialog does not come back
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await page.getByRole("button", { name: "Fortsetzen" }).click();
  await expect(page.getByText("Not-Halt aktiv.")).toHaveCount(0);
  await expect(dialog).toHaveCount(0);
});

test("A Stop closed while it is being sent still says when it failed; a refusal starts with the Stop's text", async ({ page }) => {
  await login(page);
  await page.route(/\/api\/v1\/stop$/, () => undefined);  // never answered
  await page.getByTitle("Not-Halt: alle Pumpen und Ausgänge aus").click();
  const dialog = page.getByRole("dialog");
  await dialog.getByRole("button", { name: "Alles stoppen" }).click();
  await expect(dialog.getByRole("button", { name: "Stopp wird gesendet …" })).toBeDisabled();
  await page.keyboard.press("Escape");
  await expect(dialog).toHaveCount(0);
  await expect(page.locator(".toast.error")).toContainText("Stopp nicht bestätigt – keine Antwort vom Hub. Zieh im Notfall den Netzstecker des Hubs", { timeout: 10_000 });
  // Opened again, nothing old shows; a hub that refuses names its reason after the Stop's text
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await page.route(/\/api\/v1\/stop$/, (route) => route.fulfill({ status: 500 }));
  await page.getByTitle("Not-Halt: alle Pumpen und Ausgänge aus").click();
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
  await page.getByTitle("Not-Halt: alle Pumpen und Ausgänge aus").click();
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
  await expect(page.getByText(/sprang ohne Dosierung/).first()).toBeVisible();
  await page.request.post("/api/v1/sim/fault", { data: { device: head.id, fault: "none" } });
});

test("Funktionen zeigen, was fehlt, und Verlauf zeichnet Kurven", async ({ page }) => {
  await login(page);
  await page.goto("/#/funktionen");
  await expect(page.getByTestId("fn-ph_control")).toBeVisible();
  await page.getByTestId("fn-climate_watch").click();
  await expect(page.getByTestId("fn-climate_watch")).not.toContainText("Dafür brauchst du");  // Demo hat einen Klima-Kopf
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

test("Fehlsteckung: Pumpe am Hub-Anschluss wird mit Klartext gemeldet", async ({ page }) => {
  await login(page);
  await page.request.post("/api/v1/sim/plug", { data: { port: 2, class: "pump_cap" } });
  await page.goto("/#/geraete");
  await expect(page.getByTestId("ports").getByText("Bitte auf den Dosierblock stecken")).toBeVisible();
  await page.request.post("/api/v1/sim/unplug", { data: { port: 2 } });
});

test("Problem melden erzeugt ein Diagnosepaket ohne Geheimnisse", async ({ page }) => {
  await login(page);
  await page.goto("/#/einstellungen");
  await page.getByRole("button", { name: "Diagnosepaket erstellen" }).click();
  await expect(page.getByText(/Vorgang GC-/)).toBeVisible();
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
  await expect(page.getByRole("heading", { name: "Dose manually" })).toBeVisible();
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

test("Bereiche: Klima zeigt Messwerte und VPD, Geräte lassen sich manuell schalten", async ({ page }) => {
  await login(page);
  await page.goto("/#/klima");
  await expect(page.getByTestId("metric-VPD")).toBeVisible();
  // Zustand nicht annehmen (der Not-Halt-Test davor schaltet alles aus): zweimal umschalten
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
  await page.goto("/#/bewaesserung");
  await expect(page.getByTestId("output-zone.irrigation_pump")).toContainText("Nicht zugeordnet");
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
  await editor.locator(".step").filter({ hasText: "CalMag" }).locator("input").fill("");
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
