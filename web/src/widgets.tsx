// SPDX-License-Identifier: AGPL-3.0-or-later
// Wiederverwendete Teile mehrerer Seiten: Messwert-Kachel, Regelzeile,
// Auftragsanzeige, Ereignisliste, Vorrat.
import { useEffect, useState } from "preact/hooks";
import { AlertTriangle, Cable, ChevronDown, ChevronRight, CircleCheck, CircleDot, Cpu, Droplet, FlaskConical, Gauge, Info, Play, Plug, Power, RotateCcw, Square, Waves, Zap } from "lucide-preact";
import { Sparkline } from "./chart";
import { del, get, post, put, type CtlStatus, type Device, type HubEvent, type Job, type Reading, type SeriesData } from "./api";
import { ago, day, num, time } from "./format";
import { msg, t, type TextKey } from "./i18n";
import { binding, catalog, config, canisters, refreshConfig, refreshState, state, toast } from "./store";
import { Button, CheckRow, ErrorBoundary, Pill, ctlLabel, ctlTone } from "./ui";

export const qualityText: Record<string, TextKey> = {
  not_bound: "widgets.quality.notBound",
  offline: "widgets.quality.offline",
  no_data: "widgets.quality.noData",
  stale: "widgets.quality.stale",
  frozen: "widgets.quality.frozen",
  implausible: "widgets.quality.implausible",
  jump: "term.jumpLock",
  uncalibrated: "setup.cal.notCalibrated",
  ok: "widgets.quality.ok",
};

export function MetricTile(p: { label: string; reading?: Reading; color: string; band?: [number, number] | null; spark?: (number | null)[]; notApplicable?: string }) {
  const r = p.reading;
  // Calibrated outside the hub (Home Assistant trial): a value to look at, not
  // a fault; it keeps its age and says it is for display only. It has no
  // trend: the history keeps only values usable for control.
  const external = r?.quality === "uncalibrated" && r.reason.key === "truth.external";
  const bad = r && r.quality !== "ok" && r.quality !== "not_bound" && !external;
  const outOfBand = r?.usable && p.band && r.value !== null && (r.value < p.band[0] || r.value > p.band[1]);
  return (
    <div class={`metric ${bad ? "bad" : ""}`} data-testid={`metric-${p.label}`}>
      <div class="metric-top">
        <span class="metric-label">
          <span class="dot" style={`color:var(${p.color})`} />
          {p.label}
        </span>
        {bad ? (
          <Pill tone={r!.quality === "uncalibrated" ? "warn" : "bad"} title={msg(r!.reason)}>
            {qualityText[r!.quality] && t(qualityText[r!.quality])}
          </Pill>
        ) : external ? (
          <Pill tone="warn">{t("widgets.quality.external")}</Pill>
        ) : outOfBand ? (
          <Pill tone="warn">{t("widgets.outOfBand")}</Pill>
        ) : null}
      </div>
      <div class="metric-value">
        {p.notApplicable ? <span class="faint" style="font-size:1.1rem">{t("widgets.notApplicable")}</span> : num(r?.value ?? null, r?.decimals ?? 2)}
        {!p.notApplicable && r?.unit && <span class="unit">{r.unit}</span>}
      </div>
      {p.spark && !bad && !external && <Sparkline values={p.spark} color={p.color} band={p.band} />}
      {bad && r?.reason.text && <div class="metric-reason">{msg(r.reason)}</div>}
      {external && <div class="metric-note">{msg(r!.reason)}</div>}
      <div class="metric-foot">
        <span>{p.band ? t("widgets.target", { lo: num(p.band[0], 2), hi: num(p.band[1], 2) }) : p.notApplicable ?? ""}</span>
        {!bad && <span>{r?.ageS !== null && r?.ageS !== undefined ? ago(r.ageS) : ""}</span>}
      </div>
    </div>
  );
}

export function ControllerRow(p: { name: string; st: CtlStatus; open?: boolean }) {
  const [open, setOpen] = useState(!!p.open);
  const tone = ctlTone(p.st.state);
  return (
    <div>
      <button class={`ctl ${p.st.state}`} onClick={() => setOpen(!open)} aria-expanded={open}>
        <Pill tone={tone} dot pulse={p.st.state === "working"}>
          {ctlLabel[p.st.state]}
        </Pill>
        <span>
          <span class="name">{p.name}</span>
          <span class="line"> · {msg(p.st.line)}</span>
        </span>
        {p.st.checks.length > 0 ? open ? <ChevronDown size={16} /> : <ChevronRight size={16} /> : <span />}
      </button>
      {open && p.st.checks.length > 0 && (
        <div class="checks">
          {p.st.checks.map((c) => (
            <CheckRow ok={c.ok} text={msg(c)} />
          ))}
          {(p.st.state === "latched" || latchedLine(p.st.line.key)) && <LatchActions />}
        </div>
      )}
    </div>
  );
}

