// SPDX-License-Identifier: AGPL-3.0-or-later
// Verlauf: Kurven mit Zielband und Dosier-Markierungen, Ereignisse, Export.
import { useEffect, useState } from "preact/hooks";
import { Download, History, LineChart } from "lucide-preact";
import { get, type HubEvent, type SeriesData } from "../api";
import { TimeChart, type Marker } from "../chart";
import { hasRole, state } from "../store";
import { t, type TextKey } from "../i18n";
import { Card, Seg } from "../ui";
import { EventList } from "../widgets";

const RANGES: [string, TextKey, number][] = [
  ["6h", "history.range6h", 6 * 3600],
  ["24h", "history.range24h", 24 * 3600],
  ["7d", "history.range7d", 7 * 86400],
  ["30d", "history.range30d", 30 * 86400],
  ["1y", "history.range1y", 365 * 86400],
];

const FILTERS: [string, TextKey][] = [
  ["", "common.all"],
  ["dose", "history.filterDose"],
  ["mix", "history.filterMix"],
  ["control", "overview.control"],
  ["alarm", "history.filterAlarm"],
  ["block", "tank.locks"],
  ["tank", "setup.tank.h"],
  ["calibration", "history.filterCalibration"],
  ["device", "nav.devices"],
  ["config", "nav.settings"],
  ["auth", "history.filterAuth"],
];

export function HistoryPage() {
  const st = state.value!;
  const [range, setRange] = useState("24h");
  const [data, setData] = useState<Record<string, SeriesData>>({});
  const [doses, setDoses] = useState<HubEvent[]>([]);
  const [events, setEvents] = useState<HubEvent[]>([]);
  const [filter, setFilter] = useState("");
  const span = RANGES.find((r) => r[0] === range)![2];
  const series = ["tank.ph", "tank.ec", "tank.water_temp", "tank.level"].filter((s) => hasRole(s));
  if (!series.includes("tank.level")) series.push("tank.volume");
  // Raumklima, wenn ein Sensor zugeordnet ist; VPD wird daraus abgeleitet
  const climate = ["zone.air_temp", "zone.humidity", "zone.vpd", "zone.co2"].filter((s) => st.readings[s] && st.readings[s].quality !== "not_bound");
  series.push(...climate);

  useEffect(() => {
    let alive = true;
    const load = async () => {
      const now = state.value?.now ?? Math.floor(Date.now() / 1000);
      const from = now - span;
      const [h, d] = await Promise.all([
        get<{ series: SeriesData[] }>(`/history?series=${series.join(",")}&from=${from}&to=${now}&points=700`),
        get<{ events: HubEvent[] }>(`/events?type=dose&from=${from}&limit=1000`),
      ]);
      if (!alive) return;
      setData(Object.fromEntries(h.series.map((s) => [s.series, s])));
      setDoses(d.events);
    };
    load().catch(() => {});
    const timer = setInterval(() => load().catch(() => {}), 30000);
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, [range]);

  useEffect(() => {
    get<{ events: HubEvent[] }>(`/events?limit=150${filter ? `&type=${filter}` : ""}`).then((r) => setEvents(r.events)).catch(() => {});
  }, [filter, st.eventId]);

  const ph = st.controllers.ph.info as any;
  const ec = st.controllers.ec.info as any;
  const phBand: [number, number] | null = ph?.target != null ? [ph.target - ph.tolerance, ph.target + ph.tolerance] : null;
  const ecBand: [number, number] | null = ec?.target != null ? [ec.target - ec.tolerance, ec.target + ec.tolerance] : null;
  const markers = (purposes: string[]): Marker[] =>
    doses.filter((e) => purposes.includes(e.data?.purpose)).map((e) => ({ t: e.ts, label: e.title, color: e.data?.purpose === "ph" ? "--ph" : "--ec" }));
  const now = st.now;
  const csv = `/api/v1/export.csv?series=${series.join(",")}&from=${now - span}&to=${now}`;

  return (
    <div class="stack">
      <div class="page-head">
        <div>
          <p class="muted">{t("history.intro")}</p>
        </div>
        <div class="row">
          <Seg value={range} onChange={setRange} options={RANGES.map(([v, l]) => [v, t(l)] as [string, string])} />
          <a class="btn sm" href={csv} download>
            <Download size={15} /> CSV
          </a>
        </div>
      </div>
      <div class="charts">
        {hasRole("tank.ph") && (
          <Card title="pH" icon={<LineChart size={18} />}>
            <TimeChart data={data["tank.ph"] ?? null} color="--ph" label="pH" decimals={2} band={phBand} markers={markers(["ph", "mix"])} />
          </Card>
        )}
        {hasRole("tank.ec") && (
          <Card title="EC" icon={<LineChart size={18} />}>
            <TimeChart data={data["tank.ec"] ?? null} color="--ec" label="EC" unit="mS/cm" decimals={2} band={ecBand} markers={markers(["ec", "mix"])} />
          </Card>
        )}
        {hasRole("tank.water_temp") && (
          <Card title={t("history.waterTemp")} icon={<LineChart size={18} />}>
            <TimeChart data={data["tank.water_temp"] ?? null} color="--temp" label={t("common.water")} unit="°C" decimals={1} />
          </Card>
        )}
        <Card title={hasRole("tank.level") ? t("area.tankLevel") : t("history.volumeFromMixes")} icon={<LineChart size={18} />}>
          <TimeChart data={data[hasRole("tank.level") ? "tank.level" : "tank.volume"] ?? null} color="--level" label={t("history.volume")} unit="L" decimals={1} />
        </Card>
        {climate.includes("zone.air_temp") && (
          <Card title={t("history.airTemp")} icon={<LineChart size={18} />}>
            <TimeChart data={data["zone.air_temp"] ?? null} color="--air" label={t("area.airTemp")} unit="°C" decimals={1} />
          </Card>
        )}
        {climate.includes("zone.humidity") && (
          <Card title={t("history.humidity")} icon={<LineChart size={18} />}>
            <TimeChart data={data["zone.humidity"] ?? null} color="--rh" label={t("area.humidity")} unit="%" decimals={0} />
          </Card>
        )}
        {climate.includes("zone.vpd") && (
          <Card title={t("history.vpd")} icon={<LineChart size={18} />}>
            <TimeChart data={data["zone.vpd"] ?? null} color="--vpd" label="VPD" unit="kPa" decimals={2} />
          </Card>
        )}
        {climate.includes("zone.co2") && (
          <Card title="CO2" icon={<LineChart size={18} />}>
            <TimeChart data={data["zone.co2"] ?? null} color="--co2" label="CO2" unit="ppm" decimals={0} />
          </Card>
        )}
      </div>
      <Card title={t("history.events")} icon={<History size={18} />}>
        <div class="chips" style="margin-bottom:8px">
          {FILTERS.map(([v, l]) => (
            <button class={`chip ${filter === v ? "on" : ""}`} onClick={() => setFilter(v)}>
              {t(l)}
            </button>
          ))}
        </div>
        <EventList events={events} groupByDay />
      </Card>
    </div>
  );
}
