// SPDX-License-Identifier: AGPL-3.0-or-later
// Vom ersten Einschalten bis zur ersten Mischung – so, wie ein Kunde es erlebt.
import { expect, test } from "@playwright/test";
import { login, scenario, simSpeed } from "./helpers";

test("Ersteinrichtung bis zum ersten geführten Mischlauf", async ({ page, request }) => {
  await scenario(request, "neu");
  await page.goto("/");

  // Pflichtpasswort, kein Überspringen
  await expect(page.getByRole("heading", { name: "Willkommen" })).toBeVisible();
  await page.locator("input[name=password]").fill("kurz");
  await page.locator("input[name=password2]").fill("kurz");
  await page.getByRole("button", { name: "Passwort festlegen" }).click();
  await expect(page.getByText("Mindestens 8 Zeichen").last()).toBeVisible();
  await page.locator("input[name=password]").fill("mein-passwort");
  await page.locator("input[name=password2]").fill("mein-passwort");
  await page.getByRole("button", { name: "Passwort festlegen" }).click();

  // Assistent: Start (Sprache bleibt Deutsch)
  await expect(page.getByText("Einrichtung", { exact: true })).toBeVisible();
  await expect(page.getByRole("tab", { name: "Deutsch" })).toHaveAttribute("aria-selected", "true");
  await page.getByTestId("wizard-next").click();

  // Geräte erkennen und übernehmen; Anschlüsse heißen „Anschluss n“, Pumpen „Pumpe n“
  await expect(page.getByRole("heading", { name: "Geräte erkennen" })).toBeVisible();
  await expect(page.getByTestId("ports").getByText("Anschluss 1")).toBeVisible();
  await expect(page.getByTestId("wizard-next")).toBeDisabled();
  await page.getByRole("button", { name: /übernehmen/ }).click();
  await expect(page.getByText("übernommen").first()).toBeVisible();
  await expect(page.getByText("Pumpe 1", { exact: true })).toBeVisible();
  await page.getByTestId("wizard-next").click();

  // Tank: Erklärung zum Nutzvolumen, Zurück und wieder vor
  await expect(page.getByRole("heading", { name: "Tank" })).toBeVisible();
  await page.getByRole("button", { name: "Erklärung: Nutzvolumen" }).first().click();
  await expect(page.getByText(/So misst du es/).first()).toBeVisible();
  await page.getByTestId("wizard-back").click();
  await expect(page.getByRole("heading", { name: "Geräte erkennen" })).toBeVisible();
  await page.getByTestId("wizard-next").click();
  await expect(page.getByTestId("wizard-next")).toBeDisabled(); // ohne Nutzvolumen kein Weiter
  await page.locator("input[name=capacity]").fill("60");
  await page.getByTestId("wizard-next").click();

  // Nährstoffe: Vorlage ist vorbelegt (B, A, CalMag auf Pumpe 1–3, mit ml/L)
  await expect(page.getByRole("heading", { name: "Nährstoffe" })).toBeVisible();
  await expect(page.getByRole("button", { name: /Zweikomponenten-Dünger/ })).toHaveAttribute("aria-pressed", "true");
  await expect(page.getByRole("button", { name: /Wachstum Wo\. 1–4/ })).toContainText("Herstellerangabe, unverbindlich");
  await expect(page.locator("input[name=bottle0]")).toHaveValue("Teil B");
  await expect(page.locator("input[name=dose2]")).toHaveValue("0,5");
  await page.getByTestId("wizard-next").click();

  // Einmessen aller Pumpen mit dem Simulator-Messbecher
  await expect(page.getByRole("heading", { name: "Einmessen" })).toBeVisible();
  await simSpeed(page, 10);
  for (let i = 0; i < 3; i++) {
    await page.getByRole("button", { name: "Einmessen", exact: true }).first().click();
    await page.getByRole("button", { name: "Pumpe starten" }).click();
    await page.getByRole("button", { name: /übernehmen/ }).click({ timeout: 60_000 });
    await page.getByRole("button", { name: "Speichern" }).click();
    await expect(page.getByText(/Gespeichert in der Pumpe/)).toBeVisible();
    await page.getByRole("button", { name: "Fertig", exact: true }).click();
    await expect(page.getByRole("button", { name: "Erneut" })).toHaveCount(i + 1);  // Live-Zustand abwarten
  }
  await expect(page.getByText("noch nicht eingemessen")).toHaveCount(0);
  await expect(page.getByTestId("wizard-skip")).toHaveCount(0); // alles eingemessen: nichts zu überspringen
  await page.getByTestId("wizard-next").click();

  // Fertig → erste Mischung
  await expect(page.getByRole("heading", { name: "Fertig eingerichtet" })).toBeVisible();
  await page.getByRole("button", { name: "Zur ersten Mischung" }).click();
  await page.evaluate(() => fetch("/api/v1/sim/water", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ volumeL: 10 }) }));
  await page.locator("input[name=water]").fill("10");
  await expect(page.getByRole("cell", { name: "20,0 ml" }).first()).toBeVisible();
  await page.getByTestId("mix-start").click();

  // Geführt ohne Umwälzpumpe: nach jeder Gabe "umrühren, weiter"
  for (let i = 0; i < 2; i++) {
    await page.getByRole("button", { name: /Weiter \(Schritt/ }).click({ timeout: 90_000 });
  }
  await expect(page.getByText(/Fertig: 10,0 L/)).toBeVisible({ timeout: 90_000 });
  await simSpeed(page, 1);
});

