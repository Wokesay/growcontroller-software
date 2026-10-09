// SPDX-License-Identifier: AGPL-3.0-or-later
// Vom ersten Einschalten bis zur ersten Mischung – so, wie ein Kunde es erlebt.
import { expect, test, type Page } from "@playwright/test";
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
  await asHomeAssistant(page, "ok");
  await newPassword(page);
  await expect(page.getByRole("link", { name: "Einrichtung" })).toHaveCount(0);  // no way back into it
  await expect(page.getByRole("heading", { name: "Willkommen" })).toHaveCount(0);
  // The overview points to choosing sensors
  await page.getByTestId("ha-choose-link").click();
  await expect(page).toHaveURL(/#\/geraete\?tab=zuordnung/);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("The overview points to devices waiting to be accepted", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  await page.request.post("/api/v1/sim/net_add", { data: { class: "shelly_plug", loads: [] } });  // a socket switched on
  await page.getByTestId("new-devices-link").click();
  await expect(page).toHaveURL(/#\/geraete/);
  await expect(page.getByText("Ein neues Gerät wurde erkannt.")).toBeVisible();
});

// The Home Assistant trial (docs/HOME_ASSISTANT.md): gc_ha_server offers the
// sensors Home Assistant has and picks one per measurement in one step. Here
// the simulator's answers are changed to look like that server.
const haSensors = [
  { entity: "sensor.growbox_ph", name: "Growbox pH", kind: "ph", measures: ["ph"], value: 6.1, raw: 6.1, unit: "pH", problem: null, used: null },
  { entity: "sensor.growbox_ec", name: "Growbox EC", kind: "ec", measures: ["ec"], value: 1.42, raw: 1420, unit: "µS/cm", problem: null, used: null },
  { entity: "sensor.tank_tds", name: "Tank TDS", kind: "ec", measures: ["ec"], value: null, raw: 700, unit: "ppm", problem: "unit_unsupported", used: null },
  { entity: "sensor.zelt_temperatur", name: "Zelt Temperatur", kind: "temperature", measures: ["water_temp", "air_temp"], value: 24.6, raw: 24.6, unit: "°C", problem: null, used: "air_temp" },
  { entity: "sensor.wohnzimmer_temperatur", name: "Wohnzimmer Temperatur", kind: "temperature", measures: ["water_temp", "air_temp"], value: 21.0, raw: 21.0, unit: "°C", problem: null, used: null },
  { entity: "sensor.kaputt", name: "Kaputt", kind: "temperature", measures: ["water_temp", "air_temp"], value: null, raw: null, unit: "°C", problem: null, used: null },
];
async function asHomeAssistant(page: Page, connection: string, sensors: unknown[] = haSensors) {
  let asked = 0;
  await page.route(/\/api\/v1\/info(\?|$)/, async (route) => {
    const res = await route.fetch();
    const info = await res.json();
    info.platform = { kind: "home-assistant", simulated: false, readOnly: true };
    await route.fulfill({ response: res, json: info });
  });
  await page.route(/\/api\/v1\/ha\/candidates(\?|$)/, (route) => {
    asked++;
    return route.fulfill({ json: { connection, truncated: [], candidates: sensors } });
  });
  return () => asked;
}
// The tent temperature already serves the air temperature, as gc_ha_server's config would say.
async function airTempPicked(page: Page) {
  await page.route(/\/api\/v1\/config(\?|$)/, async (route) => {
    if (route.request().method() !== "GET") return route.continue();
    const res = await route.fetch();
    const cfg = await res.json();
    cfg.devices = [...cfg.devices, { id: "ha.sensor.zelt_temperatur", class: "ha_air_temp", name: "Zelt Temperatur" }];
    cfg.zones[0].roles["zone.air_temp"] = { device: "ha.sensor.zelt_temperatur", channel: 0 };
    await route.fulfill({ response: res, json: cfg });
  });
}
async function newPassword(page: Page) {
  await page.goto("/");
  await page.locator("input[name=password]").fill("mein-passwort");
  await page.locator("input[name=password2]").fill("mein-passwort");
  await page.getByRole("button", { name: "Passwort festlegen" }).click();
  await expect(page.getByTestId("watchdog")).toBeVisible();
}

