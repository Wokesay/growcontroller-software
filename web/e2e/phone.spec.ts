// SPDX-License-Identifier: AGPL-3.0-or-later
// Phone width (#33): no page is wider than the screen, so a phone opens it at
// 100 % and nothing has to be zoomed out first; and no field makes iOS Safari
// zoom in when it gets the focus (it does so below 16 px and stays zoomed).
import type { Locator, Page } from "@playwright/test";
import { expect, login, scenario, test } from "./helpers";

// Each page with what it loads after the frame, so the check sees it
const PAGES: [string, ((p: Page) => Promise<unknown>)?][] = [
  ["/", (p) => expect(p.getByTestId("event").first()).toBeVisible()],
  ["/tank"],
  ["/mischen", async (p) => {
    await p.locator("input[name=water]").fill("40");
    await expect(p.locator(".content table").first()).toBeVisible();
  }],
  ["/funktionen"],
  ["/klima"],
  ["/licht"],
  ["/bewaesserung"],
  ["/verlauf", (p) => expect(p.locator(".chart").first()).toBeVisible()],
  ["/geraete"],
  ["/geraete?tab=zuordnung"],
  ["/geraete?tab=erweitern"],
  ["/rezepte"],
  ["/einstellungen", (p) => expect(p.getByTestId("source-link")).toBeVisible()],
];

// What runs past the right edge of the screen, and any box that scrolls
// sideways although it should not (a dialog, the drawer). Only a wide table
// in its own scroll box and code may scroll sideways.
async function offScreen(page: Page): Promise<string[]> {
  return page.evaluate(() => {
    const vw = document.documentElement.clientWidth;
    const scroller = ".table-wrap, pre";
    const describe = (el: Element) => {
      const cls = (el.getAttribute("class") ?? "").trim().replace(/\s+/g, ".");
      return `${el.tagName.toLowerCase()}${cls ? "." + cls : ""} "${(el.textContent ?? "").trim().slice(0, 40)}"`;
    };
    const sticksOut = (el: Element) => {
      const r = el.getBoundingClientRect();
      return r.width > 0 && r.height > 0 && r.right > vw + 1 && getComputedStyle(el).visibility !== "hidden" && !el.parentElement?.closest(scroller);
    };
    const out: string[] = [];
    const sw = document.documentElement.scrollWidth;
    if (sw > vw) out.push(`page ${sw} px wide`);
    for (const el of Array.from(document.body.querySelectorAll("*"))) {
      if (el.parentElement?.closest("svg")) continue;  // an icon counts once
      if (sticksOut(el) && !Array.from(el.children).some(sticksOut))  // the innermost one
        out.push(`${describe(el)} to ${Math.round(el.getBoundingClientRect().right)} px`);
      const he = el as HTMLElement;
      if (!el.matches(scroller) && ["auto", "scroll"].includes(getComputedStyle(el).overflowX) && he.scrollWidth > he.clientWidth + 1)
        out.push(`${describe(el)} scrolls sideways (${he.scrollWidth} px in ${he.clientWidth} px)`);
    }
    return out;
  });
}

// Fields below 16 px, and how many fields there were
async function smallFields(page: Page): Promise<{ seen: number; small: string[] }> {
  return page.evaluate(() => {
    const fields = Array.from(document.querySelectorAll<HTMLInputElement>("input, select, textarea")).filter(
      (el) => el.getClientRects().length > 0 && !["checkbox", "radio", "hidden", "range", "file"].includes(el.type),
    );
    const small = fields.filter((el) => parseFloat(getComputedStyle(el).fontSize) < 16);
    return { seen: fields.length, small: small.map((el) => `${el.tagName.toLowerCase()}[name=${el.getAttribute("name") ?? ""}] ${getComputedStyle(el).fontSize}`) };
  });
}

async function fits(page: Page, where: string) {
  expect(await offScreen(page), where).toEqual([]);
  expect((await smallFields(page)).small, where).toEqual([]);
}

