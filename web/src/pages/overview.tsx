// Übersicht: auf einen Blick, ob alles läuft, was gerade passiert und was zu tun ist.
import { useEffect, useState } from "preact/hooks";
import { Beaker, CircleCheck, Droplets, FlaskConical, OctagonX, ShieldAlert, ShieldCheck, Sprout, Wrench } from "lucide-preact";
import { get, post, type HubEvent, type SeriesData } from "../api";
import { ago, dateTime, num } from "../format";
import { config, hasRole, refreshState, state, tank, toast } from "../store";
import { Banner, Button, Card, NumberInput, Pill } from "../ui";
import { ControllerRow, EventList, JobView, MetricTile, StockList } from "../widgets";

function useSparks(series: string[]) {
  const [data, setData] = useState<Record<string, (number | null)[]>>({});
  useEffect(() => {
    let alive = true;
    const load = async () => {
      if (!series.length) return;
      const now = state.value?.now ?? Math.floor(Date.now() / 1000);
      const r = await get<{ series: SeriesData[] }>(`/history?series=${series.join(",")}&from=${now - 6 * 3600}&to=${now}&points=72`);
      if (alive) setData(Object.fromEntries(r.series.map((s) => [s.series, s.avg])));
    };
    load().catch(() => {});
    const t = setInterval(() => load().catch(() => {}), 60000);
    return () => {
      alive = false;
      clearInterval(t);
    };
  }, [series.join(",")]);
  return data;
}

function WatchdogBar() {
  const wd = state.value!.watchdog;
  const problems = wd.items.filter((i) => i.status === "problem");
  const tone = wd.stale ? "problem" : wd.overall;
  return (
    <div class={`statusbar ${tone}`} data-testid="watchdog">
      <div class="icon-wrap">{tone === "problem" ? <ShieldAlert size={22} /> : <ShieldCheck size={22} />}</div>
      <div class="grow" style="flex:1;min-width:0">
        <div class="title">{wd.stale ? "Überwachung liefert keine Bewertung" : wd.headline}</div>
        <div class="muted small">
          {wd.stale
            ? "Die letzte Bewertung ist älter als 3 Minuten."
            : problems.length
              ? problems.slice(0, 3).map((p) => `${p.label}: ${p.text}`).join(" · ")
              : wd.neutral > 0
                ? `${wd.neutral} Prüfungen ruhen mit Grund. Sperren bleiben aktiv.`
                : "Sperren und Sensorwahrheit laufen immer, auch ohne laufenden Durchgang."}
        </div>
      </div>
      <a class="btn sm ghost" href="#/tank">
        Details
      </a>
    </div>
  );
}

function ManualPh() {
  const [v, setV] = useState<number | null>(null);
  const m = state.value?.manual;
  return (
    <div class="stack-sm">
      <div class="row">
        <div style="width:120px">
          <NumberInput value={v} onValue={setV} placeholder="z. B. 6,1" />
        </div>
        <Button
          size="sm"
          disabled={v === null}
          onClick={async () => {
            await post("/measure", { ph: v });
            setV(null);
            await refreshState();
            toast("Handmessung gespeichert");
          }}
        >
          pH eintragen
        </Button>
      </div>
      {m?.values?.ph !== undefined && (
        <span class="muted small">
          Zuletzt von Hand: pH {num(m.values.ph, 2)} · {dateTime(m.at)}
        </span>
      )}
    </div>
  );
}

