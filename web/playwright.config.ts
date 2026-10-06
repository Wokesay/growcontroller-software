import { defineConfig } from "@playwright/test";

// E2E gegen den Simulator: echter Kern, echte API, gebaute Web-App.
// Vorher: cmake --build ../build && npm run build
const port = Number(process.env.GC_E2E_PORT ?? 8099);

export default defineConfig({
  testDir: "./e2e",
  timeout: 120_000,
  expect: { timeout: 15_000 },
  workers: 1,
  fullyParallel: false,
  reporter: process.env.CI ? [["list"], ["html", { open: "never", outputFolder: "../build/e2e-report" }]] : "list",
  outputDir: "../build/e2e-results",
  use: {
    baseURL: `http://127.0.0.1:${port}`,
    locale: "de-DE",
    timezoneId: "Europe/Berlin",
    screenshot: "only-on-failure",
    trace: "retain-on-failure",
  },
  webServer: {
    command: `../build/gc_sim_server --port ${port} --scenario neu --web dist --password demo-passwort --prefill 2`,
    url: `http://127.0.0.1:${port}/api/v1/info`,
    reuseExistingServer: !process.env.CI,
    timeout: 60_000,
  },
  projects: [
    { name: "desktop", use: { viewport: { width: 1360, height: 900 } } },
  ],
});
