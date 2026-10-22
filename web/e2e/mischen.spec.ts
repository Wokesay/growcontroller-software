// SPDX-License-Identifier: AGPL-3.0-or-later
// Mixing to the phase's target EC (SD-057): a recipe's chart values and the
// targets they propose for a grow cycle.
import type { Page } from "@playwright/test";
import { expect, login, scenario, simSpeed, test } from "./helpers";

test.beforeAll(async ({ request }) => {
  await scenario(request, "demo");
});

// The demo recipe "Veg" is 2 + 2 + 0.6 ml/L. Sets its chart values, with no part fixed, so each case
// starts from the same recipe whatever ran before
const saveChart = (page: Page, name: string, chart: { refEc: number | null; ph: [number, number] | null }) =>
  page.evaluate(
    async ([n, c]) => {
      const cfg = await (await fetch("/api/v1/config")).json();
      const r = cfg.recipes.find((x: { name: string }) => x.name === n);
      const steps = r.steps.map((s: { canister: string; mlPerL: number }) => ({ canister: s.canister, mlPerL: s.mlPerL, fixed: false }));
      const res = await fetch("/api/v1/recipes", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ id: r.id, steps, ...c }) });
      return res.status;
    },
    [name, chart] as const,
  );

test("A recipe keeps its chart EC and pH range; an EC that does not fit the amounts is named before saving, with the EC that would (SD-057)", async ({ page }) => {
  await login(page);
  expect(await saveChart(page, "Veg", { refEc: null, ph: null })).toBe(200);
  await page.reload();
  await page.goto("/#/rezepte");
  const card = page.locator(".card.flat").filter({ has: page.getByRole("heading", { name: "Veg", exact: true }) });
  await expect(card.getByTestId("recipe-chart")).toHaveCount(0);
  await card.getByTitle("Bearbeiten").click();
  const dialog = page.getByRole("dialog");
  const save = dialog.getByRole("button", { name: "Speichern" });
  const problems = dialog.locator("#recipe-problems");
  const ec = dialog.getByLabel("EC laut Plan");
  await ec.fill("0,2");
  await expect(problems).toHaveText("Zu diesen Mengen (4,6 ml/L zusammen) passt eine EC von 0,32 bis 4 mS/cm, nicht 0,2 mS/cm. Prüf EC und Mengen.");
  await expect(save).toBeDisabled();
  // At the edge the app and the hub agree: a quarter of 0.275 per ml/L is 0.316 over 4.6 ml/L
  await ec.fill("0,31");
  await expect(problems).toContainText("passt eine EC von 0,32 bis 4 mS/cm");
  await ec.fill("0,32");
  await expect(problems).toHaveCount(0);
  await ec.fill("1,25");
  await dialog.getByLabel("pH-Bereich von").fill("6,2");
  await expect(problems).toContainText("den unteren zuerst");  // half a range
  await dialog.getByLabel("pH-Bereich bis").fill("5,8");
  await expect(problems).toContainText("den unteren zuerst");  // high to low
  await expect(save).toBeDisabled();
  await dialog.getByLabel("pH-Bereich von").fill("5,8");
  await dialog.getByLabel("pH-Bereich bis").fill("6,2");
  await expect(problems).toHaveCount(0);
  await save.click();
  await expect(card.getByTestId("recipe-chart")).toHaveText("EC 1,25 mS/cm laut Plan · pH 5,8–6,2");
  // Changed amounts under a chart EC ask whether it still matches
  await card.getByTitle("Bearbeiten").click();
  await dialog.getByRole("textbox", { name: /^Menge für CalMag/ }).fill("1");
  await expect(dialog.getByText("Mengen geändert – stimmt die EC laut Plan noch?")).toBeVisible();
  await dialog.getByRole("button", { name: "Abbrechen" }).click();
});

test("A bottle that is not one of a pair can have a fixed amount; card, mix plan and the bottle editor say so (SD-057, RAT-085)", async ({ page }) => {
  await login(page);
  expect(await saveChart(page, "Veg", { refEc: null, ph: null })).toBe(200);
  await page.reload();
  await page.goto("/#/rezepte");
  const card = page.locator(".card.flat").filter({ has: page.getByRole("heading", { name: "Veg", exact: true }) });
  await card.getByTitle("Bearbeiten").click();
  const dialog = page.getByRole("dialog");
  // Only CalMag offers it, named per bottle: Part A and Part B are a pair, which goes in together
  await expect(dialog.getByRole("checkbox")).toHaveCount(1);
  const fixed = dialog.getByRole("checkbox", { name: "Feste Menge für CalMag" });
  await fixed.check();
  // The chart EC fits over the parts EC control scales: 0.29 over 4 ml/L, which over 4.6 ml/L it would not
  await dialog.getByLabel("EC laut Plan").fill("0,29");
  await expect(dialog.locator("#recipe-problems")).toHaveCount(0);
  await fixed.uncheck();
  await expect(dialog.locator("#recipe-problems")).toContainText("Zu diesen Mengen (4,6 ml/L zusammen) passt");
  await fixed.check();
  await dialog.getByLabel("EC laut Plan").fill("");
  await dialog.getByRole("button", { name: "Speichern" }).click();
  await expect(card.getByText("CalMag · feste Menge")).toBeVisible();
  const steps = await page.evaluate(async () => (await (await fetch("/api/v1/config")).json()).recipes.find((r: { name: string }) => r.name === "Veg").steps);
  expect(steps.map((s: { canister: string; fixed: boolean }) => [s.canister, s.fixed])).toEqual([["part-a", false], ["part-b", false], ["calmag", true]]);
  // Pairing CalMag now is named before saving: a fixed amount cannot join a pair
  await page.locator(".rtable tbody tr").filter({ hasText: "CalMag" }).getByTitle("Bearbeiten").click();
  const bottle = page.getByRole("dialog");
  await bottle.getByRole("textbox", { name: /^Paar/ }).fill("AB");
  await expect(bottle.locator("#bottle-problems")).toContainText("„CalMag“ ist in „Veg“ eine feste Menge.");
  await bottle.getByRole("button", { name: "Abbrechen" }).click();
  await page.goto("/#/mischen");
  await page.getByLabel("Rezept").selectOption({ label: "Veg" });
  await page.locator("input[name=water]").fill("20");
  await expect(page.getByTestId("mix-preview").getByText("feste Menge")).toBeVisible();
});

