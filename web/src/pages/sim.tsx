// Simulator-Panel: Zeitraffer, Szenarien und Störknöpfe, um Abläufe und
// Texte ohne Hardware zu prüfen (Vorschlag anwender). Nur im Simulator.
import { useEffect, useState } from "preact/hooks";
import { FlaskConical, Zap } from "lucide-preact";
import { get, sim } from "../api";
import { num } from "../format";
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
        <FlaskConical size={16} /> Simulator
      </button>
    );
  const w = world?.world;
  const head = w?.ports?.find((p: any) => p.class === "head_ph_ec");
  const lvl = w?.ports?.find((p: any) => p.class === "head_level");
  const block = w?.ports?.find((p: any) => p.class === "dosing_block");
  return (
    <aside class="drawer" aria-label="Simulator">
      <div class="row-between">
        <h2 class="row">
          <FlaskConical size={18} /> Simulator
        </h2>
        <button class="btn sm ghost" onClick={() => setOpen(false)}>
          Schließen
        </button>
      </div>
      <p class="muted small">Digitaler Zwilling des Hubs. Der Kern ist derselbe Code wie auf dem ESP32-S3; ersetzt sind nur Bus, Geräte und Tank.</p>
      <Field label="Zeitraffer">
        <Seg value={speed} onChange={(v) => act("speed", { speed: Number(v) })} options={[["1", "1×"], ["10", "10×"], ["60", "60×"], ["300", "300×"]]} />
      </Field>
      {w && (
        <div class="card flat stack-sm">
          <div class="section-title">Tank (wahr)</div>
          <div class="row small">
            <span>{num(w.tank.volumeL, 1)} L</span>·<span>EC {num(w.tank.ec + w.tank.pendingEc, 2)}</span>·<span>pH {num(w.tank.ph + w.tank.pendingPh, 2)}</span>·
            <span>{num(w.tank.temp, 1)} °C</span>
          </div>
          <div class="row small muted">
            Umwälzpumpe {w.outputs[0] ? "an" : "aus"} · Zulauf {w.outputs[1] ? "auf" : "zu"}
          </div>
        </div>
      )}
      <div class="stack-sm">
        <div class="section-title">Tank befüllen</div>
        <div class="form-grid">
          <NumberInput value={vol} onValue={setVol} unit="L" />
          <NumberInput value={ec} onValue={setEc} unit="mS/cm" />
        </div>
        <Button size="sm" onClick={() => act("water", { volumeL: vol, ec, ph: 7.0 }, "Tank neu befüllt")}>
          Frisches Wasser einfüllen
        </Button>
      </div>
      <div class="stack-sm">
        <div class="section-title">Störungen</div>
        <div class="row">
          {head && (
            <>
              <Button size="sm" onClick={() => act("fault", { device: head.id, fault: "jump" }, "pH-Sonde springt")}>
                <Zap size={14} /> pH-Sprung
              </Button>
              <Button size="sm" onClick={() => act("fault", { device: head.id, fault: "ec_zero" }, "EC-Sonde trocken")}>
                EC 0
              </Button>
              <Button size="sm" onClick={() => act("fault", { device: head.id, fault: "frozen" }, "Werte eingefroren")}>
                Wert friert
              </Button>
              <Button size="sm" onClick={() => act("fault", { device: head.id, fault: "offline" }, "Kopf antwortet nicht")}>
                Kopf offline
              </Button>
              <Button size="sm" variant="ghost" onClick={() => act("fault", { device: head.id, fault: "none" }, "Kopf wieder normal")}>
                Kopf normal
              </Button>
            </>
          )}
          {lvl && (
            <Button size="sm" onClick={() => act("fault", { device: lvl.id, fault: lvl.fault === "offline" ? "none" : "offline" })}>
              Füllstand {lvl.fault === "offline" ? "wieder an" : "offline"}
            </Button>
          )}
        </div>
        {block && (
          <div class="list">
            {block.slots.map((s: any, i: number) =>
              s ? (
                <div class="item small">
                  <span class="grow">
                    Port {i + 1}: {w.liquids[s.liquid]} <span class="faint">({num(s.trueFlow, 1)} ml/min wahr)</span>
                  </span>
                  <Button size="sm" onClick={() => act("fault", { device: s.id, fault: s.blocked ? "none" : "blocked" })}>
                    {s.blocked ? "frei" : "blockieren"}
                  </Button>
                  <Button size="sm" variant="ghost" onClick={() => act("uncap", { block: block.id, slot: i }, "Kappe abgezogen")}>
                    abziehen
                  </Button>
                </div>
              ) : (
                <div class="item small">
                  <span class="grow faint">Port {i + 1}: frei</span>
                  <Button size="sm" variant="ghost" onClick={() => act("cap", { block: block.id, slot: i, liquid: i === 3 ? "ph_down" : "grow_a" }, "Kappe gesteckt")}>
                    Kappe stecken
                  </Button>
                </div>
              ),
            )}
          </div>
        )}
        <div class="row">
          <Button size="sm" onClick={() => act("plug", { port: 2, class: "pump_cap" }, "Kappe direkt an Hub-Port 2")}>
            Fehlsteckung: Kappe an Port 2
          </Button>
          <Button size="sm" variant="ghost" onClick={() => act("unplug", { port: 2 })}>
            Port 2 frei
          </Button>
        </div>
        <div class="row">
          {!head && (
            <Button size="sm" onClick={() => act("plug", { port: 3, class: "head_ph_ec" }, "pH/EC-Kopf an Port 3")}>
              pH/EC-Kopf an Port 3
            </Button>
          )}
          {!lvl && (
            <Button size="sm" onClick={() => act("plug", { port: 5, class: "head_level" }, "Füllstands-Kopf an Port 5")}>
              Füllstand an Port 5
            </Button>
          )}
        </div>
        <Button size="sm" variant="danger-soft" onClick={() => act("reboot", {}, "Stromausfall: Hub startet neu")}>
          Stromausfall / Neustart
        </Button>
      </div>
      <div class="stack-sm">
        <div class="section-title">Szenario</div>
        <p class="faint small">Löscht Einstellungen und Verlauf des Simulators.</p>
        <div class="row">
          {[["neu", "Leer (Stufe 0)"], ["stufe1", "Stufe 1 leer"], ["demo", "Demo eingerichtet"]].map(([n, l]) => (
            <Button
              size="sm"
              onClick={async () => {
                await act("scenario", { name: n }, `Szenario „${l}“ geladen – bitte neu anmelden`);
                await refreshConfig().catch(() => location.reload());
              }}
            >
              {l}
            </Button>
          ))}
        </div>
      </div>
    </aside>
  );
}
