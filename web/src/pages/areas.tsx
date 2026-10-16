// SPDX-License-Identifier: AGPL-3.0-or-later
// Areas of the grow: climate, light, watering. Each page shows the area's
// readings and its outputs to switch by hand. Every lock applies here too
// (actuator gateway); a refused switch shows the reason. Automatic functions
// come to these pages once they exist; until then each page says "manual"
// (SD-056). In the Home Assistant app nothing is switched here; the page says
// what was chosen for the area instead (SD-037).
import type { ComponentChildren } from "preact";
import { Droplets, Fan, Hand, Sun, Thermometer, Zap } from "lucide-preact";
import { post, put, type ControlArea, type ControlMode } from "../api";
import { num } from "../format";
import { t, type TextKey } from "../i18n";
import { binding, catalog, config, hasRole, readOnly, refreshConfig, refreshState, state, toast, toastError } from "../store";
import { Banner, Button, Card, Empty, Pill, Term } from "../ui";
import { MetricTile, useSparks } from "../widgets";

/** Schaltausgang einer Rolle: Zustand, Gerät, Leistung, Handbetrieb. */
export function OutputRow(p: { role: string; compact?: boolean }) {
  const cat = catalog.value;
  const st = state.value!;
  const rd = cat?.roles[p.role];
  if (!rd) return null;
  const b = binding(p.role);
  if (!b?.device) {
    if (p.compact) return null;
    return (
      <div class="item" data-testid={`output-${p.role}`}>
        <span class="outlet-state">
          <Zap size={14} />
        </span>
        <span class="grow">
          <strong>{rd.label}</strong>
          <div class="muted small">{t("area.unassigned")}</div>
        </span>
        <a class="btn sm" href="#/geraete?tab=geraete">
          {t("area.assign")}
        </a>
      </div>
    );
  }
  const on: boolean | undefined = st.outputs[p.role];
  const dev = st.devices.find((d) => d.id === b.device);
  const net = !!dev?.class.startsWith("shelly_");
  const power = net ? dev?.info?.outlets?.[b.channel]?.powerW ?? null : null;
  const where = dev ? `${dev.name || dev.classLabel} · ${net ? t("port.outlet", { n: b.channel + 1 }) : t("port.out12v", { n: b.channel + 1 })}` : b.device;
  return (
    <div class="item" data-testid={`output-${p.role}`}>
      <span class={`outlet-state ${on ? "on" : ""}`}>
        <Zap size={14} />
      </span>
      <span class="grow">
        <strong>{rd.label}</strong>
        <div class="muted small">
          {where}
          {power !== null ? ` · ${num(power, 0)} W` : ""}
        </div>
      </span>
      <Pill tone={on === undefined ? "bad" : on ? "ok" : "neutral"}>{on === undefined ? t("area.unknown") : on ? t("common.on") : t("common.off")}</Pill>
      {!p.compact && (
        <Button size="sm" onClick={() => post(`/roles/${p.role}/switch`, { on: !on }).then(refreshState)} data-testid={`switch-${p.role}`}>
          {on ? t("area.turnOff") : t("area.turnOn")}
        </Button>
      )}
    </div>
  );
}

/** A smart plug has been added: on its card under Devices the user chooses what is plugged in. */
const plugAdded = () => !!config.value?.devices.some((d) => catalog.value?.deviceClasses[d.class]?.provides.includes("switch.mains"));

type ManualArea = Extract<ControlArea, "climate" | "light" | "irrigation">;

/** The outputs of an area; with none of them assigned, what to connect and where to assign it (SD-056). */
function Outputs(p: { roles: string[]; title: TextKey; icon: (size: number) => ComponentChildren; area: ManualArea; note?: TextKey }) {
  if (!p.roles.some(hasRole)) {
    const plug = plugAdded();
    return (
      <Card title={t(p.title)} icon={p.icon(18)}>
        <div data-testid={`area-empty-${p.area}`}>
          <Empty
            icon={p.icon(34)}
            title={t(`area.empty.${p.area}`)}
            text={
              <>
                {t(`area.empty.${p.area}Text`)}
                {!plug && (
                  <>
                    <br />
                    {t("area.noPlug")}
                  </>
                )}
              </>
            }
            action={
              <a class="btn" href="#/geraete?tab=geraete">
                {t(plug ? "area.assign" : "area.toDevices")}
              </a>
            }
          />
        </div>
      </Card>
    );
  }
  return (
    <Card title={t(p.title)} icon={p.icon(18)}>
      <div class="list">
        {p.roles.map((r) => (
          <OutputRow role={r} />
        ))}
      </div>
      {p.note && <p class="muted small">{t(p.note)}</p>}
    </Card>
  );
}

/** Climate, light and watering only show values and switch by hand so far: the page says so, and what to keep running (SD-056). */
function ManualNote(p: { area: ManualArea }) {
  return (
    <Banner icon={<Hand size={18} />}>
      <div data-testid={`manual-${p.area}`}>
        <strong>{t("area.manual")}:</strong> {t(`area.manual.${p.area}`)}
      </div>
    </Banner>
  );
}

const MODES: ControlMode[] = ["auto", "outside"];

/** Automatic or outside the control for one area (SD-037): native radios, so arrow keys and screen readers work. */
export function ControlChoice(p: { area: ControlArea; value?: ControlMode; onChange: (m: ControlMode) => void; label?: string; labelledBy?: string; describedBy?: string }) {
  return (
    <span class="ctl-modes" role="radiogroup" aria-label={p.label} aria-labelledby={p.labelledBy} aria-describedby={p.describedBy}>
      {MODES.map((m) => (
        <label class={`radio-pill ${p.value === m ? "on" : ""}`}>
          <input type="radio" name={`ctl-${p.area}`} value={m} checked={p.value === m} onChange={() => p.onChange(m)} />
          {t(`control.mode.${m}`)}
        </label>
      ))}
    </span>
  );
}

