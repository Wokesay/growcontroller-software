// Setup-Assistent: vom ersten Einschalten bis zur ersten Mischung.
// Geräte werden erkannt statt ausgewählt (Kundensicht); Einmessen ist Pflicht,
// bevor dosiert wird (Produktregel, strenger als RAT-015).
import { useState } from "preact/hooks";
import { ArrowLeft, ArrowRight, Check, PackagePlus, Sprout } from "lucide-preact";
import { del, post, put } from "../api";
import { PumpCalibration, ProbeCalibration } from "../calibration";
import { num } from "../format";
import { canisters, catalog, config, recipes, refreshConfig, refreshState, state, toast } from "../store";
import { Banner, Button, Card, CheckRow, Field, NumberInput, Pill, navigate, route, setupLabel } from "../ui";

const STEPS = ["Start", "Geräte", "Tank", "Kanister", "Einmessen", "Sonden", "Rezept", "Fertig"];
const NAMES = ["Teil A", "Teil B", "CalMag", "pH−", "Zusatz 1", "Zusatz 2"];
const COLORS = ["#3f8f4a", "#c47a2c", "#5b7fb8", "#b8455b", "#8a5cc2", "#2f9aa0"];

function StepStart(p: { next: () => void }) {
  const cfg = config.value!;
  const [name, setName] = useState(cfg.system.name);
  const tz = Intl.DateTimeFormat().resolvedOptions().timeZone || "Europe/Berlin";
  return (
    <div class="stack">
      <h2>Willkommen</h2>
      <p class="muted">In wenigen Schritten: Geräte erkennen, Tank angeben, Kanister zuordnen, Pumpen einmessen, Rezept wählen. Danach kannst du die erste Nährlösung mischen.</p>
      <Banner>Das Passwort ist gesetzt. Auf dem Hub wählst du hier auch dein WLAN; der Hub zeigt danach seine neue Adresse. Im Simulator entfällt das.</Banner>
      <div class="form-grid">
        <Field label="Name des Hubs" hint="z. B. „Zelt 1“">
          <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
        </Field>
        <Field label="Zeitzone" hint="vom Browser übernommen">
          <input class="input" value={tz} disabled />
        </Field>
      </div>
      <div>
        <Button
          variant="primary"
          onClick={async () => {
            await put("/system", { name, timezone: tz });
            await refreshConfig();
            p.next();
          }}
        >
          Weiter <ArrowRight size={16} />
        </Button>
      </div>
    </div>
  );
}

function StepDevices() {
  const st = state.value!;
  const fresh = st.devices.filter((d) => !d.configured && d.online);
  const hasBlock = st.devices.some((d) => d.class === "dosing_block" && d.configured);
  const caps = st.devices.filter((d) => d.class === "pump_cap");
  return (
    <div class="stack">
      <h2>Geräte erkennen</h2>
      <p class="muted">Dosierblock an einen Hub-Port, Pumpenkappen in den Dosierblock. Der Hub prüft jeden Port, bevor er ihn einschaltet.</p>
      <div class="ports">
        {st.ports.map((p) => {
          const d = st.devices.find((x) => x.id === p.device);
          return (
            <div class={`port ${p.state}`}>
              <span class="pn">PORT {p.port}</span>
              <span class="jack" />
              <span class="pl">{p.state === "empty" ? <span class="faint">frei</span> : d?.classLabel || p.class}</span>
              {p.state === "rejected" && <span class="pm">{p.message.text}</span>}
            </div>
          );
        })}
      </div>
      <div class="list">
        {st.devices.filter((d) => d.class !== "hub_outputs").map((d) => (
          <div class="item">
            <span class="grow">
              <strong>{d.classLabel}</strong> <span class="faint small">{d.slot >= 0 ? `Dosierblock Port ${d.slot + 1}` : `Port ${d.port}`}</span>
            </span>
            {d.configured ? <Pill tone="ok">übernommen</Pill> : <Pill tone="info">erkannt</Pill>}
          </div>
        ))}
      </div>
      {fresh.length > 0 && (
        <div>
          <Button
            variant="primary"
            onClick={async () => {
              for (const d of fresh) await post(`/devices/${d.id}/accept`, { name: "" });
              await refreshConfig();
              await refreshState();
              toast(`${fresh.length} Geräte übernommen`);
            }}
          >
            <PackagePlus size={16} /> {fresh.length === 1 ? "Gerät übernehmen" : `Alle ${fresh.length} übernehmen`}
          </Button>
        </div>
      )}
      {!hasBlock && <Banner tone="warn">Weiter geht es, sobald ein Dosierblock mit mindestens einer Pumpenkappe erkannt und übernommen ist.</Banner>}
      {hasBlock && caps.length === 0 && <Banner tone="warn">Noch keine Pumpenkappe im Dosierblock.</Banner>}
    </div>
  );
}

