// Mischen: Rezept, Wassermenge, Vorschau mit ml je Kanister, geführter Ablauf.
// Handgabe für einzelne Kanister.
import { useEffect, useState } from "preact/hooks";
import { Beaker, Droplet, FlaskConical, Hand, Play } from "lucide-preact";
import { ApiError, post, type MixPlan } from "../api";
import { duration, num } from "../format";
import { canisters, config, recipes, refreshState, state, toast } from "../store";
import { Banner, Button, Card, Empty, Field, Modal, NumberInput, Seg, Toggle } from "../ui";
import { JobView } from "../widgets";

function Preview(p: { plan: MixPlan | null; loading: boolean }) {
  const plan = p.plan;
  if (!plan) return <p class="muted">Rezept und Wassermenge wählen – die Vorschau zeigt die Mengen.</p>;
  return (
    <div class="stack-sm">
      <table class="table">
        <thead>
          <tr>
            <th>#</th>
            <th>Kanister</th>
            <th class="num">ml/L</th>
            <th class="num">Menge</th>
            <th class="num hide-sm">Dauer</th>
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
                  {s.pair && <span class="faint small">Paar {s.pair}</span>}
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
        <span class="muted">Gesamt {num(plan.totalMl, 1)} ml · ca. {duration(plan.totalMs)} Pumpenlauf</span>
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
  if (!cans.length) return <p class="muted">Erst Kanister mit Pumpe anlegen.</p>;
  const sel = cans.find((k) => k.id === can);
  return (
    <div class="stack">
      <div class="form-grid">
        <Field label="Kanister">
          <select class="select" value={can} onChange={(e) => setCan((e.target as HTMLSelectElement).value)}>
            {cans.map((k) => (
              <option value={k.id}>{k.name}</option>
            ))}
          </select>
        </Field>
        <Field label="Menge" hint={`Höchstens ${num(limit, 1)} ml je Handgabe`}>
          <NumberInput value={ml} onValue={setMl} unit="ml" />
        </Field>
      </div>
      {sel?.pair && <Banner tone="warn">{sel.name} gehört zu einem Paar. Gib den Partner im gleichen Verhältnis dazu, sonst stimmt die Mischung nicht.</Banner>}
      <div>
        <Button
          disabled={!ml || ml <= 0 || !!state.value?.job}
          onClick={async () => {
            await post("/dose", { canister: can, ml });
            toast(`${sel?.name}: ${num(ml, 1)} ml werden dosiert`);
            await refreshState();
          }}
        >
          <Hand size={16} /> Dosieren
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
      toast("Mischlauf gestartet");
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
        <Empty icon={<FlaskConical size={36} />} title="Noch kein Rezept" text="Lege ein Rezept an oder übernimm eine Vorlage." action={<a class="btn primary" href="#/rezepte">Zu den Rezepten</a>} />
      </Card>
    );
  return (
    <div class="stack">
      {job ? (
        <Card title="Läuft gerade" icon={<Beaker size={18} />}>
          <JobView job={job} />
        </Card>
      ) : st.lastJob && st.lastJob.type === "mix" && st.now - st.lastJob.finishedAt < 3600 ? (
        <Card title="Letzter Mischlauf" icon={<Beaker size={18} />}>
          <JobView job={st.lastJob} />
        </Card>
      ) : null}
      <div class="grid-2">
        <Card title="Nährlösung mischen" icon={<Droplet size={18} />}>
          <div class="stack">
            <div class="form-grid">
              <Field label="Rezept">
                <select class="select" value={recipe} onChange={(e) => setRecipe((e.target as HTMLSelectElement).value)} name="recipe">
                  {rs.map((r) => (
                    <option value={r.id}>{r.name}</option>
                  ))}
                </select>
              </Field>
              <Field label={mode === "new" ? "Wasser im Tank" : "Frisches Wasser dazu"} hint={st.tank.capacityL ? `Nutzvolumen ${num(st.tank.capacityL, 0)} L` : undefined}>
                <NumberInput value={water} onValue={setWater} unit="L" name="water" />
              </Field>
            </div>
            <div class="row-between">
              <Seg value={mode} onChange={setMode} options={[["new", "Neu ansetzen"], ["topup", "Auffüllen"]]} />
              <label class="row small">
                <Toggle checked={guided} onChange={setGuided} label="Geführt" /> Geführt {hasCirc ? "" : "(umrühren zwischen den Gaben)"}
              </label>
            </div>
            {mode === "topup" && <p class="muted small">Die Mengen rechnen auf das frische Wasser, nicht auf den ganzen Tank.</p>}
            <Preview plan={plan} loading={loading} />
            <div class="row">
              <Button variant="primary" size="lg" disabled={!plan?.ok || !!job} onClick={() => start(false)} data-testid="mix-start">
                <Play size={18} /> Mischen starten
              </Button>
              {!hasCirc && <span class="muted small">Ohne Umwälzpumpe hält der Lauf nach jeder Gabe an, bis du umgerührt hast.</span>}
            </div>
          </div>
        </Card>
        <Card title="Von Hand dosieren" icon={<Hand size={18} />}>
          <ManualDose />
        </Card>
      </div>
      {confirm && (
        <Modal
          title="Wirklich noch einmal mischen?"
          onClose={() => setConfirm(null)}
          footer={
            <>
              <button class="btn" onClick={() => setConfirm(null)}>
                Abbrechen
              </button>
              <Button variant="danger" onClick={() => start(true)}>
                Trotzdem mischen
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
