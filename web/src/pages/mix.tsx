// SPDX-License-Identifier: AGPL-3.0-or-later
// Mischen: Rezept, Wassermenge, Vorschau mit ml je Kanister, geführter Ablauf.
// Handgabe für einzelne Kanister.
import { useEffect, useState } from "preact/hooks";
import { Beaker, Droplet, FlaskConical, Hand, Play } from "lucide-preact";
import { ApiError, post, type MixPlan } from "../api";
import { duration, num } from "../format";
import { canisters, config, recipes, refreshState, state, toast } from "../store";
import { t } from "../i18n";
import { Banner, Button, Card, Empty, Field, Modal, NumberInput, Seg, Toggle } from "../ui";
import { JobView } from "../widgets";

function Preview(p: { plan: MixPlan | null; loading: boolean }) {
  const plan = p.plan;
  if (!plan) return <p class="muted">{t("mix.previewEmpty")}</p>;
  return (
    <div class="stack-sm">
      <table class="table">
        <thead>
          <tr>
            <th>#</th>
            <th>{t("term.bottle")}</th>
            <th class="num">ml/L</th>
            <th class="num">{t("mix.amount")}</th>
            <th class="num hide-sm">{t("mix.duration")}</th>
          </tr>
        </thead>
        <tbody>
          {plan.steps.map((s, i) => (
            <tr>
              <td class="faint">{i + 1}</td>
              <td>
                <span class="row" style="gap:8px">
                  <span class="swatch" style={`background:${s.color}`} />
                  {s.name}
                  {s.pair && <span class="faint small">{t("mix.pair", { pair: s.pair })}</span>}
                </span>
              </td>
              <td class="num">{num(s.mlPerL, 2)}</td>
              <td class="num">
                <strong>{num(s.ml, 1)} ml</strong>
              </td>
              <td class="num hide-sm faint">{s.runs.length ? duration(s.runs.reduce((a, b) => a + b, 0)) : "–"}</td>
            </tr>
          ))}
        </tbody>
      </table>
      <div class="row-between small">
        <span class="muted">{t("mix.total", { ml: num(plan.totalMl, 1), time: duration(plan.totalMs) })}</span>
        <span class="muted">{plan.after.text}</span>
      </div>
      {plan.errors.map((e) => (
        <Banner tone="bad">{e.text}</Banner>
      ))}
      {plan.warnings.map((w) => (
        <Banner tone="warn">{w.text}</Banner>
      ))}
    </div>
  );
}

function ManualDose() {
  const cans = canisters.value.filter((k) => k.pump);
  const limit = config.value?.limits.handDoseMaxMl ?? 5;
  const [can, setCan] = useState(cans[0]?.id ?? "");
  const [ml, setMl] = useState<number | null>(null);
  if (!cans.length) return <p class="muted">{t("mix.noPumpBottles")}</p>;
  const sel = cans.find((k) => k.id === can);
  return (
    <div class="stack">
      <div class="form-grid">
        <Field label={t("term.bottle")}>
          <select class="select" value={can} onChange={(e) => setCan((e.target as HTMLSelectElement).value)}>
            {cans.map((k) => (
              <option value={k.id}>{k.name}</option>
            ))}
          </select>
        </Field>
        <Field label={t("mix.amount")} hint={t("mix.manualMax", { ml: num(limit, 1) })}>
          <NumberInput value={ml} onValue={setMl} unit="ml" />
        </Field>
      </div>
      {sel?.pair && <Banner tone="warn">{t("mix.pairWarn", { name: sel.name })}</Banner>}
      <div>
        <Button
          disabled={!ml || ml <= 0 || !!state.value?.job}
          onClick={async () => {
            await post("/dose", { canister: can, ml });
            toast(t("mix.dosing", { name: sel?.name, ml: num(ml, 1) }));
            await refreshState();
          }}
        >
          <Hand size={16} /> {t("mix.dose")}
        </Button>
      </div>
    </div>
  );
}

