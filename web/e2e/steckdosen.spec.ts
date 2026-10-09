// SPDX-License-Identifier: AGPL-3.0-or-later
// Schaltbare Steckdosen (Shelly im Simulator): in der Einrichtung finden,
// übernehmen, je Dose sagen, was eingesteckt ist, testen.
import { expect, test } from "@playwright/test";
import { scenario } from "./helpers";

test("Einrichtung: Steckdose im WLAN übernehmen und der Umwälzpumpe zuordnen", async ({ page, request }) => {
  await scenario(request, "neu");
  await page.goto("/");
  await page.locator("input[name=password]").fill("mein-passwort");
  await page.locator("input[name=password2]").fill("mein-passwort");
  await page.getByRole("button", { name: "Passwort festlegen" }).click();
  await page.getByTestId("wizard-next").click();
  await expect(page.getByRole("heading", { name: "Geräte erkennen" })).toBeVisible();
  await expect(page.getByTestId("net-none")).toBeVisible();

  // Shelly kommt ins WLAN
  const r = await page.request.post("/api/v1/sim/net_add", { data: { class: "shelly_plug", loads: [{ load: "circulation", watts: 18 }] } });
  const { id } = await r.json();
  await expect(page.getByTestId("net-devices")).toContainText("Schaltbare Steckdose");
  await page.getByRole("button", { name: /übernehmen/ }).click();
  const select = page.locator(`select[name="outlet-${id}-0"]`);
  await expect(select).toBeVisible();
  await select.selectOption({ label: "Umwälzpumpe" });
  await expect(page.getByText("Zugeordnet; Schutz im Gerät gesetzt")).toBeVisible();
  const outlet = async () => {
    const world = await (await page.request.get("/api/v1/sim")).json();
    return world.world.netPlugs.find((p: { id: string }) => p.id === id).outlets[0];
  };
  expect((await outlet()).powerOn).toBe("off");
  const before = (await outlet()).switchOns;
  await page.getByTestId(`outlets-${id}`).getByRole("button", { name: "Testen" }).click();
  // „Testen“ schaltet 3 s ein. Statt den kurzen Zustand abzufragen (verpasst
  // ihn, wenn eine Abfrage länger dauert), auf die bleibende Spur warten.
  await expect.poll(async () => (await outlet()).switchOns).toBeGreaterThan(before);
  await expect.poll(async () => (await outlet()).on, { timeout: 10_000 }).toBe(false);
  const ev = await (await page.request.get("/api/v1/events?limit=50")).json();
  // Neither a failed switch-off nor a cut-off of the circulation pump, by key (SD-032).
  const off = (e: { title: { key: string } }) => ["ev.off_unconfirmed", "ev.circ.off", "ev.circ.dry"].includes(e.title.key);
  expect(ev.events.filter(off)).toHaveLength(0);
});