function LatchActions() {
  const l = state.value?.latches ?? {};
  const keys = Object.keys(l).filter((k) => !k.startsWith("jump."));
  if (!keys.length) return null;
  return (
    <div class="row" style="margin-top:6px">
      {keys.map((k) => (
        <Button
          size="sm"
          onClick={async () => {
            await post(`/latches/${k}/ack`);
            await refreshState();
            toast(t("widgets.acked"));
          }}
        >
          <RotateCcw size={14} /> {t("widgets.ack", { name: latchLabel(k) })}
        </Button>
      ))}
    </div>
  );
}

// Lines the core sends while a latch holds; by key, not by its German text.
const latchedLine = (key: string) => key.endsWith(".latched") || key === "refill.stopped";

const latchText: Record<string, TextKey> = { "circulation.dry": "widgets.latch.dry", "inlet.fault": "widgets.latch.inletFault", "ph.no_effect": "widgets.latch.phNoEffect", "ec.no_effect": "widgets.latch.ecNoEffect" };
export const latchLabel = (k: string) => (latchText[k] ? t(latchText[k]) : k);

const jobStateText: Record<string, TextKey> = { running: "widgets.job.running", waiting_user: "widgets.job.waitingUser", mixing: "widgets.job.mixing", done: "widgets.job.done", failed: "widgets.job.failed", aborted: "widgets.job.aborted" };

export function JobView(p: { job: Job; compact?: boolean }) {
  const j = p.job;
  const dosing = state.value?.dosing;
  const total = j.steps.length;
  const title = j.type === "mix" ? t("widgets.jobMix", { name: j.info.recipeName ?? "", l: num(j.info.waterL, 1) }) : j.type === "calibration" ? t("widgets.jobCalibration") : j.type === "prime" ? t("term.prime") : t("widgets.jobManual");
  return (
    <div class="stack-sm" data-testid="job">
      <div class="row-between">
        <h3>{title}</h3>
        <Pill tone={j.state === "failed" ? "bad" : j.state === "done" ? "ok" : j.state === "aborted" ? "neutral" : j.state === "waiting_user" ? "warn" : "info"} dot pulse={j.state === "running" || j.state === "mixing"}>
          {jobStateText[j.state] && t(jobStateText[j.state])}
        </Pill>
      </div>
      {!p.compact && (
        <div class="steps">
          {j.steps.map((s, i) => {
            const running = s.state === "running" && dosing && dosing.name === s.name;
            const pct = running && dosing?.ml ? Math.min(100, (dosing.mlDone / dosing.ml) * 100) : 0;
            return (
              <div class={`step ${s.state}`}>
                <span class="n">{s.state === "done" ? "✓" : i + 1}</span>
                <div class="stack-sm" style="gap:4px">
                  <div class="row" style="gap:8px">
                    {s.color && <span class="swatch" style={`background:${s.color}`} />}
                    <strong>{s.name}</strong>
                    {s.pair && <span class="faint small">{t("widgets.pair", { p: s.pair })}</span>}
                  </div>
                  {running && (
                    <div class="progress">
                      <span style={`width:${pct}%`} />
                    </div>
                  )}
                </div>
                <span class="num small">
                  {s.mlDone > 0 ? `${num(s.mlDone, 1)} / ` : ""}
                  {num(s.ml, 1)} ml
                </span>
              </div>
            );
          })}
        </div>
      )}
      <p class={j.state === "failed" ? "" : "muted"} style={j.state === "failed" ? "color:var(--bad);font-weight:600" : ""}>
        {msg(j.message)}
      </p>
      <div class="row">
        {(j.state === "waiting_user" || j.state === "mixing") && j.type === "mix" && (
          <Button variant="primary" onClick={() => post(`/jobs/${j.id}/continue`).then(refreshState)}>
            <Play size={16} /> {j.state === "mixing" ? t("widgets.continueNow") : t("widgets.continueStep", { n: Math.min(j.index + 1, total), total })}
          </Button>
        )}
        {j.state === "failed" && j.type === "mix" && (
          <Button variant="primary" onClick={() => post(`/jobs/${j.id}/resume`).then(refreshState)}>
            <RotateCcw size={16} /> {t("widgets.redo", { name: j.steps[j.index]?.name ?? "" })}
          </Button>
        )}
        {(j.state === "running" || j.state === "waiting_user" || j.state === "mixing" || j.state === "failed") && (
          <Button variant="danger-soft" onClick={() => post(`/jobs/${j.id}/abort`).then(refreshState)}>
            <Square size={14} /> {t("common.cancel")}
          </Button>
        )}
      </div>
    </div>
  );
}

