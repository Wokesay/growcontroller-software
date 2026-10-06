// Geräte: Ports des Hubs, erkannte Geräte, Übernehmen, Kalibrieren,
// Zuordnung der Messstellen und Ausgänge, „Erweitern“.
import { useEffect, useState } from "preact/hooks";
import { Link2, PackagePlus, Pencil, Plug, Trash2 } from "lucide-preact";
import { del, patch, post, probeKinds, put, type Device } from "../api";
import { PumpCalibration, ProbeCalibration } from "../calibration";
import { dateTime, num } from "../format";
import { binding, canisters, catalog, config, refreshConfig, refreshState, state, toast } from "../store";
import { Banner, Button, Card, Field, Modal, Pill, Seg, navigate, route, setupLabel } from "../ui";
import { DeviceIcon, PortGrid, devicePlace } from "../widgets";

function DeviceCard(p: { d: Device; onCal: (d: Device, kind: string) => void }) {
  const d = p.d;
  const [edit, setEdit] = useState(false);
  const [name, setName] = useState(d.name);
  const can = canisters.value.find((k) => k.pump === d.id);
  const flow = d.info?.flowMlPerMin ?? null;
  const where = devicePlace(d);
  return (
    <div class="item" data-testid={`device-${d.id}`}>
      <div class="grow">
        <div class="row" style="gap:8px">
          <strong>{d.name || d.classLabel}</strong>
          <Pill tone={d.online ? "ok" : "bad"} dot>
            {d.online ? "verbunden" : "antwortet nicht"}
          </Pill>
          {!d.configured && <Pill tone="info">neu erkannt</Pill>}
        </div>
        <div class="muted small">
          {d.classLabel} · {where} · <span class="mono">{d.id}</span>
          {d.class === "pump_cap" && (
            <>
              {" · "}
              {flow ? `${num(flow, 1)} ml/min eingemessen` : <span style="color:var(--bad)">nicht eingemessen</span>}
              {can ? ` · auf „${can.name}“` : " · keinem Kanister zugeordnet"}
            </>
          )}
          {Object.entries(d.calibrations ?? {}).map(([k, at]) => ` · ${k === "tank_curve" ? "Kennlinie" : k} kalibriert ${at ? dateTime(at) : ""}`)}
        </div>
      </div>
      <div class="row" style="gap:6px">
        {!d.configured ? (
          <Button
            size="sm"
            variant="primary"
            onClick={async () => {
              await post(`/devices/${d.id}/accept`, { name: "" });
              await refreshConfig();
              await refreshState();
              toast(`${d.classLabel} übernommen`);
            }}
          >
            Übernehmen
          </Button>
        ) : (
          <>
            {d.class === "pump_cap" && d.online && (
              <Button size="sm" onClick={() => p.onCal(d, "pump")}>
                Einmessen
              </Button>
            )}
            {d.online &&
              probeKinds(catalog.value, d.class).map((k) => (
                <Button size="sm" onClick={() => p.onCal(d, k)}>
                  {k === "ph" ? "pH kalibrieren" : k === "ec" ? "EC kalibrieren" : "Kennlinie"}
                </Button>
              ))}
            <Button size="sm" variant="ghost" onClick={() => setEdit(true)} title="Umbenennen">
              <Pencil size={15} />
            </Button>
          </>
        )}
      </div>
      {edit && (
        <Modal
          title="Gerät umbenennen"
          onClose={() => setEdit(false)}
          footer={
            <>
              <Button
                variant="danger-soft"
                onClick={async () => {
                  await del(`/devices/${d.id}`);
                  setEdit(false);
                  await refreshConfig();
                  await refreshState();
                }}
              >
                <Trash2 size={15} /> Aus der Einrichtung entfernen
              </Button>
              <Button
                variant="primary"
                onClick={async () => {
                  await patch(`/devices/${d.id}`, { name });
                  setEdit(false);
                  await refreshConfig();
                  await refreshState();
                }}
              >
                Speichern
              </Button>
            </>
          }
        >
          <Field label="Name" hint="Zum Beispiel die Farbe des Clips oder der Ort">
            <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} />
          </Field>
        </Modal>
      )}
    </div>
  );
}

