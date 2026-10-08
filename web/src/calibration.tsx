// SPDX-License-Identifier: AGPL-3.0-or-later
// Geführte Kalibrierung: Pumpe einmessen (Messbecher), pH-Sonde (2 Puffer),
// EC-Sonde (1 Referenz), Füllstand (Stützpunkte, stückweise linear).
import { useEffect, useState } from "preact/hooks";
import { Beaker, Check, Timer } from "lucide-preact";
import { get, post, sim, type Msg } from "./api";
import { num } from "./format";
import { msg, t, type TextKey } from "./i18n";
import { refreshConfig, refreshState, simulated, state, toast } from "./store";
import { Banner, Button, Field, Modal, NumberInput } from "./ui";

export function PumpCalibration(p: { pump: string; name: string; onClose: () => void }) {
  const st = state.value!;
  const [seconds, setSeconds] = useState<number | null>(30);
  const [jobId, setJobId] = useState<string | null>(null);
  const [ml, setMl] = useState<number | null>(null);
  const [result, setResult] = useState<{ changed: boolean; message: Msg } | null>(null);
  const [simCup, setSimCup] = useState<number | null>(null);
  // The running job, or how it ended (e.g. stopped by STOP): the window follows the hub, not the request.
  const job = [st.job, st.lastJob].find((j) => j && j.id === jobId) ?? null;
  const waiting = job?.state === "waiting_user";

  useEffect(() => {
    if (!waiting || !simulated.value) return;
    get("/sim").then((s: any) => {
      for (const port of s.world.ports) for (const slot of port.slots ?? []) if (slot?.id === p.pump && slot.cupMl) setSimCup(slot.cupMl);
    });
  }, [waiting]);

  return (
    <Modal title={t("calibration.pumpTitle", { name: p.name })} onClose={p.onClose}>
      {result !== null ? (
        <Banner tone={result.changed ? "warn" : "ok"} icon={result.changed ? undefined : <Check size={18} />}>
          {msg(result.message)}
        </Banner>
      ) : !jobId ? (
        <div class="stack">
          <ol class="stack-sm" style="margin:0;padding-left:20px">
            <li>{t("calibration.stepCup")}</li>
            <li>{t("calibration.stepReal")}</li>
            <li>{t("calibration.stepPrimed")}</li>
          </ol>
          <Field label={t("calibration.runtime")} hint={t("calibration.runtimeHint")}>
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
              <Timer size={16} /> {t("calibration.startPump")}
            </Button>
            <Button onClick={() => post(`/pumps/${p.pump}/prime`, { seconds: 5 }).then(refreshState)}>{t("calibration.prime5")}</Button>
          </div>
        </div>
      ) : !waiting ? (
        <Banner tone={job?.state === "failed" ? "bad" : job?.state === "aborted" ? "warn" : "info"} icon={job?.state === "failed" || job?.state === "aborted" ? undefined : <Beaker size={18} />}>
          {msg(job?.message) || t("calibration.running")}
        </Banner>
      ) : (
        <div class="stack">
          <p>{t("calibration.howMuch")}</p>
          <Field label={t("calibration.amount")} hint={t("calibration.amountHint")}>
            <NumberInput value={ml} onValue={setMl} unit="ml" name="cup-ml" />
          </Field>
          {simCup !== null && (
            <p class="small">
              <span class="sim-tag">{t("common.simulator")}</span> {t("calibration.inCup")} <button class="btn ghost sm" onClick={() => setMl(simCup)}>{t("calibration.takeMl", { ml: num(simCup, 2) })}</button>
            </p>
          )}
          <div class="row">
            <Button
              variant="primary"
              disabled={!ml}
              onClick={async () => {
                const r = await post(`/jobs/${jobId}/result`, { ml });
                setResult({ changed: r.changed, message: r.message });
                await refreshState();
                toast(t("calibration.pumpDone"), r.changed ? "info" : "ok");
              }}
            >
              {t("common.save")}
            </Button>
            <Button variant="ghost" onClick={() => post(`/jobs/${jobId}/abort`).then(p.onClose)}>
              {t("common.cancel")}
            </Button>
          </div>
        </div>
      )}
      {result !== null && (
        <div class="modal-foot">
          {result.changed && (
            <button
              class="btn"
              onClick={() => {
                setResult(null);
                setJobId(null);
                setMl(null);
              }}
            >
              {t("calibration.again")}
            </button>
          )}
          <button class="btn primary" onClick={p.onClose}>
            {t("common.done")}
          </button>
        </div>
      )}
    </Modal>
  );
}

const PROBE_STEPS: Record<string, { ref: number; text: TextKey }[]> = {
  ph: [
    { ref: 7.0, text: "calibration.ph7" },
    { ref: 4.0, text: "calibration.ph4" },
  ],
  ec: [{ ref: 1.413, text: "calibration.ec1413" }],
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
    toast(t("calibration.saved"));
  }

  const title = p.kind === "ph" ? t("calibration.phTitle", { name: p.name }) : p.kind === "ec" ? t("calibration.ecTitle", { name: p.name }) : t("calibration.curveTitle", { name: p.name });
  return (
    <Modal title={title} onClose={p.onClose}>
      {done ? (
        <Banner tone="ok">{t("calibration.doneText")}</Banner>
      ) : p.kind === "tank_curve" ? (
        <div class="stack">
          <p class="muted">{t("calibration.curveIntro")}</p>
          <div class="row">
            <div style="width:140px">
              <NumberInput value={liters} onValue={setLiters} unit="L" />
            </div>
            {simulated.value && (
              <Button size="sm" variant="ghost" onClick={() => sim("water", { volumeL: liters ?? 0 })}>
                <span class="sim-tag">{t("calibration.simTag")}</span> {t("calibration.simLevel")}
              </Button>
            )}
            <Button size="sm" disabled={liters === null} onClick={() => capture(liters!)}>
              {t("calibration.capture")}
            </Button>
          </div>
          <span class="muted small">{t("calibration.rawNow", { v: num(reading?.value ?? null, 3) })}</span>
          {points.length > 0 && (
            <table class="table">
              <thead>
                <tr>
                  <th>{t("calibration.raw")}</th>
                  <th class="num">{t("calibration.litres")}</th>
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
              {t("calibration.saveCurve")}
            </Button>
          </div>
        </div>
      ) : idx < steps.length ? (
        <div class="stack">
          <div class="wizard-steps">
            {steps.map((s, i) => (
              <span class={`ws ${i === idx ? "cur" : i < idx ? "done" : ""}`}>{i < idx ? "✓ " : ""}{t("calibration.buffer", { v: num(s.ref, s.ref < 2 ? 3 : 1) })}</span>
            ))}
          </div>
          <p>{t(steps[idx].text)}</p>
          <p class="muted small">{t("calibration.waitSteady")} <strong>{num(reading?.value ?? null, 3)}</strong></p>
          <div>
            <Button variant="primary" disabled={!started} onClick={() => capture(steps[idx].ref)}>
              {t("calibration.takeValue")}
            </Button>
          </div>
        </div>
      ) : (
        <div class="stack">
          <Banner tone="ok">{t("calibration.allPoints")}</Banner>
          <div>
            <Button variant="primary" onClick={commit}>
              {t("calibration.save")}
            </Button>
          </div>
        </div>
      )}
      {done && (
        <div class="modal-foot">
          <button class="btn primary" onClick={p.onClose}>
            {t("common.done")}
          </button>
        </div>
      )}
    </Modal>
  );
}
