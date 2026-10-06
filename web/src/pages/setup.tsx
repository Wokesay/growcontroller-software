// Setup-Assistent: vom ersten Einschalten bis zur ersten Mischung, in fünf
// Schritten. Geräte werden erkannt statt ausgewählt (Kundensicht); Einmessen
// ist Pflicht, bevor eine Pumpe dosiert (Produktregel, strenger als
// RAT-015). Die Leiste unten (Zurück / Überspringen / Weiter) ist immer
// sichtbar; „Weiter“ speichert den Schritt.
import type { ComponentChildren } from "preact";
import { useState } from "preact/hooks";
import { ArrowLeft, ArrowRight, Check, PackagePlus, Sprout } from "lucide-preact";
import { del, post, probeKinds, put, type Canister, type ProbeKind, type RecipeTemplate } from "../api";
import { PumpCalibration, ProbeCalibration } from "../calibration";
import { num } from "../format";
import { lang, setLang, t, type Lang, type TextKey } from "../i18n";
import { canisters, catalog, config, recipes, refreshConfig, refreshState, state, toast } from "../store";
import { Banner, Button, Card, CheckRow, Field, NumberInput, Pill, Seg, Term, navigate, route, setupLabel, type HelpTopic } from "../ui";
import { DeviceIcon, PortGrid, devicePlace } from "../widgets";

const STEPS: TextKey[] = ["setup.step.start", "setup.step.devices", "setup.step.tank", "setup.step.nutrients", "setup.step.calibrate", "setup.step.done"];
const COLORS = ["#3f8f4a", "#c47a2c", "#5b7fb8", "#b8455b", "#8a5cc2", "#2f9aa0"];
const DONE = STEPS.length - 1;

// ---------- Rahmen: Leiste unten

type FootProps = { step: number; onNext?: () => unknown; nextDisabled?: boolean; nextLabel?: ComponentChildren; onSkip?: () => void };

function Foot(p: FootProps) {
  const go = (s: number) => navigate(`/einrichtung?s=${s}`);
  return (
    <div class="wizard-foot">
      <div>
        {p.step > 0 ? (
          <Button onClick={() => go(p.step - 1)} data-testid="wizard-back">
            <ArrowLeft size={16} /> {t("common.back")}
          </Button>
        ) : (
          <span />
        )}
        <span class="spacer" />
        <span class="faint small hide-sm">{t("common.step", { n: p.step + 1, total: STEPS.length })}</span>
        {p.onSkip && (
          <Button variant="ghost" onClick={p.onSkip} data-testid="wizard-skip">
            {t("common.skip")}
          </Button>
        )}
        {p.onNext && (
          <Button variant="primary" disabled={p.nextDisabled} onClick={p.onNext} data-testid="wizard-next">
            {p.nextLabel ?? t("common.next")} <ArrowRight size={16} />
          </Button>
        )}
      </div>
    </div>
  );
}

/** Seitenleiste: Begriffe des Schritts und was für das Mischen noch fehlt. */
function Aside(p: { terms?: HelpTopic[] }) {
  const mixFn = state.value!.functions.find((f) => f.id === "mix");
  const open = mixFn?.checks.filter((c) => !c.ok && c.level !== "runtime") ?? [];
  return (
    <aside class="stack">
      {p.terms && p.terms.length > 0 && (
        <div class="card flat stack-sm">
          <div class="section-title">{t("setup.side.terms")}</div>
          {p.terms.map((k) => (
            <Term topic={k} />
          ))}
        </div>
      )}
      {mixFn && (
        <div class="card flat stack-sm">
          <div class="section-title">{t("setup.side.missing")}</div>
          {open.map((c) => (
            <CheckRow ok={false} soft={c.soft} text={c.text} />
          ))}
          {open.every((c) => c.soft) && <CheckRow ok text={t("setup.side.nothing")} />}
        </div>
      )}
    </aside>
  );
}

