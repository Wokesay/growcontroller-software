// SPDX-License-Identifier: AGPL-3.0-or-later
// Mischen: Rezept, Wassermenge, Vorschau mit ml je Kanister, geführter Ablauf.
// Manuelle Gabe für einzelne Kanister.
import { useEffect, useRef, useState } from "preact/hooks";
import { Beaker, Droplet, FlaskConical, Hand, Play } from "lucide-preact";
import { ApiError, post, type Msg, type MixPlan } from "../api";
import { duration, num } from "../format";
import { canisters, config, paramDef, recipes, refreshState, state, toast } from "../store";
import { msg, t } from "../i18n";
import { Banner, Button, Card, Empty, Field, Modal, NumberInput, Seg, Toggle } from "../ui";
import { JobView } from "../widgets";

// Refusals shown at their field instead of as a banner (SD-098)
const FIELD_ERRORS = {
  targetEc: ["mix.target_range"],
  waterEc: ["mix.water_ec_missing", "mix.water_ec_unit"],
  tankNowL: ["mix.measured_tank_now", "mix.measured_tank_now_low"],
} as const;
// A refusal that is a good outcome, not a fault: shown as a note (SD-102)
const INFO_ERRORS = ["mix.measured_at_target", "mix.measured_at_target_new"];
const atField = (m: Msg) => Object.values(FIELD_ERRORS).some((keys) => (keys as readonly string[]).includes(m.key ?? ""));
const fieldError = (plan: MixPlan | null, field: keyof typeof FIELD_ERRORS) => {
  const e = plan?.errors.find((m) => (FIELD_ERRORS[field] as readonly string[]).includes(m.key ?? ""));
  return e ? msg(e) : null;
};

