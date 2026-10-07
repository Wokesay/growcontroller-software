// Geführte Kalibrierung: Pumpe einmessen (Messbecher), pH-Sonde (2 Puffer),
// EC-Sonde (1 Referenz), Füllstand (Stützpunkte, stückweise linear).
import { useEffect, useState } from "preact/hooks";
import { Beaker, Check, Timer } from "lucide-preact";
import { get, post, sim } from "./api";
import { num } from "./format";
import { refreshConfig, refreshState, simulated, state, toast } from "./store";
import { Banner, Button, Field, Modal, NumberInput } from "./ui";

export function PumpCalibration(p: { pump: string; name: string; onClose: () => void }) {
  const st = state.value!;
  const [seconds, setSeconds] = useState<number | null>(30);
  const [jobId, setJobId] = useState<string | null>(null);
  const [ml, setMl] = useState<number | null>(null);
  const [result, setResult] = useState<number | null>(null);
  const [simCup, setSimCup] = useState<number | null>(null);
  const job = st.job && st.job.id === jobId ? st.job : null;
  const waiting = job?.state === "waiting_user";

  useEffect(() => {
    if (!waiting || !simulated.value) return;
    get("/sim").then((s: any) => {
      for (const port of s.world.ports) for (const slot of port.slots ?? []) if (slot?.id === p.pump && slot.cupMl) setSimCup(slot.cupMl);
    });
  }, [waiting]);

  return (
    <Modal title={`Pumpe einmessen – ${p.name}`} onClose={p.onClose}>
      {result !== null ? (
        <Banner tone="ok" icon={<Check size={18} />}>
          Gespeichert in der Pumpe: <strong>{num(result, 1)} ml/min</strong>. Der Wert bleibt beim Umstecken erhalten.
        </Banner>
      ) : !jobId ? (
        <div class="stack">
          <ol class="stack-sm" style="margin:0;padding-left:20px">
            <li>Schlauchende in einen Messbecher halten (ab ca. 20 ml gut ablesbar).</li>
            <li>Mit dem echten Nährstoff messen, nicht mit Wasser – Konzentrat fließt anders. Danach zurück in den Kanister gießen.</li>
            <li>Der Schlauch muss gefüllt und blasenfrei sein.</li>
          </ol>
          <Field label="Laufzeit" hint="10–60 s. Der Hub rechnet mit der Ist-Laufzeit, die der Dosierblock meldet.">
            <NumberInput value={seconds} onValue={setSeconds} unit="s" />
          </Field>
          <div class="row">
            <Button
              variant="primary"
              onClick={async () => {
                const r = await post(`/pumps/${p.pump}/calibrate`, { seconds });
                setJobId(r.job.id);
                await refreshState();
              }}
            >
              <Timer size={16} /> Pumpe starten
            </Button>
            <Button onClick={() => post(`/pumps/${p.pump}/prime`, { seconds: 5 }).then(refreshState)}>Schlauch füllen (5 s)</Button>
          </div>
        </div>
      ) : !waiting ? (
        <Banner icon={<Beaker size={18} />}>Pumpe läuft in den Messbecher … {job?.message.text}</Banner>
      ) : (
        <div class="stack">
          <p>Wie viel ist im Messbecher?</p>
          <Field label="Menge" hint="Bei einer Küchenwaage: Gramm ≈ ml nur für wasserähnliche Flüssigkeiten">
            <NumberInput value={ml} onValue={setMl} unit="ml" name="cup-ml" />
          </Field>
          {simCup !== null && (
            <p class="small">
              <span class="sim-tag">Simulator</span> Im Becher: <button class="btn ghost sm" onClick={() => setMl(simCup)}>{num(simCup, 2)} ml übernehmen</button>
            </p>
          )}
          <div class="row">
            <Button
              variant="primary"
              disabled={!ml}
              onClick={async () => {
                const r = await post(`/jobs/${jobId}/result`, { ml });
                setResult(r.flowMlPerMin);
                await refreshState();
                toast("Pumpe eingemessen");
              }}
            >
              Speichern
            </Button>
            <Button variant="ghost" onClick={() => post(`/jobs/${jobId}/abort`).then(p.onClose)}>
              Abbrechen
            </Button>
          </div>
        </div>
      )}
      {result !== null && (
        <div class="modal-foot">
          <button class="btn primary" onClick={p.onClose}>
            Fertig
          </button>
        </div>
      )}
    </Modal>
  );
}

const PROBE_STEPS: Record<string, { ref: number; text: string }[]> = {
  ph: [
    { ref: 7.0, text: "Sonde abspülen und in Pufferlösung pH 7,00 stellen." },
    { ref: 4.0, text: "Sonde abspülen und in Pufferlösung pH 4,00 stellen." },
  ],
  ec: [{ ref: 1.413, text: "Sonde abspülen und in Kalibrierlösung 1,413 mS/cm stellen." }],
};