function Page(p: { aside?: ComponentChildren; children: ComponentChildren; foot: FootProps }) {
  return (
    <>
      <div class={`wizard-body ${p.aside ? "" : "single"}`}>
        <Card>
          <div class="stack">{p.children}</div>
        </Card>
        {p.aside}
      </div>
      <Foot {...p.foot} />
    </>
  );
}

// ---------- 1 Start

type ZoneKind = "room" | "tent" | "greenhouse";

function StepStart(p: { step: number; next: () => void }) {
  const cfg = config.value!;
  const [name, setName] = useState(cfg.system.name);
  const z = cfg.zones?.[0];
  const [zoneName, setZoneName] = useState(z?.name ?? "");
  const [zoneKind, setZoneKind] = useState<ZoneKind>(z?.kind ?? "room");
  const tz = Intl.DateTimeFormat().resolvedOptions().timeZone || "Europe/Berlin";
  const pick = (l: Lang) => setLang(l);
  return (
    <Page
      foot={{
        step: p.step,
        onNext: async () => {
          await put("/system", { name, timezone: tz, language: lang.value });
          await put("/zone", { name: zoneName.trim() || t(`zone.${zoneKind}`), kind: zoneKind });
          await refreshConfig();
          p.next();
        },
      }}
    >
      <h2>{t("setup.start.h")}</h2>
      <p class="muted">{t("setup.start.intro")}</p>
      <Banner>{t("setup.start.pw")}</Banner>
      <div class="form-grid">
        <Field label={t("setup.start.name")} hint={t("setup.start.nameHint")}>
          <input class="input" name="hubname" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
        </Field>
        <div class="field">
          <span>{t("common.language")}</span>
          <Seg<Lang> value={lang.value} onChange={pick} options={[["de", "Deutsch"], ["en", "English"]]} />
        </div>
        <div class="field">
          <span>{t("setup.start.zoneKind")}</span>
          <Seg<ZoneKind>
            value={zoneKind}
            onChange={setZoneKind}
            options={[
              ["room", t("zone.room")],
              ["tent", t("zone.tent")],
              ["greenhouse", t("zone.greenhouse")],
            ]}
          />
        </div>
        <Field label={t("setup.start.zoneName")} hint={t("setup.start.zoneHint")}>
          <input class="input" name="zonename" value={zoneName} placeholder={t(`zone.${zoneKind}`)} onInput={(e) => setZoneName((e.target as HTMLInputElement).value)} />
        </Field>
        <Field label={t("setup.start.tz")} hint={t("setup.start.tzHint")}>
          <input class="input" value={tz} disabled />
        </Field>
      </div>
    </Page>
  );
}

// ---------- 2 Geräte

function StepDevices(p: { step: number; next: () => void }) {
  const st = state.value!;
  const fresh = st.devices.filter((d) => !d.configured && d.online);
  const hasBlock = st.devices.some((d) => d.class === "dosing_block" && d.configured);
  const pumps = st.devices.filter((d) => d.class === "pump_cap");
  const ready = hasBlock && pumps.some((d) => d.configured);
  const list = st.devices.filter((d) => d.class !== "hub_outputs").sort((a, b) => (a.slot >= 0 ? 100 + a.slot : a.port) - (b.slot >= 0 ? 100 + b.slot : b.port));
  return (
    <Page foot={{ step: p.step, onNext: p.next, nextDisabled: !ready }} aside={<Aside />}>
      <h2>{t("setup.dev.h")}</h2>
      <p class="muted">{t("setup.dev.intro")}</p>
      <div class="section-title">{t("setup.dev.hubPorts")}</div>
      <PortGrid />
      <div class="section-title">{t("setup.dev.found")}</div>
      <div class="list">
        {list.map((d) => (
          <div class="item">
            <span class="dev-ic">
              <DeviceIcon cls={d.class} />
            </span>
            <span class="grow">
              <strong>{d.classLabel}</strong>
              <div class="faint small">{devicePlace(d)}</div>
            </span>
            {d.configured ? <Pill tone="ok">{t("setup.dev.stateAccepted")}</Pill> : <Pill tone="info">{t("setup.dev.stateFound")}</Pill>}
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
              toast(t("setup.dev.accepted", { n: fresh.length }));
            }}
          >
            <PackagePlus size={16} /> {fresh.length === 1 ? t("setup.dev.acceptOne") : t("setup.dev.acceptAll", { n: fresh.length })}
          </Button>
        </div>
      )}
      {!hasBlock && <Banner tone="warn">{t("setup.dev.needBlock")}</Banner>}
      {hasBlock && pumps.length === 0 && <Banner tone="warn">{t("setup.dev.needPump")}</Banner>}
      <p class="faint small">{t("setup.dev.plugs")}</p>
    </Page>
  );
}

