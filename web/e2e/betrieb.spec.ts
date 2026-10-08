// SPDX-License-Identifier: AGPL-3.0-or-later
// Betrieb im eingerichteten System: Übersicht, Not-Halt, Störungen, Verlauf, Funktionen.
import { expect, test } from "@playwright/test";
import { login, scenario } from "./helpers";

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
  await page.getByLabel("Grenze je Handgabe").fill("2,5");
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
  await expect(page.getByRole("heading", { name: "Dose by hand" })).toBeVisible();
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

test("Bereiche: Klima zeigt Messwerte und VPD, Geräte lassen sich von Hand schalten", async ({ page }) => {
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
