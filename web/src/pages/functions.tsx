// SPDX-License-Identifier: AGPL-3.0-or-later
// Funktionen = Konfigurationsbaum. Jede Funktion zeigt, was mit der
// vorhandenen Hardware geht, was fehlt (mit „Jetzt erledigen“) und klappt
// ihre Einstellungen erst auf, wenn sie einrichtbar ist.
import { useEffect, useState } from "preact/hooks";
import { ChevronDown, ChevronRight, SlidersHorizontal } from "lucide-preact";
import { patch, type FunctionState, type ParamDef } from "../api";
import { catalog, config, recipes, refreshConfig, refreshState, state, toast } from "../store";
import { t, type TextKey } from "../i18n";
import { figure } from "../format";
import { Button, Card, CheckRow, Field, NumberInput, Pill, Toggle, ctlLabel, ctlTone, route, setupLabel } from "../ui";

const CTL: Record<string, "ec" | "ph" | "refill" | "circulation"> = { ec_control: "ec", ph_control: "ph", refill: "refill", circulation: "circulation" };
const STAGES: TextKey[] = ["functions.stage0", "functions.stage1", "functions.stage2", "functions.stage3", "functions.stage4"];

function Params(p: { f: FunctionState; defs: ParamDef[] }) {
  const cfg = config.value!;
  const saved = cfg.functions[p.f.id]?.params ?? {};
  const [vals, setVals] = useState<Record<string, unknown>>(Object.fromEntries(p.defs.map((d) => [d.key, saved[d.key] ?? d.default ?? null])));
  const phaseActive = cfg.grow.state === "running";
  return (
    <div class="stack">
      <div class="form-grid">
        {p.defs.map((d) => (
          <Field label={d.label} hint={d.phase && phaseActive ? t("functions.phaseOverride") : d.min !== undefined ? `${figure(d.min)}–${d.max !== undefined ? figure(d.max) : ""}` : undefined}>
            {d.type === "number" ? (
              <NumberInput value={(vals[d.key] as number) ?? null} onValue={(v) => setVals({ ...vals, [d.key]: v })} unit={d.unit || undefined} />
            ) : d.type === "enum" ? (
              <select class="select" value={String(vals[d.key] ?? "")} onChange={(e) => setVals({ ...vals, [d.key]: (e.target as HTMLSelectElement).value })}>
                {d.options?.map(([v, l]) => (
                  <option value={v}>{l}</option>
                ))}
              </select>
            ) : (
              <select class="select" value={String(vals[d.key] ?? "")} onChange={(e) => setVals({ ...vals, [d.key]: (e.target as HTMLSelectElement).value })}>
                <option value="">{t("functions.firstRecipe")}</option>
                {recipes.value.map((r) => (
                  <option value={r.id}>{r.name}</option>
                ))}
              </select>
            )}
          </Field>
        ))}
      </div>
      <div>
        <Button
          size="sm"
          variant="primary"
          onClick={async () => {
            const params = Object.fromEntries(Object.entries(vals).filter(([, v]) => v !== null && v !== ""));
            await patch(`/functions/${p.f.id}`, { params });
            await refreshConfig();
            toast(t("functions.saved"));
          }}
        >
          {t("common.save")}
        </Button>
      </div>
    </div>
  );
}

