// SPDX-License-Identifier: AGPL-3.0-or-later
// Vom ersten Einschalten bis zur ersten Mischung – so, wie ein Kunde es erlebt.
import type { Page } from "@playwright/test";
import { expect, login, scenario, simSpeed, test } from "./helpers";

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
  // The browser proposes the time zone; it can be changed, a wrong one is refused at the field,
  // and it is saved in its usual spelling
  const tz = page.locator("input[name=timezone]");
  await expect(tz).toHaveValue("Europe/Berlin");
  await expect(page.getByText("Vom Browser vorgeschlagen.")).toBeVisible();
  await tz.fill("Europa/Berlin");
  await page.getByTestId("wizard-next").click();
  await expect(page.getByText("Diese Zeitzone gibt es nicht. Schreib sie wie Europe/Berlin.")).toBeVisible();
  await expect(tz).toHaveAttribute("aria-invalid", "true");
  await tz.fill("europe/vienna ");
  await page.getByTestId("wizard-next").click();
  // Back to the first step: what was saved, not the browser's proposal again
  await expect(page.locator('.wizard-steps [aria-current="step"]')).toContainText("Geräte");
  await page.getByTestId("wizard-back").click();
  await expect(tz).toHaveValue("Europe/Vienna");
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
    await page.getByRole("dialog").getByRole("button", { name: "Fertig", exact: true }).click();
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

  // Opened again later, the setup keeps the saved time zone instead of proposing the browser's
  await page.goto("/#/einrichtung");
  await expect(tz).toHaveValue("Europe/Vienna");
  await expect(page.getByText("Vom Browser vorgeschlagen.")).toHaveCount(0);
});

test.describe("A browser in New York", () => {
  test.use({ timezoneId: "America/New_York" });
  test("Setup: the first run proposes the browser's time zone, not the hub's factory one", async ({ page, request }) => {
    await scenario(request, "neu");
    await page.goto("/");
    await page.locator("input[name=password]").fill("mein-passwort");
    await page.locator("input[name=password2]").fill("mein-passwort");
    await page.getByRole("button", { name: "Passwort festlegen" }).click();
    await expect(page.locator("input[name=timezone]")).toHaveValue("America/New_York");
    await expect(page.getByText("Vom Browser vorgeschlagen.")).toBeVisible();
  });
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
    cat.deviceClasses.ha_ph = { label: "pH aus Home Assistant", stage: 0, attach: "ha", provides: ["measure.ph"], externalCalibration: true };
    // Looks the same but the hub does not say so: the setup does not guess (#71)
    cat.deviceClasses.ph_unsaid = { label: "pH ohne Angabe", stage: 0, attach: "ha", provides: ["measure.ph"], externalCalibration: false };
    // Calibrated elsewhere too, but a temperature needs no calibration at all, as in the hub
    cat.deviceClasses.ha_water_temp = { label: "Wassertemperatur aus Home Assistant", stage: 0, attach: "ha", provides: ["measure.water_temp"], externalCalibration: true };
    await route.fulfill({ response: res, json: cat });
  });
  await page.route(/\/api\/v1\/config(\?|$)/, async (route) => {
    if (route.request().method() !== "GET") return route.continue();
    const res = await route.fetch();
    const cfg = await res.json();
    cfg.devices = [
      ...cfg.devices.filter((d: { class: string }) => !d.class.startsWith("head_")),
      { id: "ha.sensor.grow_ph", class: "ha_ph", name: "pH aus Home Assistant" },
      { id: "ha.sensor.other_ph", class: "ph_unsaid", name: "pH ohne Angabe" },
      { id: "ha.sensor.water", class: "ha_water_temp", name: "Wassertemperatur aus Home Assistant" },
    ];
    await route.fulfill({ response: res, json: cfg });
  });
  await login(page);
  await page.goto("/#/einrichtung?s=4");
  await expect(page.getByTestId("external-probe")).toHaveCount(1);
  await expect(page.getByTestId("external-probe")).toHaveText("pH aus Home Assistant: wird außerhalb des Hubs kalibriert – hier nichts zu tun.");
  await expect(page.getByText(/Keine pH\/EC-Sonde/)).toHaveCount(0);
});

test("Setup: a temperature from Home Assistant is no probe; without pH, EC or level the hint to measure pH by hand shows (#71)", async ({ page, request }) => {
  await scenario(request, "demo");
  await page.route(/\/api\/v1\/catalog(\?|$)/, async (route) => {
    const res = await route.fetch();
    const cat = await res.json();
    cat.deviceClasses.ha_water_temp = { label: "Wassertemperatur aus Home Assistant", stage: 0, attach: "ha", provides: ["measure.water_temp"], externalCalibration: true };
    await route.fulfill({ response: res, json: cat });
  });
  await page.route(/\/api\/v1\/config(\?|$)/, async (route) => {
    if (route.request().method() !== "GET") return route.continue();
    const res = await route.fetch();
    const cfg = await res.json();
    cfg.devices = [
      ...cfg.devices.filter((d: { class: string }) => !d.class.startsWith("head_")),
      { id: "ha.sensor.water", class: "ha_water_temp", name: "Wassertemperatur aus Home Assistant" },
    ];
    await route.fulfill({ response: res, json: cfg });
  });
  await login(page);
  await page.goto("/#/einrichtung?s=4");
  await expect(page.getByText("Keine pH/EC-Sonde und kein Füllstandssensor erkannt – pH misst du dann von Hand.")).toBeVisible();
  await expect(page.getByTestId("external-probe")).toHaveCount(0);
});