const sevIcon = (s: string) =>
  s === "alarm" ? <AlertTriangle size={15} /> : s === "warn" ? <AlertTriangle size={15} /> : s === "notice" ? <Info size={15} /> : s === "info" ? <CircleDot size={15} /> : <CircleCheck size={15} />;

export function EventList(p: { events: HubEvent[]; groupByDay?: boolean }) {
  return (
    <ErrorBoundary>
      <Events {...p} />
    </ErrorBoundary>
  );
}

function Events(p: { events: HubEvent[]; groupByDay?: boolean }) {
  if (!p.events.length) return <p class="muted">{t("widgets.noEvents")}</p>;
  let lastDay = "";
  return (
    <div>
      {p.events.map((e) => {
        const d = day(e.ts);
        const head = p.groupByDay && d !== lastDay ? <div class="day-head">{d}</div> : null;
        lastDay = d;
        return (
          <>
            {head}
            <div class="event" data-testid="event">
              <span class="t">{time(e.ts)}</span>
              <span class={`sev ${e.severity}`}>{sevIcon(e.severity)}</span>
              <div>
                <div class="et">{msg(e.title)}</div>
                {msg(e.text) && <div class="ex">{msg(e.text)}</div>}
              </div>
            </div>
          </>
        );
      })}
    </div>
  );
}

export function StockList(p: { compact?: boolean }) {
  const st = state.value;
  const cans = canisters.value;
  if (!cans.length) return <p class="muted">{t("widgets.noCanisters")}</p>;
  const rows = cans.map((k) => {
    const ml = st?.stock[k.id] ?? null;
    const cap = k.capacityMl ?? null;
    const min = k.kind === "nutrient" ? 150 : 20;
    const low = ml !== null && ml < min;
    return { k, ml, cap, low };
  });
  const lows = rows.filter((r) => r.low);
  if (p.compact && !lows.length)
    return (
      <p class="row">
        <Pill tone="ok">{t("widgets.stockOk", { n: rows.length })}</Pill>
        <span class="muted small">{t("widgets.stockAllOk")}</span>
      </p>
    );
  return (
    <div class="list">
      {(p.compact ? lows : rows).map(({ k, ml, cap, low }) => (
        <div class="item">
          <span class="swatch" style={`background:${k.color}`} />
          <div class="grow">
            <div class="title">{k.name}</div>
            <div class={`bar ${low ? "low" : ""}`}>
              <span style={`width:${cap && ml !== null ? Math.max(2, Math.min(100, (ml / cap) * 100)) : 0}%`} />
            </div>
          </div>
          <span class="num small nowrap">{ml === null ? t("area.unknown") : `${num(ml, 0)} ml`}</span>
        </div>
      ))}
    </div>
  );
}

// ---------- Geräte und Anschlüsse

/** Symbol je Geräteklasse. */
export function DeviceIcon(p: { cls: string; size?: number }) {
  const size = p.size ?? 18;
  if (p.cls === "dosing_block") return <Cable size={size} />;
  if (p.cls === "pump_cap") return <Droplet size={size} />;
  if (p.cls === "head_ph_ec" || p.cls === "head_ph" || p.cls === "head_ec") return <FlaskConical size={size} />;
  if (p.cls === "head_level") return <Waves size={size} />;
  if (p.cls === "hub_outputs") return <Power size={size} />;
  if (p.cls.startsWith("shelly_")) return <Plug size={size} />;
  if (p.cls.startsWith("head")) return <Gauge size={size} />;
  return <Cpu size={size} />;
}

/** Wo ein Gerät steckt: „Pumpe 2“ auf dem Dosierblock, sonst „Anschluss 3“ am Hub. */
export function devicePlace(d: Device): string {
  if (d.slot >= 0) return t("port.pump", { n: d.slot + 1 });
  if (d.port > 0) return t("port.hub", { n: d.port });
  if (d.info?.ip) return t("port.wifi", { ip: d.info.ip });
  if (d.info?.entity) return t("port.ha", { entity: d.info.entity });
  return t("widgets.hub");
}

