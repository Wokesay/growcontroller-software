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
  await page.getByRole("tablist", { name: "Mengen" }).getByRole("tab", { name: "Laut Rezept" }).click();  // with EC top-up on, the probe's way is preselected (SD-102)
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

// EC control on or off, as a person switches it under Functions
const ecControl = (page: Page, enabled: boolean) =>
  page.evaluate(async (on) => (await fetch("/api/v1/functions/ec_control", { method: "PATCH", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ enabled: on }) })).status, enabled);

test("Mixing to an EC without measuring is offered with EC control off, needs the recipe's chart EC and the water's EC, and doses at most the recipe's amounts (SD-057, SD-098, RAT-087)", async ({ page }) => {
  await login(page);
  expect(await saveChart(page, "Veg", { refEc: null, ph: null })).toBe(200);
  await page.goto("/#/mischen");
  await page.reload();
  // EC control on: to its target with the probe, or the recipe's amounts; no fixed pattern, as EC control takes
  // the tank to its own target after mixing (SD-089 S20, SD-102 D1)
  const choice = page.getByRole("tablist", { name: "Mengen" });
  await expect(choice.getByRole("tab", { name: "Aufs Ziel mit Sonde" })).toBeVisible();
  await expect(choice.getByRole("tab", { name: "Auf Ziel-EC" })).toHaveCount(0);
  await choice.getByRole("tab", { name: "Laut Rezept" }).click();
  await expect(page.getByTestId("mix-ec-control")).toContainText("Die EC-Nachdosierung ist an");
  expect(await ecControl(page, false)).toBe(200);
  await page.reload();
  await page.locator("select[name=recipe]").selectOption({ label: "Veg" });
  await page.locator("input[name=water]").fill("20");
  await page.getByRole("tablist", { name: "Mengen" }).getByRole("tab", { name: "Auf Ziel-EC" }).click();
  await expect(page.getByText("Möglich: 0,2 bis 4,0 mS/cm")).toBeVisible();
  const preview = page.getByTestId("mix-preview");
  // No chart EC: refused, with what to do; the recipe's amounts stay
  await page.locator("input[name=targetEc]").fill("0,85");
  await page.locator("input[name=waterEc]").fill("0,2");
  await expect(preview).toContainText("Rezept „Veg“ hat keine passende EC laut Plan");
  await expect(page.getByTestId("mix-start")).toBeDisabled();
  // A chart EC of 1.15 over 4.6 ml/L: (0.85 − 0.1) / 0.25 = 3 ml/L, two thirds of each part; a larger
  // effect EC control learned for Veg before it was switched off makes it less (SD-098)
  expect(await saveChart(page, "Veg", { refEc: 1.15, ph: null })).toBe(200);
  const learned = await page.evaluate(async () => (await (await fetch("/api/v1/state")).json()).effects.ecByRecipe?.veg ?? null);
  const [lo, hi] = [0.25 * 0.275, 4 * 0.275];  // the band around the start effect (RAT-056, REF_EFFECT_BAND)
  const e = typeof learned === "number" && learned >= lo ? Math.max(0.25, Math.min(learned, hi)) : 0.25;
  const de = (v: number, d: number) => v.toFixed(d).replace(".", ",");
  await page.locator("input[name=waterEc]").fill("0,1");
  await expect(page.getByTestId("mix-expected")).toHaveText("Zu erwarten: etwa 0,85 mS/cm (gerechnet, nicht gemessen)");
  await expect(preview).toContainText("Ohne Messen: Der Hub gibt die berechneten Mengen");
  await expect(preview).toContainText(`Gesamt ${de(15 / e, 1)} ml`);
  // Above the chart: the recipe's amounts, and what to expect instead
  await page.locator("input[name=targetEc]").fill("2,5");
  await expect(preview).toContainText("Ohne Messen gibt der Hub höchstens die Mengen des Rezepts – das reicht nicht für 2,50 mS/cm");
  await expect(page.getByTestId("mix-expected")).toContainText(`etwa ${de(0.1 + e * 4.6, 2)} mS/cm`);
  await expect(preview).toContainText("Gesamt 92,0 ml");
  // The water's EC is never taken as 0: said at its field
  const waterEc = page.locator("input[name=waterEc]");
  await waterEc.fill("");
  await expect(page.getByText("Trag die EC des Wassers ein (0 bei Osmosewasser)")).toBeVisible();
  await expect(page.getByTestId("mix-start")).toBeDisabled();
  // On a new fill the probe's reading is taken over with one tap, the focus on the field; not on a top-up
  const useProbe = page.getByRole("button", { name: /^Frisches Wasser schon im Tank\? Sonde übernehmen: \d+,\d{2}\smS\/cm$/ });
  const mode = page.getByRole("tablist", { name: "Neu oder auffüllen" });
  await mode.getByRole("tab", { name: "Auffüllen" }).click();
  await expect(page.getByText("EC des frischen Wassers")).toBeVisible();
  await expect(useProbe).toHaveCount(0);
  await mode.getByRole("tab", { name: "Neu ansetzen" }).click();
  await useProbe.click();
  await expect(waterEc).toBeFocused();
  await expect(waterEc).toHaveValue(/^\d+(,\d{1,2})?$/);
  // Started: the job doses the scaled amounts, keeps what it aimed at, and its card names it
  await waterEc.fill("0,1");
  await page.locator("input[name=targetEc]").fill("0,85");
  await expect(page.getByTestId("mix-expected")).toHaveText("Zu erwarten: etwa 0,85 mS/cm (gerechnet, nicht gemessen)");
  await expect(page.getByTestId("mix-start")).toBeEnabled();
  await simSpeed(page, 20);
  await expect.poll(async () => page.evaluate(async () => (await (await fetch("/api/v1/state")).json()).dosing), { timeout: 60_000 }).toBeNull();
  await simSpeed(page, 0);  // held: no dose starts and no time passes between this check and the start
  const recent = await page.evaluate(async () => {
    const st = await (await fetch("/api/v1/state")).json();
    return st.lastMixAt > 0 && st.now - st.lastMixAt < 30 * 60;
  });
  await page.getByTestId("mix-start").click();
  if (recent) await page.getByRole("button", { name: "Trotzdem mischen" }).click();  // the mix before was minutes ago (RAT-003)
  const job = page.getByTestId("job");
  await expect(job).toContainText("Auf 0,85 mS/cm, ohne Messen – etwa 0,85 mS/cm zu erwarten");
  const info = await page.evaluate(async () => {
    const j = (await (await fetch("/api/v1/state")).json()).job;
    return { amounts: j.info.amounts, ml: j.steps.reduce((a: number, x: { ml: number }) => a + x.ml, 0) };
  });
  expect(info.amounts).toBe("target");
  expect(info.ml).toBeCloseTo(15 / e, 1);
  await simSpeed(page, 20);
  await expect(job.getByText("fertig", { exact: true })).toBeVisible({ timeout: 90_000 });
  await simSpeed(page, 1);
  expect(await saveChart(page, "Veg", { refEc: null, ph: null })).toBe(200);
  expect(await ecControl(page, true)).toBe(200);
});

