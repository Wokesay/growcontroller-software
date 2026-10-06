// Vom ersten Einschalten bis zur ersten Mischung – so, wie ein Kunde es erlebt.
import { expect, test } from "@playwright/test";
import { scenario, simSpeed } from "./helpers";

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

  // Assistent: Start
  await expect(page.getByText("Einrichtung")).toBeVisible();
  await page.getByRole("button", { name: /Weiter/ }).first().click();

  // Geräte erkennen und übernehmen
  await expect(page.getByRole("heading", { name: "Geräte erkennen" })).toBeVisible();
  await page.getByRole("button", { name: /übernehmen/ }).click();
  await expect(page.getByText("übernommen").first()).toBeVisible();
  await page.getByTestId("wizard-next").click();

  // Tank
  await page.locator("input[name=capacity]").fill("60");
  await page.getByRole("button", { name: /Speichern und weiter/ }).click();

  // Kanister (Vorschläge Teil A/B als Paar, CalMag)
  await expect(page.getByRole("heading", { name: "Kanister zuordnen" })).toBeVisible();
  await page.getByRole("button", { name: "Speichern" }).click();
  await expect(page.getByText("Kanister gespeichert")).toBeVisible();
  await page.getByTestId("wizard-next").click();

  // Einmessen aller Pumpen mit dem Simulator-Messbecher
  await simSpeed(page, 10);
  for (let i = 0; i < 3; i++) {
    await page.getByRole("button", { name: "Einmessen", exact: true }).first().click();
    await page.getByRole("button", { name: "Pumpe starten" }).click();
    await page.getByRole("button", { name: /übernehmen/ }).click({ timeout: 60_000 });
    await page.getByRole("button", { name: "Speichern" }).click();
    await expect(page.getByText(/Gespeichert in der Kappe/)).toBeVisible();
    await page.getByRole("button", { name: "Fertig" }).click();
    await expect(page.getByRole("button", { name: "Erneut" })).toHaveCount(i + 1);  // Live-Zustand abwarten
  }
  await expect(page.getByText("noch nicht eingemessen")).toHaveCount(0);
  await page.getByTestId("wizard-next").click();
  await page.getByTestId("wizard-next").click(); // Sonden entfallen in Stufe 0

  // Rezept
  const fields = page.locator(".form-grid input[inputmode=decimal]");
  await fields.nth(0).fill("2");
  await fields.nth(1).fill("2");
  await fields.nth(2).fill("0,5");
  await page.getByRole("button", { name: "Rezept speichern" }).click();
  await expect(page.getByText("Vorhanden: Wachstum")).toBeVisible();
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
