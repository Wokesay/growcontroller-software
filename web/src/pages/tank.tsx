// SPDX-License-Identifier: AGPL-3.0-or-later
// Tank & Regelung: Regelzeilen mit Checkliste, Überwachung, Ausgänge,
// Rastungen, Pflegemodus, Tank-Einstellungen, Durchgang und Phasen.
import { useState } from "preact/hooks";
import { Droplets, ListChecks, Power, ShieldCheck, Sprout, Wrench } from "lucide-preact";
import { post, put } from "../api";
import { dateTime, num, numUpTo } from "../format";
import { config, hasRole, paramDef, phMiddle, recipes, refreshConfig, refreshState, state, tank, toast } from "../store";
import { msg, t as tr } from "../i18n";
import { Banner, Button, Card, Field, Modal, NumberInput, Pill, Seg } from "../ui";
import { ControllerRow, latchLabel } from "../widgets";

function TankSettings() {
  const t = tank.value!;
  const [name, setName] = useState(t.name);
  const [cap, setCap] = useState<number | null>(t.capacityL);
  const [min, setMin] = useState<number | null>(t.minL);
  const [water, setWater] = useState(t.water);
  const [vol, setVol] = useState<number | null>(null);
  return (
    <div class="stack">
      <div class="form-grid">
        <Field label={tr("setup.tank.name")}>
          <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
        </Field>
        <Field label={tr("term.usableVolume")} hint={tr("tank.capacityHint")}>
          <NumberInput value={cap} onValue={setCap} unit="L" />
        </Field>
        <Field label={tr("term.minLevel")} hint={tr("tank.minHint")} help="minLevel">
          <NumberInput value={min} onValue={setMin} unit="L" />
        </Field>
        <Field label={tr("setup.tank.water")}>
          <select class="select" value={water} onChange={(e) => setWater((e.target as HTMLSelectElement).value)}>
            <option value="ro">{tr("setup.tank.ro")}</option>
            <option value="tap">{tr("setup.tank.tap")}</option>
          </select>
        </Field>
        {!hasRole("tank.level") && (
          <Field label={tr("tank.volume")} hint={tr("tank.volumeHint")}>
            <NumberInput value={vol} onValue={setVol} unit="L" placeholder={num(state.value?.tank.volumeL ?? null, 1)} />
          </Field>
        )}
      </div>
      <div>
        <Button
          variant="primary"
          onClick={async () => {
            await put("/tank", { name, capacityL: cap, minL: min, water, ...(vol !== null ? { volumeL: vol } : {}) });
            await refreshConfig();
            toast(tr("tank.saved"));
          }}
        >
          {tr("common.save")}
        </Button>
      </div>
    </div>
  );
}

// The value a phase without its own target gets: the function's setting, else the catalog's default
function targetFallback(fnId: string, key: string): number | null {
  const v = config.value?.functions[fnId]?.params?.[key];
  if (typeof v === "number") return v;
  const d = paramDef(fnId, key)?.default;
  return typeof d === "number" ? d : null;
}

type PhaseDraft = { name: string; days: number; recipe: string; ph: number | null; ec: number | null; ownPh: boolean; ownEc: boolean };

