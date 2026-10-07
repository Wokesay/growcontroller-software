// SPDX-License-Identifier: AGPL-3.0-or-later
import { execSync } from "node:child_process";
import { defineConfig } from "vite";
import preact from "@preact/preset-vite";

// Source revision for the source code link in the app (AGPL-3.0 §13): a
// release sets GC_SOURCE_REV to its tag; CI has GITHUB_SHA; locally the
// current commit. Empty if none is known (the link then shows the repository).
function sourceRev(): string {
  const env = process.env.GC_SOURCE_REV || process.env.GITHUB_SHA;
  if (env) return env.trim();
  try {
    return execSync("git rev-parse HEAD", { stdio: ["ignore", "pipe", "ignore"] }).toString().trim();
  } catch {
    return "";
  }
}

// Entwicklung: API an den Simulator weiterreichen (cmake-build/gc_sim_server --port 8080).
const target = process.env.GC_SIM ?? "http://127.0.0.1:8080";

export default defineConfig({
  plugins: [preact()],
  define: { __GC_SOURCE_REV__: JSON.stringify(sourceRev()) },
  build: {
    target: "es2020",
    outDir: "dist",
    assetsInlineLimit: 0,
    cssCodeSplit: false,
    sourcemap: false,
  },
  server: {
    // Host bleibt localhost:5173: Herkunftsprüfung des Simulators greift auch hier
    proxy: { "/api": { target, changeOrigin: false } },
  },
});
