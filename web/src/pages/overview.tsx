// SPDX-License-Identifier: AGPL-3.0-or-later
// Übersicht: auf einen Blick, ob alles läuft, was gerade passiert und was zu tun ist.
import { useEffect, useState } from "preact/hooks";
import { Beaker, CircleCheck, Droplets, FlaskConical, OctagonX, PackagePlus, ShieldAlert, ShieldCheck, Sprout, Thermometer, Wrench } from "lucide-preact";
import { get, post, type HubEvent } from "../api";
import { ago, dateTime, num } from "../format";
import { catalog, config, hasRole, refreshState, state, tank, toast } from "../store";
import { msg, t as tr } from "../i18n";
import { ClimateTiles, OutputsOverview } from "./areas";
import { Banner, Button, Card, NumberInput, Pill } from "../ui";
import { ControllerRow, EventList, JobView, MetricTile, StockList, useSparks } from "../widgets";
import { HaBanner, isHa } from "../ha";


function WatchdogBar() {
  const wd = state.value!.watchdog;
  const problems = wd.items.filter((i) => i.status === "problem");
  const tone = wd.stale ? "problem" : wd.overall;
  return (
    <div class={`statusbar ${tone}`} data-testid="watchdog">
      <div class="icon-wrap">{tone === "problem" ? <ShieldAlert size={22} /> : <ShieldCheck size={22} />}</div>
      <div class="grow" style="flex:1;min-width:0">
        <div class="title">{wd.stale ? tr("overview.watchdogStale") : msg(wd.headline)}</div>
        <div class="muted small">
          {wd.stale
            ? tr("overview.watchdogStaleText")
            : problems.length
              ? problems.slice(0, 3).map((p) => `${msg(p.label)}: ${msg(p.text)}`).join(" · ")
              : wd.neutral > 0
                ? tr("overview.watchdogNeutral", { n: wd.neutral })
                : tr("overview.watchdogIdle")}
        </div>
      </div>
      <a class="btn sm ghost" href="#/tank">
        {tr("common.details")}
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
          <NumberInput value={v} onValue={setV} placeholder={tr("overview.phPlaceholder")} />
        </div>
        <Button
          size="sm"
          disabled={v === null}
          onClick={async () => {
            await post("/measure", { ph: v });
            setV(null);
            await refreshState();
            toast(tr("overview.manualSaved"));
          }}
        >
          {tr("overview.enterPh")}
        </Button>
      </div>
      {m?.values?.ph !== undefined && (
        <span class="muted small">
          {tr("overview.lastManual", { ph: num(m.values.ph, 2), at: dateTime(m.at) })}
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
  const hasClimate = hasRole("zone.air_temp") || hasRole("zone.humidity");
  const cat = catalog.value;
  const hasOutputs = !!cat && Object.entries(cat.roles).some(([id, r]) => r.profile && hasRole(id));
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
  const newDevices = st.devices.filter((d) => !d.configured && d.online).length;

  return (
    <div class="stack">
      {st.stopped && (
        <Banner tone="bad" icon={<OctagonX size={18} />}>
          <strong>{tr("overview.stoppedTitle")}</strong> {tr("overview.stoppedText")}
        </Banner>
      )}
      {st.maintenanceUntil > st.now && (
        <Banner tone="warn" icon={<Wrench size={18} />}>
          {tr("overview.maintenance", { until: dateTime(st.maintenanceUntil) })}
        </Banner>
      )}
      {isHa.value && <HaBanner />}
      {!isHa.value && newDevices > 0 && (
        <Banner tone="info" icon={<PackagePlus size={18} />}>
          {newDevices === 1 ? tr("devices.newOne") : tr("devices.newMany", { n: newDevices })} {tr("devices.acceptHint")}{" "}
          <a href="#/geraete" data-testid="new-devices-link">
            {tr("overview.toDevices")}
          </a>
        </Banner>
      )}
      <WatchdogBar />

      {job && (
        <Card title={tr("overview.running")} icon={<Beaker size={18} />}>
          <JobView job={job} />
        </Card>
      )}

      <div class="grid-2">
        <Card
          title={t?.name ?? tr("setup.tank.h")}
          icon={<Droplets size={18} />}
          actions={
            <a class="btn sm" href="#/mischen">
              <Beaker size={15} /> {tr("nav.mix")}
            </a>
          }
        >
          <div class="stack">
            <div class="stack-sm">
              <div class="row-between">
                <span class="muted small">{st.tank.source === "level" ? tr("overview.volumeMeasured") : tr("overview.volumeFromMix")}</span>
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
                    label={tr("common.water")}
                    reading={st.readings["tank.water_temp"]}
                    color="--temp"
                    band={tempFn?.enabled ? [Number(tempFn.params.min_c ?? 18), Number(tempFn.params.max_c ?? 23)] : null}
                    spark={sparks["tank.water_temp"]}
                    notApplicable={empty ? tr("overview.tankEmpty") : undefined}
                  />
                )}
              </div>
            ) : (
              <div class="stack-sm">
                <p class="muted">
                  {st.lastMixAt ? tr("overview.lastMixed", { ago: ago(st.now - st.lastMixAt) }) : tr("overview.notMixed")} {tr("overview.manualPhHint")}
                </p>
                <ManualPh />
              </div>
            )}
          </div>
        </Card>

        <Card title={tr("overview.control")} icon={<Sprout size={18} />} actions={<a class="btn sm ghost" href="#/tank">{tr("overview.allDetails")}</a>}>
          <div class="stack-sm">
            {hasRole("tank.ec") && <ControllerRow name="EC" st={st.controllers.ec} />}
            {hasRole("tank.ph") && <ControllerRow name="pH" st={st.controllers.ph} />}
            {hasRole("tank.inlet") && <ControllerRow name={tr("overview.refill")} st={st.controllers.refill} />}
            {hasRole("tank.circulation") && <ControllerRow name={tr("overview.circulate")} st={st.controllers.circulation} />}
            {!hasHead && !hasRole("tank.circulation") && (
              <div class="stack-sm">
                <p class="muted">{tr("overview.stage0Control")}</p>
                <a href="#/geraete?tab=erweitern">{tr("overview.expandLink")}</a>
              </div>
            )}
          </div>
        </Card>
      </div>

      {(hasClimate || hasOutputs) && (
        <div class="grid-2">
          {hasClimate && (
            <Card title={tr("area.climate")} icon={<Thermometer size={18} />} actions={<a class="btn sm ghost" href="#/klima">{tr("nav.climate")}</a>}>
              <ClimateTiles />
            </Card>
          )}
          <OutputsOverview />
        </div>
      )}

      <div class="grid">
        <Card title={tr("overview.bottles")} icon={<FlaskConical size={18} />} actions={<a class="btn sm ghost" href="#/rezepte">{tr("overview.manage")}</a>}>
          <StockList compact />
        </Card>
        <Card title={tr("overview.grow")} icon={<Sprout size={18} />} actions={<a class="btn sm ghost" href="#/tank">{tr("nav.phases")}</a>}>
          {grow.state === "running" ? (
            <div class="stack-sm">
              <div class="row-between">
                <strong>{grow.name}</strong>
                <Pill tone="accent">{tr("overview.day", { n: Math.floor((st.now - grow.startedAt) / 86400) + 1 })}</Pill>
              </div>
              <span class="muted">
                {tr("overview.phaseLine", {
                  name: grow.phases[grow.phase]?.name ?? "",
                  day: Math.floor((st.now - grow.phaseStartedAt) / 86400) + 1,
                  days: grow.phases[grow.phase]?.days || "–",
                })}
              </span>
              <span class="faint small">{tr("overview.phaseNote")}</span>
            </div>
          ) : (
            <p class="muted">{tr("overview.noGrow")}</p>
          )}
        </Card>
        <Card title={tr("overview.recentEvents")} icon={<CircleCheck size={18} />} actions={<a class="btn sm ghost" href="#/verlauf">{tr("common.all")}</a>}>
          <EventList events={events} />
        </Card>
      </div>
    </div>
  );
}