test("Einrichtung zeigt vorhandene Rezepte und schaltet auf Englisch", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  await page.goto("/#/einrichtung?s=3");
  await expect(page.getByTestId("existing-recipes")).toContainText("Wachstum");
  await expect(page.getByTestId("existing-recipes")).toContainText("Blüte");
  await page.getByTestId("wizard-back").click();
  await page.getByTestId("wizard-back").click();
  await page.getByTestId("wizard-back").click();
  await page.getByRole("tab", { name: "English" }).click();
  await expect(page.getByRole("heading", { name: "Welcome" })).toBeVisible();
  await expect(page.getByTestId("wizard-next")).toHaveText(/Next/);
  await page.getByRole("tab", { name: "Deutsch" }).click();
  await expect(page.getByRole("heading", { name: "Willkommen" })).toBeVisible();
});

test("Setup: a pH calibrated outside the hub needs nothing here and is no missing probe", async ({ page, request }) => {
  await scenario(request, "demo");
  // The Home Assistant trial: a pH device the hub cannot calibrate, and no probe head of the hub's own
  await page.route(/\/api\/v1\/catalog(\?|$)/, async (route) => {
    const res = await route.fetch();
    const cat = await res.json();
    cat.deviceClasses.ha_ph = { label: "pH aus Home Assistant", stage: 0, attach: "ha", provides: ["measure.ph"] };
    await route.fulfill({ response: res, json: cat });
  });
  await page.route(/\/api\/v1\/config(\?|$)/, async (route) => {
    if (route.request().method() !== "GET") return route.continue();
    const res = await route.fetch();
    const cfg = await res.json();
    cfg.devices = [...cfg.devices.filter((d: { class: string }) => !d.class.startsWith("head_")), { id: "ha.sensor.grow_ph", class: "ha_ph", name: "pH aus Home Assistant" }];
    await route.fulfill({ response: res, json: cfg });
  });
  await login(page);
  await page.goto("/#/einrichtung?s=4");
  await expect(page.getByTestId("external-probe")).toHaveText("pH aus Home Assistant: wird außerhalb des Hubs kalibriert – hier nichts zu tun.");
  await expect(page.getByText(/Keine pH\/EC-Sonde/)).toHaveCount(0);
});

test("Read-only trial (Home Assistant): after the password the app opens, not the setup for dosing hardware", async ({ page, request }) => {
  await scenario(request, "neu");
  // gc_ha_server announces itself as read-only; the setup asks for a dosing block it cannot have
  await page.route(/\/api\/v1\/info(\?|$)/, async (route) => {
    const res = await route.fetch();
    const info = await res.json();
    info.platform = { kind: "home-assistant", simulated: false, readOnly: true };
    await route.fulfill({ response: res, json: info });
  });
  await page.goto("/");
  await page.locator("input[name=password]").fill("mein-passwort");
  await page.locator("input[name=password2]").fill("mein-passwort");
  await page.getByRole("button", { name: "Passwort festlegen" }).click();
  await expect(page.getByTestId("watchdog")).toBeVisible();
  await expect(page.getByRole("link", { name: "Einrichtung" })).toHaveCount(0);  // no way back into it
  // The overview points to the devices waiting to be accepted
  await page.getByTestId("new-devices-link").click();
  await expect(page).toHaveURL(/#\/geraete/);
  await expect(page.getByRole("heading", { name: "Willkommen" })).toHaveCount(0);
  await page.unroute(/\/api\/v1\/info(\?|$)/);
});
