// SPDX-License-Identifier: AGPL-3.0-or-later
// Bereiche der Anlage: Klima, Licht, Bewässerung. Jede Seite zeigt die
// Messwerte des Bereichs und seine Schaltausgänge mit Handbetrieb. Alle
// Sperren gelten auch hier (Aktor-Gateway); eine abgelehnte Schaltung zeigt
// den Grund. Automatische Funktionen kommen auf diese Seiten, sobald es sie
// gibt.
import type { ComponentChildren } from "preact";
import { Droplets, Fan, Sun, Thermometer, Zap } from "lucide-preact";
import { post } from "../api";
import { num } from "../format";
import { t, type TextKey } from "../i18n";
import { binding, catalog, refreshState, state } from "../store";
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
        <a class="btn sm" href="#/geraete">
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

function Outputs(p: { roles: string[]; title: TextKey; icon: ComponentChildren }) {
  return (
    <Card title={t(p.title)} icon={p.icon}>
      <div class="list">
        {p.roles.map((r) => (
          <OutputRow role={r} />
        ))}
      </div>
      <p class="faint small">{t("area.manualNote")}</p>
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
      <Card title={t("area.climate")} icon={<Thermometer size={18} />}>
        {hasSensor ? (
          <div class="stack">
            <ClimateTiles />
            <Term topic="vpd" />
          </div>
        ) : (
          <Empty
            icon={<Thermometer size={34} />}
            title={t("area.noClimateSensor")}
            text={t("area.noClimateSensorText")}
            action={
              <a class="btn" href="#/geraete?tab=erweitern">
                {t("area.expand")}
              </a>
            }
          />
        )}
      </Card>
      <Outputs roles={["zone.exhaust", "zone.circulation_fan", "zone.humidifier", "zone.dehumidifier"]} title="area.climateDevices" icon={<Fan size={18} />} />
    </div>
  );
}

export function LightPage() {
  return (
    <div class="stack">
      <Outputs roles={["zone.light"]} title="nav.light" icon={<Sun size={18} />} />
      <Banner>{t("area.lightNote")}</Banner>
    </div>
  );
}

export function IrrigationPage() {
  const st = state.value!;
  return (
    <div class="stack">
      <Outputs roles={["zone.irrigation_pump"]} title="nav.irrigation" icon={<Droplets size={18} />} />
      {shown("tank.level") ? (
        <div class="grid-3">
          <MetricTile label={t("area.tankLevel")} reading={st.readings["tank.level"]} color="--level" />
        </div>
      ) : (
        <Banner tone="warn">{t("area.irrigationNeedsLevel")}</Banner>
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
