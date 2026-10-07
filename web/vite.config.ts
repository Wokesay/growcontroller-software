// SPDX-License-Identifier: AGPL-3.0-or-later
import { execSync } from "node:child_process";
import { defineConfig } from "vite";
import preact from "@preact/preset-vite";

// Source revision for the source code link in the app (AGPL-3.0 §13): a
// release sets GC_SOURCE_REV to its tag; CI has GITHUB_SHA; locally the
// current commit. Empty if none is known (the app then says so); CI and
// release builds must know it.
function sourceRev(): string {
  let rev = (process.env.GC_SOURCE_REV || process.env.GITHUB_SHA || "").trim();
  if (!rev) {
    try {
      rev = execSync("git rev-parse HEAD", { stdio: ["ignore", "pipe", "ignore"] }).toString().trim();
    } catch {
      rev = "";
    }
  }
  if (rev && !/^[0-9A-Za-z._-]+$/.test(rev)) throw new Error(`Invalid source revision: ${rev}`);
  if (!rev && process.env.CI) throw new Error("Source revision unknown: set GC_SOURCE_REV (AGPL-3.0 §13)");
  return rev;
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
