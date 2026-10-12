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

test("The setup fits the phone's screen in every step; its footer leaves the simulator's button free", async ({ page, request }) => {
  await scenario(request, "neu");
  await newPassword(page);
  await expect(page).toHaveURL(/#\/einrichtung/);
  for (let s = 0; s < 6; s++) {
    await page.goto(`/#/einrichtung?s=${s}`);
    await expect(page.locator('.wizard-steps [aria-current="step"]')).toContainText(String(s + 1));
    await fits(page, `setup step ${s + 1}`);
    const fab = await page.locator(".sim-fab").boundingBox();
    const foot = await page.locator(".wizard-foot").boundingBox();
    expect(fab && foot, `setup step ${s + 1}: simulator button and footer shown`).toBeTruthy();
    expect(apart(fab!, foot!), `setup step ${s + 1}`).toBe(true);
  }
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
    await expect(page.locator('.wizard-steps [aria-current="step"]')).toContainText(String(s + 1));
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
