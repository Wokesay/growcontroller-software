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
  await expect(page.getByText("Dafür brauchst du: Kopf Klima").first()).toBeVisible();
  await page.goto("/#/verlauf");
  await expect(page.locator(".chart canvas").first()).toBeVisible();
  await expect(page.getByTestId("event").first()).toBeVisible();
});

test("Fehlsteckung: Kappe am Hub-Port wird mit Klartext gemeldet", async ({ page }) => {
  await login(page);
  await page.request.post("/api/v1/sim/plug", { data: { port: 2, class: "pump_cap" } });
  await page.goto("/#/geraete");
  await expect(page.getByTestId("ports").getByText("Bitte in den Dosierblock stecken")).toBeVisible();
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