/** Anschlüsse des Hubs als Kacheln; ein belegter Anschluss zeigt das Symbol des Geräts. */
export function PortGrid() {
  const st = state.value!;
  return (
    <div class="ports" data-testid="ports">
      {st.ports.map((p) => {
        const d = st.devices.find((x) => x.id === p.device);
        return (
          <div class={`port ${p.state}`} title={p.message?.text}>
            <span class="pn">{t("port.hub", { n: p.port })}</span>
            {p.state === "empty" ? <span class="jack" /> : <span class="pi"><DeviceIcon cls={d?.class ?? p.class} size={22} /></span>}
            <span class="pl">{p.state === "empty" ? <span class="faint">{t("common.free")}</span> : d?.name || d?.classLabel || p.class}</span>
            {(p.state === "rejected" || p.state === "fault") && <span class="pm">{msg(p.message)}</span>}
            {p.state === "checking" && <span class="faint small">…</span>}
          </div>
        );
      })}
    </div>
  );
}

/** Schaltrollen, die an diese Dose dürfen (Netzsteckdose oder 12-V-Ausgang). */
export function switchRolesFor(cls: string): [string, string][] {
  const cat = catalog.value;
  if (!cat) return [];
  const provides = cat.deviceClasses[cls]?.provides ?? [];
  return Object.entries(cat.roles)
    .filter(([, r]) => r.profile && (r.accepts ?? [r.capability]).some((a) => provides.includes(a)))
    .map(([id, r]) => [id, r.label]);
}

/** Dosen einer Netzsteckdose: was eingesteckt ist (Rolle), Zustand, Leistung, Testen. */
export function OutletRoles(p: { d: Device }) {
  const outlets = p.d.info?.outlets ?? [];
  const roles = switchRolesFor(p.d.class);
  const roleAt = (ch: number) => roles.find(([id]) => binding(id)?.device === p.d.id && binding(id)?.channel === ch)?.[0] ?? "";
  if (!p.d.configured) return null;
  return (
    <div class="outlets" data-testid={`outlets-${p.d.id}`}>
      {outlets.map((o, ch) => {
        const role = roleAt(ch);
        return (
          <div class="outlet">
            <span class={`outlet-state ${o.on ? "on" : ""}`} title={o.on ? t("common.on") : t("common.off")}>
              <Zap size={14} />
            </span>
            <span class="nowrap">{outlets.length > 1 ? t("port.outlet", { n: ch + 1 }) : t("port.socket")}</span>
            <select
              class="select"
              name={`outlet-${p.d.id}-${ch}`}
              value={role}
              aria-label={t("net.plugged")}
              onChange={async (e) => {
                const v = (e.target as HTMLSelectElement).value;
                try {
                  if (role) await del(`/roles/${role}`);
                  if (v) await put(`/roles/${v}`, { device: p.d.id, channel: ch });
                  await refreshConfig();
                  await refreshState();
                  if (v) toast(t("net.assigned"));
                } catch (err: any) {
                  toast(err.message, "error");
                  await refreshConfig();
                }
              }}
            >
              <option value="">{t("net.nothing")}</option>
              {roles.map(([id, label]) => {
                const taken = binding(id)?.device && !(binding(id)?.device === p.d.id && binding(id)?.channel === ch);
                return (
                  <option value={id} disabled={!!taken}>
                    {label}
                    {taken ? ` (${t("net.elsewhere")})` : ""}
                  </option>
                );
              })}
            </select>
            <span class="faint small nowrap">{o.powerW === null ? "–" : `${num(o.powerW, 0)} W`}</span>
            <Button size="sm" disabled={!role} onClick={() => post(`/roles/${role}/test`).then(refreshState)}>
              {t("common.test")}
            </Button>
          </div>
        );
      })}
      {config.value && outlets.length > 1 && <p class="faint small">{t("net.countHint")}</p>}
    </div>
  );
}

/** Kleine Kurven der letzten 6 h für Messwert-Kacheln. */
export function useSparks(series: string[]) {
  const [data, setData] = useState<Record<string, (number | null)[]>>({});
  useEffect(() => {
    let alive = true;
    const load = async () => {
      if (!series.length) return;
      const now = state.value?.now ?? Math.floor(Date.now() / 1000);
      const r = await get<{ series: SeriesData[] }>(`/history?series=${series.join(",")}&from=${now - 6 * 3600}&to=${now}&points=72`);
      if (alive) setData(Object.fromEntries(r.series.map((s) => [s.series, s.avg])));
    };
    load().catch(() => {});
    const t = setInterval(() => load().catch(() => {}), 60000);
    return () => {
      alive = false;
      clearInterval(t);
    };
  }, [series.join(",")]);
  return data;
}