test("Mixing to the target with the probe is preselected with EC top-up on: it shows the tank's EC and round 1, measures first, can be cancelled meanwhile, and hands over to EC top-up (SD-057, SD-102)", async ({ page }) => {
  await login(page);
  await page.goto("/#/mischen");
  // Fresh water in the twin; EC top-up off meanwhile, else it raises the water itself once the probe reads it
  expect(await ecControl(page, false)).toBe(200);
  await expect(page.getByText("„Aufs Ziel mit Sonde“ gibt es, wenn „EC nachdosieren“ unter Funktionen an ist.")).toBeVisible();
  await page.request.post("/api/v1/sim/water", { data: { volumeL: 40, ec: 0.02, ph: 6.5 } });
  await simSpeed(page, 20);
  await expect
    .poll(async () => page.evaluate(async () => {
      const r = (await (await fetch("/api/v1/state")).json()).readings["tank.ec"];
      return r.usable === true && r.value < 0.2;
    }), { timeout: 120_000 })
    .toBe(true);
  await simSpeed(page, 0);  // held: EC top-up must not start a round before the mix
  expect(await ecControl(page, true)).toBe(200);
  await page.reload();
  const choice = page.getByRole("tablist", { name: "Mengen" });
  await expect(choice.getByRole("tab", { name: "Aufs Ziel mit Sonde" })).toHaveAttribute("aria-selected", "true");
  await page.locator("input[name=water]").fill("40");
  const preview = page.getByTestId("mix-preview");
  await expect(preview).not.toContainText("geht nur mit dem Rezept");  // EC top-up's own recipe is preselected (D2)
  // EC top-up's recipe as the hub names it (D2), and its target, also before EC top-up has run since it was switched on
  const ecRecipe = await page.evaluate(async () => {
    const st = await (await fetch("/api/v1/state")).json();
    const cfg = await (await fetch("/api/v1/config")).json();
    return (cfg.recipes as { id: string; name: string }[]).find((r) => r.id === st.mixWay1.recipe)?.name ?? "?";
  });
  await expect(page.getByTestId("mix-ec-control")).toContainText(`Mit „${ecRecipe}“: Der Hub wälzt um, misst die EC und dosiert Runde 1`);
  await expect(page.getByTestId("mix-ec-control")).toContainText(/auf ihr Ziel \(\d+,\d{2}\smS\/cm\)/);
  await expect(page.getByTestId("mix-measured")).toContainText(/^Tank jetzt: 0,0\d\smS\/cm laut Sonde · \d+\sL laut Füllstand/);
  await expect(page.getByTestId("mix-expected")).toContainText("Runde 1 bis etwa");
  await expect(preview).toContainText("ohne Temperaturausgleich");
  await expect(preview.locator("th").nth(2)).toHaveText("ml/L im Rezept");
  // A top-up asks for the litres in the tank now only without a level; the demo's level reads
  const mode = page.getByRole("tablist", { name: "Neu oder auffüllen" });
  await mode.getByRole("tab", { name: "Auffüllen" }).click();
  await expect(page.getByText("Frisches Wasser (schon drin)")).toBeVisible();
  await expect(page.locator("input[name=tankNow]")).toHaveCount(0);
  await mode.getByRole("tab", { name: "Neu ansetzen" }).click();
  await expect(page.getByTestId("mix-start")).toBeEnabled();
  const recent = await page.evaluate(async () => {
    const st = await (await fetch("/api/v1/state")).json();
    return st.lastMixAt > 0 && st.now - st.lastMixAt < 30 * 60;
  });
  await page.getByTestId("mix-start").click();
  if (recent) await page.getByRole("button", { name: "Trotzdem mischen" }).click();  // the mix before was minutes ago (RAT-003)
  const job = page.getByTestId("job");
  await expect(job.getByText("misst", { exact: true })).toBeVisible();
  await expect(job).toContainText("Aufs Ziel mit Sonde (");
  await expect(job).toContainText("Wälzt um und misst die EC");
  await expect(job.getByRole("button", { name: "Abbrechen" })).toBeVisible();  // nothing is in yet: no need for the Stop
  await expect(job.locator(".step .num").first()).toHaveText("–");  // the amounts are worked out again once measured
  await simSpeed(page, 20);
  await expect(job.getByText("fertig", { exact: true })).toBeVisible({ timeout: 120_000 });
  await expect(job).toContainText("Runde 1 drin: EC");
  await expect(page.getByTestId("job-ec-now")).toContainText("EC nachdosieren jetzt:");
  await simSpeed(page, 1);
});