/** Read-only (Home Assistant): no switches that switch nothing, but what the area is left to, changed in one tap. */
function ControlNote(p: { area: ControlArea; title: TextKey; icon: ComponentChildren }) {
  const mode = config.value?.system.control?.[p.area];
  const choose = async (m: ControlMode) => {
    try {
      await put("/system", { control: { [p.area]: m } });
      toast(t("settings.saved"));
    } catch (x: any) {
      toastError(x);
    } finally {
      await refreshConfig().catch(() => undefined);  // also puts the radios back if saving failed; offline, the toast has said so
    }
  };
  return (
    <Card title={t(p.title)} icon={p.icon}>
      <div class="stack-sm" data-testid={`control-note-${p.area}`}>
        <p aria-live="polite">{mode === "auto" ? t("control.page.auto") : mode === "outside" ? t("control.page.outside") : t("control.page.none")}</p>
        <ControlChoice area={p.area} value={mode} onChange={choose} label={t(`control.${p.area}`)} />
      </div>
    </Card>
  );
}

const shown = (role: string) => state.value?.readings[role] && state.value.readings[role].quality !== "not_bound";

/** Raumklima als Kacheln; leer, wenn kein Klimasensor zugeordnet ist. */
export function ClimateTiles() {
  const st = state.value!;
  const series = ["zone.air_temp", "zone.humidity", "zone.vpd", "zone.co2"].filter(shown);
  const sparks = useSparks(series);
  if (!series.length) return null;
  return (
    <div class="grid-3">
      {shown("zone.air_temp") && <MetricTile label={t("area.airTemp")} reading={st.readings["zone.air_temp"]} color="--air" spark={sparks["zone.air_temp"]} />}
      {shown("zone.humidity") && <MetricTile label={t("area.humidity")} reading={st.readings["zone.humidity"]} color="--rh" spark={sparks["zone.humidity"]} />}
      {shown("zone.vpd") && <MetricTile label="VPD" reading={st.readings["zone.vpd"]} color="--vpd" spark={sparks["zone.vpd"]} />}
      {shown("zone.co2") && <MetricTile label="CO2" reading={st.readings["zone.co2"]} color="--co2" spark={sparks["zone.co2"]} />}
    </div>
  );
}

export function ClimatePage() {
  const hasSensor = shown("zone.air_temp") || shown("zone.humidity");
  return (
    <div class="stack">
      {!readOnly.value && <ManualNote area="climate" />}
      <Card title={t("area.climate")} icon={<Thermometer size={18} />}>
        {hasSensor ? (
          <div class="stack">
            <ClimateTiles />
            <Term topic="vpd" />
          </div>
        ) : (
          <Empty
            icon={<Thermometer size={34} />}
            title={t(readOnly.value ? "area.noClimateSensorHa" : "area.noClimateSensor")}
            text={t(readOnly.value ? "area.noClimateSensorHaText" : "area.noClimateSensorText")}
            action={
              readOnly.value ? (  // no hardware to buy in the Home Assistant app
                <a class="btn" href="#/geraete?tab=zuordnung">
                  {t("area.chooseSensor")}
                </a>
              ) : (
                <a class="btn" href="#/geraete?tab=erweitern">
                  {t("area.expand")}
                </a>
              )
            }
          />
        )}
      </Card>
      {readOnly.value ? (
        <ControlNote area="climate" title="area.climateDevices" icon={<Fan size={18} />} />
      ) : (
        <Outputs roles={["zone.exhaust", "zone.circulation_fan", "zone.humidifier", "zone.dehumidifier"]} title="area.climateDevices" icon={(n) => <Fan size={n} />} area="climate" note="area.climateNote" />
      )}
    </div>
  );
}

export function LightPage() {
  if (readOnly.value) return <ControlNote area="light" title="nav.light" icon={<Sun size={18} />} />;
  return (
    <div class="stack">
      <ManualNote area="light" />
      <Outputs roles={["zone.light"]} title="nav.light" icon={(n) => <Sun size={n} />} area="light" />
    </div>
  );
}

export function IrrigationPage() {
  const st = state.value!;
  return (
    <div class="stack">
      {readOnly.value ? (
        <ControlNote area="irrigation" title="nav.irrigation" icon={<Droplets size={18} />} />
      ) : (
        <>
          <ManualNote area="irrigation" />
          <Outputs roles={["zone.irrigation_pump"]} title="nav.irrigation" icon={(n) => <Droplets size={n} />} area="irrigation" />
        </>
      )}
      {shown("tank.level") ? (
        <div class="grid-3">
          <MetricTile label={t("area.tankLevel")} reading={st.readings["tank.level"]} color="--level" />
        </div>
      ) : (
        !readOnly.value && hasRole("zone.irrigation_pump") && <Banner tone="warn">{t("area.irrigationNeedsLevel")}</Banner>  // only a pump the hub switches is protected (RAT-068)
      )}
    </div>
  );
}

/** Übersicht: alle zugeordneten Schaltausgänge auf einen Blick. */
export function OutputsOverview() {
  const cat = catalog.value;
  if (!cat) return null;
  const roles = Object.entries(cat.roles)
    .filter(([id, r]) => r.profile && binding(id)?.device)
    .map(([id]) => id);
  if (!roles.length) return null;
  return (
    <Card title={t("area.outputs")} icon={<Zap size={18} />}>
      <div class="list">
        {roles.map((r) => (
          <OutputRow role={r} compact />
        ))}
      </div>
    </Card>
  );
}