// ---------- 3 Tank

function StepTank(p: { step: number; next: () => void }) {
  const cfg = config.value!;
  const tk = cfg.tanks[0];
  const hubOut = cfg.devices.find((d) => d.class === "hub_outputs");
  const hasLevel = cfg.devices.some((d) => d.class === "head_level");
  const [name, setName] = useState(tk?.name ?? "Tank 1");
  const [cap, setCap] = useState<number | null>(tk?.capacityL ?? null);
  const [water, setWater] = useState(tk?.water ?? "ro");
  const [circ, setCirc] = useState(!!tk?.roles?.["tank.circulation"]);
  const [inlet, setInlet] = useState(!!tk?.roles?.["tank.inlet"]);
  const [min, setMin] = useState<number | null>(tk?.minL ?? 3);
  const save = async () => {
    await put("/tank", { name, capacityL: cap, water, minL: hasLevel ? min : null });
    if (hubOut) {
      if (circ) await put("/roles/tank.circulation", { device: hubOut.id, channel: 0 });
      else if (tk?.roles?.["tank.circulation"]) await del("/roles/tank.circulation");
      if (inlet) await put("/roles/tank.inlet", { device: hubOut.id, channel: 1 });
      else if (tk?.roles?.["tank.inlet"]) await del("/roles/tank.inlet");
    }
    await refreshConfig();
    await refreshState();
    p.next();
  };
  return (
    <Page foot={{ step: p.step, onNext: save, nextDisabled: !cap }} aside={<Aside terms={hasLevel ? ["usableVolume", "minLevel"] : ["usableVolume"]} />}>
      <h2>{t("setup.tank.h")}</h2>
      <div class="form-grid">
        <Field label={t("setup.tank.name")}>
          <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
        </Field>
        <Field label={t("term.usableVolume")} help="usableVolume">
          <NumberInput value={cap} onValue={setCap} unit="L" name="capacity" />
        </Field>
        <Field label={t("setup.tank.water")}>
          <select class="select" value={water} onChange={(e) => setWater((e.target as HTMLSelectElement).value)}>
            <option value="ro">{t("setup.tank.ro")}</option>
            <option value="tap">{t("setup.tank.tap")}</option>
          </select>
        </Field>
        {hasLevel && (
          <Field label={t("term.minLevel")} help="minLevel">
            <NumberInput value={min} onValue={setMin} unit="L" name="minLevel" />
          </Field>
        )}
      </div>
      {hubOut && (
        <div class="stack-sm">
          <label class="row">
            <input type="checkbox" checked={circ} onChange={(e) => setCirc((e.target as HTMLInputElement).checked)} /> {t("setup.tank.circ")}
          </label>
          {hasLevel && (
            <label class="row">
              <input type="checkbox" checked={inlet} onChange={(e) => setInlet((e.target as HTMLInputElement).checked)} /> {t("setup.tank.inlet")}
            </label>
          )}
          {!circ && <p class="muted small">{t("setup.tank.noCirc")}</p>}
        </div>
      )}
    </Page>
  );
}

// ---------- 4 Nährstoffe

