// SPDX-License-Identifier: AGPL-3.0-or-later
// Tank & Regelung: Regelzeilen mit Checkliste, Überwachung, Ausgänge,
// Rastungen, Pflegemodus, Tank-Einstellungen, Durchgang und Phasen.
import { useState } from "preact/hooks";
import { Droplets, ListChecks, Power, ShieldCheck, Sprout, Wrench } from "lucide-preact";
import { post, put } from "../api";
import { dateTime, num } from "../format";
import { config, hasRole, recipes, refreshConfig, refreshState, state, tank, toast } from "../store";
import { t as tr } from "../i18n";
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

function GrowCard() {
  const st = state.value!;
  const g = st.grow;
  const rs = recipes.value;
  const [open, setOpen] = useState(false);
  const [name, setName] = useState(tr("tank.growDefault"));
  const [phases, setPhases] = useState([
    { name: tr("tank.phaseVeg"), days: 21, ph: 5.8 as number | null, ec: 1.4 as number | null, recipe: rs[0]?.id ?? "" },
    { name: tr("tank.phaseFlower"), days: 56, ph: 5.9 as number | null, ec: 1.8 as number | null, recipe: rs[1]?.id ?? rs[0]?.id ?? "" },
  ]);
  const upd = (i: number, k: string, v: unknown) => setPhases(phases.map((p, j) => (j === i ? { ...p, [k]: v } : p)));
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
                  <td>{p.name}</td>
                  <td class="num">{tr("tank.days", { n: p.days })}</td>
                  <td class="small muted">{Object.entries(p.params).map(([k, v]) => (k === "recipe" ? tr("tank.paramRecipe", { name: recipes.value.find((r) => r.id === v)?.name ?? v }) : `${k === "ph_target" ? "pH" : k === "ec_target" ? "EC" : k} ${typeof v === "number" ? num(v, 2) : v}`)).join(" · ")}</td>
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
            <Button size="sm" onClick={() => setOpen(true)}>
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
                    phases: phases.map((p) => ({ name: p.name, days: p.days, params: { ph_target: p.ph, ec_target: p.ec, recipe: p.recipe } })),
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
          <Field label={tr("setup.tank.name")}>
            <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
          </Field>
          {phases.map((p, i) => (
            <div class="card flat">
              <div class="form-grid">
                <Field label={tr("tank.phaseN", { n: i + 1 })}>
                  <input class="input" value={p.name} onInput={(e) => upd(i, "name", (e.target as HTMLInputElement).value)} />
                </Field>
                <Field label={tr("mix.duration")}>
                  <NumberInput value={p.days} onValue={(v) => upd(i, "days", v ?? 0)} unit={tr("tank.daysUnit")} />
                </Field>
                <Field label={tr("tank.targetPh")}>
                  <NumberInput value={p.ph} onValue={(v) => upd(i, "ph", v)} />
                </Field>
                <Field label={tr("tank.targetEc")}>
                  <NumberInput value={p.ec} onValue={(v) => upd(i, "ec", v)} unit="mS/cm" />
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
            <button class="btn sm" onClick={() => setPhases([...phases, { name: tr("tank.phaseN", { n: phases.length + 1 }), days: 14, ph: 5.8, ec: 1.6, recipe: rs[0]?.id ?? "" }])}>
              {tr("tank.addPhase")}
            </button>
          </div>
        </Modal>
      )}
    </Card>
  );
}

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
                    {v?.why ? ` – ${v.why}` : ""}
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
              <Pill tone={wd.stale || wd.overall === "problem" ? "bad" : wd.overall === "ok" ? "ok" : "neutral"}>{wd.stale ? tr("tank.noAssessment") : wd.headline}</Pill>
              <span class="faint small">{tr("tank.evaluated", { at: dateTime(wd.evaluatedAt) })}</span>
            </div>
            <div class="list">
              {items.map((a) => (
                <div class="item">
                  <Pill tone={a.status === "ok" ? "ok" : a.status === "problem" ? "bad" : "neutral"}>{a.status === "ok" ? tr("tank.statusOk") : a.status === "problem" ? tr("tank.statusProblem") : tr("tank.statusIdle")}</Pill>
                  <div class="grow">
                    <div class="title">{a.label}</div>
                    <div class="muted small">{a.text}</div>
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
              <div class="row-between">
                <span>{tr("tank.maintenanceUntil", { at: dateTime(st.maintenanceUntil) })}</span>
                <Button size="sm" onClick={() => post("/maintenance", { minutes: 0 }).then(refreshState)}>
                  {tr("tank.end")}
                </Button>
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
