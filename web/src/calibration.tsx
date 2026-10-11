// SPDX-License-Identifier: AGPL-3.0-or-later
// Geführte Kalibrierung: Pumpe einmessen (Messbecher), pH-Sonde (2 Puffer),
// EC-Sonde (1 Referenz), Füllstand (Stützpunkte, stückweise linear).
import { useEffect, useRef, useState } from "preact/hooks";
import { Beaker, Check, Timer } from "lucide-preact";
import { get, isSignalClass, post, sim, type Msg } from "./api";
import { ago, num } from "./format";
import { msg, t, type TextKey } from "./i18n";
import { catalog, config, refreshConfig, refreshState, simulated, state, toast, toastError } from "./store";
import { millivoltsText } from "./widgets";
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
  const ended = job?.state === "failed" || job?.state === "aborted";
  // A failed run stays the app's current job until it is cancelled: every way of closing cancels it,
  // once, and only while it is still the hub's current job.
  const closing = useRef(false);
  const close = () => {
    if (closing.current) return;
    closing.current = true;
    const abort = job?.state === "failed" && st.job?.id === jobId;
    (abort ? post(`/jobs/${jobId}/abort`).then(refreshState).catch(toastError) : Promise.resolve()).finally(p.onClose);
  };

  useEffect(() => {
    if (!waiting || !simulated.value) return;
    get("/sim").then((s: any) => {
      for (const port of s.world.ports) for (const slot of port.slots ?? []) if (slot?.id === p.pump && slot.cupMl) setSimCup(slot.cupMl);
    });
  }, [waiting]);

  return (
    <Modal title={t("calibration.pumpTitle", { name: p.name })} onClose={close}>
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
        <Banner tone={job?.state === "failed" ? "bad" : job?.state === "aborted" ? "warn" : "info"} icon={ended ? undefined : <Beaker size={18} />}>
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
                setSimCup(null);
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
      {ended && (
        <div class="modal-foot">
          <button class="btn primary" onClick={close}>
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
  const [done, setDone] = useState(false);
  const [lost, setLost] = useState(false); // the hub ended the session (a restart, or left for 30 min)
  const [refused, setRefused] = useState(""); // why the hub refused to save: start again
  const [announce, setAnnounce] = useState("");
  const [liters, setLiters] = useState<number | null>(0);
  const ended = useRef(false); // saved or cancelled: nothing left to end on close
  const seen = useRef(false); // the hub's session has been seen, so its absence means it ended
  const committing = useRef(false);
  const cancelBtn = useRef<HTMLButtonElement>(null);
  const takeBtn = useRef<HTMLButtonElement>(null);
  const saveBtn = useRef<HTMLButtonElement>(null);
  const restartBtn = useRef<HTMLButtonElement>(null);
  const steps = PROBE_STEPS[p.kind] ?? [];
  const role = p.kind === "ph" ? "tank.ph" : p.kind === "ec" ? "tank.ec" : "tank.level";
  const reading = st.readings[role];
  // The hub's session is the truth, read only once this dialog has started its own (SD-041)
  const session = started ? st.probeCalibration?.[`${p.device}:${p.kind}`] : undefined;
  const points = session?.points ?? [];
  const idx = p.kind === "tank_curve" ? 0 : points.length;
  // A sensor on its own schedule (Home Assistant): a point waits for its next report
  const pending = !!session?.pending;
  const failed = !pending ? session?.failed : undefined;
  // A pH probe's voltage from Home Assistant is shown in mV while no calibration is stored;
  // once one is, the value is a calibrated pH and stays one
  const cls = config.value?.devices.find((d) => d.id === p.device)?.class;
  const asMv = p.kind === "ph" && isSignalClass(catalog.value, cls) && !st.devices.find((d) => d.id === p.device)?.calibrations?.ph;
  const shown = (v: number | null | undefined) => (v == null ? num(null, 3) : asMv ? millivoltsText(v) : num(v, 3));

  async function begin() {
    await post("/probe", { device: p.device, kind: p.kind, action: "start" });
    await refreshState();
    seen.current = false;
    setLost(false);
    setRefused("");
    setAnnounce("");
    setStarted(true);
  }
  useEffect(() => {
    begin().catch((e) => {
      toastError(e);
      p.onClose();
    });
    return () => {
      // Closed without saving: the hub ends the calibration too, so jumps count again (RAT-042)
      if (!ended.current) post("/probe", { device: p.device, kind: p.kind, action: "cancel" }).catch(() => {});
      if (simulated.value && p.kind !== "tank_curve") sim("probe", { kind: p.kind, buffer: null }).catch(() => {});
    };
  }, []);
  // Ended on the hub meanwhile (a restart, the device removed): say so instead of a button that no longer works
  useEffect(() => {
    if (session) seen.current = true;
    else if (started && !done && seen.current && !committing.current) {
      setLost(true);
      window.setTimeout(() => restartBtn.current?.focus());
    }
  }, [started, done, !!session]);
  // A point taken from the sensor's next report: said aloud, the focus back on the next step
  const before = useRef(0);
  useEffect(() => {
    if (points.length > before.current && p.kind !== "tank_curve") {
      setAnnounce(t("calibration.pointTaken", { v: num(steps[points.length - 1]?.ref ?? null, 2) }));
      window.setTimeout(() => (points.length >= steps.length ? saveBtn.current : takeBtn.current)?.focus());
    }
    before.current = points.length;
  }, [points.length]);
  useEffect(() => {
    if (pending) cancelBtn.current?.focus();
  }, [pending]);
  useEffect(() => {
    if (!simulated.value || !started || p.kind === "tank_curve" || done) return;
    const s = steps[idx];
    if (s) sim("probe", { kind: p.kind, buffer: s.ref }).catch(() => {});
  }, [idx, started]);

  async function capture(ref: number) {
    await post("/probe", { device: p.device, kind: p.kind, action: "point", reference: ref });
    await refreshState();
  }
  async function cancel() {
    try {
      await post("/probe", { device: p.device, kind: p.kind, action: "cancel" });
      ended.current = true;
    } catch (e) {
      toastError(e);
    }
    p.onClose();
  }
  async function commit() {
    committing.current = true;
    try {
      await post("/probe", { device: p.device, kind: p.kind, action: "commit" });
    } catch (e: any) {
      committing.current = false;
      setRefused(e?.message ?? String(e)); // the reason stays in view, not only in a toast
      window.setTimeout(() => restartBtn.current?.focus());
      return;
    }
    ended.current = true;
    if (simulated.value && p.kind !== "tank_curve") await sim("probe", { kind: p.kind, buffer: null });
    await refreshConfig();
    await refreshState();
    setDone(true);
    toast(t("calibration.saved"));
  }

  const title = p.kind === "ph" ? t("calibration.phTitle", { name: p.name }) : p.kind === "ec" ? t("calibration.ecTitle", { name: p.name }) : t("calibration.curveTitle", { name: p.name });
  const restart = (
    <div>
      <button ref={restartBtn} class="btn primary" onClick={() => begin().catch(toastError)} data-testid="probe-restart">
        {t("calibration.restart")}
      </button>
    </div>
  );
  return (
    <Modal title={title} onClose={p.onClose}>
      {/* One live region that stays in place: waiting, a point taken, no report in time */}
      <div class="sr-only" role="status" aria-live="polite">
        {lost ? t("calibration.lost") : pending ? t("calibration.waitReport") : failed ? msg(failed) : announce}
      </div>
      {done ? (
        <Banner tone="ok">{t("calibration.doneText")}</Banner>
      ) : lost ? (
        <div class="stack">
          <Banner tone="warn">{t("calibration.lost")}</Banner>
          {restart}
        </div>
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
            <Button size="sm" disabled={liters === null || !started} onClick={() => capture(liters!)}>
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
              <span class={`ws ${i === idx ? "cur" : i < idx ? "done" : ""}`}>{i < idx ? "✓ " : ""}{t("calibration.buffer", { v: num(s.ref, s.ref < 2 ? 3 : 2) })}</span>
            ))}
          </div>
          <p>{t(steps[idx].text)}</p>
          <p class="muted small">
            {t("calibration.waitSteady")} <strong>{shown(reading?.value)}</strong>
            {reading?.ageS != null && ` · ${t("calibration.lastReport", { ago: ago(reading.ageS) })}`}
          </p>
          {failed && <Banner tone="warn">{msg(failed)}</Banner>}
          {pending ? (
            <div class="stack-sm" data-testid="probe-pending">
              <strong>{t("calibration.waitReport")}</strong>
              <span class="muted small">{t("calibration.waitReportText", { v: num(steps[idx].ref, steps[idx].ref < 2 ? 3 : 2) })}</span>
              <div class="row wrap">
                <button ref={cancelBtn} class="btn" onClick={cancel} data-testid="probe-cancel">
                  {t("common.cancel")}
                </button>
                <span class="muted small">{t("calibration.cancelHint")}</span>
              </div>
            </div>
          ) : (
            <div>
              <button ref={takeBtn} class="btn primary" disabled={!started} onClick={() => capture(steps[idx].ref).catch(toastError)} data-testid="probe-take">
                {t("calibration.takeValue")}
              </button>
            </div>
          )}
        </div>
      ) : (
        <div class="stack">
          {refused ? (
            <>
              <Banner tone="warn">{refused}</Banner>
              {restart}
            </>
          ) : (
            <>
              <Banner tone="ok">{t("calibration.allPoints")}</Banner>
              <div>
                <button ref={saveBtn} class="btn primary" onClick={commit} data-testid="probe-save">
                  {t("calibration.save")}
                </button>
              </div>
            </>
          )}
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