function GrowCard() {
  const st = state.value!;
  const g = st.grow;
  const rs = recipes.value;
  const [open, setOpen] = useState(false);
  const [name, setName] = useState(tr("tank.growDefault"));
  // Each phase's targets come from its recipe's chart (SD-057): its EC at its amounts and the middle of its
  // pH range (RAT-066). A value the user typed stays when the recipe changes; empty is not sent, the
  // function's setting then applies.
  const fromRecipe = (id: string) => {
    const r = rs.find((x) => x.id === id);
    return { ec: r?.refEc ?? null, ph: phMiddle(r) };
  };
  const draft = (n: string, days: number, recipe: string): PhaseDraft => ({ name: n, days, recipe, ...fromRecipe(recipe), ownPh: false, ownEc: false });
  // Proposed afresh each time the dialog opens, from the recipes as they are then
  const fresh = (): PhaseDraft[] => [draft(tr("tank.phaseVeg"), 21, rs[0]?.id ?? ""), draft(tr("tank.phaseFlower"), 56, rs[1]?.id ?? rs[0]?.id ?? "")];
  const [phases, setPhases] = useState<PhaseDraft[]>(fresh);
  const upd = (i: number, k: keyof PhaseDraft, v: unknown) =>
    setPhases(
      phases.map((p, j) => {
        if (j !== i) return p;
        // Clearing a proposal is a choice too: the function's value then applies, also after another recipe
        if (k === "ph") return { ...p, ph: v as number | null, ownPh: true };
        if (k === "ec") return { ...p, ec: v as number | null, ownEc: true };
        if (k !== "recipe") return { ...p, [k]: v };
        const proposed = fromRecipe(v as string);
        return { ...p, recipe: v as string, ph: p.ownPh ? p.ph : proposed.ph, ec: p.ownEc ? p.ec : proposed.ec };
      }),
    );
  const phFallback = targetFallback("ph_control", "ph_target");
  const ecFallback = targetFallback("ec_control", "ec_target");
  // Below the pH lock's EC, pH control never doses (RAT-046): said at the field while pH control is on
  const ecFloor = config.value?.functions["ph_control"]?.enabled ? targetFallback("ph_control", "ec_floor") : null;
  return (
    <Card title={tr("tank.growTitle")} icon={<Sprout size={18} />}>
      {g.state === "running" ? (
        <div class="stack">
          <div class="row-between">
            <div>
              <strong>{g.name}</strong>
              <div class="muted small">{tr("tank.started", { at: dateTime(g.startedAt), day: Math.floor((st.now - g.startedAt) / 86400) + 1 })}</div>
            </div>
            <Pill tone="accent">{tr("tank.phaseOf", { n: g.phase + 1, total: g.phases.length })}</Pill>
          </div>
          <table class="table">
            <tbody>
              {g.phases.map((p, i) => (
                <tr style={i === g.phase ? "font-weight:650" : ""}>
                  <td>{i === g.phase ? "▶" : ""}</td>
                  {/* The days under the name: a phone's width holds three columns, not four (#106) */}
                  <td>
                    {p.name}
                    <div class="small muted">{tr("tank.days", { n: p.days })}</div>
                  </td>
                  <td class="small muted">
                    {[
                      ...Object.entries(p.params).map(([k, v]) =>
                        k === "recipe" ? tr("tank.paramRecipe", { name: recipes.value.find((r) => r.id === v)?.name ?? v }) : `${k === "ph_target" ? "pH" : k === "ec_target" ? "EC" : k} ${typeof v === "number" ? numUpTo(v, 2) : v}${k === "ec_target" ? " mS/cm" : ""}`,
                      ),
                      // A phase without its own target follows the function's, which can change during the grow
                      ...([["ph_target", "pH", phFallback], ["ec_target", "EC", ecFallback]] as const)
                        .filter(([k, , v]) => !(k in p.params) && v !== null)
                        .map(([, label, v]) => `${label} ${numUpTo(v, 2)}${label === "EC" ? " mS/cm" : ""} (${tr("tank.fromFunctions")})`),
                    ].join(" · ")}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
          <p class="faint small">{tr("tank.phaseNote")}</p>
          <div class="row">
            <Button size="sm" disabled={g.phase + 1 >= g.phases.length} onClick={() => post("/grow/next").then(refreshConfig).then(refreshState)}>
              {tr("tank.nextPhase")}
            </Button>
            <Button size="sm" onClick={() => post("/grow/harvest").then(refreshState).then(() => toast(tr("tank.harvested")))}>
              {tr("tank.recordHarvest")}
            </Button>
            <Button size="sm" variant="ghost" onClick={() => post("/grow/complete").then(refreshConfig).then(refreshState)}>
              {tr("tank.completeGrow")}
            </Button>
          </div>
          {g.harvestedAt > 0 && <span class="muted small">{tr("tank.harvestedAt", { at: dateTime(g.harvestedAt) })}</span>}
        </div>
      ) : (
        <div class="stack-sm">
          <p class="muted">{g.state === "completed" ? tr("tank.growCompleted", { name: g.name }) : tr("tank.noGrow")} {tr("tank.growIntro")}</p>
          <div>
            <Button
              size="sm"
              onClick={() => {
                setPhases(fresh());
                setOpen(true);
              }}
            >
              {tr("tank.startGrow")}
            </Button>
          </div>
        </div>
      )}
      {open && (
        <Modal
          wide
          title={tr("tank.startGrow")}
          onClose={() => setOpen(false)}
          footer={
            <>
              <button class="btn" onClick={() => setOpen(false)}>
                {tr("common.cancel")}
              </button>
              <Button
                variant="primary"
                onClick={async () => {
                  await post("/grow/start", {
                    name,
                    phases: phases.map((p) => ({
                      name: p.name,
                      days: p.days,
                      params: { ...(p.ph !== null ? { ph_target: p.ph } : {}), ...(p.ec !== null ? { ec_target: p.ec } : {}), recipe: p.recipe },
                    })),
                  });
                  setOpen(false);
                  await refreshConfig();
                  await refreshState();
                  toast(tr("tank.growStarted"));
                }}
              >
                {tr("tank.start")}
              </Button>
            </>
          }
        >
          <Field label={tr("common.name")}>
            <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
          </Field>
          <p class="muted small">{tr("tank.targetsFromRecipe")}</p>
          {phases.map((p, i) => (
            <div class="card flat">
              <div class="form-grid">
                <Field label={tr("tank.phaseN", { n: i + 1 })}>
                  <input class="input" value={p.name} onInput={(e) => upd(i, "name", (e.target as HTMLInputElement).value)} />
                </Field>
                <Field label={tr("tank.phaseLength")}>
                  <NumberInput value={p.days} onValue={(v) => upd(i, "days", v ?? 0)} unit={tr("tank.daysUnit")} />
                </Field>
                {/* Keyed by the recipe: a new proposal, also an empty one, shows at once */}
                <Field label={tr("tank.targetPh")} hint={p.ph === null && phFallback !== null ? tr("tank.targetFallback", { value: numUpTo(phFallback, 2) }) : undefined}>
                  <NumberInput key={`ph-${p.recipe}`} value={p.ph} onValue={(v) => upd(i, "ph", v)} placeholder={phFallback === null ? undefined : numUpTo(phFallback, 2)} />
                </Field>
                <Field
                  label={tr("tank.targetEc")}
                  hint={p.ec === null && ecFallback !== null ? tr("tank.targetFallback", { value: `${numUpTo(ecFallback, 2)} mS/cm` }) : undefined}
                  error={ecFloor !== null && (p.ec ?? ecFallback ?? Infinity) < ecFloor ? tr("tank.belowEcFloor", { floor: numUpTo(ecFloor, 2) }) : null}
                >
                  <NumberInput key={`ec-${p.recipe}`} value={p.ec} onValue={(v) => upd(i, "ec", v)} unit="mS/cm" placeholder={ecFallback === null ? undefined : numUpTo(ecFallback, 2)} />
                </Field>
                <Field label={tr("mix.recipe")}>
                  <select class="select" value={p.recipe} onChange={(e) => upd(i, "recipe", (e.target as HTMLSelectElement).value)}>
                    {rs.map((r) => (
                      <option value={r.id}>{r.name}</option>
                    ))}
                  </select>
                </Field>
              </div>
            </div>
          ))}
          <div>
            <button class="btn sm" onClick={() => setPhases([...phases, draft(tr("tank.phaseN", { n: phases.length + 1 }), 14, rs[0]?.id ?? "")])}>
              {tr("tank.addPhase")}
            </button>
          </div>
        </Modal>
      )}
    </Card>
  );
}

// A latch's reason: a message, or plain text in a state saved before SD-032.
const latchWhy = (v: { why?: unknown } | undefined) => (typeof v?.why === "string" ? v.why : msg(v?.why as Parameters<typeof msg>[0]));

export function TankPage() {
  const st = state.value!;
  const wd = st.watchdog;
  const [filter, setFilter] = useState<"all" | "problem">("all");
  const [maint, setMaint] = useState<number | null>(30);
  const latches = Object.entries(st.latches);
  const items = wd.items.filter((i) => filter === "all" || i.status === "problem");
  return (
    <div class="stack">
      <div class="grid-2">
        <Card title={tr("overview.control")} icon={<ListChecks size={18} />}>
          <p class="muted small" style="margin-bottom:10px">
            {tr("tank.controlHint")}
          </p>
          <div class="stack-sm">
            <ControllerRow name="EC" st={st.controllers.ec} open />
            <ControllerRow name="pH" st={st.controllers.ph} open />
            <ControllerRow name={tr("overview.refill")} st={st.controllers.refill} />
            <ControllerRow name={tr("overview.circulate")} st={st.controllers.circulation} />
          </div>
          {latches.length > 0 && (
            <div class="stack-sm" style="margin-top:14px">
              <div class="section-title">{tr("tank.locks")}</div>
              {latches.map(([k, v]) => (
                <div class="row-between">
                  <span>
                    <Pill tone="bad">{k.startsWith("jump.") ? tr("term.jumpLock") : tr("tank.latched")}</Pill> {latchLabel(k)}
                    {latchWhy(v) ? ` – ${latchWhy(v)}` : ""}
                  </span>
                  {!k.startsWith("jump.") && (
                    <Button size="sm" onClick={() => post(`/latches/${k}/ack`).then(refreshState)}>
                      {tr("tank.ack")}
                    </Button>
                  )}
                </div>
              ))}
            </div>
          )}
        </Card>
        <Card
          title={tr("tank.monitoring")}
          icon={<ShieldCheck size={18} />}
          actions={<Seg value={filter} onChange={setFilter} options={[["all", tr("common.all")], ["problem", tr("tank.problems")]]} />}
        >
          <div class="stack-sm">
            <div class="row">
              <Pill tone={wd.stale || wd.overall === "problem" ? "bad" : wd.overall === "ok" ? "ok" : "neutral"}>{wd.stale ? tr("tank.noAssessment") : msg(wd.headline)}</Pill>
              <span class="faint small">{tr("tank.evaluated", { at: dateTime(wd.evaluatedAt) })}</span>
            </div>
            <div class="list">
              {items.map((a) => (
                <div class="item">
                  <Pill tone={a.status === "ok" ? "ok" : a.status === "problem" ? "bad" : "neutral"}>{a.status === "ok" ? tr("tank.statusOk") : a.status === "problem" ? tr("tank.statusProblem") : tr("tank.statusIdle")}</Pill>
                  <div class="grow">
                    <div class="title">{msg(a.label)}</div>
                    <div class="muted small">{msg(a.text)}</div>
                  </div>
                </div>
              ))}
              {!items.length && <p class="muted">{tr("tank.noProblems")}</p>}
            </div>
          </div>
        </Card>
      </div>
      <div class="grid-2">
        <Card title={tr("setup.tank.h")} icon={<Droplets size={18} />}>
          <TankSettings />
        </Card>
        <div class="stack">
          <Card title={tr("tank.outputs")} icon={<Power size={18} />}>
            <div class="list">
              {[["tank.circulation", tr("tank.circPump")], ["tank.inlet", tr("tank.inletValve")]].map(([role, label]) => (
                <div class="item">
                  <div class="grow">
                    <div class="title">{label}</div>
                    <div class="muted small">{hasRole(role) ? tr("tank.assigned") : tr("tank.unassigned")}</div>
                  </div>
                  {hasRole(role) && <Pill tone={st.outputs[role] ? "info" : "neutral"} dot>{st.outputs[role] ? tr("common.on") : tr("common.off")}</Pill>}
                </div>
              ))}
            </div>
            <p class="faint small" style="margin-top:8px">
              {tr("tank.outputsNote")}
            </p>
          </Card>
          <Card title={tr("term.maintenance")} icon={<Wrench size={18} />}>
            {st.maintenanceUntil > st.now ? (
              <div class="stack-sm">
                <div class="row-between">
                  <span>{tr("tank.maintenanceUntil", { at: dateTime(st.maintenanceUntil) })}</span>
                  <Button size="sm" aria-describedby="maintenance-end-hint" onClick={() => post("/maintenance", { minutes: 0 }).then(refreshState)}>
                    {tr("tank.end")}
                  </Button>
                </div>
                <p class="muted small" id="maintenance-end-hint" data-testid="maintenance-end-hint">{tr("tank.maintenanceEndHint")}</p>
              </div>
            ) : (
              <div class="stack-sm">
                <p class="muted small">{tr("tank.maintenanceText")}</p>
                <div class="row">
                  <div style="width:140px">
                    <NumberInput value={maint} onValue={setMaint} unit="min" />
                  </div>
                  <Button size="sm" onClick={() => post("/maintenance", { minutes: maint }).then(refreshState)}>
                    {tr("tank.startMaintenance")}
                  </Button>
                </div>
              </div>
            )}
          </Card>
        </div>
      </div>
      <GrowCard />
      {config.value && !hasRole("tank.ph") && (
        <Banner>{tr("tank.headHint")}</Banner>
      )}
    </div>
  );
}
