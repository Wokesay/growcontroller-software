// SPDX-License-Identifier: AGPL-3.0-or-later
// Simulator-Panel: Zeitraffer, Szenarien und Störknöpfe, um Abläufe und
// Texte ohne Hardware zu prüfen (Vorschlag anwender). Nur im Simulator.
import { useEffect, useState } from "preact/hooks";
import { FlaskConical, Zap } from "lucide-preact";
import { get, sim } from "../api";
import { num } from "../format";
import { t, type TextKey } from "../i18n";
import { refreshConfig, refreshState, state, toast } from "../store";
import { Button, Field, NumberInput, Seg } from "../ui";

export function SimPanel() {
  const [open, setOpen] = useState(false);
  const [world, setWorld] = useState<any>(null);
  const [vol, setVol] = useState<number | null>(40);
  const [ec, setEc] = useState<number | null>(0.02);
  const st = state.value;
  const speed = String(st?.sim?.speed ?? 1);

  const load = () => get("/sim").then((s: any) => setWorld(s)).catch(() => {});
  useEffect(() => {
    if (!open) return;
    load();
    const t = setInterval(load, 2000);
    return () => clearInterval(t);
  }, [open]);

  const act = async (action: string, body: unknown = {}, msg?: string) => {
    try {
      await sim(action, body);
      await load();
      await refreshState();
      if (msg) toast(msg, "info");
    } catch (e: any) {
      toast(e.message, "error");
    }
  };

  if (!open)
    return (
      <button class="btn sim-fab" onClick={() => setOpen(true)} data-testid="sim-open">
        <FlaskConical size={16} /> {t("common.simulator")}
      </button>
    );
  const w = world?.world;
  // Sonden: ein gemeinsamer pH/EC-Kopf oder zwei einzelne Köpfe
  const heads = (w?.ports ?? []).filter((p: any) => p.class === "head_ph_ec" || p.class === "head_ph" || p.class === "head_ec");
  const head = heads[0];
  const phHead = heads.find((p: any) => p.class !== "head_ec");
  const ecHead = heads.find((p: any) => p.class !== "head_ph");
  const allHeads = async (fault: string, msg: string) => {
    try {
      for (const h of heads) await sim("fault", { device: h.id, fault });
      await load();
      await refreshState();
      toast(msg, "info");
    } catch (e: any) {
      toast(e.message, "error");
    }
  };
  const lvl = w?.ports?.find((p: any) => p.class === "head_level");
  const block = w?.ports?.find((p: any) => p.class === "dosing_block");
  return (
    <aside class="drawer" aria-label={t("common.simulator")}>
      <div class="row-between">
        <h2 class="row">
          <FlaskConical size={18} /> {t("common.simulator")}
        </h2>
        <button class="btn sm ghost" onClick={() => setOpen(false)}>
          {t("common.close")}
        </button>
      </div>
      <p class="muted small">{t("sim.intro")}</p>
      <Field label={t("sim.speed")}>
        <Seg value={speed} onChange={(v) => act("speed", { speed: Number(v) })} options={[["1", "1×"], ["10", "10×"], ["60", "60×"], ["300", "300×"]]} />
      </Field>
      {w && (
        <div class="card flat stack-sm">
          <div class="section-title">{t("sim.tankTrue")}</div>
          <div class="row small">
            <span>{num(w.tank.volumeL, 1)} L</span>·<span>EC {num(w.tank.ec + w.tank.pendingEc, 2)}</span>·<span>pH {num(w.tank.ph + w.tank.pendingPh, 2)}</span>·
            <span>{num(w.tank.temp, 1)} °C</span>
          </div>
          <div class="row small muted">
            {t("sim.outputs", { circ: w.outputs[0] ? t("common.on") : t("common.off"), inlet: w.outputs[1] ? t("sim.inletOpen") : t("sim.inletClosed") })}
          </div>
        </div>
      )}
      <div class="stack-sm">
        <div class="section-title">{t("sim.fill")}</div>
        <div class="form-grid">
          <NumberInput value={vol} onValue={setVol} unit="L" />
          <NumberInput value={ec} onValue={setEc} unit="mS/cm" />
        </div>
        <Button size="sm" onClick={() => act("water", { volumeL: vol, ec, ph: 7.0 }, t("sim.filled"))}>
          {t("sim.freshWater")}
        </Button>
      </div>
      <div class="stack-sm">
        <div class="section-title">{t("sim.faults")}</div>
        <div class="row">
          {head && (
            <>
              {phHead && (
                <Button size="sm" onClick={() => act("fault", { device: phHead.id, fault: "jump" }, t("sim.phJumped"))}>
                  <Zap size={14} /> {t("sim.phJump")}
                </Button>
              )}
              {ecHead && (
                <Button size="sm" onClick={() => act("fault", { device: ecHead.id, fault: "ec_zero" }, t("sim.ecDry"))}>
                  EC 0
                </Button>
              )}
              <Button size="sm" onClick={() => allHeads("frozen", t("sim.frozen"))}>
                {t("sim.freeze")}
              </Button>
              <Button size="sm" onClick={() => allHeads("offline", t("sim.headNotResponding"))}>
                {t("sim.headOffline")}
              </Button>
              <Button size="sm" variant="ghost" onClick={() => allHeads("none", t("sim.headBackNormal"))}>
                {t("sim.headNormal")}
              </Button>
            </>
          )}
          {lvl && (
            <Button size="sm" onClick={() => act("fault", { device: lvl.id, fault: lvl.fault === "offline" ? "none" : "offline" })}>
              {lvl.fault === "offline" ? t("sim.levelOn") : t("sim.levelOff")}
            </Button>
          )}
        </div>
        {block && (
          <div class="list">
            {block.slots.map((s: any, i: number) =>
              s ? (
                <div class="item small">
                  <span class="grow">
                    {t("sim.pumpLiquid", { n: i + 1, liquid: w.liquids[s.liquid] ?? "" })} <span class="faint">{t("sim.trueFlow", { flow: num(s.trueFlow, 1) })}</span>
                  </span>
                  <Button size="sm" onClick={() => act("fault", { device: s.id, fault: s.blocked ? "none" : "blocked" })}>
                    {s.blocked ? t("sim.unblock") : t("sim.block")}
                  </Button>
                  <Button size="sm" variant="ghost" onClick={() => act("uncap", { block: block.id, slot: i }, t("sim.unplugged"))}>
                    {t("sim.unplug")}
                  </Button>
                </div>
              ) : (
                <div class="item small">
                  <span class="grow faint">{t("sim.slotFree", { n: i + 1 })}</span>
                  <Button size="sm" variant="ghost" onClick={() => act("cap", { block: block.id, slot: i, liquid: i === 3 ? "ph_down" : "grow_a" }, t("sim.plugged"))}>
                    {t("sim.plug")}
                  </Button>
                </div>
              ),
            )}
          </div>
        )}
        <div class="row">
          <Button size="sm" onClick={() => act("plug", { port: 2, class: "pump_cap" }, t("sim.wrongPlugged"))}>
            {t("sim.wrongPlug")}
          </Button>
          <Button size="sm" variant="ghost" onClick={() => act("unplug", { port: 2 })}>
            {t("sim.port2Free")}
          </Button>
        </div>
        <div class="row">
          {heads.length === 0 && (
            <>
              <Button size="sm" onClick={() => act("plug", { port: 3, class: "head_ph_ec" }, t("sim.phEcHead"))}>
                {t("sim.phEcHead")}
              </Button>
              <Button
                size="sm"
                onClick={async () => {
                  await act("plug", { port: 3, class: "head_ph" });
                  await act("plug", { port: 4, class: "head_ec" }, t("sim.splitHeadsDone"));
                }}
              >
                {t("sim.splitHeads")}
              </Button>
            </>
          )}
          {!lvl && (
            <Button size="sm" onClick={() => act("plug", { port: 5, class: "head_level" }, t("sim.levelPlugged"))}>
              {t("sim.levelPlug")}
            </Button>
          )}
        </div>
        <div class="section-title">{t("sim.netPlugs")}</div>
        <div class="row wrap">
          <Button size="sm" onClick={() => act("net_add", { class: "shelly_plug", loads: [{ load: "circulation", watts: 18 }] }, t("sim.plugAdded"))}>
            {t("sim.plugAdd")}
          </Button>
          <Button
            size="sm"
            onClick={() =>
              act(
                "net_add",
                { class: "shelly_strip4", loads: [{ load: "light", watts: 240 }, { load: "exhaust", watts: 35 }, { load: "circulation_fan", watts: 15 }, { load: "humidifier", watts: 30 }] },
                t("sim.stripAdded"),
              )
            }
          >
            {t("sim.stripAdd")}
          </Button>
        </div>
        {(w?.netPlugs ?? []).map((np: any) => (
          <div class="row wrap">
            <span class="grow small">
              <span class="mono">{np.id}</span> · {np.outlets.map((o: any, i: number) => `${i + 1}: ${o.load || "–"} ${o.on ? t("common.on") : t("common.off")}`).join(", ")}
            </span>
            <Button size="sm" variant="ghost" onClick={() => act("fault", { device: np.id, fault: np.fault === "offline" ? "none" : "offline" })}>
              {np.fault === "offline" ? t("sim.wifiBack") : t("sim.wifiGone")}
            </Button>
          </div>
        ))}
        <Button size="sm" variant="danger-soft" onClick={() => act("reboot", {}, t("sim.powerCut"))}>
          {t("sim.reboot")}
        </Button>
      </div>
      <div class="stack-sm">
        <div class="section-title">{t("sim.scenario")}</div>
        <p class="faint small">{t("sim.scenarioNote")}</p>
        <div class="row">
          {([["neu", "sim.scenarioEmpty"], ["stufe1", "sim.scenarioStage1"], ["demo", "sim.scenarioDemo"]] as [string, TextKey][]).map(([n, l]) => (
            <Button
              size="sm"
              onClick={async () => {
                await act("scenario", { name: n }, t("sim.scenarioLoaded", { name: t(l) }));
                await refreshConfig().catch(() => location.reload());
              }}
            >
              {t(l)}
            </Button>
          ))}
        </div>
      </div>
    </aside>
  );
}