function StepTank(p: { next: () => void }) {
  const cfg = config.value!;
  const t = cfg.tanks[0];
  const hubOut = config.value!.devices.find((d) => d.class === "hub_outputs");
  const hasLevel = config.value!.devices.some((d) => d.class === "head_level");
  const [name, setName] = useState(t?.name ?? "Tank 1");
  const [cap, setCap] = useState<number | null>(t?.capacityL ?? null);
  const [water, setWater] = useState(t?.water ?? "ro");
  const [circ, setCirc] = useState(!!t?.roles?.["tank.circulation"]);
  const [inlet, setInlet] = useState(!!t?.roles?.["tank.inlet"]);
  const [min, setMin] = useState<number | null>(t?.minL ?? 3);
  return (
    <div class="stack">
      <h2>Tank</h2>
      <div class="form-grid">
        <Field label="Name">
          <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
        </Field>
        <Field label="Nutzvolumen" hint="Mehr wird nie angenommen – schützt vor Tippfehlern">
          <NumberInput value={cap} onValue={setCap} unit="L" name="capacity" />
        </Field>
        <Field label="Wasser">
          <select class="select" value={water} onChange={(e) => setWater((e.target as HTMLSelectElement).value)}>
            <option value="ro">Osmosewasser</option>
            <option value="tap">Leitungswasser</option>
          </select>
        </Field>
        {hasLevel && (
          <Field label="Trockenlaufgrenze">
            <NumberInput value={min} onValue={setMin} unit="L" />
          </Field>
        )}
      </div>
      {hubOut && (
        <div class="stack-sm">
          <label class="row">
            <input type="checkbox" checked={circ} onChange={(e) => setCirc((e.target as HTMLInputElement).checked)} /> Umwälzpumpe am Hub-Ausgang 1
          </label>
          {hasLevel && (
            <label class="row">
              <input type="checkbox" checked={inlet} onChange={(e) => setInlet((e.target as HTMLInputElement).checked)} /> Zulaufventil (stromlos zu) am Hub-Ausgang 2
            </label>
          )}
          {!circ && <p class="muted small">Ohne Umwälzpumpe führt der Mischlauf mit „jetzt umrühren“ zwischen den Gaben.</p>}
        </div>
      )}
      <div>
        <Button
          variant="primary"
          disabled={!cap}
          onClick={async () => {
            await put("/tank", { name, capacityL: cap, water, minL: hasLevel ? min : null });
            if (hubOut) {
              if (circ) await put("/roles/tank.circulation", { device: hubOut.id, channel: 0 });
              else if (t?.roles?.["tank.circulation"]) await del("/roles/tank.circulation");
              if (inlet) await put("/roles/tank.inlet", { device: hubOut.id, channel: 1 });
            }
            await refreshConfig();
            await refreshState();
            p.next();
          }}
        >
          Speichern und weiter <ArrowRight size={16} />
        </Button>
      </div>
    </div>
  );
}

