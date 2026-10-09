// SPDX-License-Identifier: AGPL-3.0-or-later
// Geräte: Ports des Hubs, erkannte Geräte, Übernehmen, Kalibrieren,
// Zuordnung der Messstellen und Ausgänge, „Erweitern“.
import { useEffect, useState } from "preact/hooks";
import { Link2, PackagePlus, Pencil, Plug, Trash2 } from "lucide-preact";
import { del, patch, post, probeKinds, put, type Device, type ProbeKind } from "../api";
import { PumpCalibration, ProbeCalibration } from "../calibration";
import { dateTime, num } from "../format";
import { msg, t } from "../i18n";
import { binding, canisters, catalog, config, refreshConfig, refreshState, state, toast } from "../store";
import { Banner, Button, Card, Field, Modal, Pill, Seg, navigate, route, setupLabel } from "../ui";
import { DeviceIcon, OutletRoles, PortGrid, devicePlace } from "../widgets";
import { HaRoles, isHa } from "../ha";

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
            {d.online ? t("devices.online") : t("devices.offline")}
          </Pill>
          {!d.configured && <Pill tone="info">{t("devices.newFound")}</Pill>}
        </div>
        <div class="muted small">
          {d.classLabel} · {where} · <span class="mono">{d.id}</span>
          {d.class === "pump_cap" && (
            <>
              {" · "}
              {flow ? t("devices.flowCalibrated", { flow: num(flow, 1) }) : <span style="color:var(--bad)">{t("devices.notCalibrated")}</span>}
              {can ? ` · ${t("devices.onBottle", { name: can.name })}` : ` · ${t("devices.noBottle")}`}
            </>
          )}
          {Object.entries(d.calibrations ?? {}).map(([k, at]) => ` · ${k === "tank_curve" ? t("devices.curveCalibrated", { at: at ? dateTime(at) : "" }) : t("devices.kindCalibrated", { kind: k, at: at ? dateTime(at) : "" })}`)}
        </div>
        {d.class.startsWith("shelly_") && <OutletRoles d={d} />}
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
              toast(t("devices.accepted", { name: d.classLabel }));
            }}
          >
            {t("devices.accept")}
          </Button>
        ) : (
          <>
            {d.class === "pump_cap" && d.online && (
              <Button size="sm" onClick={() => p.onCal(d, "pump")}>
                {t("setup.cal.calibrate")}
              </Button>
            )}
            {d.online &&
              probeKinds(catalog.value, d.class).map((k) => (
                <Button size="sm" onClick={() => p.onCal(d, k)}>
                  {k === "ph" ? t("devices.calPh") : k === "ec" ? t("devices.calEc") : t("devices.curve")}
                </Button>
              ))}
            <Button size="sm" variant="ghost" onClick={() => setEdit(true)} title={t("devices.rename")}>
              <Pencil size={15} />
            </Button>
          </>
        )}
      </div>
      {edit && (
        <Modal
          title={t("devices.renameTitle")}
          onClose={() => setEdit(false)}
          footer={
            <>
              {/* A sensor from Home Assistant goes through Assignment, which also stops reading it. */}
              {d.id.startsWith("ha.") ? (
                <span class="muted small grow">{t("ha.removeHint")}</span>
              ) : (
                <Button
                  variant="danger-soft"
                  onClick={async () => {
                    await del(`/devices/${d.id}`);
                    setEdit(false);
                    await refreshConfig();
                    await refreshState();
                  }}
                >
                  <Trash2 size={15} /> {t("devices.remove")}
                </Button>
              )}
              <Button
                variant="primary"
                onClick={async () => {
                  await patch(`/devices/${d.id}`, { name });
                  setEdit(false);
                  await refreshConfig();
                  await refreshState();
                }}
              >
                {t("common.save")}
              </Button>
            </>
          }
        >
          <Field label={t("common.name")} hint={t("devices.nameHint")}>
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
      <p class="muted">{t("devices.rolesIntro")}</p>
      <table class="table">
        <thead>
          <tr>
            <th>{t("devices.colRole")}</th>
            <th>{t("devices.colDevice")}</th>
            <th class="hide-sm">{t("devices.colState")}</th>
          </tr>
        </thead>
        <tbody>
          {rolesUsed.map(([id, r]) => {
            const b = binding(id);
            const cands = devs.filter((d) => cat.deviceClasses[d.class]?.provides.includes(r.capability));
            const channels = (cls: string) => cat.deviceClasses[cls]?.channels ?? 1;
            const opts: [string, string][] = [["", t("devices.unassigned")]];
            for (const d of cands) {
              const n = channels(d.class);
              const what = (c: number) => (d.class.startsWith("shelly_") ? t("port.outlet", { n: c + 1 }) : t("devices.output", { n: c + 1 }));
              if (n > 1) for (let c = 0; c < n; c++) opts.push([`${d.id}|${c}`, `${d.name || d.id} · ${what(c)}`]);
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
                        toast(t("devices.saved"));
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
                <td class="hide-sm small muted">{reading ? msg(reading.reason) : b ? (st.outputs[id] ? t("common.on") : t("common.off")) : ""}</td>
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
      <p class="muted">{t("devices.expandIntro")}</p>
      {byShop.size === 0 && <Banner tone="ok">{t("devices.allPossible")}</Banner>}
      <div class="grid">
        {[...byShop.entries()].map(([cls, fns]) => {
          const d = cat.deviceClasses[cls];
          return (
            <div class="card flat">
              <div class="row-between">
                <h3>{d?.label ?? cls}</h3>
                <Pill tone="neutral">{t("devices.stage", { n: d?.stage ?? "" })}</Pill>
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
      <div class="section-title">{t("devices.allFunctions")}</div>
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
  // A Home Assistant hub starts on Assignment until a sensor is chosen.
  const haFirst = isHa.value && !Object.entries(catalog.value?.roles ?? {}).some(([r, def]) => def.capability?.startsWith("measure.") && binding(r));
  // Expand lists hardware to plug in; a Home Assistant hub has none, so links to it land on Assignment.
  const tabOf = (v?: string) => (isHa.value && v === "erweitern" ? "zuordnung" : v) as "geraete" | "zuordnung" | "erweitern" | undefined;
  const [tab, setTab] = useState<"geraete" | "zuordnung" | "erweitern">(tabOf(q.tab) || (haFirst ? "zuordnung" : "geraete"));
  const [cal, setCal] = useState<{ d: Device; kind: string } | null>(null);
  useEffect(() => {
    if (q.tab) setTab(tabOf(q.tab)!);
    if (q.pump) {
      const d = st.devices.find((x) => x.id === q.pump);
      if (d) setCal({ d, kind: "pump" });
    }
    if (q.cal) {
      const [dev, kind] = decodeURIComponent(q.cal).split(":");
      const d = st.devices.find((x) => x.id === dev);
      // Only a kind the device offers; a device calibrated elsewhere offers none
      if (d && probeKinds(catalog.value, d.class).includes(kind as ProbeKind)) setCal({ d, kind });
    }
  }, [q.tab, q.pump, q.cal]);
  const newOnes = st.devices.filter((d) => !d.configured && d.online);
  const blocks = st.devices.filter((d) => d.class !== "pump_cap");
  return (
    <div class="stack">
      {!isHa.value && (
        <Card title={t("setup.dev.hubPorts")} icon={<Plug size={18} />} actions={<span class="faint small">{t("devices.portsNote")}</span>}>
          <PortGrid />
        </Card>
      )}
      <Seg
        value={tab}
        onChange={(v) => {
          setTab(v);
          navigate(`/geraete?tab=${v}`);
        }}
        options={
          [
            ["geraete", t("nav.devices")],
            ["zuordnung", t("devices.tabAssign")],
            ...(isHa.value ? [] : [["erweitern", t("devices.tabExpand")]]),
          ] as ["geraete" | "zuordnung" | "erweitern", string][]
        }
      />
      {tab === "geraete" && (
        <>
          {newOnes.length > 0 && (
            <Banner>
              <div class="row-between">
                <span>
                  {newOnes.length === 1 ? t("devices.newOne") : t("devices.newMany", { n: newOnes.length })} {t("devices.acceptHint")}
                </span>
                <Button
                  size="sm"
                  variant="primary"
                  onClick={async () => {
                    for (const d of newOnes) await post(`/devices/${d.id}/accept`, { name: "" });
                    await refreshConfig();
                    await refreshState();
                    toast(t("devices.acceptedAll"));
                  }}
                >
                  <PackagePlus size={15} /> {t("devices.acceptAll")}
                </Button>
              </div>
            </Banner>
          )}
          {isHa.value && blocks.length === 0 && (
            <div class="stack">
              <p class="muted">{t("ha.devicesEmpty")}</p>
              <div>
                <Button
                  variant="primary"
                  onClick={() => {
                    setTab("zuordnung");
                    navigate("/geraete?tab=zuordnung");
                  }}
                >
                  {t("ha.bannerChooseLink")}
                </Button>
              </div>
            </div>
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
                  {b.class === "dosing_block" && caps.length === 0 && <p class="muted">{t("devices.noPump")}</p>}
                </div>
              </Card>
            );
          })}
        </>
      )}
      {tab === "zuordnung" && (
        <Card title={isHa.value ? t("ha.title") : t("devices.tabAssign")} icon={<Link2 size={18} />}>
          {isHa.value ? <HaRoles /> : <Roles />}
        </Card>
      )}
      {tab === "erweitern" && !isHa.value && (
        <Card title={t("devices.tabExpand")} icon={<PackagePlus size={18} />}>
          <Expand />
        </Card>
      )}
      {cal && cal.kind === "pump" && <PumpCalibration pump={cal.d.id} name={canisters.value.find((k) => k.pump === cal.d.id)?.name ?? cal.d.name} onClose={() => { setCal(null); navigate("/geraete"); }} />}
      {cal && cal.kind !== "pump" && <ProbeCalibration device={cal.d.id} kind={cal.kind as any} name={cal.d.name} onClose={() => { setCal(null); navigate("/geraete"); }} />}
    </div>
  );
}