type Row = { pump: string; slot: number; id?: string; name: string; kind: Canister["kind"]; pair: string; color: string; capacityMl: number | null; mlPerL: number | null };

const tplName = (x: { name: string; nameEn?: string }) => (lang.value === "en" && x.nameEn ? x.nameEn : x.name);
const tplNote = (x: RecipeTemplate) => (lang.value === "en" && x.noteEn ? x.noteEn : x.note);

/** Vorlage auf die Pumpen legen: Teile der Reihe nach, eine freie Pumpe wird pH−. */
function rowsFromTemplate(pumps: { id: string; slot: number }[], tpl: RecipeTemplate | null, prev: Row[]): Row[] {
  let extra = 0;
  return pumps.map((pu, i) => {
    const old = prev.find((r) => r.pump === pu.id);
    const base = { pump: pu.id, slot: pu.slot, id: old?.id, color: old?.color ?? COLORS[i % COLORS.length], capacityMl: old?.capacityMl ?? 1000 };
    if (!tpl) return { ...base, name: old?.name ?? "", kind: old?.kind ?? "nutrient", pair: old?.pair ?? "", mlPerL: old?.mlPerL ?? null };
    const s = tpl.steps[i];
    if (s) return { ...base, name: tplName(s), kind: "nutrient", pair: s.pair ?? "", mlPerL: s.mlPerL };
    if (i === tpl.steps.length) return { ...base, name: "pH−", kind: "ph_down", pair: "", mlPerL: null };
    return { ...base, name: t("setup.nut.extra", { n: ++extra }), kind: "nutrient", pair: "", mlPerL: null };
  });
}