function StepCanisters() {
  const st = state.value!;
  const caps = st.devices.filter((d) => d.class === "pump_cap" && d.configured).sort((a, b) => a.slot - b.slot);
  const existing = canisters.value;
  const [rows, setRows] = useState(
    caps.map((c, i) => {
      const k = existing.find((x) => x.pump === c.id);
      return { pump: c.id, slot: c.slot, id: k?.id, name: k?.name ?? NAMES[i] ?? `Kanister ${i + 1}`, kind: k?.kind ?? (NAMES[i] === "pH−" ? "ph_down" : "nutrient"), pair: k?.pair ?? (i < 2 ? "AB" : ""), color: k?.color ?? COLORS[i % COLORS.length], capacityMl: k?.capacityMl ?? 1000 };
    }),
  );
  const upd = (i: number, k: string, v: unknown) => setRows(rows.map((r, j) => (j === i ? { ...r, [k]: v } : r)));
  return (
    <div class="stack">
      <h2>Kanister zuordnen</h2>
      <p class="muted">Welcher Nährstoff steht unter welcher Kappe? Ein Paar (z. B. A und B) wird immer gemeinsam skaliert.</p>
      <table class="table">
        <thead>
          <tr>
            <th>Kappe</th>
            <th>Name</th>
            <th>Typ</th>
            <th>Paar</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((r, i) => (
            <tr>
              <td class="nowrap">
                <span class="swatch" style={`background:${r.color}`} /> Port {r.slot + 1}
              </td>
              <td>
                <input class="input" value={r.name} onInput={(e) => upd(i, "name", (e.target as HTMLInputElement).value)} />
              </td>
              <td>
                <select class="select" value={r.kind} onChange={(e) => upd(i, "kind", (e.target as HTMLSelectElement).value)}>
                  <option value="nutrient">Nährstoff</option>
                  <option value="ph_down">pH−</option>
                  <option value="ph_up">pH+</option>
                </select>
              </td>
              <td style="width:90px">
                <input class="input" value={r.kind === "nutrient" ? r.pair : ""} disabled={r.kind !== "nutrient"} onInput={(e) => upd(i, "pair", (e.target as HTMLInputElement).value.toUpperCase())} />
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      <p class="faint small">Tipp: Farbige Clips an Kappe und Kanister verhindern Verwechslungen – elektrisch kann der Hub nicht sehen, auf welchem Kanister eine Kappe sitzt.</p>
      <div>
        <Button
          variant="primary"
          onClick={async () => {
            for (const r of rows) await post("/canisters", { id: r.id, name: r.name, kind: r.kind, pump: r.pump, pair: r.kind === "nutrient" ? r.pair : "", color: r.color, capacityMl: r.capacityMl });
            await refreshConfig();
            toast("Kanister gespeichert");
          }}
        >
          Speichern
        </Button>
      </div>
    </div>
  );
}

function StepCalibrate() {
  const st = state.value!;
  const [cal, setCal] = useState<{ id: string; name: string } | null>(null);
  const assigned = canisters.value.filter((k) => k.pump);
  return (
    <div class="stack">
      <h2>Pumpen einmessen</h2>
      <p class="muted">Ohne Einmesswert dosiert der Hub nicht. Du brauchst einen Messbecher oder eine Küchenwaage.</p>
      <div class="list">
        {assigned.map((k) => {
          const d = st.devices.find((x) => x.id === k.pump);
          const flow = d?.info?.flowMlPerMin ?? null;
          return (
            <div class="item">
              <span class="swatch" style={`background:${k.color}`} />
              <span class="grow">
                <strong>{k.name}</strong>
                <div class="muted small">{flow ? `${num(flow, 1)} ml/min` : "noch nicht eingemessen"}</div>
              </span>
              <Button size="sm" onClick={() => post(`/pumps/${k.pump}/prime`, { seconds: 5 }).then(refreshState)}>
                Schlauch füllen
              </Button>
              <Button size="sm" variant={flow ? "default" : "primary"} onClick={() => setCal({ id: k.pump, name: k.name })}>
                {flow ? "Erneut" : "Einmessen"}
              </Button>
              {flow && <Check size={18} color="var(--ok)" />}
            </div>
          );
        })}
      </div>
      {cal && <PumpCalibration pump={cal.id} name={cal.name} onClose={() => setCal(null)} />}
    </div>
  );
}

function StepProbes() {
  const devs = config.value!.devices;
  const head = devs.find((d) => d.class === "head_ph_ec");
  const level = devs.find((d) => d.class === "head_level");
  const [cal, setCal] = useState<{ dev: string; kind: "ph" | "ec" | "tank_curve"; name: string } | null>(null);
  const cals = config.value!.calibrations;
  if (!head && !level)
    return (
      <div class="stack">
        <h2>Sonden</h2>
        <Banner>Kein pH/EC-Kopf und kein Füllstands-Kopf erkannt – in Stufe 0 misst du pH von Hand. Dieser Schritt entfällt.</Banner>
      </div>
    );
  const row = (dev: string, kind: "ph" | "ec" | "tank_curve", label: string) => (
    <div class="item">
      <span class="grow">
        <strong>{label}</strong>
        <div class="muted small">{cals[dev]?.[kind] ? "kalibriert" : "nicht kalibriert"}</div>
      </span>
      <Button size="sm" variant={cals[dev]?.[kind] ? "default" : "primary"} onClick={() => setCal({ dev, kind, name: label })}>
        {cals[dev]?.[kind] ? "Erneut" : "Kalibrieren"}
      </Button>
    </div>
  );
  return (
    <div class="stack">
      <h2>Sonden kalibrieren</h2>
      <p class="muted">Ohne Kalibrierung zeigt der Hub die Werte nur an; regeln kann er damit nicht. Das geht auch später unter Geräte.</p>
      <div class="list">
        {head && row(head.id, "ph", "pH-Sonde (Puffer 7 und 4)")}
        {head && row(head.id, "ec", "EC-Sonde (1,413 mS/cm)")}
        {level && row(level.id, "tank_curve", "Füllstand (Kennlinie)")}
      </div>
      {cal && <ProbeCalibration device={cal.dev} kind={cal.kind} name={cal.name} onClose={() => setCal(null)} />}
    </div>
  );
}

function StepRecipe() {
  const tpls = catalog.value?.templates?.recipes ?? [];
  const nutrients = canisters.value.filter((k) => k.kind === "nutrient");
  const [vals, setVals] = useState<Record<string, number | null>>({});
  const [name, setName] = useState("Wachstum");
  return (
    <div class="stack">
      <h2>Rezept</h2>
      {recipes.value.length > 0 && (
        <Banner tone="ok">
          Vorhanden: {recipes.value.map((r) => r.name).join(", ")}
        </Banner>
      )}
      <div class="stack-sm">
        <div class="section-title">Eigenes Rezept (ml je Liter Wasser)</div>
        <Field label="Name">
          <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
        </Field>
        <div class="form-grid">
          {nutrients.map((k) => (
            <Field label={k.name}>
              <NumberInput value={vals[k.id] ?? null} onValue={(v) => setVals({ ...vals, [k.id]: v })} unit="ml/L" />
            </Field>
          ))}
        </div>
        <div>
          <Button
            variant="primary"
            disabled={!nutrients.some((k) => vals[k.id])}
            onClick={async () => {
              await post("/recipes", { name, steps: nutrients.filter((k) => vals[k.id]).map((k) => ({ canister: k.id, mlPerL: vals[k.id] })) });
              await refreshConfig();
              toast("Rezept gespeichert");
            }}
          >
            Rezept speichern
          </Button>
        </div>
      </div>
      {tpls.length > 0 && (
        <div class="stack-sm">
          <div class="section-title">Oder Vorlage</div>
          <div class="row">
            {tpls.map((t) => (
              <Button
                size="sm"
                onClick={async () => {
                  await post("/recipes/template", { id: t.id });
                  await refreshConfig();
                  toast("Vorlage übernommen");
                }}
              >
                {t.name}
              </Button>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}

function StepDone() {
  const st = state.value!;
  const usable = st.functions.filter((f) => f.setup === "ready" || f.setup === "limited");
  const todo = st.functions.filter((f) => f.setup === "needs_setup");
  return (
    <div class="stack">
      <h2>Fertig eingerichtet</h2>
      <div class="list">
        {[...usable, ...todo].map((f) => (
          <div class="item">
            <span class="grow">
              <strong>{f.label}</strong>
              {f.setup === "needs_setup" && <div class="muted small">{f.summary.text}</div>}
            </span>
            <Pill tone={setupLabel[f.setup][1]}>{setupLabel[f.setup][0]}</Pill>
          </div>
        ))}
      </div>
      <Banner>Vorschlag für den ersten Lauf: 10 Liter in einem Eimer mischen und danach von Hand nachmessen.</Banner>
      <div class="row">
        <Button
          variant="primary"
          size="lg"
          onClick={async () => {
            await post("/setup/complete");
            await refreshConfig();
            await refreshState();
            navigate("/mischen");
          }}
        >
          <Sprout size={18} /> Zur ersten Mischung
        </Button>
        <Button
          onClick={async () => {
            await post("/setup/complete");
            await refreshState();
            navigate("/");
          }}
        >
          Zur Übersicht
        </Button>
      </div>
    </div>
  );
}

export function SetupWizard() {
  const st = state.value!;
  const step = Math.max(0, Math.min(STEPS.length - 1, Number(route.value.query.s ?? 0)));
  const go = (s: number) => navigate(`/einrichtung?s=${s}`);
  const hasBlock = st.devices.some((d) => d.class === "dosing_block" && d.configured) && st.devices.some((d) => d.class === "pump_cap" && d.configured);
  const assigned = canisters.value.filter((k) => k.pump);
  const allCal = assigned.length > 0 && assigned.every((k) => st.devices.find((d) => d.id === k.pump)?.info?.flowMlPerMin);
  const canNext = [true, hasBlock, !!config.value?.tanks[0]?.capacityL, assigned.length > 0, true, true, recipes.value.length > 0, true][step];
  const mixFn = st.functions.find((f) => f.id === "mix");
  return (
    <div class="login" style="align-items:start;padding-top:40px">
      <div style="width:min(860px,100%)" class="stack">
        <div class="row-between">
          <div class="row">
            <div class="brand-mark">
              <Sprout size={18} />
            </div>
            <strong>Einrichtung</strong>
          </div>
          {st.setupDone && (
            <a class="btn sm ghost" href="#/">
              Zur App
            </a>
          )}
        </div>
        <div class="wizard-steps">
          {STEPS.map((s, i) => (
            <span class={`ws ${i === step ? "cur" : i < step ? "done" : ""}`}>
              {i < step ? "✓" : i + 1} {s}
            </span>
          ))}
        </div>
        <Card>
          {step === 0 && <StepStart next={() => go(1)} />}
          {step === 1 && <StepDevices />}
          {step === 2 && <StepTank next={() => go(3)} />}
          {step === 3 && <StepCanisters />}
          {step === 4 && <StepCalibrate />}
          {step === 5 && <StepProbes />}
          {step === 6 && <StepRecipe />}
          {step === 7 && <StepDone />}
        </Card>
        {step === 4 && !allCal && assigned.length > 0 && <Banner tone="warn">Nicht eingemessene Pumpen dosieren nicht. Du kannst das später unter Geräte nachholen.</Banner>}
        {mixFn && step >= 3 && step < 7 && (
          <div class="card flat stack-sm">
            <div class="section-title">Für „Nährlösung mischen“ fehlt noch</div>
            {mixFn.checks.filter((c) => !c.ok && c.level !== "runtime").map((c) => <CheckRow ok={false} soft={c.soft} text={c.text} />)}
            {mixFn.checks.every((c) => c.ok || c.level === "runtime" || c.soft) && <CheckRow ok text="nichts – Mischen ist bereit" />}
          </div>
        )}
        {step !== 0 && step !== 7 && (
          <div class="row-between">
            <Button variant="ghost" onClick={() => go(step - 1)}>
              <ArrowLeft size={16} /> Zurück
            </Button>
            <Button variant="primary" disabled={!canNext} onClick={() => go(step + 1)} data-testid="wizard-next">
              Weiter <ArrowRight size={16} />
            </Button>
          </div>
        )}
      </div>
    </div>
  );
}
