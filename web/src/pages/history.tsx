// Verlauf: Kurven mit Zielband und Dosier-Markierungen, Ereignisse, Export.
import { useEffect, useState } from "preact/hooks";
import { Download, History, LineChart } from "lucide-preact";
import { get, type HubEvent, type SeriesData } from "../api";
import { TimeChart, type Marker } from "../chart";
import { hasRole, state } from "../store";
import { Card, Seg } from "../ui";
import { EventList } from "../widgets";

const RANGES: [string, string, number][] = [
  ["6h", "6 h", 6 * 3600],
  ["24h", "24 h", 24 * 3600],
  ["7d", "7 T", 7 * 86400],
  ["30d", "30 T", 30 * 86400],
  ["1y", "1 J", 365 * 86400],
];

const FILTERS: [string, string][] = [
  ["", "Alle"],
  ["dose", "Dosierung"],
  ["mix", "Mischläufe"],
  ["control", "Regelung"],
  ["alarm", "Alarme"],
  ["block", "Sperren"],
  ["tank", "Tank"],
  ["calibration", "Kalibrierung"],
  ["device", "Geräte"],
  ["config", "Einstellungen"],
  ["auth", "Anmeldung"],
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
    const t = setInterval(() => load().catch(() => {}), 30000);
    return () => {
      alive = false;
      clearInterval(t);
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
          <p class="muted">Gestrichelte Linien zeigen Dosierungen. Der grüne Streifen ist das Zielband. Lücken sind fehlende Werte, nicht 0.</p>
        </div>
        <div class="row">
          <Seg value={range} onChange={setRange} options={RANGES.map(([v, l]) => [v, l] as [string, string])} />
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
          <Card title="Wassertemperatur" icon={<LineChart size={18} />}>
            <TimeChart data={data["tank.water_temp"] ?? null} color="--temp" label="Wasser" unit="°C" decimals={1} />
          </Card>
        )}
        <Card title={hasRole("tank.level") ? "Füllstand" : "Volumen (aus Mischläufen)"} icon={<LineChart size={18} />}>
          <TimeChart data={data[hasRole("tank.level") ? "tank.level" : "tank.volume"] ?? null} color="--level" label="Volumen" unit="L" decimals={1} />
        </Card>
        {climate.includes("zone.air_temp") && (
          <Card title="Lufttemperatur" icon={<LineChart size={18} />}>
            <TimeChart data={data["zone.air_temp"] ?? null} color="--air" label="Luft" unit="°C" decimals={1} />
          </Card>
        )}
        {climate.includes("zone.humidity") && (
          <Card title="Luftfeuchte" icon={<LineChart size={18} />}>
            <TimeChart data={data["zone.humidity"] ?? null} color="--rh" label="Feuchte" unit="%" decimals={0} />
          </Card>
        )}
        {climate.includes("zone.vpd") && (
          <Card title="VPD (Luft)" icon={<LineChart size={18} />}>
            <TimeChart data={data["zone.vpd"] ?? null} color="--vpd" label="VPD" unit="kPa" decimals={2} />
          </Card>
        )}
        {climate.includes("zone.co2") && (
          <Card title="CO2" icon={<LineChart size={18} />}>
            <TimeChart data={data["zone.co2"] ?? null} color="--co2" label="CO2" unit="ppm" decimals={0} />
          </Card>
        )}
      </div>
      <Card title="Ereignisse" icon={<History size={18} />}>
        <div class="chips" style="margin-bottom:8px">
          {FILTERS.map(([v, l]) => (
            <button class={`chip ${filter === v ? "on" : ""}`} onClick={() => setFilter(v)}>
              {l}
            </button>
          ))}
        </div>
        <EventList events={events} groupByDay />
      </Card>
    </div>
  );
}