export function Overview() {
  const st = state.value!;
  const cfg = config.value!;
  const t = tank.value;
  const hasHead = hasRole("tank.ph") || hasRole("tank.ec");
  const series = ["tank.ph", "tank.ec", "tank.water_temp"].filter((s) => hasRole(s));
  const sparks = useSparks(series);
  const [events, setEvents] = useState<HubEvent[]>([]);
  useEffect(() => {
    get<{ events: HubEvent[] }>("/events?limit=6").then((r) => setEvents(r.events)).catch(() => {});
  }, [st.eventId]);

  const phInfo = st.controllers.ph.info as any;
  const ecInfo = st.controllers.ec.info as any;
  const phBand: [number, number] | null = st.controllers.ph.state !== "off" && phInfo?.target != null ? [phInfo.target - phInfo.tolerance, phInfo.target + phInfo.tolerance] : null;
  const ecBand: [number, number] | null = st.controllers.ec.state !== "off" && ecInfo?.target != null ? [ecInfo.target - ecInfo.tolerance, ecInfo.target + ecInfo.tolerance] : null;
  const tempFn = cfg.functions["water_temp_watch"];
  const level = st.readings["tank.level"];
  const empty = level?.usable && t?.minL != null && (level.value ?? 0) < t.minL;
  const job = st.job ?? (st.lastJob && st.lastJob.finishedAt && st.now - st.lastJob.finishedAt < 600 ? st.lastJob : null);
  const vol = st.tank.volumeL;
  const cap = st.tank.capacityL;
  const grow = st.grow;

  return (
    <div class="stack">
      {st.stopped && (
        <Banner tone="bad" icon={<OctagonX size={18} />}>
          <strong>Not-Halt aktiv.</strong> Alle Pumpen und Ausgänge sind aus, die Automatik ruht. Oben rechts „Fortsetzen“, wenn alles geprüft ist.
        </Banner>
      )}
      {st.maintenanceUntil > st.now && (
        <Banner tone="warn" icon={<Wrench size={18} />}>
          Pflegemodus bis {dateTime(st.maintenanceUntil)}: Die Automatik ruht, Sperren und Messungen laufen weiter.
        </Banner>
      )}
      <WatchdogBar />

      {job && (
        <Card title="Gerade läuft" icon={<Beaker size={18} />}>
          <JobView job={job} />
        </Card>
      )}

      <div class="grid-2">
        <Card
          title={t?.name ?? "Tank"}
          icon={<Droplets size={18} />}
          actions={
            <a class="btn sm" href="#/mischen">
              <Beaker size={15} /> Mischen
            </a>
          }
        >
          <div class="stack">
            <div class="stack-sm">
              <div class="row-between">
                <span class="muted small">Volumen {st.tank.source === "level" ? "(gemessen)" : "(aus dem letzten Mischlauf)"}</span>
                <strong>
                  {num(vol, 1)} L{cap ? <span class="faint"> / {num(cap, 0)} L</span> : null}
                </strong>
              </div>
              {cap ? (
                <div class="gauge">
                  <span style={`left:0;width:${vol ? Math.min(100, (vol / cap) * 100) : 0}%;background:var(--level)`} />
                </div>
              ) : null}
            </div>
            {hasHead ? (
              <div class="grid-3">
                {hasRole("tank.ph") && <MetricTile label="pH" reading={st.readings["tank.ph"]} color="--ph" band={phBand} spark={sparks["tank.ph"]} />}
                {hasRole("tank.ec") && <MetricTile label="EC" reading={st.readings["tank.ec"]} color="--ec" band={ecBand} spark={sparks["tank.ec"]} />}
                {hasRole("tank.water_temp") && (
                  <MetricTile
                    label="Wasser"
                    reading={st.readings["tank.water_temp"]}
                    color="--temp"
                    band={tempFn?.enabled ? [Number(tempFn.params.min_c ?? 18), Number(tempFn.params.max_c ?? 23)] : null}
                    spark={sparks["tank.water_temp"]}
                    notApplicable={empty ? "Tank leer" : undefined}
                  />
                )}
              </div>
            ) : (
              <div class="stack-sm">
                <p class="muted">
                  {st.lastMixAt ? `Zuletzt gemischt ${ago(st.now - st.lastMixAt)}.` : "Noch nicht gemischt."} pH misst du in Stufe 0 von Hand – trag den Wert ein, dann steht er im Verlauf.
                </p>
                <ManualPh />
              </div>
            )}
          </div>
        </Card>

        <Card title="Regelung" icon={<Sprout size={18} />} actions={<a class="btn sm ghost" href="#/tank">Alle Details</a>}>
          <div class="stack-sm">
            {hasRole("tank.ec") && <ControllerRow name="EC" st={st.controllers.ec} />}
            {hasRole("tank.ph") && <ControllerRow name="pH" st={st.controllers.ph} />}
            {hasRole("tank.inlet") && <ControllerRow name="Nachfüllen" st={st.controllers.refill} />}
            {hasRole("tank.circulation") && <ControllerRow name="Umwälzen" st={st.controllers.circulation} />}
            {!hasHead && !hasRole("tank.circulation") && (
              <div class="stack-sm">
                <p class="muted">In Stufe 0 regelt der Hub nichts selbst. Mit dem pH/EC-Sensorkopf misst er dauerhaft und regelt nach.</p>
                <a href="#/geraete?tab=erweitern">Was kann ich erweitern? →</a>
              </div>
            )}
          </div>
        </Card>
      </div>

      <div class="grid">
        <Card title="Kanister" icon={<FlaskConical size={18} />} actions={<a class="btn sm ghost" href="#/rezepte">Verwalten</a>}>
          <StockList compact />
        </Card>
        <Card title="Durchgang" icon={<Sprout size={18} />} actions={<a class="btn sm ghost" href="#/tank">Phasen</a>}>
          {grow.state === "running" ? (
            <div class="stack-sm">
              <div class="row-between">
                <strong>{grow.name}</strong>
                <Pill tone="accent">Tag {Math.floor((st.now - grow.startedAt) / 86400) + 1}</Pill>
              </div>
              <span class="muted">
                Phase „{grow.phases[grow.phase]?.name}“ · Tag {Math.floor((st.now - grow.phaseStartedAt) / 86400) + 1} von {grow.phases[grow.phase]?.days || "–"}
              </span>
              <span class="faint small">Phasen liefern nur Zielwerte (pH, EC, Rezept). Ihr Name steuert nichts.</span>
            </div>
          ) : (
            <p class="muted">Kein Durchgang aktiv. Für Stufe 0 nicht nötig – Mischen geht immer.</p>
          )}
        </Card>
        <Card title="Letzte Ereignisse" icon={<CircleCheck size={18} />} actions={<a class="btn sm ghost" href="#/verlauf">Alle</a>}>
          <EventList events={events} />
        </Card>
      </div>
    </div>
  );
}