type Box = { x: number; y: number; width: number; height: number };
function apart(a: Box, b: Box) {
  return a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y || b.y + b.height <= a.y;
}

async function newPassword(page: Page) {
  await page.goto("/");
  await page.locator("input[name=password]").fill("mein-passwort");
  await page.locator("input[name=password2]").fill("mein-passwort");
  await page.getByRole("button", { name: "Passwort festlegen" }).click();
}

for (const lang of ["de", "en"]) {
  test(`Every page fits the phone's screen (${lang})`, async ({ page, request }) => {
    await scenario(request, "demo");
    await login(page);
    await page.evaluate((l) => localStorage.setItem("gc.lang", l), lang);
    for (const [path, loaded] of PAGES) {
      await page.goto(`/#${path}`);
      await page.reload();
      await expect(page.locator("html")).toHaveAttribute("lang", lang);
      await expect(page.locator(".topbar h1")).toBeVisible();
      await loaded?.(page);
      await fits(page, path);
    }
  });
}

test("A device row keeps its name readable: the buttons go below it", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  await page.goto("/#/geraete");
  const rows = page.locator('[data-testid^="device-"]');
  await expect(rows.first()).toBeVisible();
  for (const row of await rows.all()) {
    // Never squeezed to a letter per line: the name takes at most two lines
    const lines = await row.locator("strong").first().evaluate((el) => {
      const lh = parseFloat(getComputedStyle(el).lineHeight) || parseFloat(getComputedStyle(el).fontSize) * 1.3;
      return Math.round(el.getBoundingClientRect().height / lh);
    });
    expect(lines, await row.getAttribute("data-testid") ?? "").toBeLessThanOrEqual(2);
  }
});

test("Values that stop coming show as a bar on the phone, where the live badge is hidden", async ({ page, request }) => {
  await scenario(request, "demo");
  await login(page);
  // Without the live stream the polls stand in for it: the values stay current, no bar
  await page.route(/\/api\/v1\/events\/stream/, (route) => route.abort());
  await page.reload();
  await expect(page.getByTestId("event").first()).toBeVisible();
  await page.waitForTimeout(12_000);  // longer than the 10 s after which values count as not current
  await expect(page.getByTestId("offline-bar")).toHaveCount(0);
  // Then the polls get no answer either
  await page.route(/\/api\/v1\/state(\?|$)/, (route) => route.abort());
  const bar = page.getByTestId("offline-bar");
  await expect(bar).toBeVisible({ timeout: 20_000 });
  await expect(bar).toContainText("Keine Verbindung zum Hub – die Werte sind nicht aktuell.");
  await fits(page, "offline bar");
  // It stays in view below the top bar when the page scrolls
  await page.evaluate(() => window.scrollBy(0, 2000));
  const top = await page.locator(".topbar").boundingBox();
  const box = await bar.boundingBox();
  expect(top && box && Math.abs(box.y - (top.y + top.height)) < 2, "bar right below the top bar").toBe(true);
  // Values again: the bar goes
  await page.unrouteAll({ behavior: "ignoreErrors" });
  await expect(bar).toHaveCount(0, { timeout: 20_000 });
});

