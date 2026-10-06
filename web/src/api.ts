// REST-Client und Typen der Hub-API (/api/v1). Die Web-App nutzt nur diese API.

export type Msg = { key: string; text: string; args?: Record<string, unknown> };

export type Quality =
  | "not_bound" | "offline" | "no_data" | "stale" | "frozen" | "implausible" | "jump" | "uncalibrated" | "ok";

export type Reading = {
  value: number | null;
  quality: Quality;
  reason: Msg;
  ageS: number | null;
  unit: string;
  decimals: number;
  usable: boolean;
};

export type Port = { port: number; state: "empty" | "checking" | "ok" | "fault" | "rejected"; device: string; class: string; message: Msg };

export type Device = {
  id: string; class: string; classLabel: string; name: string; configured: boolean; online: boolean;
  port: number; slot: number; parent: string; fw: string; fault: string;
  info: { flowMlPerMin?: number | null };
  calibrations: Record<string, number>;
};

export type CtlStatus = {
  state: "off" | "idle" | "working" | "waiting" | "blocked" | "latched";
  line: Msg;
  checks: { ok: boolean; text: string }[];
  info: Record<string, unknown>;
};

export type JobStep = { name: string; canister: string; color: string; pair: string; ml: number | null; mlDone: number; state: string };
export type Job = {
  id: string; type: "mix" | "manual" | "calibration" | "prime";
  state: "running" | "waiting_user" | "mixing" | "done" | "failed" | "aborted";
  index: number; steps: JobStep[]; message: Msg; startedAt: number; finishedAt: number; guided: boolean;
  info: Record<string, any>;
};

export type Check = { level: "hardware" | "setup" | "runtime"; ok: boolean; soft: boolean; text: string; fix: string; shop: string[] };
export type FunctionState = {
  id: string; label: string; group: string; text: string; stage: number;
  setup: "unavailable" | "needs_setup" | "limited" | "ready"; enabled: boolean; alwaysOn: boolean;
  checks: Check[]; summary: Msg;
};

export type Assessment = { id: string; label: string; status: "ok" | "problem" | "neutral"; text: string };
export type Watchdog = {
  evaluatedAt: number; overall: "ok" | "problem" | "neutral"; ok: number; problems: number; neutral: number;
  headline: string; items: Assessment[]; stale: boolean;
};

export type Grow = {
  state: "none" | "running" | "completed"; name: string; startedAt: number; phase: number; phaseStartedAt: number;
  harvestedAt: number; phases: { name: string; days: number; params: Record<string, unknown> }[];
};

export type HubState = {
  now: number; uptimeS: number; setupDone: boolean; stopped: boolean; maintenanceUntil: number;
  ports: Port[]; devices: Device[]; readings: Record<string, Reading>;
  tank: { volumeL: number | null; source: "level" | "mix"; capacityL: number | null; minL: number | null };
  controllers: { ec: CtlStatus; ph: CtlStatus; refill: CtlStatus; circulation: CtlStatus };
  outputs: Record<string, boolean>;
  job: Job | null; lastJob: Job | null;
  dosing: { order: string; purpose: string; canister: string; name: string; ml: number | null; mlDone: number; run: number; runs: number } | null;
  watchdog: Watchdog; functions: FunctionState[]; latches: Record<string, any>;
  stock: Record<string, number | null>; effects: { ph: number | null; ec: number | null };
  lastMixAt: number; manual: { values: Record<string, number>; at: number }; grow: Grow; eventId: number;
  probeCalibration: Record<string, { device: string; kind: string; points: [number, number][] }>;
  sim?: { speed: number; scenario: string };
};