test("Home Assistant: from the overview to a sensor per measurement, in one tap each", async ({ page, request }) => {
  await scenario(request, "neu");
  const asked = await asHomeAssistant(page, "ok");
  const picked: unknown[] = [];
  await page.route(/\/api\/v1\/ha\/assign(\?|$)/, (route) => {
    picked.push(route.request().postDataJSON());
    return route.fulfill({ json: { ok: true } });
  });
  await newPassword(page);
  await page.getByTestId("ha-choose-link").click();
  await expect(page).toHaveURL(/tab=zuordnung/);
  await expect(page.getByText("Sensoren aus Home Assistant")).toBeVisible();
  await expect(page.getByRole("tab", { name: "Erweitern" })).toHaveCount(0);  // hardware to buy: not here
  // A role with no fitting sensor is named once, not shown as an empty row
  await expect(page.getByText(/Kein passender Sensor in Home Assistant für: .*Luftfeuchte/)).toBeVisible();
  await expect(page.getByTestId("ha-role-zone.humidity")).toHaveCount(0);
  await page.getByRole("button", { name: "Sensor wählen: pH im Tank" }).click();
  const picker = page.getByTestId("ha-picker");
  await expect(picker.getByText("Growbox pH")).toBeVisible();
  await expect(picker.getByText("Zelt Temperatur")).toHaveCount(0);  // only what measures pH
  await expect(picker.getByRole("button", { name: /Growbox pH/ })).not.toContainText("in Home Assistant");  // nothing converted
  await expect(page.getByText(/Pflanzen- und Poolsensoren/)).toBeVisible();
  const before = asked();
  await picker.getByText("Growbox pH").click();
  await expect(page.getByText("Gespeichert: Growbox pH für pH im Tank")).toBeVisible();
  expect(picked).toEqual([{ role: "tank.ph", entity: "sensor.growbox_ph" }]);
  expect(asked()).toBeGreaterThan(before);  // the list is fetched again after a save
  // EC: the Home Assistant value next to the converted one; TDS in ppm gives no value, never a guess (RAT-006)
  await page.getByRole("button", { name: "Sensor wählen: EC im Tank" }).click();
  await expect(picker.getByRole("button", { name: /Growbox EC/ })).toContainText("in Home Assistant: 1.420 µS/cm");
  await expect(picker.getByRole("button", { name: /Tank TDS/ })).toContainText("Einheit ppm passt nicht");
  await page.keyboard.press("Escape");
  await expect(picker).toHaveCount(0);
  expect(picked).toHaveLength(1);  // closing changes nothing
  // Selected but not bound to any role (e.g. from the mapping file): still free to pick
  await page.getByRole("button", { name: "Sensor wählen: Lufttemperatur" }).click();
  await expect(picker.getByRole("button", { name: /Zelt Temperatur/ })).toBeEnabled();
  await page.keyboard.press("Escape");
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: a sensor serving one measurement cannot be taken for another", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok");
  await airTempPicked(page);
  await newPassword(page);
  await page.goto("/#/geraete?tab=zuordnung");
  await expect(page.getByRole("button", { name: "Ändern: Sensor für Lufttemperatur" })).toBeVisible();
  await page.getByRole("button", { name: "Sensor wählen: Wassertemperatur" }).click();
  const picker = page.getByTestId("ha-picker");
  await expect(picker.getByRole("button", { name: /Zelt Temperatur/ })).toBeDisabled();
  await expect(picker.getByText("Schon für Lufttemperatur gewählt – erst dort ändern")).toBeVisible();
  await expect(picker.getByRole("button", { name: /Kaputt/ })).toContainText("kein Wert");  // never 0 (R5)
  await expect(page.getByText(/Nimm ihn kurz in die Hand/)).toBeVisible();
  await page.keyboard.press("Escape");
  // In its own row it can be changed or taken away
  await page.getByRole("button", { name: "Ändern: Sensor für Lufttemperatur" }).click();
  await expect(picker.getByRole("button", { name: /Zelt Temperatur/ })).toBeEnabled();
  await expect(picker.getByRole("button", { name: "Keinen Sensor nutzen" })).toBeVisible();
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: a refused token or no answer is said as such, not as \"no sensors\"", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "refused", []);
  await newPassword(page);
  await expect(page.getByText("Home Assistant lehnt den Zugang ab. Leg dort einen neuen Token an, speichere ihn in der Token-Datei und starte den Hub neu.")).toBeVisible();
  await page.goto("/#/geraete?tab=zuordnung");
  await expect(page.getByText("Home Assistant lehnt den Zugang ab", { exact: true })).toBeVisible();
  await expect(page.getByText("Keine passenden Sensoren gefunden")).toHaveCount(0);
  // Links to "Expand" (hardware to plug in) land on the sensors here, not on an empty page
  await page.goto("/#/geraete?tab=erweitern");
  await expect(page.getByText("Sensoren aus Home Assistant")).toBeVisible();
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: the picker says when Home Assistant does not answer, instead of showing old values", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "unreachable");
  await airTempPicked(page);
  await newPassword(page);
  await page.goto("/#/geraete?tab=zuordnung");
  await page.getByRole("button", { name: "Ändern: Sensor für Lufttemperatur" }).click();
  await expect(page.getByText("Home Assistant antwortet nicht", { exact: true }).last()).toBeVisible();
  await expect(page.getByTestId("ha-picker").getByText("Wohnzimmer Temperatur")).toHaveCount(0);
  await expect(page.getByText(/passende Sensoren/)).toHaveCount(0);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});
