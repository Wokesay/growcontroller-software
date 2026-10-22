// SPDX-License-Identifier: AGPL-3.0-or-later
// Behind Home Assistant's Ingress the app lives below a path,
// /api/hassio_ingress/<token>/ (SD-077): everything it loads and asks for
// stays below it. A proxy in these tests strips the path as the Supervisor
// does and passes the rest on: to the simulator, or to the Home Assistant
// app's server with the headers that name Home Assistant's user.
import { spawn, type ChildProcess } from "node:child_process";
import { chmodSync, existsSync, mkdtempSync, rmSync } from "node:fs";
import { createServer, request, type IncomingHttpHeaders, type Server } from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, scenario, test } from "./helpers";

const PREFIX = "/api/hassio_ingress/test-token";

// Starts the proxy; `outside` collects what the page asked for outside the path.
// `lapse(true)` answers as Home Assistant does once its Ingress session has
// lapsed (a phone that slept): 401 to everything, open connections closed.
async function ingressProxy(target: URL, user: IncomingHttpHeaders = {}) {
  const outside: string[] = [];
  let lapsed = false;
  let refused = 0;
  const proxy: Server = createServer((req, res) => {
    const url = req.url ?? "/";
    if (lapsed) {
      refused++;
      res.writeHead(401, { "Content-Type": "text/plain" }).end("401: Unauthorized");
      return;
    }
    if (!url.startsWith(`${PREFIX}/`)) {
      outside.push(url);
      res.writeHead(404).end();
      return;
    }
    const forward = request(
      { host: target.hostname, port: target.port, path: url.slice(PREFIX.length), method: req.method, headers: { ...req.headers, ...user, host: target.host } },
      (up) => {
        res.writeHead(up.statusCode ?? 502, up.headers);
        up.pipe(res);
      },
    );
    forward.on("error", () => res.destroy());
    req.pipe(forward);
  });
  await new Promise<void>((r) => proxy.listen(0, "127.0.0.1", r));
  const address = proxy.address();
  const base = `http://127.0.0.1:${typeof address === "object" && address ? address.port : 0}${PREFIX}/`;
  const close = async () => {
    proxy.closeAllConnections();  // the live stream keeps a connection open
    await new Promise((r) => proxy.close(r));
  };
  const lapse = (on: boolean) => {
    lapsed = on;
    if (on) proxy.closeAllConnections();
  };
  return { base, outside, close, lapse, refused: () => refused };
}