export type Canister = { id: string; name: string; kind: "nutrient" | "ph_down" | "ph_up"; pump: string; pair: string; color: string; capacityMl: number | null };
export type Recipe = { id: string; name: string; note: string; steps: { canister: string; mlPerL: number | null }[] };
export type Config = {
  schemaVersion: number; revision: number;
  system: { name: string; setupDone: boolean; timezone: string; language: string; updateCheck: boolean; updateChannel: string };
  limits: { handDoseMaxMl: number; minRunS: number; maxRunS: number; maxPartialRuns: number };
  devices: { id: string; class: string; name: string }[];
  tanks: { id: string; name: string; capacityL: number | null; minL: number | null; water: string; roles: Record<string, { device: string; channel: number }> }[];
  canisters: Canister[]; recipes: Recipe[];
  functions: Record<string, { enabled: boolean; params: Record<string, unknown> }>;
  calibrations: Record<string, Record<string, unknown>>;
  grow: Grow;
};

export type ParamDef = { key: string; label: string; type: "number" | "enum" | "recipe"; unit?: string; min?: number; max?: number; step?: number; default?: unknown; phase?: boolean; options?: [string, string][] };
export type Catalog = {
  catalogVersion: number;
  capabilities: Record<string, { label: string; unit?: string; kind: string; decimals?: number }>;
  deviceClasses: Record<string, { label: string; stage: number; attach: string; provides: string[]; shop?: string; text?: string; channels?: number; slots?: number }>;
  roles: Record<string, { label: string; capability: string; series?: boolean }>;
  functions: { id: string; label: string; stage: number; group: string; text: string; params?: ParamDef[] }[];
  templates: { recipes: RecipeTemplate[] };
};

export type Info = {
  product: string; version: string; api: number; catalogVersion: number; schemaVersion: number;
  platform: { kind: string; simulated?: boolean; scenario?: string }; setupDone: boolean; hasPassword: boolean; name: string;
};

export type HubEvent = { id: number; ts: number; type: string; severity: "info" | "notice" | "warn" | "alarm"; title: string; text: string; data: Record<string, any> };

export type MixPlan = {
  ok: boolean; recipe: string; recipeName: string; mode: string; waterL: number | null;
  steps: { canister: string; name: string; color: string; pair: string; ml: number | null; mlPerL: number | null; flowMlPerMin: number | null; runs: number[] }[];
  errors: Msg[]; warnings: Msg[]; after: Msg; totalMl: number; totalMs: number;
};

export type SeriesData = { series: string; stepS: number; t: number[]; avg: (number | null)[]; min: (number | null)[]; max: (number | null)[] };

export class ApiError extends Error {
  constructor(public status: number, public key: string, text: string, public body: any) {
    super(text);
  }
}

let onUnauthorized: () => void = () => {};
export function setUnauthorizedHandler(f: () => void) {
  onUnauthorized = f;
}

export async function api<T = any>(method: string, path: string, body?: unknown): Promise<T> {
  const res = await fetch(`/api/v1${path}`, {
    method,
    headers: body !== undefined ? { "Content-Type": "application/json" } : undefined,
    body: body !== undefined ? JSON.stringify(body) : undefined,
    credentials: "same-origin",
  });
  const text = await res.text();
  let data: any = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = text;
  }
  if (!res.ok) {
    if (res.status === 401 && !path.startsWith("/auth/")) onUnauthorized();
    const err = data?.error ?? {};
    throw new ApiError(res.status, err.key ?? "http", err.text ?? `Fehler ${res.status}`, data);
  }
  return data as T;
}

export const get = <T = any>(p: string) => api<T>("GET", p);
export const post = <T = any>(p: string, b: unknown = {}) => api<T>("POST", p, b);
export const put = <T = any>(p: string, b: unknown) => api<T>("PUT", p, b);
export const patch = <T = any>(p: string, b: unknown) => api<T>("PATCH", p, b);
export const del = <T = any>(p: string) => api<T>("DELETE", p);

// Simulator-Steuerung (nur Simulator)
export const sim = (action: string, body: unknown = {}) => api("POST", `/sim/${action}`, body);

export type TemplateStep = { role: string; name: string; nameEn?: string; pair?: string; mlPerL: number };
export type RecipeTemplate = {
  id: string; name: string; nameEn?: string; note?: string; noteEn?: string; source?: string;
  ec?: number; ph?: [number, number]; steps: TemplateStep[];
};