function FunctionRow(p: { f: FunctionState; open: boolean }) {
  const [open, setOpen] = useState(p.open);
  const st = state.value!;
  const defs = catalog.value?.functions.find((x) => x.id === p.f.id)?.params ?? [];
  const [label, tone] = setupLabel[p.f.setup];
  const ctl = CTL[p.f.id] ? st.controllers[CTL[p.f.id]] : null;
  const canToggle = !p.f.alwaysOn && (p.f.setup === "ready" || p.f.setup === "limited" || p.f.enabled);
  const hw = p.f.checks.filter((c) => c.level === "hardware");
  const setup = p.f.checks.filter((c) => c.level === "setup");
  const runtime = p.f.checks.filter((c) => c.level === "runtime");
  useEffect(() => {
    if (p.open) setOpen(true);
  }, [p.open]);
  return (
    <div class="card flat" id={`fn-${p.f.id}`} data-testid={`fn-${p.f.id}`}>
      <div class="row-between" style="cursor:pointer" onClick={() => setOpen(!open)}>
        <div class="row" style="gap:10px">
          {open ? <ChevronDown size={16} /> : <ChevronRight size={16} />}
          <div>
            <h3>{p.f.label}</h3>
            <div class="muted small">{p.f.setup === "ready" ? p.f.text : p.f.summary.text}</div>
          </div>
        </div>
        <div class="row" onClick={(e) => e.stopPropagation()}>
          {p.f.enabled && ctl && <Pill tone={ctlTone(ctl.state)} dot>{ctlLabel[ctl.state]}</Pill>}
          <Pill tone={p.f.enabled && p.f.setup !== "unavailable" ? "accent" : tone}>{p.f.alwaysOn && p.f.setup !== "unavailable" && p.f.setup !== "needs_setup" ? t("functions.alwaysOn") : p.f.enabled && !p.f.alwaysOn ? t("functions.active") : label}</Pill>
          {!p.f.alwaysOn && (
            <Toggle
              checked={p.f.enabled}
              disabled={!canToggle}
              label={p.f.enabled ? t("area.turnOff") : t("area.turnOn")}
              onChange={async (v) => {
                try {
                  await patch(`/functions/${p.f.id}`, { enabled: v });
                  await refreshConfig();
                  await refreshState();
                  toast(t(v ? "functions.switchedOn" : "functions.switchedOff", { name: p.f.label }));
                } catch (e: any) {
                  toast(e.message, "error");
                }
              }}
            />
          )}
        </div>
      </div>
      {open && (
        <div class="stack" style="margin-top:12px">
          {hw.length > 0 && (
            <div class="stack-sm">
              <div class="section-title">{t("functions.hardware")}</div>
              {hw.map((c) => (
                <CheckRow ok={c.ok} text={c.text} fix={c.fix} />
              ))}
            </div>
          )}
          {setup.length > 0 && (
            <div class="stack-sm">
              <div class="section-title">{t("nav.setup")}</div>
              {setup.map((c) => (
                <CheckRow ok={c.ok} soft={c.soft} text={c.text} fix={c.fix} />
              ))}
            </div>
          )}
          {runtime.length > 0 && p.f.setup !== "unavailable" && (
            <div class="stack-sm">
              <div class="section-title">{t("functions.now")}</div>
              {runtime.map((c) => (
                <CheckRow ok={c.ok} text={c.text} />
              ))}
              {ctl && p.f.enabled && <p class="muted small">{ctl.line.text}</p>}
            </div>
          )}
          {defs.length > 0 && (p.f.setup === "unavailable" ? (
            <p class="faint small">{t("functions.settingsLater")}</p>
          ) : (
            <div class="stack-sm">
              <div class="section-title">{t("nav.settings")}</div>
              <Params f={p.f} defs={defs} />
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export function FunctionsPage() {
  const st = state.value!;
  const focus = route.value.query.f;
  const byStage = new Map<number, FunctionState[]>();
  for (const f of st.functions) byStage.set(f.stage, [...(byStage.get(f.stage) ?? []), f]);
  useEffect(() => {
    if (focus) document.getElementById(`fn-${focus}`)?.scrollIntoView({ behavior: "smooth", block: "start" });
  }, [focus]);
  const counts = st.functions.reduce((a, f) => ({ ...a, [f.setup]: (a[f.setup] ?? 0) + 1 }), {} as Record<string, number>);
  return (
    <div class="stack">
      <Card title={t("functions.title")} icon={<SlidersHorizontal size={18} />}>
        <div class="row">
          <Pill tone="ok">{t("functions.usable", { n: (counts.ready ?? 0) + (counts.limited ?? 0) })}</Pill>
          <Pill tone="warn">{t("functions.toSetUp", { n: counts.needs_setup ?? 0 })}</Pill>
          <Pill tone="neutral">{t("functions.needHardware", { n: counts.unavailable ?? 0 })}</Pill>
        </div>
        <p class="muted small" style="margin-top:10px">
          {t("functions.intro")}
        </p>
      </Card>
      {[...byStage.entries()].sort((a, b) => a[0] - b[0]).map(([stage, fns]) => (
        <div class="stack-sm">
          <div class="section-title">{STAGES[stage] ? t(STAGES[stage]) : t("functions.stage", { n: stage })}</div>
          {fns.map((f) => (
            <FunctionRow f={f} open={focus === f.id} />
          ))}
        </div>
      ))}
    </div>
  );
}