test("Mixing to the target with the probe is not preselected while the probe gives no EC: the recipe's amounts are, and the page says why (SD-102 D1)", async ({ page }) => {
  await login(page);
  await page.request.post("/api/v1/sim/fault", { data: { device: "PHEC-3F2A91", fault: "offline" } });
  await simSpeed(page, 20);
  await expect
    .poll(async () => page.evaluate(async () => (await (await fetch("/api/v1/state")).json()).mixWay1.blocked?.key ?? null), { timeout: 120_000 })
    .toBe("mix.measured_no_ec");
  await simSpeed(page, 1);
  await page.goto("/#/mischen");
  await page.reload();
  const choice = page.getByRole("tablist", { name: "Mengen" });
  await expect(choice.getByRole("tab", { name: "Laut Rezept" })).toHaveAttribute("aria-selected", "true");
  await expect(page.getByTestId("mix-ec-control")).toContainText("„Aufs Ziel mit Sonde“ braucht eine gültige EC von der Sonde im Tank");
  await expect(page.getByTestId("mix-ec-control")).toContainText("Schalte „EC nachdosieren“ unter Funktionen aus, dann geht „Auf Ziel-EC“ ohne Sonde.");
  await page.request.post("/api/v1/sim/fault", { data: { device: "PHEC-3F2A91", fault: "none" } });
});
