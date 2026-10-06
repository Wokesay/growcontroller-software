import { defineConfig } from "vite";
import preact from "@preact/preset-vite";

// Entwicklung: API an den Simulator weiterreichen (cmake-build/gc_sim_server --port 8080).
const target = process.env.GC_SIM ?? "http://127.0.0.1:8080";

export default defineConfig({
  plugins: [preact()],
  build: {
    target: "es2020",
    outDir: "dist",
    assetsInlineLimit: 0,
    cssCodeSplit: false,
    sourcemap: false,
  },
  server: {
    proxy: { "/api": { target, changeOrigin: true } },
  },
});