function Roles() {
  const st = state.value!;
  const cat = catalog.value!;
  const devs = config.value!.devices;
  const rolesUsed = Object.entries(cat.roles).filter(([, r]) => devs.some((d) => cat.deviceClasses[d.class]?.provides.includes(r.capability)));
  return (
    <div class="stack">
      <p class="muted">Messstellen und Ausgänge hängen an der Geräte-ID, nicht am Port. Umstecken ändert nichts.</p>
      <table class="table">
        <thead>
          <tr>
            <th>Rolle</th>
            <th>Gerät</th>
            <th class="hide-sm">Zustand</th>
          </tr>
        </thead>
        <tbody>
          {rolesUsed.map(([id, r]) => {
            const b = binding(id);
            const cands = devs.filter((d) => cat.deviceClasses[d.class]?.provides.includes(r.capability));
            const channels = (cls: string) => cat.deviceClasses[cls]?.channels ?? 1;
            const opts: [string, string][] = [["", "– nicht zugeordnet –"]];
            for (const d of cands) {
              const n = channels(d.class);
              if (n > 1) for (let c = 0; c < n; c++) opts.push([`${d.id}|${c}`, `${d.name || d.id} · Ausgang ${c + 1}`]);
              else opts.push([`${d.id}|0`, d.name || d.id]);
            }
            const reading = st.readings[id];
            return (
              <tr>
                <td>
                  <strong>{r.label}</strong>
                </td>
                <td>
                  <select
                    class="select"
                    value={b ? `${b.device}|${b.channel}` : ""}
                    onChange={async (e) => {
                      const v = (e.target as HTMLSelectElement).value;
                      try {
                        if (!v) await del(`/roles/${id}`);
                        else {
                          const [device, ch] = v.split("|");
                          await put(`/roles/${id}`, { device, channel: Number(ch) });
                        }
                        await refreshConfig();
                        await refreshState();
                        toast("Zuordnung gespeichert");
                      } catch (x: any) {
                        toast(x.message, "error");
                      }
                    }}
                  >
                    {opts.map(([v, l]) => (
                      <option value={v}>{l}</option>
                    ))}
                  </select>
                </td>
                <td class="hide-sm small muted">{reading ? reading.reason.text : b ? (st.outputs[id] ? "an" : "aus") : ""}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}

function Expand() {
  const st = state.value!;
  const cat = catalog.value!;
  const unavailable = st.functions.filter((f) => f.setup === "unavailable");
  const byShop = new Map<string, string[]>();
  for (const f of unavailable) for (const c of f.checks) for (const s of c.shop ?? []) byShop.set(s, [...(byShop.get(s) ?? []), f.label]);
  return (
    <div class="stack">
      <p class="muted">Was du mit weiteren Geräten zusätzlich könntest. Einstecken, übernehmen – die Funktionen erscheinen dann unter „Funktionen“.</p>
      {byShop.size === 0 && <Banner tone="ok">Alle Funktionen dieses Katalogs sind mit deiner Hardware möglich.</Banner>}
      <div class="grid">
        {[...byShop.entries()].map(([cls, fns]) => {
          const d = cat.deviceClasses[cls];
          return (
            <div class="card flat">
              <div class="row-between">
                <h3>{d?.label ?? cls}</h3>
                <Pill tone="neutral">Stufe {d?.stage}</Pill>
              </div>
              <p class="muted small">{d?.text}</p>
              <div class="chips" style="margin-top:8px">
                {[...new Set(fns)].map((f) => (
                  <span class="chip">{f}</span>
                ))}
              </div>
            </div>
          );
        })}
      </div>
      <div class="section-title">Alle Funktionen</div>
      <div class="list">
        {st.functions.map((f) => (
          <div class="item">
            <span class="grow">{f.label}</span>
            <Pill tone={setupLabel[f.setup][1]}>{setupLabel[f.setup][0]}</Pill>
          </div>
        ))}
      </div>
    </div>
  );
}

export function DevicesPage() {
  const st = state.value!;
  const q = route.value.query;
  const [tab, setTab] = useState<"geraete" | "zuordnung" | "erweitern">((q.tab as any) || "geraete");
  const [cal, setCal] = useState<{ d: Device; kind: string } | null>(null);
  useEffect(() => {
    if (q.tab) setTab(q.tab as any);
    if (q.pump) {
      const d = st.devices.find((x) => x.id === q.pump);
      if (d) setCal({ d, kind: "pump" });
    }
    if (q.cal) {
      const [dev, kind] = decodeURIComponent(q.cal).split(":");
      const d = st.devices.find((x) => x.id === dev);
      if (d) setCal({ d, kind });
    }
  }, [q.tab, q.pump, q.cal]);
  const newOnes = st.devices.filter((d) => !d.configured && d.online);
  const blocks = st.devices.filter((d) => d.class !== "pump_cap");
  return (
    <div class="stack">
      <Card title="Anschlüsse am Hub" icon={<Plug size={18} />} actions={<span class="faint small">RJ45, jeder Anschluss mit eigener Prüfmessung</span>}>
        <PortGrid />
      </Card>
      <Seg
        value={tab}
        onChange={(v) => {
          setTab(v);
          navigate(`/geraete?tab=${v}`);
        }}
        options={[
          ["geraete", "Geräte"],
          ["zuordnung", "Zuordnung"],
          ["erweitern", "Erweitern"],
        ]}
      />
      {tab === "geraete" && (
        <>
          {newOnes.length > 0 && (
            <Banner>
              <div class="row-between">
                <span>
                  {newOnes.length === 1 ? "Ein neues Gerät wurde erkannt." : `${newOnes.length} neue Geräte wurden erkannt.`} Übernehmen, damit der Hub sie nutzt.
                </span>
                <Button
                  size="sm"
                  variant="primary"
                  onClick={async () => {
                    for (const d of newOnes) await post(`/devices/${d.id}/accept`, { name: "" });
                    await refreshConfig();
                    await refreshState();
                    toast("Geräte übernommen");
                  }}
                >
                  <PackagePlus size={15} /> Alle übernehmen
                </Button>
              </div>
            </Banner>
          )}
          {blocks.map((b) => {
            const caps = st.devices.filter((d) => d.parent === b.id);
            return (
              <Card title={b.name || b.classLabel} icon={<DeviceIcon cls={b.class} />}>
                <div class="list">
                  <DeviceCard d={b} onCal={(d, kind) => setCal({ d, kind })} />
                  {caps.map((c) => (
                    <DeviceCard d={c} onCal={(d, kind) => setCal({ d, kind })} />
                  ))}
                  {b.class === "dosing_block" && caps.length === 0 && <p class="muted">Keine Pumpe gesteckt.</p>}
                </div>
              </Card>
            );
          })}
        </>
      )}
      {tab === "zuordnung" && (
        <Card title="Zuordnung" icon={<Link2 size={18} />}>
          <Roles />
        </Card>
      )}
      {tab === "erweitern" && (
        <Card title="Erweitern" icon={<PackagePlus size={18} />}>
          <Expand />
        </Card>
      )}
      {cal && cal.kind === "pump" && <PumpCalibration pump={cal.d.id} name={canisters.value.find((k) => k.pump === cal.d.id)?.name ?? cal.d.name} onClose={() => { setCal(null); navigate("/geraete"); }} />}
      {cal && cal.kind !== "pump" && <ProbeCalibration device={cal.d.id} kind={cal.kind as any} name={cal.d.name} onClose={() => { setCal(null); navigate("/geraete"); }} />}
    </div>
  );
}