function Preview(p: { plan: MixPlan | null; loading: boolean; liveEc: number | null }) {
  const plan = p.plan;
  if (!plan) return <p class="muted">{t("mix.previewEmpty")}</p>;
  return (
    <div class="stack-sm" data-testid="mix-preview">
      {plan.steps.length > 0 && (
      <table class="table">
        <thead>
          <tr>
            <th>#</th>
            <th>{t("term.bottle")}</th>
            <th class="num">{plan.amounts === "measured" ? t("mix.mlPerLRecipe") : "ml/L"}</th>
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
                  {s.fixed && <span class="faint small">{t("mix.fixed")}</span>}
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
      )}
      {plan.steps.length > 0 && (
        <div class="row-between small">
          <span class="muted">{t("mix.total", { ml: num(plan.totalMl, 1), time: duration(plan.totalMs) })}</span>
          <span class="muted">{msg(plan.after)}</span>
        </div>
      )}
      {/* live, outside the announced region: it changes every few seconds (SD-102 D4) */}
      {plan.amounts === "measured" && p.liveEc != null && (
        <p class="small" data-testid="mix-measured">
          {t("mix.measuredNow", { ec: num(p.liveEc, 2) })}
          {plan.tankL != null && <> {t(plan.litresFrom === "level" ? "mix.measuredNowLevel" : "mix.measuredNowEntered", { l: num(plan.tankL, 0) })}</>}
        </p>
      )}
      <div class="stack-sm" aria-live="polite">
        {plan.ok && plan.amounts === "target" && plan.expectedEc != null && (
          <p class="small" data-testid="mix-expected">
            {t(plan.mode === "topup" ? "mix.expectedFresh" : "mix.expected", { ec: num(plan.expectedEc, 2) })}
          </p>
        )}
        {plan.ok && plan.amounts === "measured" && plan.expectedEc != null && (
          <p class="small" data-testid="mix-expected">
            {t("mix.expectedMeasured", { ec: num(plan.expectedEc, 2) })}
          </p>
        )}
        {plan.errors
          .filter((e) => !atField(e))
          .map((e) => (
            <Banner tone={INFO_ERRORS.includes(e.key ?? "") ? "info" : "bad"}>{msg(e)}</Banner>
          ))}
        {plan.warnings.map((w) => (
          <Banner tone="warn">{msg(w)}</Banner>
        ))}
        {(plan.infos ?? []).map((m) => (
          <Banner tone="info">{msg(m)}</Banner>
        ))}
      </div>
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
  // With EC control on and free, and an EC probe and a circulation pump assigned: to its target with the
  // tank's probe, preselected while it can run, or the recipe's amounts (SD-057 way 1, SD-102 D1). Else the
  // recipe's amounts or a fixed pattern to a target EC, without measuring (way 2, SD-098): EC control would
  // take the tank to its own target afterwards (SD-089 S20). Until a tab is tapped the choice follows what
  // the hub can do; once anything in the form is changed or a tab tapped, it stays (nothing switches under
  // the user's finger).
  const ecFree = !!st.functions.find((f) => f.id === "ec_control")?.enabled && !st.latches["ec.no_effect"];
  const way1 = ecFree && !!st.mixWay1?.hardware;
  const way1Blocked = way1 ? (st.mixWay1.blocked ?? null) : null;
  const [chosen, setChosen] = useState<"recipe" | "target" | "measured" | null>(null);
  const amounts = way1
    ? chosen === "recipe" || chosen === "measured" ? chosen : way1Blocked ? "recipe" : "measured"
    : !ecFree && chosen === "target" ? "target" : "recipe";
  // To the target with the probe mixes only EC control's own recipe, as the hub picks it (SD-102 D2): it
  // follows that recipe until the user picks one, never overwriting a recipe picked by hand
  const ecRecipe = st.mixWay1?.recipe ?? undefined;
  const ecRecipeName = rs.find((r) => r.id === ecRecipe)?.name ?? "";
  const [recipePicked, setRecipePicked] = useState(false);
  const choose = (a: "recipe" | "target" | "measured") => {
    setChosen(a);
    if (a === "measured" && typeof ecRecipe === "string") {
      setRecipe(ecRecipe);
      setRecipePicked(false);
    }
  };
  const freeze = () => {
    if (chosen === null) setChosen(amounts);
  };
  useEffect(() => {
    if (!recipePicked && amounts === "measured" && typeof ecRecipe === "string") setRecipe(ecRecipe);
  }, [amounts, ecRecipe, recipePicked]);
  // A proposal for the target until one is typed: the phase's (for its own recipe), the recipe's chart EC,
  // else EC control's own setting
  const g = st.grow;
  const phase = g.state === "running" ? g.phases[g.phase] : undefined;
  const phaseEc = phase && (phase.params.recipe === undefined || phase.params.recipe === recipe) ? phase.params.ec_target : undefined;
  const fnEc = config.value?.functions["ec_control"]?.params?.ec_target;
  const refEc = rs.find((r) => r.id === recipe)?.refEc;
  const proposal: [number, "mix.targetFromGrow" | "mix.targetFromSettings" | "mix.targetFromRecipe"] | null =
    typeof phaseEc === "number" ? [phaseEc, "mix.targetFromGrow"] : typeof refEc === "number" ? [refEc, "mix.targetFromRecipe"] : typeof fnEc === "number" ? [fnEc, "mix.targetFromSettings"] : null;
  const [typedTarget, setTypedTarget] = useState<number | null | undefined>(undefined);
  const targetEc = typedTarget !== undefined ? typedTarget : (proposal?.[0] ?? null);
  const range = paramDef("ec_control", "ec_target");
  const rangeText = range?.min != null && range?.max != null ? t("mix.targetRange", { min: num(range.min, 1), max: num(range.max, 1) }) : "";
  const targetHint = typedTarget === undefined && proposal ? `${t(proposal[1])} ${rangeText}` : rangeText;
  const [waterEc, setWaterEc] = useState<number | null>(null);
  const waterEcInput = useRef<HTMLDivElement>(null);
  // Only on a new fill: the tank then holds the water being mixed (SD-089 S20), from a valid reading only (R5)
  const probe = st.readings["tank.ec"];
  const probeEc = mode === "new" && probe?.usable && typeof probe.value === "number" ? Math.round(probe.value * 100) / 100 : null;
  // A top-up without a usable level: way 1 asks for the litres in the tank now (SD-102 D4)
  const [tankNow, setTankNow] = useState<number | null>(null);
  const needTankNow = amounts === "measured" && mode === "topup" && st.tank.source !== "level";
  const pattern =
    amounts === "target" ? { amounts, targetEc, waterEc } : needTankNow ? { amounts, tankNowL: tankNow } : { amounts };
  const liveEc = st.readings["tank.ec"]?.usable && typeof st.readings["tank.ec"].value === "number" ? st.readings["tank.ec"].value : null;
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
    const timer = setTimeout(async () => {
      try {
        setPlan(await post<MixPlan>("/mix/plan", { recipe, waterL: water, mode, confirmRepeat: true, ...pattern }));
      } finally {
        setLoading(false);
      }
    }, 250);
    return () => clearTimeout(timer);
  }, [recipe, water, mode, amounts, targetEc, waterEc, tankNow, rs]);  // a recipe saved again (config reloaded) plans anew

  async function start(confirmRepeat = false) {
    try {
      await post("/mix/start", { recipe, waterL: water, mode, guided, confirmRepeat, ...pattern });
      setConfirm(null);
      toast(t("mix.started"));
      await refreshState();
    } catch (e) {
      if (e instanceof ApiError && ["mix.recent", "mix.recent_measured", "mix.recent_measured_plain"].includes(e.key ?? "")) setConfirm(e.message);
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
                <select
                  class="select"
                  value={recipe}
                  onChange={(e) => {
                    freeze();
                    setRecipePicked(true);
                    setRecipe((e.target as HTMLSelectElement).value);
                  }}
                  name="recipe"
                >
                  {rs.map((r) => (
                    <option value={r.id}>{r.name}</option>
                  ))}
                </select>
              </Field>
              <Field
                label={mode === "new" ? t("mix.waterInTank") : amounts === "measured" ? t("mix.freshWaterIn") : t("mix.freshWater")}
                hint={st.tank.capacityL ? t("mix.capacityHint", { l: num(st.tank.capacityL, 0) }) : undefined}
              >
                <NumberInput
                  value={water}
                  onValue={(v) => {
                    freeze();
                    setWater(v);
                  }}
                  unit="L"
                  name="water"
                />
              </Field>
              {needTankNow && (
                <Field label={t("mix.tankNow")} error={fieldError(plan, "tankNowL")}>
                  <NumberInput value={tankNow} onValue={setTankNow} unit="L" name="tankNow" />
                </Field>
              )}
            </div>
            <div class="row-between">
              <Seg
                value={mode}
                onChange={(m) => {
                  freeze();
                  setMode(m);
                }}
                label={t("mix.modeLabel")}
                options={[["new", t("mix.modeNew")], ["topup", t("mix.modeTopup")]]}
              />
              <label class="row small">
                <Toggle checked={guided} onChange={setGuided} label={t("mix.guided")} /> {t("mix.guided")} {hasCirc ? "" : t("mix.stirHint")}
              </label>
            </div>
            {mode === "topup" && <p class="muted small">{t(amounts === "measured" ? "mix.topupNoteMeasured" : "mix.topupNote")}</p>}
            {mode === "new" && amounts === "measured" && <p class="muted small">{t("mix.newNoteMeasured")}</p>}
            {(way1 || !ecFree) && (
              <div class="field">
                <span id="mix-amounts">{t("mix.amountsLabel")}</span>
                {/* "Laut Rezept" always first, so it keeps its place whatever is offered next to it */}
                {way1 ? (
                  <Seg value={amounts} onChange={choose} labelledBy="mix-amounts" large options={[["recipe", t("mix.amountsRecipe")], ["measured", t("mix.amountsMeasured")]]} />
                ) : (
                  <Seg value={amounts} onChange={choose} labelledBy="mix-amounts" large options={[["recipe", t("mix.amountsRecipe")], ["target", t("mix.amountsTarget")]]} />
                )}
              </div>
            )}
            {!ecFree && st.mixWay1?.hardware && <p class="muted small">{t(st.latches["ec.no_effect"] ? "mix.way1Locked" : "mix.way1Needs")}</p>}
            {ecFree && (
              <div class="stack-sm muted small" data-testid="mix-ec-control">
                {amounts === "measured" ? (
                  <p>{t("mix.byEcControlMeasured", {
                    recipe: ecRecipeName,
                    // the plan's, as the hub plans with it; EC top-up's own until a plan is there
                    target: num((plan?.amounts === "measured" ? plan.ecTarget : null) ?? (st.controllers.ec.info as { target?: number }).target ?? null, 2),
                  })}</p>
                ) : (
                  <>
                    {/* one reason line: the way's own, or EC top-up's when the way has none */}
                    {way1Blocked ? (
                      <p>{msg(way1Blocked)}</p>
                    ) : (
                      <p>
                        {st.controllers.ec.state === "blocked"
                          ? t("mix.byEcControlBlocked", { line: msg(st.controllers.ec.line) })
                          : t("mix.byEcControl", { target: num((st.controllers.ec.info as { target?: number }).target ?? null, 2) })}
                      </p>
                    )}
                    {/* switching EC top-up off helps only where the probe or the pump is the reason */}
                    {(!way1 || ["mix.measured_no_ec", "mix.measured_no_circ", "mix.measured_circ_blocked"].includes(way1Blocked?.key ?? "")) && <p>{t("mix.byEcControlOff")}</p>}
                  </>
                )}
              </div>
            )}
            {amounts === "target" && (
              <div class="form-grid">
                <Field label={t("mix.targetEc")} hint={targetHint} error={fieldError(plan, "targetEc")}>
                  <NumberInput value={targetEc} onValue={setTypedTarget} unit="mS/cm" step={0.05} name="targetEc" />
                </Field>
                <div class="stack-sm" ref={waterEcInput}>
                  <Field label={mode === "new" ? t("mix.waterEc") : t("mix.waterEcFresh")} hint={t("mix.waterEcHint")} error={fieldError(plan, "waterEc")}>
                    <NumberInput value={waterEc} onValue={setWaterEc} unit="mS/cm" step={0.01} name="waterEc" />
                  </Field>
                  {probeEc != null && waterEc == null && (
                    <button
                      type="button"
                      class="btn sm wrap"
                      onClick={() => {
                        setWaterEc(probeEc);
                        waterEcInput.current?.querySelector<HTMLInputElement>("input[name=waterEc]")?.focus();
                      }}
                    >
                      {t("mix.useProbe", { ec: num(probeEc, 2) })}
                    </button>
                  )}
                </div>
              </div>
            )}
            <Preview plan={plan} loading={loading} liveEc={liveEc} />
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
          message={confirm}
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
        />
      )}
    </div>
  );
}
