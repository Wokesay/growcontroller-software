// SPDX-License-Identifier: AGPL-3.0-or-later
// Tank & Regelung: Regelzeilen mit Checkliste, Überwachung, Ausgänge,
// Rastungen, Pflegemodus, Tank-Einstellungen, Durchgang und Phasen.
import { useState } from "preact/hooks";
import { Droplets, ListChecks, Power, ShieldCheck, Sprout, Wrench } from "lucide-preact";
import { post, put } from "../api";
import { dateTime, num } from "../format";
import { config, hasRole, recipes, refreshConfig, refreshState, state, tank, toast } from "../store";
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
        <Field label="Name">
          <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
        </Field>
        <Field label="Nutzvolumen" hint="Obergrenze beim Mischen und Notgrenze beim Zulauf">
          <NumberInput value={cap} onValue={setCap} unit="L" />
        </Field>
        <Field label="Mindestfüllstand" hint="Darunter bleibt die Umwälzpumpe aus" help="minLevel">
          <NumberInput value={min} onValue={setMin} unit="L" />
        </Field>
        <Field label="Wasser">
          <select class="select" value={water} onChange={(e) => setWater((e.target as HTMLSelectElement).value)}>
            <option value="ro">Osmosewasser</option>
            <option value="tap">Leitungswasser</option>
          </select>
        </Field>
        {!hasRole("tank.level") && (
          <Field label="Aktuelles Volumen" hint="Ohne Füllstandssensor von Hand angeben">
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
            toast("Tank gespeichert");
          }}
        >
          Speichern
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
  const [name, setName] = useState("Durchgang 1");
  const [phases, setPhases] = useState([
    { name: "Wachstum", days: 21, ph: 5.8 as number | null, ec: 1.4 as number | null, recipe: rs[0]?.id ?? "" },
    { name: "Blüte", days: 56, ph: 5.9 as number | null, ec: 1.8 as number | null, recipe: rs[1]?.id ?? rs[0]?.id ?? "" },
  ]);
  const upd = (i: number, k: string, v: unknown) => setPhases(phases.map((p, j) => (j === i ? { ...p, [k]: v } : p)));
  return (
    <Card title="Durchgang und Phasen" icon={<Sprout size={18} />}>
      {g.state === "running" ? (
        <div class="stack">
          <div class="row-between">
            <div>
              <strong>{g.name}</strong>
              <div class="muted small">Gestartet {dateTime(g.startedAt)} · Tag {Math.floor((st.now - g.startedAt) / 86400) + 1}</div>
            </div>
            <Pill tone="accent">Phase {g.phase + 1}/{g.phases.length}</Pill>
          </div>
          <table class="table">
            <tbody>
              {g.phases.map((p, i) => (
                <tr style={i === g.phase ? "font-weight:650" : ""}>
                  <td>{i === g.phase ? "▶" : ""}</td>
                  <td>{p.name}</td>
                  <td class="num">{p.days} Tage</td>
                  <td class="small muted">{Object.entries(p.params).map(([k, v]) => (k === "recipe" ? `Rezept „${recipes.value.find((r) => r.id === v)?.name ?? v}“` : `${k === "ph_target" ? "pH" : k === "ec_target" ? "EC" : k} ${typeof v === "number" ? num(v, 2) : v}`)).join(" · ")}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <p class="faint small">Phasen liefern nur Parameter. Die Regelung liest Zielwerte, nie den Phasennamen.</p>
          <div class="row">
            <Button size="sm" disabled={g.phase + 1 >= g.phases.length} onClick={() => post("/grow/next").then(refreshConfig).then(refreshState)}>
              Nächste Phase
            </Button>
            <Button size="sm" onClick={() => post("/grow/harvest").then(refreshState).then(() => toast("Ernte erfasst"))}>
              Ernte erfassen
            </Button>
            <Button size="sm" variant="ghost" onClick={() => post("/grow/complete").then(refreshConfig).then(refreshState)}>
              Durchgang abschließen
            </Button>
          </div>
          {g.harvestedAt > 0 && <span class="muted small">Ernte erfasst am {dateTime(g.harvestedAt)}</span>}
        </div>
      ) : (
        <div class="stack-sm">
          <p class="muted">{g.state === "completed" ? `„${g.name}“ ist abgeschlossen.` : "Kein Durchgang aktiv."} Ein Durchgang gruppiert den Verlauf und liefert je Phase Zielwerte.</p>
          <div>
            <Button size="sm" onClick={() => setOpen(true)}>
              Durchgang starten
            </Button>
          </div>
        </div>
      )}
      {open && (
        <Modal
          wide
          title="Durchgang starten"
          onClose={() => setOpen(false)}
          footer={
            <>
              <button class="btn" onClick={() => setOpen(false)}>
                Abbrechen
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
                  toast("Durchgang gestartet");
                }}
              >
                Starten
              </Button>
            </>
          }
        >
          <Field label="Name">
            <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
          </Field>
          {phases.map((p, i) => (
            <div class="card flat">
              <div class="form-grid">
                <Field label={`Phase ${i + 1}`}>
                  <input class="input" value={p.name} onInput={(e) => upd(i, "name", (e.target as HTMLInputElement).value)} />
                </Field>
                <Field label="Dauer">
                  <NumberInput value={p.days} onValue={(v) => upd(i, "days", v ?? 0)} unit="Tage" />
                </Field>
                <Field label="Ziel-pH">
                  <NumberInput value={p.ph} onValue={(v) => upd(i, "ph", v)} />
                </Field>
                <Field label="Ziel-EC">
                  <NumberInput value={p.ec} onValue={(v) => upd(i, "ec", v)} unit="mS/cm" />
                </Field>
                <Field label="Rezept">
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
            <button class="btn sm" onClick={() => setPhases([...phases, { name: `Phase ${phases.length + 1}`, days: 14, ph: 5.8, ec: 1.6, recipe: rs[0]?.id ?? "" }])}>
              Phase hinzufügen
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
        <Card title="Regelung" icon={<ListChecks size={18} />}>
          <p class="muted small" style="margin-bottom:10px">
            Tippe auf eine Zeile für die Bedingungen: Warum dosiert er gerade (nicht)?
          </p>
          <div class="stack-sm">
            <ControllerRow name="EC" st={st.controllers.ec} open />
            <ControllerRow name="pH" st={st.controllers.ph} open />
            <ControllerRow name="Nachfüllen" st={st.controllers.refill} />
            <ControllerRow name="Umwälzen" st={st.controllers.circulation} />
          </div>
          {latches.length > 0 && (
            <div class="stack-sm" style="margin-top:14px">
              <div class="section-title">Sperren</div>
              {latches.map(([k, v]) => (
                <div class="row-between">
                  <span>
                    <Pill tone="bad">{k.startsWith("jump.") ? "Sprungsperre" : "gerastet"}</Pill> {latchLabel(k)}
                    {v?.why ? ` – ${v.why}` : ""}
                  </span>
                  {!k.startsWith("jump.") && (
                    <Button size="sm" onClick={() => post(`/latches/${k}/ack`).then(refreshState)}>
                      Quittieren
                    </Button>
                  )}
                </div>
              ))}
            </div>
          )}
        </Card>
        <Card
          title="Überwachung"
          icon={<ShieldCheck size={18} />}
          actions={<Seg value={filter} onChange={setFilter} options={[["all", "Alle"], ["problem", "Probleme"]]} />}
        >
          <div class="stack-sm">
            <div class="row">
              <Pill tone={wd.stale || wd.overall === "problem" ? "bad" : wd.overall === "ok" ? "ok" : "neutral"}>{wd.stale ? "keine Bewertung" : wd.headline}</Pill>
              <span class="faint small">bewertet {dateTime(wd.evaluatedAt)} · bewertet nur, schaltet nichts</span>
            </div>
            <div class="list">
              {items.map((a) => (
                <div class="item">
                  <Pill tone={a.status === "ok" ? "ok" : a.status === "problem" ? "bad" : "neutral"}>{a.status === "ok" ? "OK" : a.status === "problem" ? "Problem" : "ruht"}</Pill>
                  <div class="grow">
                    <div class="title">{a.label}</div>
                    <div class="muted small">{a.text}</div>
                  </div>
                </div>
              ))}
              {!items.length && <p class="muted">Keine Probleme.</p>}
            </div>
          </div>
        </Card>
      </div>
      <div class="grid-2">
        <Card title="Tank" icon={<Droplets size={18} />}>
          <TankSettings />
        </Card>
        <div class="stack">
          <Card title="Ausgänge" icon={<Power size={18} />}>
            <div class="list">
              {[["tank.circulation", "Umwälzpumpe"], ["tank.inlet", "Zulaufventil"]].map(([role, label]) => (
                <div class="item">
                  <div class="grow">
                    <div class="title">{label}</div>
                    <div class="muted small">{hasRole(role) ? "zugeordnet" : "nicht zugeordnet"}</div>
                  </div>
                  {hasRole(role) && <Pill tone={st.outputs[role] ? "info" : "neutral"} dot>{st.outputs[role] ? "an" : "aus"}</Pill>}
                </div>
              ))}
            </div>
            <p class="faint small" style="margin-top:8px">
              Ausgänge schaltet nur das Aktor-Gateway mit Trockenlaufschutz und Notgrenze. 230 V nie auf der Platine.
            </p>
          </Card>
          <Card title="Pflegemodus" icon={<Wrench size={18} />}>
            {st.maintenanceUntil > st.now ? (
              <div class="row-between">
                <span>Automatik ruht bis {dateTime(st.maintenanceUntil)}.</span>
                <Button size="sm" onClick={() => post("/maintenance", { minutes: 0 }).then(refreshState)}>
                  Beenden
                </Button>
              </div>
            ) : (
              <div class="stack-sm">
                <p class="muted small">Für Tankwechsel und Reinigung: Die Automatik dosiert nicht, Sprünge der Messwerte gelten als angekündigt. Sperren bleiben aktiv.</p>
                <div class="row">
                  <div style="width:140px">
                    <NumberInput value={maint} onValue={setMaint} unit="min" />
                  </div>
                  <Button size="sm" onClick={() => post("/maintenance", { minutes: maint }).then(refreshState)}>
                    Pflegemodus starten
                  </Button>
                </div>
              </div>
            )}
          </Card>
        </div>
      </div>
      <GrowCard />
      {config.value && !hasRole("tank.ph") && (
        <Banner>Mit einem pH/EC-Sensorkopf misst der Hub pH und EC dauerhaft und regelt nach. Siehe Geräte › Erweitern.</Banner>
      )}
    </div>
  );
}