test("below Ingress's path the app loads, signs in, and asks only below that path", async ({ page, request: api, baseURL }) => {
  await scenario(api, "demo");
  const { base, outside, close } = await ingressProxy(new URL(baseURL!));
  try {
    await page.goto(base);
    await page.locator("input[name=password]").fill("demo-passwort");
    await page.getByRole("button", { name: "Anmelden" }).click();
    await expect(page.getByTestId("watchdog")).toBeVisible();
    await page.goto(`${base}#/einstellungen`);
    await expect(page.getByTestId("source-link")).toBeVisible();
    // Links the browser follows itself stay below the path too
    for (const name of [/Einstellungen sichern/, /Verlauf \(CSV/]) {
      const href = await page.getByRole("link", { name }).first().getAttribute("href");
      expect(new URL(href!, page.url()).pathname.startsWith(`${PREFIX}/api/v1/`), String(href)).toBe(true);
    }
    // A reload of a deep link comes back to the same page
    await page.reload();
    await expect(page.getByTestId("source-link")).toBeVisible();
    expect(outside, "requests outside Ingress's path").toEqual([]);
  } finally {
    await page.close();
    await close();
  }
});

// The Home Assistant app's server, as tools/ci.sh builds it
const server = fileURLToPath(new URL("../../build/gc_ha_server", import.meta.url));
const dist = fileURLToPath(new URL("../dist", import.meta.url));

async function freePort(): Promise<number> {
  const s = createServer();
  await new Promise<void>((r) => s.listen(0, "127.0.0.1", r));
  const address = s.address();
  await new Promise((r) => s.close(r));
  return typeof address === "object" && address ? address.port : 0;
}

test("the Home Assistant app: its user sets and changes the password for direct access, the bookmark asks for it, and removed, the bookmark shows only a notice (SD-077)", async ({ page, browser }) => {
  test.skip(process.platform !== "linux" || (!process.env.CI && !existsSync(server)), "the app's server runs on Linux; tools/ci.sh builds it");
  const parent = mkdtempSync(join(tmpdir(), "gc-e2e-app-"));
  chmodSync(parent, 0o755);  // as /data: started as root, the app runs as an account of its own
  const port = await freePort();
  let ingressPort = await freePort();
  while (ingressPort === port) ingressPort = await freePort();
  const app: ChildProcess = spawn(
    server,
    ["--app", "--data", join(parent, "data"), "--host", "127.0.0.1", "--web", dist, "--port", String(port), "--ingress-port", String(ingressPort), "--ingress-from", "127.0.0.1"],
    { env: { ...process.env, SUPERVISOR_TOKEN: "test-token" }, stdio: "ignore" },
  );
  const ingress = await ingressProxy(new URL(`http://127.0.0.1:${ingressPort}`), {
    "X-Remote-User-Id": "0123456789abcdef0123456789abcdef",
    "X-Remote-User-Name": "alex",
    "X-Remote-User-Display-Name": "Alex Example",
  });
  const bookmark = `http://127.0.0.1:${port}/`;
  const elsewhere = await browser.newContext({ locale: "de-DE" });  // another device, with the bookmark
  await elsewhere.addInitScript(() => {
    try {
      if (!localStorage.getItem("gc.lang")) localStorage.setItem("gc.lang", "de");  // German, as the helpers set the page (SD-083)
    } catch {
      // the notice page has no storage of its own to keep
    }
  });
  try {
    await expect.poll(async () => (await fetch(`http://127.0.0.1:${ingressPort}/api/v1/info`).catch(() => null))?.status, { timeout: 30_000 }).toBe(200);
    // Through Home Assistant: signed in as its user, no password asked
    await page.goto(`${ingress.base}#/einstellungen?skip=1`);
    await expect(page.getByText("Über Home Assistant angemeldet als Alex Example.")).toBeVisible();
    await expect(page.getByText("Aus", { exact: true })).toBeVisible();
    // The bookmark without a password: a notice, no sign-in
    const direct = await elsewhere.newPage();
    await direct.goto(bookmark);
    await expect(direct.getByText("Direct access at this address is off")).toBeVisible();
    await expect(direct.locator("input[name=password]")).toHaveCount(0);
    // Set through Home Assistant, at least 8 characters
    await page.locator("#direct-password").fill("kurz");
    await expect(page.getByRole("button", { name: "Passwort festlegen" })).toBeDisabled();
    await page.locator("#direct-password").fill("direkt-passwort");
    await page.getByRole("button", { name: "Passwort festlegen" }).click();
    await expect(page.getByText("Passwort gespeichert.", { exact: false })).toBeVisible();
    await expect(page.getByText("Passwort gesetzt", { exact: true })).toBeVisible();
    await expect(page.getByText("Weißt du nicht sicher, wer dieses Passwort gesetzt hat?", { exact: false })).toBeVisible();
    // The log names who did it
    await page.goto(`${ingress.base}#/verlauf?skip=1`);
    const event = page.getByTestId("event").filter({ hasText: "Direkter Zugang: Passwort gesetzt" });
    await expect(event).toContainText("von Alex Example");
    // The bookmark now asks for that password and says where a forgotten one is set
    await direct.reload();
    await expect(direct.getByText("Passwort vergessen? Öffne growcontroller in der Seitenleiste von Home Assistant", { exact: false })).toBeVisible();
    await direct.locator("input[name=password]").fill("direkt-passwort");
    await direct.getByRole("button", { name: "Anmelden" }).click();
    await expect(direct).toHaveURL(/#\/willkommen/);
    // Changed through Home Assistant: the bookmark is signed out and needs the new one
    await page.goto(`${ingress.base}#/einstellungen?skip=1`);
    await page.locator("#direct-password").fill("neues-passwort");
    await page.getByRole("button", { name: "Passwort ändern" }).click();
    await expect(page.getByText("Passwort geändert.", { exact: false })).toBeVisible();
    await direct.reload();
    await direct.locator("input[name=password]").fill("neues-passwort");
    await direct.getByRole("button", { name: "Anmelden" }).click();
    await expect(direct).toHaveURL(/#\/willkommen/);
    // Removed through Home Assistant, after asking: the bookmark is back to the notice
    await page.getByRole("button", { name: "Direkten Zugang entfernen" }).click();
    const dialog = page.getByRole("alertdialog", { name: "Direkten Zugang entfernen?" });
    await expect(dialog).toHaveAccessibleDescription(/werden abgemeldet/);
    await dialog.getByRole("button", { name: "Entfernen" }).click();
    await expect(page.getByText("Direkter Zugang entfernt")).toBeVisible();
    await expect(page.getByText("Aus", { exact: true })).toBeVisible();
    await direct.reload();
    await expect(direct.getByText("Direct access at this address is off")).toBeVisible();
    // Home Assistant's session lapses for a moment: the app asks again and goes on, never with a sign-in of its own
    ingress.lapse(true);
    await expect.poll(ingress.refused).toBeGreaterThan(0);
    await page.waitForTimeout(2000);
    ingress.lapse(false);
    await page.waitForTimeout(12_000);  // longer than the app asks again
    await expect(page.getByTestId("platform-reload")).toHaveCount(0);
    await expect(page.locator("input[name=password]")).toHaveCount(0);
    // For good: the app says so and reloads Home Assistant's page, which makes a new session
    ingress.lapse(true);
    await expect(page.getByRole("alert").filter({ hasText: "Deine Anmeldung über Home Assistant ist abgelaufen" })).toBeVisible({ timeout: 30_000 });
    await expect(page.locator("input[name=password]")).toHaveCount(0);
    // What is clicked meanwhile says it was not carried out, not an HTTP code
    await page.locator("#direct-password").fill("noch-ein-passwort");
    await page.getByRole("button", { name: "Passwort festlegen" }).click();
    await expect(page.getByText("Nicht ausgeführt – deine Anmeldung über Home Assistant ist abgelaufen.", { exact: false })).toBeVisible();
    ingress.lapse(false);
    await page.getByTestId("platform-reload").click();
    await expect(page.getByText("Über Home Assistant angemeldet als Alex Example.")).toBeVisible();
    await expect(page.getByTestId("platform-reload")).toHaveCount(0);
    expect(ingress.outside, "requests outside Ingress's path").toEqual([]);
  } finally {
    await elsewhere.close();
    await page.close();
    await ingress.close();
    app.kill("SIGTERM");
    await new Promise((r) => (app.exitCode !== null ? r(null) : app.once("exit", r)));
    rmSync(parent, { recursive: true, force: true });
  }
});
