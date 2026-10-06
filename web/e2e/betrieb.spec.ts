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
  await expect(page.getByTestId("event").first()).toBeVisible();
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
});

test("Rezept-Vorlage: Vorschau, Kanister zuordnen, Rezept anlegen", async ({ page }) => {
  await login(page);
  await page.goto("/#/rezepte");
  await page.getByTestId("templates").getByRole("button", { name: /Athena Blended – Wachstum/ }).click();
  await expect(page.getByRole("dialog")).toContainText("Grow B");
  await expect(page.getByRole("button", { name: "Rezept anlegen" })).toBeDisabled();
  await page.locator("select[name=map-b]").selectOption({ label: "Teil B" });
  await page.locator("select[name=map-a]").selectOption({ label: "Teil A" });
  await page.locator("select[name=map-calmag]").selectOption({ label: "CalMag" });
  await page.getByRole("button", { name: "Rezept anlegen" }).click();
  await expect(page.getByText("Rezept „Athena Blended – Wachstum“ angelegt")).toBeVisible();
  await expect(page.getByRole("dialog")).toHaveCount(0);
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
