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