function StepNutrients(p: { step: number; next: () => void }) {
  const st = state.value!;
  const tpls = catalog.value?.templates?.recipes ?? [];
  const pumps = st.devices.filter((d) => d.class === "pump_cap" && d.configured).sort((a, b) => a.slot - b.slot);
  const existing = canisters.value;
  const initial: Row[] = pumps.map((pu, i) => {
    const k = existing.find((x) => x.pump === pu.id);
    return { pump: pu.id, slot: pu.slot, id: k?.id, name: k?.name ?? "", kind: k?.kind ?? "nutrient", pair: k?.pair ?? "", color: k?.color ?? COLORS[i % COLORS.length], capacityMl: k?.capacityMl ?? 1000, mlPerL: null };
  });
  const fresh = existing.length === 0;
  const [sel, setSel] = useState<string>(fresh && tpls[0] ? tpls[0].id : "own");
  const [rows, setRows] = useState<Row[]>(() => (fresh && tpls[0] ? rowsFromTemplate(pumps, tpls[0], initial) : initial));
  const [rname, setRname] = useState(fresh && tpls[0] ? tplName(tpls[0]) : "");
  const choose = (id: string) => {
    setSel(id);
    const tpl = tpls.find((x) => x.id === id) ?? null;
    setRows(rowsFromTemplate(pumps, tpl, rows));
    setRname(tpl ? tplName(tpl) : "");
  };
  const upd = (i: number, k: keyof Row, v: unknown) => setRows(rows.map((r, j) => (j === i ? { ...r, [k]: v } : r)));
  const cur = tpls.find((x) => x.id === sel);
  const named = rows.filter((r) => r.name.trim());
  const dosed = rows.filter((r) => r.kind === "nutrient" && r.name.trim() && r.mlPerL);
  const save = async () => {
    const ids: Record<string, string> = {};
    for (const r of named) {
      const res = await post<{ id: string }>("/canisters", { id: r.id, name: r.name.trim(), kind: r.kind, pump: r.pump, pair: r.kind === "nutrient" ? r.pair : "", color: r.color, capacityMl: r.capacityMl });
      ids[r.pump] = res?.id ?? r.id ?? "";
    }
    if (dosed.length > 0) {
      const name = rname.trim() || (cur ? tplName(cur) : t("setup.nut.own"));
      const same = recipes.value.find((x) => x.name === name);
      await post("/recipes", { id: same?.id, name, note: cur ? tplNote(cur) ?? "" : "", steps: dosed.map((r) => ({ canister: ids[r.pump], mlPerL: r.mlPerL })) });
    }
    await refreshConfig();
    toast(t("setup.nut.saved"));
    p.next();
  };
  return (
    <Page foot={{ step: p.step, onNext: save, nextDisabled: named.length === 0 }} aside={<Aside terms={["bottle", "pair"]} />}>
      <h2>{t("setup.nut.h")}</h2>
      <p class="muted">{t("setup.nut.intro")}</p>
      {recipes.value.length > 0 && (
        <div class="stack-sm" data-testid="existing-recipes">
          <div class="section-title">{t("setup.nut.existing")}</div>
          <div class="row wrap">
            {recipes.value.map((r) => (
              <Pill tone="ok">
                {r.name} · {r.steps.map((s) => `${existing.find((k) => k.id === s.canister)?.name ?? "?"} ${num(s.mlPerL, 1)}`).join(", ")}
              </Pill>
            ))}
          </div>
        </div>
      )}
      <div class="section-title">{t("setup.nut.templates")}</div>
      <div class="tpl-grid">
        {tpls.map((x) => (
          <button type="button" class={`tpl ${sel === x.id ? "on" : ""}`} onClick={() => choose(x.id)} aria-pressed={sel === x.id}>
            <strong>{tplName(x)}</strong>
            <div class="tpl-rows">
              {x.steps.map((s) => (
                <div>
                  <span>{tplName(s)}</span>
                  <span class="faint">{num(s.mlPerL, 1)} ml/L</span>
                </div>
              ))}
            </div>
            {x.ec && <span class="faint small">{t("setup.nut.ec", { ec: num(x.ec, 1) })}</span>}
            {tplNote(x) && <span class="muted small">{tplNote(x)}</span>}
          </button>
        ))}
        <button type="button" class={`tpl ${sel === "own" ? "on" : ""}`} onClick={() => choose("own")} aria-pressed={sel === "own"}>
          <strong>{t("setup.nut.own")}</strong>
          <span class="muted small">{t("setup.nut.ownText")}</span>
        </button>
      </div>
      {cur && cur.steps.length > pumps.length && <Banner tone="warn">{t("setup.nut.tooFew", { n: cur.steps.length, p: pumps.length })}</Banner>}
      <div class="section-title">{t("setup.nut.map")}</div>
      <p class="faint small">{t("setup.nut.mapIntro")}</p>
      <div class="table-wrap">
        <table class="table rtable">
          <thead>
            <tr>
              <th>{t("setup.nut.pump")}</th>
              <th>{t("setup.nut.name")}</th>
              <th>{t("setup.nut.kind")}</th>
              <th>{t("setup.nut.pair")}</th>
              <th>{t("setup.nut.dose")}</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((r, i) => (
              <tr>
                <td class="nowrap full">
                  <span class="swatch" style={`background:${r.color}`} /> <strong>{t("port.pump", { n: r.slot + 1 })}</strong>
                </td>
                <td class="full" data-label={t("setup.nut.name")}>
                  <input class="input" name={`bottle${i}`} value={r.name} onInput={(e) => upd(i, "name", (e.target as HTMLInputElement).value)} />
                </td>
                <td data-label={t("setup.nut.kind")}>
                  <select class="select" value={r.kind} onChange={(e) => upd(i, "kind", (e.target as HTMLSelectElement).value)}>
                    <option value="nutrient">{t("setup.nut.kindNutrient")}</option>
                    <option value="ph_down">{t("setup.nut.kindPhDown")}</option>
                    <option value="ph_up">{t("setup.nut.kindPhUp")}</option>
                  </select>
                </td>
                <td style="width:80px" data-label={t("setup.nut.pair")}>
                  <input class="input" value={r.kind === "nutrient" ? r.pair : ""} disabled={r.kind !== "nutrient"} onInput={(e) => upd(i, "pair", (e.target as HTMLInputElement).value.toUpperCase())} />
                </td>
                <td style="width:130px" class="full" data-label={t("setup.nut.dose")}>
                  {r.kind === "nutrient" ? <NumberInput value={r.mlPerL} onValue={(v) => upd(i, "mlPerL", v)} unit="ml/L" name={`dose${i}`} /> : <span class="faint">–</span>}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {dosed.length > 0 && (
        <Field label={t("setup.nut.recipeName")}>
          <input class="input" name="recipeName" value={rname} onInput={(e) => setRname((e.target as HTMLInputElement).value)} />
        </Field>
      )}
      {named.length === 0 && <Banner tone="warn">{t("setup.nut.needOne")}</Banner>}
      {cur?.source && <p class="faint small">{t("setup.nut.source", { s: lang.value === "en" && cur.sourceEn ? cur.sourceEn : cur.source })}</p>}
    </Page>
  );
}

// ---------- 5 Einmessen (Pumpen und Sonden)

function StepCalibrate(p: { step: number; next: () => void }) {
  const st = state.value!;
  const cfg = config.value!;
  const [cal, setCal] = useState<{ id: string; name: string } | null>(null);
  const [probe, setProbe] = useState<{ dev: string; kind: ProbeKind; name: string } | null>(null);
  const assigned = canisters.value.filter((k) => k.pump);
  const allCal = assigned.length > 0 && assigned.every((k) => st.devices.find((d) => d.id === k.pump)?.info?.flowMlPerMin);
  // Alle Sondenköpfe mit Kalibrierung: ein pH/EC-Kopf oder zwei einzelne, Füllstand
  const probes = cfg.devices.flatMap((d) => probeKinds(catalog.value, d.class).map((k) => ({ dev: d.id, kind: k })));
  const probeLabel = (k: ProbeKind) => (k === "ph" ? t("setup.cal.ph") : k === "ec" ? t("setup.cal.ecProbe") : t("setup.cal.level"));
  const cals = cfg.calibrations;
  const probeRow = (dev: string, kind: ProbeKind, label: string) => (
    <div class="item">
      <span class="grow">
        <strong>{label}</strong>
        <div class="muted small">{cals[dev]?.[kind] ? t("setup.cal.calibrated") : t("setup.cal.notCalibrated")}</div>
      </span>
      <Button size="sm" variant={cals[dev]?.[kind] ? "default" : "primary"} onClick={() => setProbe({ dev, kind, name: label })}>
        {cals[dev]?.[kind] ? t("setup.cal.again") : t("setup.cal.probeCal")}
      </Button>
    </div>
  );
  return (
    <Page foot={{ step: p.step, onNext: p.next, onSkip: allCal ? undefined : p.next }} aside={<Aside terms={["prime", "calibratePump", "calibrateProbe"]} />}>
      <h2>{t("setup.cal.h")}</h2>
      <p class="muted">{t("setup.cal.intro")}</p>
      <div class="section-title">{t("setup.cal.pumps")}</div>
      <div class="list">
        {assigned.map((k) => {
          const d = st.devices.find((x) => x.id === k.pump);
          const flow = d?.info?.flowMlPerMin ?? null;
          return (
            <div class="item">
              <span class="swatch" style={`background:${k.color}`} />
              <span class="grow">
                <strong>{k.name}</strong> <span class="faint small">{d ? devicePlace(d) : ""}</span>
                <div class="muted small">{flow ? `${num(flow, 1)} ml/min` : t("setup.cal.notYet")}</div>
              </span>
              <Button size="sm" onClick={() => post(`/pumps/${k.pump}/prime`, { seconds: 5 }).then(refreshState)}>
                {t("term.prime")}
              </Button>
              <Button size="sm" variant={flow ? "default" : "primary"} onClick={() => setCal({ id: k.pump, name: k.name })}>
                {flow ? t("setup.cal.again") : t("setup.cal.calibrate")}
              </Button>
              {flow && <Check size={18} color="var(--ok)" />}
            </div>
          );
        })}
      </div>
      {!allCal && assigned.length > 0 && <Banner tone="warn">{t("setup.cal.warn")}</Banner>}
      <div class="section-title">{t("setup.cal.probes")}</div>
      {probes.length === 0 ? (
        <p class="muted">{t("setup.cal.noProbes")}</p>
      ) : (
        <div class="list">{probes.map((x) => probeRow(x.dev, x.kind, probeLabel(x.kind)))}</div>
      )}
      {cal && <PumpCalibration pump={cal.id} name={cal.name} onClose={() => setCal(null)} />}
      {probe && <ProbeCalibration device={probe.dev} kind={probe.kind} name={probe.name} onClose={() => setProbe(null)} />}
    </Page>
  );
}

// ---------- Fertig

function StepDone(p: { step: number }) {
  const st = state.value!;
  const usable = st.functions.filter((f) => f.setup === "ready" || f.setup === "limited");
  const todo = st.functions.filter((f) => f.setup === "needs_setup");
  const finish = async (to: string) => {
    await post("/setup/complete");
    await refreshConfig();
    await refreshState();
    navigate(to);
  };
  return (
    <Page foot={{ step: p.step }}>
      <h2>{t("setup.done.h")}</h2>
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
      <Banner>{t("setup.done.first")}</Banner>
      <div class="row wrap">
        <Button variant="primary" size="lg" onClick={() => finish("/mischen")}>
          <Sprout size={18} /> {t("setup.done.mix")}
        </Button>
        <Button onClick={() => finish("/")}>{t("setup.done.overview")}</Button>
      </div>
      <div class="section-title">{t("setup.done.later")}</div>
      <div class="later">
        <a href="#/geraete?tab=erweitern">
          <strong>{t("setup.done.laterDevices")}</strong>
          <span class="muted small">{t("setup.done.laterDevicesText")}</span>
        </a>
        <a href="#/geraete">
          <strong>{t("setup.done.laterProbes")}</strong>
          <span class="muted small">{t("setup.done.laterProbesText")}</span>
        </a>
        <a href="#/funktionen">
          <strong>{t("setup.done.laterFunctions")}</strong>
          <span class="muted small">{t("setup.done.laterFunctionsText")}</span>
        </a>
      </div>
    </Page>
  );
}

export function SetupWizard() {
  const st = state.value!;
  const step = Math.max(0, Math.min(DONE, Number(route.value.query.s ?? 0) || 0));
  const go = (s: number) => navigate(`/einrichtung?s=${s}`);
  const next = () => go(step + 1);
  return (
    <div class="wizard-page">
      <div class="wizard">
        <div class="row-between">
          <div class="row">
            <div class="brand-mark">
              <Sprout size={18} />
            </div>
            <strong>{t("setup.title")}</strong>
          </div>
          {st.setupDone && (
            <a class="btn sm ghost" href="#/">
              {t("setup.toApp")}
            </a>
          )}
        </div>
        <nav class="wizard-steps" aria-label={t("setup.title")}>
          {STEPS.map((s, i) => (
            <button type="button" class={`ws ${i === step ? "cur" : i < step ? "done" : ""}`} disabled={i > step} aria-current={i === step ? "step" : undefined} onClick={() => i < step && go(i)}>
              {i < step ? "✓" : i + 1} {t(s)}
            </button>
          ))}
        </nav>
        {step === 0 && <StepStart step={step} next={next} />}
        {step === 1 && <StepDevices step={step} next={next} />}
        {step === 2 && <StepTank step={step} next={next} />}
        {step === 3 && <StepNutrients step={step} next={next} />}
        {step === 4 && <StepCalibrate step={step} next={next} />}
        {step === DONE && <StepDone step={step} />}
      </div>
    </div>
  );
}