test("Without a grow the overview names the target ranges that apply, and the grow dialog proposes each phase's targets from its recipe (SD-057)", async ({ page }) => {
  await login(page);
  expect(await saveChart(page, "Veg", { refEc: 1.25, ph: [5.8, 6.2] })).toBe(200);
  expect(await saveChart(page, "Bloom", { refEc: null, ph: null })).toBe(200);
  expect(await page.evaluate(async () => (await fetch("/api/v1/grow/complete", { method: "POST" })).status)).toBe(200);
  await page.reload();
  await page.goto("/#/");
  // The demo's functions: pH 5.8 ± 0.15, EC 1.4 ± 0.1
  await expect(page.getByTestId("default-targets")).toHaveText("Ohne Durchgang gelten die Zielbereiche unter Funktionen: pH 5,65–5,95 · EC 1,3–1,5 mS/cm.");
  await page.getByRole("link", { name: "Durchgang starten" }).click();
  await page.getByRole("button", { name: "Durchgang starten" }).click();
  const dialog = page.getByRole("dialog");
  const ec = (i: number) => dialog.getByLabel("Ziel-EC").nth(i);
  const ph = (i: number) => dialog.getByLabel("Ziel-pH").nth(i);
  const recipe = (i: number) => dialog.getByLabel("Rezept").nth(i);
  // "Veg" proposes its chart values; "Bloom" has none, so its fields stay empty and say what applies
  await expect(ec(0)).toHaveValue("1,25");
  await expect(ph(0)).toHaveValue("6");
  await expect(ec(1)).toHaveValue("");
  await expect(ec(1)).toHaveAttribute("placeholder", "1,4");
  await expect(ph(1)).toHaveAttribute("placeholder", "5,8");
  await expect(dialog.getByText("Leer: es gilt 1,4 mS/cm aus Funktionen")).toBeVisible();
  await expect(dialog.getByText("Leer: es gilt 5,8 aus Funktionen")).toBeVisible();
  // Another recipe proposes again, unless the user typed a value
  await recipe(1).selectOption({ label: "Veg" });
  await expect(ec(1)).toHaveValue("1,25");
  await ec(0).fill("1,5");
  await recipe(0).selectOption({ label: "Bloom" });
  await expect(ec(0)).toHaveValue("1,5");
  await expect(ph(0)).toHaveValue("");
  // An added phase proposes from the first recipe
  await dialog.getByRole("button", { name: "Phase hinzufügen" }).click();
  await expect(ec(2)).toHaveValue("1,25");
  // Below the pH lock's EC (0.5) pH control would never dose: said at the field
  await ec(2).fill("0,4");
  await expect(dialog.getByText("Unter 0,5 mS/cm dosiert die pH-Regelung nicht")).toBeVisible();
  await ec(2).fill("1,25");
  await dialog.getByRole("button", { name: "Starten" }).click();
  await expect(dialog).toHaveCount(0);
  const phases = await page.evaluate(async () => (await (await fetch("/api/v1/config")).json()).grow.phases);
  expect(phases).toHaveLength(3);
  expect(phases[0].params.ec_target).toBe(1.5);
  expect(phases[0].params).not.toHaveProperty("ph_target");  // empty is not sent: the function's value applies
  expect(phases[1].params.ec_target).toBe(1.25);
  expect(phases[1].params.ph_target).toBe(6);
  // The phase line says where a target comes from
  await expect(page.getByText("pH 5,8 (aus Funktionen)")).toBeVisible();
});

test("A mix with the circulation pump mixes in before it ends; the card says for how long and offers no way to skip or cancel it (SD-057)", async ({ page }) => {
  await login(page);
  await page.goto("/#/mischen");
  await simSpeed(page, 20);  // the parts go in within seconds; the 5 min of mixing in last 15 s
  // A mix does not start over a control dose: tried until none runs
  const start = () =>
    page.evaluate(async () => {
      const res = await fetch("/api/v1/mix/start", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ recipe: "veg", waterL: 20, guided: false, confirmRepeat: true }),
      });
      return res.status;
    });
  await expect.poll(start, { timeout: 120_000 }).toBe(200);
  const job = page.getByTestId("job");
  await expect(job.getByText(/^Alles drin\. Durchmischen mit der Umwälzpumpe \(5\smin\) – danach übernimmt die Regelung\./)).toBeVisible({ timeout: 60_000 });
  await simSpeed(page, 0);  // held while the card is read
  await expect(job).toContainText(/Noch etwa \d\smin\./);
  await expect(job.getByText("mischt durch", { exact: true })).toBeVisible();
  await expect(job.getByRole("button")).toHaveCount(0);  // control needs the mixed tank, and nothing is left to dose
  await simSpeed(page, 20);
  await expect(job.getByText("fertig", { exact: true })).toBeVisible({ timeout: 60_000 });
  await simSpeed(page, 1);
});