test("Read-only trial (Home Assistant): after the password a short introduction opens, not the setup for dosing hardware (SD-037)", async ({ page, request }) => {
  await scenario(request, "neu");
  // gc_ha_server announces itself as read-only; the setup asks for a dosing block it cannot have
  await asHomeAssistant(page, "ok");
  await airTempPicked(page);
  await page.goto("/");
  await page.locator("input[name=password]").fill("mein-passwort");
  await page.locator("input[name=password2]").fill("mein-passwort");
  await page.getByRole("button", { name: "Passwort festlegen" }).click();
  await expect(page).toHaveURL(/#\/willkommen/);
  await expect(page.getByRole("heading", { name: "Willkommen bei growcontroller" })).toBeVisible();
  // Basics: the name, the colour scheme
  await page.locator("input[name=hubname]").fill("Zelt 1");
  await page.getByRole("tab", { name: "Dunkel" }).click();
  await expect(page.locator("html")).toHaveAttribute("data-theme", "dark");
  await page.getByTestId("wizard-next").click();
  // Sensors: the same choice as under Devices › Assignment
  await expect(page.getByRole("heading", { name: "Ein Sensor je Messwert" })).toBeFocused();
  await expect(page.getByTestId("ha-role-zone.air_temp")).toContainText("Zelt Temperatur");
  await page.getByTestId("wizard-next").click();
  // Control: outside unless chosen, and said that nothing is switched yet
  await expect(page.getByText(/Diese Version schaltet noch nichts/)).toBeVisible();
  await expect(page.getByRole("radiogroup", { name: "Klima" }).getByRole("radio", { name: "Außerhalb der Steuerung" })).toBeChecked();
  await page.getByRole("radiogroup", { name: "Licht" }).getByText("Automatisch").click();
  await expect(page.getByRole("radiogroup", { name: "Licht" }).getByRole("radio", { name: "Automatisch" })).toBeChecked();
  await page.getByTestId("wizard-next").click();
  await expect(page.getByTestId("welcome-sensors")).toHaveText("Sensor gewählt für: Lufttemperatur");
  await expect(page.getByTestId("welcome-control-light")).toContainText("Automatisch");
  await expect(page.getByTestId("welcome-control-dosing")).toContainText("Außerhalb der Steuerung");
  await expect(page.getByText("„Automatisch“ ist vorgemerkt. Diese Version schaltet noch nichts.")).toBeVisible();
  await expect(page.getByTestId("welcome-skip")).toHaveCount(0);  // nothing left to skip
  await page.getByTestId("wizard-next").click();
  await expect(page.getByTestId("watchdog")).toBeVisible();
  const sys = (await (await page.request.get("/api/v1/config")).json()).system;
  expect(sys.name).toBe("Zelt 1");
  expect(sys.setupDone).toBe(true);
  expect(sys.control).toEqual({ climate: "outside", light: "auto", irrigation: "outside", refill: "outside", circulation: "outside", dosing: "outside" });
  await expect(page.getByRole("link", { name: "Einrichtung" })).toHaveCount(0);  // no way into the dosing setup
  await page.goto("/#/einrichtung");
  await expect(page).toHaveURL(/#\/willkommen/);  // not even by its address
  // Area pages: no switches that switch nothing and no note about switching, but what was chosen
  for (const [path, area] of [["klima", "climate"], ["licht", "light"], ["bewaesserung", "irrigation"]]) {
    await page.goto(`/#/${path}`);
    await expect(page.getByTestId(`control-note-${area}`)).toBeVisible();
    await expect(page.locator('[data-testid^="output-"]')).toHaveCount(0);
    await expect(page.getByText(/Manuell schalten|manuell schalten|Gießpumpe läuft nur/)).toHaveCount(0);
  }
  await page.goto("/#/licht");
  await expect(page.getByTestId("control-note-light")).toContainText("„Automatisch“ ist vorgemerkt. Diese Version schaltet noch nichts");
  // Changed right there, in one tap
  await page.goto("/#/klima");
  await expect(page.getByTestId("control-note-climate")).toContainText("Du schaltest das in Home Assistant oder am Gerät.");
  await page.getByRole("radiogroup", { name: "Klima" }).getByText("Automatisch").click();
  await expect(page.getByText("Gespeichert")).toBeVisible();
  await expect(page.getByTestId("control-note-climate")).toContainText("„Automatisch“ ist vorgemerkt.");
  expect((await (await page.request.get("/api/v1/config")).json()).system.control.climate).toBe("auto");
  // Done once: it does not open by itself again; the colour scheme stays
  await page.reload();
  await expect(page).toHaveURL(/#\/klima/);
  await expect(page.locator("html")).toHaveAttribute("data-theme", "dark");
  await page.goto("/#/einstellungen");
  await expect(page.getByRole("tab", { name: "Dunkel" })).toHaveAttribute("aria-selected", "true");
  await page.getByRole("tab", { name: "System" }).click();
  await page.reload();
  await expect(page.locator("html")).not.toHaveAttribute("data-theme", /./);
  // Opened again from Settings, without "skip"
  await page.getByTestId("open-welcome").click();
  await expect(page.getByRole("heading", { name: "Willkommen bei growcontroller" })).toBeVisible();
  await expect(page.getByTestId("welcome-skip")).toHaveCount(0);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Read-only trial (Home Assistant): the introduction can be skipped; nothing is chosen then", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok");
  await newPassword(page);  // skips it
  await expect(page.getByText("Die Einführung findest du jederzeit unter Einstellungen.")).toBeVisible();
  const sys = (await (await page.request.get("/api/v1/config")).json()).system;
  expect(sys.setupDone).toBe(true);
  expect(sys.control).toEqual({});
  await page.reload();
  await expect(page.getByTestId("watchdog")).toBeVisible();  // not opened again
  // The overview points to choosing sensors
  await page.getByTestId("ha-choose-link").click();
  await expect(page).toHaveURL(/#\/geraete\?tab=zuordnung/);
  // No hardware to buy for a climate sensor, but one to choose
  await page.goto("/#/klima");
  await expect(page.getByText("Kein Klimasensor gewählt")).toBeVisible();
  await page.getByRole("link", { name: "Sensor wählen" }).click();
  await expect(page).toHaveURL(/#\/geraete\?tab=zuordnung/);
  await page.goto("/#/bewaesserung");
  await expect(page.getByTestId("control-note-irrigation")).toContainText("Diese App schaltet hier nichts. Wähle, was sie künftig automatisch machen soll.");
  await expect(page.getByRole("radiogroup", { name: "Bewässerung" }).getByRole("radio", { checked: true })).toHaveCount(0);
  await page.getByRole("radiogroup", { name: "Bewässerung" }).getByText("Außerhalb der Steuerung").click();
  await expect(page.getByTestId("control-note-irrigation")).toContainText("Du schaltest das in Home Assistant oder am Gerät.");
  expect((await (await page.request.get("/api/v1/config")).json()).system.control).toEqual({ irrigation: "outside" });
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("A hub with its own hardware has no introduction of the Home Assistant app", async ({ page, request }) => {
  await scenario(request, "neu");
  await page.goto("/");
  await page.locator("input[name=password]").fill("mein-passwort");
  await page.locator("input[name=password2]").fill("mein-passwort");
  await page.getByRole("button", { name: "Passwort festlegen" }).click();
  await page.goto("/#/willkommen");
  await expect(page).toHaveURL(/#\/einrichtung/);  // its own setup instead
  expect((await page.request.post("/api/v1/setup/complete")).ok()).toBe(true);
  await page.reload();  // the page learns of the completed setup
  await expect(page.getByTestId("wizard-next")).toBeVisible();
  await page.goto("/#/willkommen");
  await expect(page.getByTestId("watchdog")).toBeVisible();
  await expect(page.getByRole("heading", { name: "Willkommen bei growcontroller" })).toHaveCount(0);
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
  { entity: "sensor.growbox_wassertemperatur_ds18b20_sonde_unten", name: "Growboxwassertemperatursondeimtankunten", kind: "temperature", measures: ["water_temp", "air_temp"], value: 21.3, raw: 21.3, unit: "°C", problem: null, used: null },
];
// Sensors the hub does not suggest (SD-036), sent only with ?others=1
const haOthers = [
  { entity: "sensor.diy_rh", name: "Selbstbau Feuchte", measures: ["humidity"], value: 58, raw: 58, unit: "%", problem: null, deviceClass: "", used: null },
  { entity: "sensor.diy_co2", name: "Selbstbau CO2", measures: ["co2"], value: 720, raw: 720, unit: "ppm", problem: null, deviceClass: "", used: null },
  { entity: "sensor.handy_akku", name: "Handy Akku", measures: [], value: null, raw: null, unit: "%", problem: "device_class", deviceClass: "battery", used: null },
  { entity: "sensor.rh_raw", name: "Feuchte ohne Einheit", measures: [], value: null, raw: null, unit: "", problem: "unit_missing", deviceClass: "", used: null },
];
async function asHomeAssistant(page: Page, connection: string, sensors: unknown[] = haSensors, others: unknown[] = []) {
  let asked = 0;
  await page.route(/\/api\/v1\/info(\?|$)/, async (route) => {
    const res = await route.fetch();
    const info = await res.json();
    info.platform = { kind: "home-assistant", simulated: false, readOnly: true };
    await route.fulfill({ response: res, json: info });
  });
  await page.route(/\/api\/v1\/ha\/candidates(\?|$)/, (route) => {
    asked++;
    const withOthers = new URL(route.request().url()).searchParams.get("others") === "1";
    const otherFits: Record<string, number> = {};
    for (const o of others as { measures: string[] }[]) for (const m of o.measures) otherFits[m] = (otherFits[m] ?? 0) + 1;
    return route.fulfill({ json: { connection, truncated: [], candidates: sensors, otherFits, otherCount: others.length, ...(withOthers ? { others } : {}) } });
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
  await page.getByTestId("welcome-skip").click();  // the introduction (SD-037) has its own test
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
  await expect(picker).toHaveCount(0);
  // Escape closes a window as soon as it is visible, not only a frame later
  for (let i = 0; i < 5; i++) {
    await page.getByRole("button", { name: "Sensor wählen: EC im Tank" }).click();
    await page.keyboard.press("Escape");
    await expect(picker).toHaveCount(0);
  }
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: a sensor the hub does not suggest can be chosen from the other sensors, in a unit that fits (SD-036)", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok", haSensors, haOthers);
  const picked: unknown[] = [];
  await page.route(/\/api\/v1\/ha\/assign(\?|$)/, (route) => {
    picked.push(route.request().postDataJSON());
    return route.fulfill({ json: { ok: true } });
  });
  await newPassword(page);
  await page.goto("/#/geraete?tab=zuordnung");
  // Humidity, CO2 and level have no suggestion: each can still be opened, level at least to see why
  await expect(page.getByTestId("ha-pick-anyway")).toContainText("Ohne Vorschlag – trotzdem einen Sensor wählen für:");
  await expect(page.getByText(/Kein passender Sensor in Home Assistant für/)).toHaveCount(0);
  await page.getByRole("button", { name: "Sensor wählen: Füllstand" }).click();
  await expect(page.getByTestId("ha-others").getByText("Kein anderer Sensor passt zu Füllstand.")).toBeVisible();
  await expect(page.getByTestId("ha-others-not").getByRole("button", { name: /Handy Akku/ })).toBeVisible();  // the reasons open
  await page.keyboard.press("Escape");
  await expect(page.getByRole("dialog")).toHaveCount(0);
  await page.getByRole("button", { name: "Sensor wählen: Luftfeuchte" }).click();
  const picker = page.getByRole("dialog");
  await expect(picker).toBeVisible();
  // No suggestion: the other sensors show at once, the fitting first, the rest folded with the reason
  await expect(page.getByText("Kein Vorschlag. Wähle unten selbst einen Sensor.")).toBeVisible();
  const others = page.getByTestId("ha-others");
  await expect(others.getByRole("button", { name: /Selbstbau Feuchte/ })).toBeEnabled();
  const not = page.getByTestId("ha-others-not");
  await expect(not.locator("summary")).toHaveText("Passt nicht zu Luftfeuchte (3) – mit Grund");
  await expect(not.getByRole("button", { name: /Handy Akku/ })).not.toBeVisible();
  await not.locator("summary").click();
  await expect(not.getByRole("button", { name: /Handy Akku/ })).toBeDisabled();
  await expect(not.getByRole("button", { name: /Handy Akku/ })).toContainText("Misst laut Home Assistant Akkustand");
  await expect(not.getByRole("button", { name: /Feuchte ohne Einheit/ })).toContainText("Ohne Einheit – der Hub kann den Wert nicht verwenden");
  await expect(not.getByRole("button", { name: /Selbstbau CO2/ })).toContainText("Einheit ppm passt nur zu CO2");
  // A search covers both lists; Escape clears it first and keeps the window open
  const search = page.getByPlaceholder("Nach Namen suchen, z. B. Zelt");
  await search.fill("handy");
  await expect(not.getByRole("button", { name: /Handy Akku/ })).toBeVisible();
  await expect(others.getByText("Kein Sensor passt zu „handy“.")).toHaveCount(0);  // it is found, just below with its reason
  await search.fill("xyz");
  await expect(others.getByText("Kein Sensor passt zu „xyz“.")).toBeVisible();
  await search.press("Escape");
  await expect(search).toHaveValue("");
  await expect(picker).toBeVisible();
  await others.getByRole("button", { name: /Selbstbau Feuchte/ }).click();
  await expect(page.getByText("Gespeichert: Selbstbau Feuchte für Luftfeuchte")).toBeVisible();
  expect(picked).toEqual([{ role: "zone.humidity", entity: "sensor.diy_rh" }]);
  // With suggestions, the others come on request; there nothing fits pH, and the reason is said
  await page.getByRole("button", { name: "Sensor wählen: pH im Tank" }).click();
  await expect(picker).toBeVisible();
  await expect(page.getByTestId("ha-others")).toHaveCount(0);
  await expect(page.getByText("Dein Sensor ist nicht dabei?")).toBeVisible();
  await page.getByTestId("ha-show-others").click();
  await expect(page.getByTestId("ha-others").getByText("Kein anderer Sensor passt zu pH im Tank.")).toBeVisible();
  // Nothing fits, so the reasons are open already
  await expect(page.getByTestId("ha-others-not").getByRole("button", { name: /Selbstbau Feuchte/ })).toContainText("Einheit % passt nur zu Luftfeuchte");
  await page.keyboard.press("Escape");
  await expect(picker).toHaveCount(0);
  expect(picked).toHaveLength(1);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: without any suggestion, the sensors of one's own choosing are offered right away (SD-036)", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok", [], haOthers);
  await newPassword(page);
  await page.goto("/#/geraete?tab=zuordnung");
  await expect(page.getByText("Keine passenden Sensoren gefunden")).toBeVisible();
  await page.getByRole("button", { name: "Sensor wählen: CO2" }).click();
  await expect(page.getByTestId("ha-others").getByRole("button", { name: /Selbstbau CO2/ })).toBeEnabled();
  await page.keyboard.press("Escape");
  await expect(page.getByRole("dialog")).toHaveCount(0);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: the history shows only curves that can have values, and says how to get more (SD-039)", async ({ page, request }) => {
  await scenario(request, "neu");
  await page.goto("/");
  await page.locator("input[name=password]").fill("mein-passwort");
  await page.locator("input[name=password2]").fill("mein-passwort");
  await page.getByRole("button", { name: "Passwort festlegen" }).click();
  // The simulated hub mixes, so its volume from mixes stays (the setup is skipped for this look)
  await page.goto("/#/verlauf?skip=1");
  await expect(page.getByText("Volumen (aus Mischläufen)")).toBeVisible();
  await expect(page.getByTestId("history-more")).toHaveCount(0);
  expect((await page.request.post("/api/v1/setup/complete")).ok()).toBe(true);  // past the introduction (SD-037)
  await asHomeAssistant(page, "ok");
  await page.reload();
  await expect(page.getByTestId("history-more")).toBeVisible();
  await expect(page.getByText("Volumen (aus Mischläufen)")).toHaveCount(0);  // nothing mixes here
  await page.getByTestId("history-more").getByRole("link").click();
  await expect(page).toHaveURL(/tab=zuordnung/);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: no emergency stop where nothing is switched, but a stop from before can be resumed (SD-040)", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok");
  await newPassword(page);
  await expect(page.getByTitle("Not-Halt: alle Pumpen und Ausgänge aus")).toHaveCount(0);
  await page.getByTestId("read-only-tag").click();
  const dialog = page.getByRole("dialog");
  await expect(dialog.getByText("Kein Not-Halt in dieser App")).toBeVisible();
  // The window is whole on a phone too, over a dimmed page (it was cut off when opened from the top bar)
  await page.setViewportSize({ width: 375, height: 740 });
  expect((await dialog.boundingBox())!.y).toBeGreaterThanOrEqual(0);
  const overlay = (await page.locator(".overlay").boundingBox())!;
  expect(overlay.width).toBeGreaterThanOrEqual(375);
  expect(overlay.height).toBeGreaterThanOrEqual(740);
  await page.setViewportSize({ width: 1360, height: 900 });
  await expect(dialog.getByText(/Im Notfall schaltest du in Home Assistant oder direkt am Gerät aus/)).toBeVisible();
  await dialog.getByRole("button", { name: "Verstanden" }).click();
  await expect(dialog).toHaveCount(0);
  // A phone shows it too
  await page.setViewportSize({ width: 375, height: 740 });
  await expect(page.getByTestId("read-only-tag")).toBeVisible();
  // Stopped by an earlier version: it can be cleared, and nothing claims that everything is off
  expect((await page.request.post("/api/v1/stop")).status()).toBe(200);
  await page.reload();
  await expect(page.getByText("Not-Halt aktiv.")).toBeVisible();
  await expect(page.getByText(/Er schaltet nichts aus: Was Home Assistant schaltet, läuft weiter/)).toBeVisible();
  await expect(page.getByText(/Alle Pumpen und Ausgänge sind aus/)).toHaveCount(0);
  await expect(page.getByTestId("read-only-tag")).toBeVisible();
  // On a phone the bar holds both without scrolling sideways
  const clear = page.getByRole("button", { name: "Not-Halt aufheben" });
  const box = await clear.boundingBox();
  expect(box!.x + box!.width).toBeLessThanOrEqual(375);
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(375);
  await clear.click();
  await expect(page.getByText("Not-Halt aktiv.")).toHaveCount(0);
  await expect(page.getByTestId("read-only-tag")).toBeVisible();
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

test("Home Assistant: on a phone, a long sensor name never pushes its live value out of view", async ({ page, request }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok");
  await newPassword(page);
  await page.goto("/#/geraete?tab=zuordnung");
  await page.getByRole("button", { name: "Sensor wählen: Wassertemperatur" }).click();
  const option = page.getByTestId("ha-picker").getByRole("button", { name: /Growboxwassertemperatursonde/ });
  const value = option.locator(".ha-value");
  await expect(value).toBeVisible();
  const box = await value.boundingBox();
  expect(box!.x + box!.width).toBeLessThanOrEqual(390);
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(390);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: if the hub delivers no sensor list, the assigned sensors still show, with a notice", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok");
  await airTempPicked(page);
  await page.route(/\/api\/v1\/ha\/candidates(\?|$)/, (route) => route.fulfill({ status: 500, json: { error: "x" } }));
  await newPassword(page);
  await page.goto("/#/geraete?tab=zuordnung");
  await expect(page.getByText("Der Hub liefert gerade keine Sensorliste. Die Seite versucht es weiter, deine Zuordnungen bleiben gespeichert.")).toBeVisible({ timeout: 20000 });
  await expect(page.getByTestId("ha-role-zone.air_temp")).toContainText("Zelt Temperatur");
  await expect(page.getByText("Der Hub fragt Home Assistant nach Sensoren …")).toHaveCount(0);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: \"renamed?\" is asked only while Home Assistant answers", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok", haSensors.filter((c) => c.entity !== "sensor.zelt_temperatur"));
  await airTempPicked(page);
  // The picked sensor is offline in the hub; the state comes by polling, not from the live stream
  await page.route(/\/api\/v1\/events\/stream/, (route) => route.abort());
  await page.route(/\/api\/v1\/state(\?|$)/, async (route) => {
    const res = await route.fetch();
    const st = await res.json();
    st.devices = [...st.devices, { id: "ha.sensor.zelt_temperatur", class: "ha_air_temp", classLabel: "", name: "Zelt Temperatur", configured: true, online: false, port: 0, slot: 0, parent: "", fw: "", fault: "", info: {}, calibrations: {} }];
    await route.fulfill({ response: res, json: st });
  });
  await newPassword(page);
  await page.goto("/#/geraete?tab=zuordnung");
  const row = page.getByTestId("ha-role-zone.air_temp");
  await expect(row).toContainText("fehlt in Home Assistant");
  // Home Assistant stops answering: the notice says why, the row does not guess a rename
  await asHomeAssistant(page, "unreachable");
  await expect(page.getByText("Home Assistant antwortet nicht", { exact: true })).toBeVisible({ timeout: 10000 });
  await expect(row).not.toContainText("fehlt in Home Assistant");
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

test("Home Assistant: a pH probe's voltage is suggested first, as a raw value calibrated in the app (SD-038, SD-041)", async ({ page, request }) => {
  await scenario(request, "neu");
  const raw = { entity: "sensor.tank_ph_mv", name: "Tank pH Spannung", kind: "ph", measures: ["ph"], value: null, raw: -10, unit: "mV", problem: null, used: null, signal: true };
  await asHomeAssistant(page, "ok", [...haSensors, raw], [
    { entity: "sensor.esp_vcc", name: "ESP Versorgung", measures: ["ph"], value: null, raw: 3.3, unit: "V", problem: null, deviceClass: "", used: null, signal: true },
  ]);
  const picked: unknown[] = [];
  await page.route(/\/api\/v1\/ha\/assign(\?|$)/, (route) => {
    picked.push(route.request().postDataJSON());
    return route.fulfill({ json: { ok: true } });
  });
  await newPassword(page);
  await page.goto("/#/geraete?tab=zuordnung");
  await page.getByRole("button", { name: "Sensor wählen: pH im Tank" }).click();
  const picker = page.getByTestId("ha-picker");
  // First, ahead of the pH calibrated in Home Assistant, with its voltage as Home Assistant reports it
  const first = picker.getByRole("button").first();
  await expect(first).toContainText("Tank pH Spannung");
  await expect(first).toContainText("Empfohlen: Rohwert der Sonde – du kalibrierst ihn hier mit zwei Puffern");
  await expect(first).toContainText("-10 mV");
  await expect(picker.getByRole("button", { name: /Growbox pH/ })).not.toContainText("Rohwert");
  // Any other voltage only under the other sensors, marked the same way
  await page.getByTestId("ha-show-others").click();
  await expect(page.getByTestId("ha-others").getByRole("button", { name: /ESP Versorgung/ })).toContainText("Nur wenn das deine pH-Sonde ist");
  await first.click();
  await expect(page.getByText("Gespeichert: Tank pH Spannung für pH im Tank. Kalibriere ihn jetzt mit zwei Puffern.")).toBeVisible();
  expect(picked).toEqual([{ role: "tank.ph", entity: "sensor.tank_ph_mv" }]);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: a sensor gets a sign of life; its age then counts from the last measurement, in colour and words (SD-042)", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok");
  await newPassword(page);
  const device = "ha.sensor.tank_water";
  let life: { source: string; channel: string } | undefined;
  const sent: Record<string, unknown>[] = [];
  let ageMs = 12000;
  await page.route(/\/api\/v1\/events\/stream/, (route) => route.abort());  // the state comes from the polls below
  const lives = [
    { entity: "sensor.other_life", name: "Anderes Gerät", kind: "text", channels: [{ code: "zz", ageS: 4 }] },
    { entity: "sensor.tank_hub_life", name: "Hub Lebenszeichen", kind: "text", channels: [{ code: "wt", ageS: 3 }, { code: "ph", ageS: null }] },
    { entity: "sensor.tent_life", name: "Zelt Lebenszeichen", kind: "text", channels: [{ code: "at", ageS: 2 }] },  // shares only "sensor.t"
    { entity: "sensor.water_read", name: "Wasser gelesen", kind: "timestamp", ageS: 20 },
  ];
  let askedLives = 0;
  await page.route(/\/api\/v1\/ha\/candidates(\?|$)/, (route) => {
    // The list of signs of life only on request, as the other sensors (SD-036)
    const withLives = new URL(route.request().url()).searchParams.get("lives") === "1";
    if (withLives) askedLives++;
    return route.fulfill({
      json: {
        connection: "ok",
        truncated: [],
        candidates: [
          { entity: "sensor.tank_water", name: "Tank Wasser", kind: "temperature", measures: ["water_temp", "air_temp"], value: 21.5, raw: 21.5, unit: "°C", problem: null, used: "water_temp",
            ...(life ? { life: { entity: life.source, channel: life.channel } } : {}) },
        ],
        otherFits: {},
        otherCount: 0,
        lifeCount: lives.length,
        lifeNames: life ? { [life.source]: lives.find((l) => l.entity === life!.source)?.name } : {},
        ...(withLives ? { lives } : {}),
      },
    });
  });
  await page.route(/\/api\/v1\/config(\?|$)/, async (route) => {
    if (route.request().method() !== "GET") return route.continue();
    const res = await route.fetch();
    const cfg = await res.json();
    cfg.devices = [...cfg.devices, { id: device, class: "ha_water_temp", name: "Tank Wasser", ...(life ? { life } : {}) }];
    cfg.tanks[0].roles["tank.water_temp"] = { device, channel: 0 };
    await route.fulfill({ response: res, json: cfg });
  });
  await page.route(/\/api\/v1\/state(\?|$)/, async (route) => {
    const res = await route.fetch();
    const st = await res.json();
    st.devices = [...st.devices, { id: device, class: "ha_water_temp", classLabel: "Wassertemperatur aus Home Assistant", name: "Tank Wasser", configured: true, online: true, port: -1, slot: -1, parent: "", fw: "", fault: "", info: { entity: "sensor.tank_water" }, calibrations: {} }];
    st.readings["tank.water_temp"] = { value: 21.5, quality: "ok", usable: true, unit: "°C", decimals: 1, ageS: Math.floor(ageMs / 1000), ageMs, ageOf: life ? "measurement" : "change", reason: { key: "truth.ok", text: "", args: {} } };
    await route.fulfill({ response: res, json: st });
  });
  await page.route(/\/api\/v1\/ha\/life(\?|$)/, (route) => {
    const body = route.request().postDataJSON();
    sent.push(body);
    life = body.source ? { source: body.source, channel: body.channel } : undefined;
    return route.fulfill({ json: { ok: true } });
  });
  await page.goto("/#/geraete?tab=zuordnung");
  await page.reload();
  const row = page.getByTestId("ha-role-tank.water_temp");
  // Without a sign of life the app cannot see the last measurement, only the last change
  await expect(row).toContainText("unverändert seit 12 s");
  expect(askedLives).toBe(0);
  // On a phone the buttons sit below the sensor; nothing runs off the screen
  await page.setViewportSize({ width: 375, height: 740 });
  expect(await row.evaluate((el) => el.scrollWidth <= el.clientWidth)).toBe(true);
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(375);
  await page.setViewportSize({ width: 1360, height: 900 });
  await page.getByTestId("ha-life-tank.water_temp").click();
  const dialog = page.getByRole("dialog");
  await expect(dialog).toContainText("Home Assistant meldet einen gleichen Wert nicht neu");
  // The device's own sign of life first, marked so, each channel with its age now; another device is said so
  await expect(dialog.locator("fieldset legend").first()).toContainText("Hub Lebenszeichen");
  await expect(dialog.locator("fieldset legend").first()).toContainText("vermutlich gleiches Gerät");
  await expect(dialog.locator("fieldset legend").nth(1)).toContainText("Anderes Gerät – nur wählen, wenn dein Sensor daran hängt");
  // One shared letter is no shared device name: sorted by name among the others, marked as another device
  await expect(dialog.locator("fieldset legend").nth(2)).toContainText("Zelt Lebenszeichen");
  await expect(dialog.locator("fieldset legend").nth(2)).toContainText("Anderes Gerät – nur wählen, wenn dein Sensor daran hängt");
  await expect(dialog.getByTestId("ha-life-option-sensor.tank_hub_life-wt")).toContainText(/passt zu/);
  await expect(dialog.getByTestId("ha-life-option-sensor.tank_hub_life-ph")).toContainText("noch keine Messung");
  await expect(dialog.getByTestId("ha-life-option-sensor.water_read")).toContainText("vor 20 s");
  await dialog.getByTestId("ha-life-option-sensor.tank_hub_life-wt").click();
  await expect(page.getByText(/Lebenszeichen für .+ gespeichert/)).toBeVisible();
  expect(sent).toEqual([{ role: "tank.water_temp", source: "sensor.tank_hub_life", channel: "wt" }]);
  await expect(row).toContainText("Lebenszeichen: Hub Lebenszeichen, Kanal wt");
  // Now by the measurement: green up to 30 s, yellow from 31 s, in words too
  await expect(row.locator(".age")).toHaveText("gemessen vor 12 s");
  await expect(row.locator(".age")).toHaveClass(/age-fresh/);
  ageMs = 45000;
  await expect(row.locator(".age")).toHaveText("gemessen vor 45 s", { timeout: 10000 });
  await expect(row.locator(".age")).toHaveClass(/age-late/);
  await expect(row.locator(".age")).toHaveAttribute("title", "über 30 s alt");
  // In whole seconds rounded half up: 30.499 s is still green, 30.5 s is yellow, 60 s red
  for (const [ms, tone] of [[30499, "age-fresh"], [30500, "age-late"], [60000, "age-old"]] as const) {
    ageMs = ms;
    await expect(row.locator(".age")).toHaveClass(new RegExp(tone), { timeout: 10000 });
  }
  await expect(row.locator(".age")).toHaveText("gemessen vor 1 min");
  // And off again
  await page.getByTestId("ha-life-tank.water_temp").click();
  await dialog.getByRole("button", { name: "Kein Lebenszeichen" }).click();
  await expect(page.getByText(/Lebenszeichen für .+ entfernt/)).toBeVisible();
  expect(sent[1]).toEqual({ role: "tank.water_temp", source: "", channel: "" });
  await page.unrouteAll({ behavior: "ignoreErrors" });
});

test("Home Assistant: a pH voltage is calibrated from the assignment; each step waits for the sensor's next reading (SD-041)", async ({ page, request }) => {
  await scenario(request, "neu");
  await asHomeAssistant(page, "ok");
  await newPassword(page);
  const device = "ha.sensor.tank_ph_mv";
  let session: Record<string, unknown> | null = null;
  const probes: Record<string, unknown>[] = [];
  await page.route(/\/api\/v1\/events\/stream/, (route) => route.abort());  // the state comes from the polls below
  await page.route(/\/api\/v1\/catalog(\?|$)/, async (route) => {
    const res = await route.fetch();
    const cat = await res.json();
    cat.deviceClasses.ha_ph_raw = { label: "pH aus Home Assistant (Rohwert)", stage: 0, attach: "ha", provides: ["measure.ph"], calibrations: ["ph"] };
    await route.fulfill({ response: res, json: cat });
  });
  await page.route(/\/api\/v1\/config(\?|$)/, async (route) => {
    if (route.request().method() !== "GET") return route.continue();
    const res = await route.fetch();
    const cfg = await res.json();
    cfg.devices = [...cfg.devices, { id: device, class: "ha_ph_raw", name: "Tank pH Spannung" }];
    cfg.tanks[0].roles["tank.ph"] = { device, channel: 0 };
    await route.fulfill({ response: res, json: cfg });
  });
  await page.route(/\/api\/v1\/state(\?|$)/, async (route) => {
    const res = await route.fetch();
    const st = await res.json();
    st.devices = [...st.devices, { id: device, class: "ha_ph_raw", classLabel: "pH aus Home Assistant (Rohwert)", name: "Tank pH Spannung", configured: true, online: true, port: -1, slot: -1, parent: "", fw: "", fault: "", info: { entity: "sensor.tank_ph_mv" }, calibrations: {} }];
    st.readings["tank.ph"] = { ...st.readings["tank.ph"], value: 7.169, quality: "uncalibrated", usable: false, ageS: 20, reason: { key: "truth.uncalibrated", text: "Nicht kalibriert", args: {} } };
    st.probeCalibration = session ? { [`${device}:ph`]: session } : {};
    await route.fulfill({ response: res, json: st });
  });
  await page.route(/\/api\/v1\/probe(\?|$)/, (route) => {
    const body = route.request().postDataJSON();
    probes.push(body);
    if (body.action === "start") session = { device, kind: "ph", points: [] };
    if (body.action === "point" && session) session = { ...session, pending: { since: 1, reference: body.reference } };
    if (body.action === "cancel") session = null;
    return route.fulfill({ json: session ?? { ok: true } });
  });
  await page.goto("/#/geraete?tab=zuordnung");
  await page.reload();
  const row = page.getByTestId("ha-role-tank.ph");
  // Before its calibration: the voltage it reports, and what to do
  await expect(row).toContainText("Rohwert -10 mV");
  await expect(row).toContainText("Tippe auf „Kalibrieren“. Du brauchst Pufferlösung pH 7 und pH 4.");
  await expect(page.getByTestId("ha-role-tank.ph").getByText("Nicht kalibriert", { exact: true })).toHaveCount(0);  // said once, by the pill
  await page.getByTestId("ha-calibrate-tank.ph").click();
  const dialog = page.getByRole("dialog");
  await expect(dialog).toContainText("-10 mV");
  await dialog.getByTestId("probe-take").click();
  // Waiting for the sensor's next report, with a way out
  await expect(dialog.getByTestId("probe-pending")).toContainText("Warte auf den nächsten Messwert vom Sensor");
  await expect(dialog.getByTestId("probe-pending")).toContainText("in der Pufferlösung pH 7,00");
  await expect(dialog.getByTestId("probe-take")).toHaveCount(0);
  await expect(dialog.getByTestId("probe-cancel")).toBeFocused();
  // The hub takes the point from the next report: on to pH 4
  session = { device, kind: "ph", points: [[7.17, 7]] };
  await expect(dialog.locator(".ws.cur")).toContainText("Puffer 4,00");
  await expect(dialog.getByTestId("probe-take")).toBeVisible();
  // No report in time: said, and the step can be taken again
  session = { device, kind: "ph", points: [[7.17, 7]], failed: { key: "probe.no_raw", text: "No new reading from the sensor.", args: {} } };
  await expect(dialog.locator(".banner")).toContainText("Kein neuer Messwert vom Sensor");
  await expect(dialog.getByTestId("probe-take")).toBeVisible();
  // Closed: the calibration ends on the hub too
  await page.keyboard.press("Escape");
  await expect.poll(() => probes.some((b) => b.action === "cancel")).toBe(true);
  await page.unrouteAll({ behavior: "ignoreErrors" });
});
