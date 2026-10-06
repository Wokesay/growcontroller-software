// Zustand der App: Info, Sitzung, Live-Zustand (SSE), Konfiguration, Katalog.
import { computed, signal } from "@preact/signals";
import { ApiError, get, setUnauthorizedHandler, type Catalog, type Config, type HubState, type Info } from "./api";
import { hasOwnLang, setLang } from "./i18n";

export const info = signal<Info | null>(null);
export const authed = signal<boolean | null>(null);
export const state = signal<HubState | null>(null);
export const config = signal<Config | null>(null);
export const catalog = signal<Catalog | null>(null);
export const live = signal<"connecting" | "live" | "offline">("connecting");
export const lastUpdate = signal<number>(0);

export const simulated = computed(() => !!info.value?.platform?.simulated);

type Toast = { id: number; kind: "ok" | "error" | "info"; text: string };
export const toasts = signal<Toast[]>([]);
let toastId = 0;
export function toast(text: string, kind: Toast["kind"] = "ok") {
  const id = ++toastId;
  toasts.value = [...toasts.value, { id, kind, text }];
  setTimeout(() => (toasts.value = toasts.value.filter((t) => t.id !== id)), kind === "error" ? 7000 : 3500);
}
export function toastError(e: unknown) {
  if (e instanceof ApiError) toast(e.message, "error");
  else toast(String(e), "error");
}

let source: EventSource | null = null;
let pollTimer: number | undefined;

function startLive() {
  stopLive();
  live.value = "connecting";
  try {
    source = new EventSource("/api/v1/events/stream", { withCredentials: true });
    source.addEventListener("state", (ev) => {
      state.value = JSON.parse((ev as MessageEvent).data);
      lastUpdate.value = Date.now();
      live.value = "live";
    });
    source.onerror = () => {
      live.value = "offline";
      // EventSource verbindet sich selbst neu; zur Sicherheit zusätzlich abfragen
      if (!pollTimer) pollTimer = window.setInterval(refreshState, 3000);
    };
    source.onopen = () => {
      if (pollTimer) {
        clearInterval(pollTimer);
        pollTimer = undefined;
      }
    };
  } catch {
    pollTimer = window.setInterval(refreshState, 2000);
  }
}

function stopLive() {
  source?.close();
  source = null;
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = undefined;
}

export async function refreshState() {
  try {
    state.value = await get<HubState>("/state");
    lastUpdate.value = Date.now();
    if (live.value === "offline" && source?.readyState === EventSource.OPEN) live.value = "live";
  } catch {
    /* Verbindung später erneut */
  }
}

export async function refreshConfig() {
  config.value = await get<Config>("/config");
}

export async function boot() {
  setUnauthorizedHandler(() => {
    authed.value = false;
    stopLive();
  });
  info.value = await get<Info>("/info");
  const s = await get<{ authenticated: boolean }>("/auth/session");
  authed.value = s.authenticated;
  if (s.authenticated) await afterLogin();
}

export async function afterLogin() {
  authed.value = true;
  info.value = await get<Info>("/info");
  const [cfg, cat] = await Promise.all([get<Config>("/config"), get<Catalog>("/catalog"), refreshState()]);
  config.value = cfg;
  catalog.value = cat;
  // Ohne eigene Wahl im Browser gilt die Sprache, die am Hub eingestellt ist
  if (!hasOwnLang() && (cfg.system.language === "de" || cfg.system.language === "en")) setLang(cfg.system.language, false);
  startLive();
}

export function logoutLocal() {
  stopLive();
  authed.value = false;
  state.value = null;
  config.value = null;
}

// Ableitungen, die viele Seiten brauchen
export const tank = computed(() => config.value?.tanks?.[0]);
export const canisters = computed(() => config.value?.canisters ?? []);
export const recipes = computed(() => config.value?.recipes ?? []);
export const fn = (id: string) => state.value?.functions.find((f) => f.id === id);
export const zone = computed(() => config.value?.zones?.[0]);
/** Zuordnung einer Rolle: „zone.*“ liegt an der Zone, alles andere am Tank. */
export const binding = (role: string) => (role.startsWith("zone.") ? zone.value?.roles?.[role] : tank.value?.roles?.[role]);
export const hasRole = (role: string) => !!binding(role)?.device;
