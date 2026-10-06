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
  expect((await outlet()).initialOff).toBe(true);
  await page.getByTestId(`outlets-${id}`).getByRole("button", { name: "Testen" }).click();
  // Der Klick geht asynchron an den Hub: auf den Zustand warten, nicht sofort lesen
  await expect.poll(async () => (await outlet()).on).toBe(true);
});