test("The setup fits the phone's screen in every step; its footer leaves the simulator's button free", async ({ page, request }) => {
  await scenario(request, "neu");
  await newPassword(page);
  await expect(page).toHaveURL(/#\/einrichtung/);
  for (let s = 0; s < 6; s++) {
    await page.goto(`/#/einrichtung?s=${s}`);
    await expect(page.locator('.wizard-steps [aria-current="step"]')).toContainText(s < 5 ? String(s + 1) : "Fertig");
    await fits(page, `setup step ${s + 1}`);
    const fab = await page.locator(".sim-fab").boundingBox();
    const foot = await page.locator(".wizard-foot").boundingBox();
    expect(fab && foot, `setup step ${s + 1}: simulator button and footer shown`).toBeTruthy();
    expect(apart(fab!, foot!), `setup step ${s + 1}`).toBe(true);
  }
});

test("The Home Assistant app's first-password page with its setup code fits the phone's screen (#79)", async ({ page, request }) => {
  await scenario(request, "neu");
  // gc_ha_server asks for a code; here the simulator's answer is changed to look like that
  await page.route(/\/api\/v1\/info(\?|$)/, async (route) => {
    const res = await route.fetch();
    const info = await res.json();
    await route.fulfill({ response: res, json: { ...info, platform: { kind: "home-assistant", simulated: false, readOnly: true }, setupCode: true } });
  });
  await page.goto("/");
  await expect(page.locator("input[name=code]")).toBeVisible();
  await page.locator("input[name=code]").fill("K7QF-M2XP-9WTB-H4NC");
  await fits(page, "first password with the setup code");
});

test("Fields are at least 16 px, so iOS Safari does not zoom in when typing", async ({ page, request }) => {
  await scenario(request, "demo");
  await page.goto("/");
  await expect(page.locator("input[name=password]")).toBeVisible();
  const check = async (where: string) => {
    const f = await smallFields(page);
    expect(f.seen, where).toBeGreaterThan(0);
    expect(f.small, where).toEqual([]);
  };
  await check("login");
  await login(page);
  for (const path of ["/einstellungen", "/mischen"]) {
    await page.goto(`/#${path}`);
    await expect(page.locator(".topbar h1")).toBeVisible();
    await check(path);
  }
  // A dialog too: editing a bottle
  await page.goto("/#/rezepte");
  await page.locator(".rtable tbody tr").first().getByTitle("Bearbeiten").click();
  await expect(page.getByRole("dialog").locator("input").first()).toBeVisible();
  await check("bottle dialog");
  expect(await offScreen(page), "bottle dialog").toEqual([]);
});

// A device that sends nothing is one problem (#107); it names its sign of
// life by the entity ID, a long word without a break.
test("Monitoring: a device that sends nothing is one problem titled with its readings; its long sign of life fits the phone's screen (#107)", async ({ page, request }) => {
  await scenario(request, "demo");
  // The Home Assistant trial's state, made from the simulator's: one device's sign of life is silent
  const source = "sensor.growbox_tank_esp32_lebenszeichen_und_zeitstempel_text";
  const sensor = (text: string) => ({ key: "", text, args: {} });
  await page.route(/\/api\/v1\/events\/stream/, (route) => route.abort());
  await page.route(/\/api\/v1\/state(\?|$)/, async (route) => {
    const res = await route.fetch();
    const st = await res.json();
    st.watchdog = {
      ...st.watchdog,
      overall: "problem",
      ok: 0,
      problems: 1,
      neutral: 0,
      headline: { key: "watch.headline.problem", text: "1 problem", args: {} },
      items: [
        {
          id: `life.${source}`,
          status: "problem",
          label: { key: "watch.life.label", text: "pH im Tank, Wassertemperatur", args: { sensors: [sensor("pH im Tank"), sensor("Wassertemperatur")] } },
          text: {
            key: "watch.life.silent",
            text: `No valid reading: the device has sent nothing for 75 s – check or restart it (sign of life ${source})`,
            args: { ageS: 75, source },
          },
        },
      ],
    };
    await route.fulfill({ response: res, json: st });
  });
  await login(page);
  const bar = page.getByTestId("watchdog");
  await expect(bar).toContainText("1 Problem");
  await expect(bar).toContainText(`pH im Tank, Wassertemperatur: Datenausfall: Gerät sendet seit 75 s nichts – prüfen oder neu starten (Lebenszeichen ${source})`);
  await fits(page, "overview");
  await page.goto("/#/tank");
  const item = page.locator(".list .item").filter({ hasText: source });
  await expect(item.locator(".title")).toHaveText("pH im Tank, Wassertemperatur");
  await fits(page, "tank");
  await page.unrouteAll({ behavior: "ignoreErrors" });  // a poll may still be in flight
});

// The Home Assistant app (docs/HOME_ASSISTANT.md) names sensors by their
// entity IDs, which can be long words without a break.
test("Home Assistant: the introduction, devices, assignment and sign of life fit the phone's screen with long sensor IDs", async ({ page, request }) => {
  await scenario(request, "neu");
  const device = "ha.sensor.growbox_wassertemperatur_ds18b20_sonde_unten_links";
  const entity = "sensor.growbox_wassertemperatur_ds18b20_sonde_unten_links";
  const life = "sensor.growbox_tank_esp32_lebenszeichen_und_zeitstempel_text";
  await page.route(/\/api\/v1\/info(\?|$)/, async (route) => {
    const res = await route.fetch();
    const info = await res.json();
    info.platform = { kind: "home-assistant", simulated: false, readOnly: true };
    await route.fulfill({ response: res, json: info });
  });
  await page.route(/\/api\/v1\/events\/stream/, (route) => route.abort());  // the state comes from the polls below
  await page.route(/\/api\/v1\/ha\/candidates(\?|$)/, (route) => {
    const withLives = new URL(route.request().url()).searchParams.get("lives") === "1";
    const lives = [{ entity: life, name: "Growboxtankesp32lebenszeichen", kind: "text", channels: [{ code: "wt", ageS: 3 }] }];
    return route.fulfill({
      json: {
        connection: "ok",
        truncated: [],
        candidates: [{ entity, name: "Growboxwassertemperatursondeimtankuntenlinks", kind: "temperature", measures: ["water_temp", "air_temp"], value: 21.5, raw: 21.5, unit: "°C", problem: null, used: "water_temp" }],
        otherFits: {},
        otherCount: 0,
        lifeCount: lives.length,
        lifeNames: {},
        ...(withLives ? { lives } : {}),
      },
    });
  });
  await page.route(/\/api\/v1\/config(\?|$)/, async (route) => {
    if (route.request().method() !== "GET") return route.continue();
    const res = await route.fetch();
    const cfg = await res.json();
    cfg.devices = [...cfg.devices, { id: device, class: "ha_water_temp", name: "" }];
    cfg.tanks[0].roles["tank.water_temp"] = { device, channel: 0 };
    await route.fulfill({ response: res, json: cfg });
  });
  await page.route(/\/api\/v1\/state(\?|$)/, async (route) => {
    const res = await route.fetch();
    const st = await res.json();
    st.devices = [...st.devices, { id: device, class: "ha_water_temp", classLabel: "Wassertemperatur aus Home Assistant", name: "", configured: true, online: true, port: -1, slot: -1, parent: "", fw: "", fault: "", info: { entity }, calibrations: {} }];
    await route.fulfill({ response: res, json: st });
  });
  await newPassword(page);
  await expect(page).toHaveURL(/#\/willkommen/);
  for (let s = 0; s < 4; s++) {
    await page.goto(`/#/willkommen?s=${s}`);
    await expect(page.locator('.wizard-steps [aria-current="step"]')).toContainText(s < 3 ? String(s + 1) : "Fertig");
    await fits(page, `introduction step ${s + 1}`);
  }
  await page.goto("/#/willkommen?s=0");
  await page.getByTestId("welcome-skip").click();
  await expect(page.getByTestId("watchdog")).toBeVisible();
  await page.goto("/#/geraete?tab=geraete");
  await expect(page.getByTestId(`device-${device}`)).toBeVisible();
  await fits(page, "devices");
  await page.goto("/#/geraete?tab=zuordnung");
  await expect(page.getByTestId("ha-role-tank.water_temp")).toBeVisible();
  await fits(page, "assignment");
  await page.getByTestId("ha-life-tank.water_temp").click();
  const dialog: Locator = page.getByRole("dialog");
  await expect(dialog.locator("fieldset legend")).toContainText("Growboxtankesp32lebenszeichen");
  await fits(page, "sign of life");
  await page.unrouteAll({ behavior: "ignoreErrors" });
});