export function MixPage() {
  const st = state.value!;
  const rs = recipes.value;
  const [recipe, setRecipe] = useState(rs[0]?.id ?? "");
  const [water, setWater] = useState<number | null>(null);
  const [mode, setMode] = useState<"new" | "topup">("new");
  const [guided, setGuided] = useState(true);
  const [plan, setPlan] = useState<MixPlan | null>(null);
  const [loading, setLoading] = useState(false);
  const [confirm, setConfirm] = useState<string | null>(null);
  const hasCirc = !!config.value?.tanks[0]?.roles?.["tank.circulation"]?.device;

  useEffect(() => {
    if (!recipe || !water) {
      setPlan(null);
      return;
    }
    setLoading(true);
    const t = setTimeout(async () => {
      try {
        setPlan(await post<MixPlan>("/mix/plan", { recipe, waterL: water, mode, confirmRepeat: true }));
      } finally {
        setLoading(false);
      }
    }, 250);
    return () => clearTimeout(t);
  }, [recipe, water, mode]);

  async function start(confirmRepeat = false) {
    try {
      await post("/mix/start", { recipe, waterL: water, mode, guided, confirmRepeat });
      setConfirm(null);
      toast(t("mix.started"));
      await refreshState();
    } catch (e) {
      if (e instanceof ApiError && e.key === "mix.recent") setConfirm(e.message);
      else throw e;
    }
  }

  const job = st.job;
  if (!rs.length)
    return (
      <Card>
        <Empty icon={<FlaskConical size={36} />} title={t("mix.noRecipe")} text={t("mix.noRecipeText")} action={<a class="btn primary" href="#/rezepte">{t("mix.toRecipes")}</a>} />
      </Card>
    );
  return (
    <div class="stack">
      {job ? (
        <Card title={t("mix.running")} icon={<Beaker size={18} />}>
          <JobView job={job} />
        </Card>
      ) : st.lastJob && st.lastJob.type === "mix" && st.now - st.lastJob.finishedAt < 3600 ? (
        <Card title={t("mix.lastMix")} icon={<Beaker size={18} />}>
          <JobView job={st.lastJob} />
        </Card>
      ) : null}
      <div class="grid-2">
        <Card title={t("mix.title")} icon={<Droplet size={18} />}>
          <div class="stack">
            <div class="form-grid">
              <Field label={t("mix.recipe")}>
                <select class="select" value={recipe} onChange={(e) => setRecipe((e.target as HTMLSelectElement).value)} name="recipe">
                  {rs.map((r) => (
                    <option value={r.id}>{r.name}</option>
                  ))}
                </select>
              </Field>
              <Field label={mode === "new" ? t("mix.waterInTank") : t("mix.freshWater")} hint={st.tank.capacityL ? t("mix.capacityHint", { l: num(st.tank.capacityL, 0) }) : undefined}>
                <NumberInput value={water} onValue={setWater} unit="L" name="water" />
              </Field>
            </div>
            <div class="row-between">
              <Seg value={mode} onChange={setMode} options={[["new", t("mix.modeNew")], ["topup", t("mix.modeTopup")]]} />
              <label class="row small">
                <Toggle checked={guided} onChange={setGuided} label={t("mix.guided")} /> {t("mix.guided")} {hasCirc ? "" : t("mix.stirHint")}
              </label>
            </div>
            {mode === "topup" && <p class="muted small">{t("mix.topupNote")}</p>}
            <Preview plan={plan} loading={loading} />
            <div class="row">
              <Button variant="primary" size="lg" disabled={!plan?.ok || !!job} onClick={() => start(false)} data-testid="mix-start">
                <Play size={18} /> {t("mix.start")}
              </Button>
              {!hasCirc && <span class="muted small">{t("mix.noCirc")}</span>}
            </div>
          </div>
        </Card>
        <Card title={t("mix.manualTitle")} icon={<Hand size={18} />}>
          <ManualDose />
        </Card>
      </div>
      {confirm && (
        <Modal
          title={t("mix.repeatTitle")}
          onClose={() => setConfirm(null)}
          footer={
            <>
              <button class="btn" onClick={() => setConfirm(null)}>
                {t("common.cancel")}
              </button>
              <Button variant="danger" onClick={() => start(true)}>
                {t("mix.repeatConfirm")}
              </Button>
            </>
          }
        >
          <p>{confirm}</p>
        </Modal>
      )}
    </div>
  );
}