export function ProbeCalibration(p: { device: string; kind: "ph" | "ec" | "tank_curve"; name: string; onClose: () => void }) {
  const st = state.value!;
  const [started, setStarted] = useState(false);
  const [idx, setIdx] = useState(0);
  const [done, setDone] = useState(false);
  const [liters, setLiters] = useState<number | null>(0);
  const [points, setPoints] = useState<[number, number][]>([]);
  const steps = PROBE_STEPS[p.kind] ?? [];
  const role = p.kind === "ph" ? "tank.ph" : p.kind === "ec" ? "tank.ec" : "tank.level";
  const reading = st.readings[role];

  useEffect(() => {
    post("/probe", { device: p.device, kind: p.kind, action: "start" }).then(() => setStarted(true));
    return () => {
      if (simulated.value && p.kind !== "tank_curve") sim("probe", { kind: p.kind, buffer: null }).catch(() => {});
    };
  }, []);
  useEffect(() => {
    if (!simulated.value || !started || p.kind === "tank_curve" || done) return;
    const s = steps[idx];
    if (s) sim("probe", { kind: p.kind, buffer: s.ref }).catch(() => {});
  }, [idx, started]);

  async function capture(ref: number) {
    const r = await post("/probe", { device: p.device, kind: p.kind, action: "point", reference: ref });
    setPoints(r.points);
    if (p.kind !== "tank_curve") setIdx(idx + 1);
  }
  async function commit() {
    await post("/probe", { device: p.device, kind: p.kind, action: "commit" });
    if (simulated.value && p.kind !== "tank_curve") await sim("probe", { kind: p.kind, buffer: null });
    await refreshConfig();
    await refreshState();
    setDone(true);
    toast("Kalibrierung gespeichert");
  }

  const title = p.kind === "ph" ? "pH-Sonde kalibrieren" : p.kind === "ec" ? "EC-Sonde kalibrieren" : "Füllstand: Kennlinie aufnehmen";
  return (
    <Modal title={`${title} – ${p.name}`} onClose={p.onClose}>
      {done ? (
        <Banner tone="ok">Gespeichert. Ab jetzt gelten die Werte als kalibriert.</Banner>
      ) : p.kind === "tank_curve" ? (
        <div class="stack">
          <p class="muted">Tank schrittweise füllen und bei jedem Stand die Literzahl eintragen. Mindestens 2 Punkte, besser 4–6 über den ganzen Tank (unten ist die Kennlinie oft nicht linear).</p>
          <div class="row">
            <div style="width:140px">
              <NumberInput value={liters} onValue={setLiters} unit="L" />
            </div>
            {simulated.value && (
              <Button size="sm" variant="ghost" onClick={() => sim("water", { volumeL: liters ?? 0 })}>
                <span class="sim-tag">Sim</span> Tank auf diesen Stand
              </Button>
            )}
            <Button size="sm" disabled={liters === null} onClick={() => capture(liters!)}>
              Punkt erfassen
            </Button>
          </div>
          <span class="muted small">Rohwert jetzt: {num(reading?.value ?? null, 3)}</span>
          {points.length > 0 && (
            <table class="table">
              <thead>
                <tr>
                  <th>Rohwert</th>
                  <th class="num">Liter</th>
                </tr>
              </thead>
              <tbody>
                {points.map(([raw, l]) => (
                  <tr>
                    <td class="mono">{num(raw, 4)}</td>
                    <td class="num">{num(l, 1)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
          <div>
            <Button variant="primary" disabled={points.length < 2} onClick={commit}>
              Kennlinie speichern
            </Button>
          </div>
        </div>
      ) : idx < steps.length ? (
        <div class="stack">
          <div class="wizard-steps">
            {steps.map((s, i) => (
              <span class={`ws ${i === idx ? "cur" : i < idx ? "done" : ""}`}>{i < idx ? "✓ " : ""}Puffer {num(s.ref, s.ref < 2 ? 3 : 1)}</span>
            ))}
          </div>
          <p>{steps[idx].text}</p>
          <p class="muted small">Warten, bis der Wert ruhig steht (ca. 1 min). Aktueller Rohwert: <strong>{num(reading?.value ?? null, 3)}</strong></p>
          <div>
            <Button variant="primary" disabled={!started} onClick={() => capture(steps[idx].ref)}>
              Wert übernehmen
            </Button>
          </div>
        </div>
      ) : (
        <div class="stack">
          <Banner tone="ok">Alle Punkte erfasst. Der Hub prüft die Steigung der Sonde – ist sie unplausibel, wird nicht gespeichert.</Banner>
          <div>
            <Button variant="primary" onClick={commit}>
              Kalibrierung speichern
            </Button>
          </div>
        </div>
      )}
      {done && (
        <div class="modal-foot">
          <button class="btn primary" onClick={p.onClose}>
            Fertig
          </button>
        </div>
      )}
    </Modal>
  );
}
