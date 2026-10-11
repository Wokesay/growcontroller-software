// SPDX-License-Identifier: AGPL-3.0-or-later
// Home Assistant as the device layer (docs/HOME_ASSISTANT.md): the user picks
// for each measuring role a sensor from Home Assistant. gc_ha_server offers
// the candidates (/ha/candidates) and picks, accepts and binds in one step
// (/ha/assign). Read-only: nothing is switched from here.
import { useCallback, useEffect, useRef, useState } from "preact/hooks";
import { computed } from "@preact/signals";
import { get, post } from "./api";
import { ago, num, numUpTo } from "./format";
import { msg, t, type TextKey } from "./i18n";
import { binding, catalog, config, info, refreshConfig, refreshState, state, toast } from "./store";
import { Banner, Button, Modal, Pill } from "./ui";
import { qualityText } from "./widgets";

export const isHa = computed(() => info.value?.platform?.kind === "home-assistant");

type Connection = "starting" | "ok" | "unreachable" | "refused";
export type HaCandidate = {
  entity: string;
  name: string;
  kind: string;
  measures: string[];
  value: number | null; // in the hub's unit
  raw: number | null; // as Home Assistant reports it
  unit: string; // Home Assistant's unit
  problem: "unit_missing" | "unit_unsupported" | null;
  used: string | null;
};
// Any numeric sensor that is no suggestion (SD-036): it serves what its unit fits, nothing if
// Home Assistant says it measures something else.
type HaOther = Omit<HaCandidate, "kind" | "problem"> & { deviceClass: string; problem: HaCandidate["problem"] | "device_class" | "meter" };
type HaList = {
  connection: Connection;
  truncated: string[];
  candidates: HaCandidate[];
  otherFits?: Record<string, number>;
  otherCount?: number;
  others?: HaOther[];
};

// The measuring roles and the measure each one reads ("zone.air_temp" → "air_temp").
export const ROLES: [string, string][] = [
  ["tank.ph", "ph"],
  ["tank.ec", "ec"],
  ["tank.water_temp", "water_temp"],
  ["zone.air_temp", "air_temp"],
  ["zone.humidity", "humidity"],
  ["zone.co2", "co2"],
  ["tank.level", "level"],
];
const TIP = new Set(["water_temp", "air_temp", "humidity", "co2"]); // warm it by hand or breath to find it
const ALSO_ELSEWHERE = new Set(["ph", "ec"]); // plant and pool sensors report these too

/** The candidates, refreshed every few seconds while a view needs them; reload() after a change. */
export function useHaList(everyMs = 5000): { list: HaList | null; reload: () => Promise<void>; failing: boolean } {
  const [list, setList] = useState<HaList | null>(null);
  const [fails, setFails] = useState(0);
  const alive = useRef(true);
  const reload = useCallback(
    () =>
      get<HaList>("/ha/candidates")
        .then((l) => {
          if (!alive.current) return;
          setList(l);
          setFails(0);
        })
        .catch(() => {
          if (alive.current) setFails((n) => n + 1);
        }),
    [],
  );
  useEffect(() => {
    alive.current = true;
    reload();
    const id = window.setInterval(reload, everyMs);
    return () => {
      alive.current = false;
      window.clearInterval(id);
    };
  }, []);
  // Three misses in a row: say so instead of waiting for good
  return { list, reload, failing: fails >= 3 };
}

const roleLabel = (role: string) => catalog.value?.roles[role]?.label ?? role;
const capOf = (measure: string) => catalog.value?.capabilities[`measure.${measure}`];
const valueText = (measure: string, v: number | null | undefined) => {
  const cap = capOf(measure);
  return v === null || v === undefined ? "" : `${num(v, cap?.decimals ?? 1)}${cap?.unit ? ` ${cap.unit}` : ""}`;
};
// By name only, so a row never moves under the finger when its value comes and goes.
const byName = (a: HaCandidate, b: HaCandidate) => (a.name || a.entity).localeCompare(b.name || b.entity);
const kindOf = (measure: string) => (measure === "water_temp" || measure === "air_temp" ? "temperature" : measure);
const unitProblem = (c: HaCandidate | undefined) =>
  c?.problem === "unit_missing" ? t("ha.unitMissing") : c?.problem === "unit_unsupported" ? t("ha.unitUnsupported", { unit: c.unit }) : "";
const fold = (s: string) => s.normalize("NFD").replace(/\p{M}/gu, "").toLowerCase();
// The other role this sensor is bound to, if any.
const boundElsewhere = (entity: string, role: string) => ROLES.find(([r]) => r !== role && binding(r)?.device === `ha.${entity}`)?.[0];

/** The connection state as one notice; null while all is well. */
function ConnectionNotice(p: { list: HaList | null; failing?: boolean }) {
  if (!p.list && p.failing) return <Banner tone="warn">{t("ha.listFailed")}</Banner>;
  if (!p.list || p.list.connection === "starting") return <p class="muted">{t("ha.loading")}</p>;
  if (p.list.connection === "refused")
    return (
      <Banner tone="bad">
        <strong>{t("ha.refusedTitle")}</strong> {t("ha.refusedText")}
      </Banner>
    );
  if (p.list.connection === "unreachable")
    return (
      <Banner tone="warn">
        <strong>{t("ha.unreachableTitle")}</strong> {t("ha.unreachableText")}
      </Banner>
    );
  return null;
}

/** Devices › Assignment on a Home Assistant hub: one row per measuring role. */
export function HaRoles() {
  const { list, reload, failing } = useHaList();
  const [pick, setPick] = useState<{ role: string; measure: string } | null>(null);
  const st = state.value!;
  const cands = list?.candidates ?? [];
  const fits = (measure: string) => cands.filter((c) => c.measures.includes(measure));
  const rows = ROLES.filter(([role, m]) => binding(role) || fits(m).length > 0);
  const without = ROLES.filter(([role, m]) => !binding(role) && fits(m).length === 0);
  // Roles without a suggestion: open to a sensor of one's own choosing, or at least to why
  // the user's own one is not offered, as long as Home Assistant has any other sensor
  const anyOthers = (list?.otherCount ?? 0) > 0;
  const anyway = anyOthers ? without : [];
  const noneAtAll = anyOthers ? [] : without;
  const ok = list?.connection === "ok";
  // Until the list is there, show nothing that would move once it arrives;
  // if it does not come, show the assigned sensors with a notice
  if (!list && !failing)
    return (
      <div class="stack">
        <p class="muted">{t("ha.help")}</p>
        <ConnectionNotice list={list} />
      </div>
    );
  return (
    <div class="stack">
      <p class="muted">{t("ha.help")}</p>
      <ConnectionNotice list={list} failing={failing} />
      {ok && cands.length === 0 && rows.length === 0 && (
        <div>
          <strong>{t("ha.emptyTitle")}</strong>
          <p class="muted">{t("ha.emptyText")}</p>
        </div>
      )}
      {ok && rows.length === 0 && <PickAnyway roles={anyway} onPick={setPick} />}
      {rows.length > 0 && (
        <div class="list">
          {rows.map(([role, measure]) => {
            const b = binding(role);
            const device = b ? config.value!.devices.find((d) => d.id === b.device) : undefined;
            const entity = b?.device.startsWith("ha.") ? b.device.slice(3) : "";
            const reading = st.readings[role];
            const live = st.devices.find((d) => d.id === b?.device)?.online;
            // The badge follows the reading, as on the tiles
            const external = reading?.quality === "uncalibrated" && reading.reason.key === "truth.external";
            const bad = !!reading && reading.quality !== "ok" && reading.quality !== "not_bound" && !external;
            const problem = unitProblem(cands.find((c) => c.entity === entity)); // a unit the hub cannot use, said as such
            return (
              <div class="item" data-testid={`ha-role-${role}`}>
                <span class="grow">
                  <span class="row" style="gap:8px">
                    <strong>{roleLabel(role)}</strong>
                    {b && external && <Pill tone="warn">{t("widgets.quality.external")}</Pill>}
                    {b && bad && qualityText[reading!.quality] && <Pill tone="bad">{t(qualityText[reading!.quality])}</Pill>}
                  </span>
                  {b && (
                    <div class="muted small" style="overflow-wrap:anywhere">
                      {[device?.name || entity, valueText(measure, reading?.value), reading?.ageS != null ? ago(reading.ageS) : ""].filter(Boolean).join(" · ")}
                      {bad && reading?.reason.text && live !== false && !problem && <div>{msg(reading.reason)}</div>}
                      {problem && <div>{problem}</div>}
                      {live === false && entity && ok && <div>{t("ha.missing", { entity })}</div>}
                    </div>
                  )}
                </span>
                <Button
                  size="sm"
                  variant={b ? "default" : "primary"}
                  onClick={() => setPick({ role, measure })}
                  aria-label={b ? t("ha.changeFor", { role: roleLabel(role) }) : t("ha.chooseFor", { role: roleLabel(role) })}
                >
                  {b ? t("ha.change") : t("ha.choose")}
                </Button>
              </div>
            );
          })}
        </div>
      )}
      {ok && rows.length > 0 && <PickAnyway roles={anyway} onPick={setPick} />}
      {ok && rows.length > 0 && noneAtAll.length > 0 && (
        <p class="muted small">{t("ha.noneFor", { list: noneAtAll.map(([role]) => roleLabel(role)).join(", ") })}</p>
      )}
      <details>
        <summary>{t("ha.why")}</summary>
        <p class="muted small">{t("ha.whyText")}</p>
      </details>
      {pick && <HaPicker role={pick.role} measure={pick.measure} list={list} failing={failing} reload={reload} onClose={() => setPick(null)} />}
    </div>
  );
}

/** Roles without a suggestion that an other sensor fits, each still open to a sensor of one's own choosing (SD-036). */
function PickAnyway(p: { roles: [string, string][]; onPick: (pick: { role: string; measure: string }) => void }) {
  if (p.roles.length === 0) return null;
  return (
    <div class="row" style="gap:8px;flex-wrap:wrap" data-testid="ha-pick-anyway">
      <span class="muted small">{t("ha.pickAnyway")}</span>
      {p.roles.map(([role, measure]) => (
        <Button size="sm" onClick={() => p.onPick({ role, measure })} aria-label={t("ha.chooseFor", { role: roleLabel(role) })}>
          {roleLabel(role)}
        </Button>
      ))}
    </div>
  );
}

// Home Assistant's device classes in words; any other says only "something else"
const CLASS_TEXT: Record<string, TextKey> = {
  battery: "ha.cls.battery",
  power: "ha.cls.power",
  apparent_power: "ha.cls.power",
  energy: "ha.cls.energy",
  voltage: "ha.cls.voltage",
  current: "ha.cls.current",
  illuminance: "ha.cls.illuminance",
  signal_strength: "ha.cls.signal",
  moisture: "ha.cls.moisture",
  pressure: "ha.cls.pressure",
  atmospheric_pressure: "ha.cls.pressure",
  pm1: "ha.cls.particulates",
  pm25: "ha.cls.particulates",
  pm10: "ha.cls.particulates",
  volatile_organic_compounds: "ha.cls.voc",
  volatile_organic_compounds_parts: "ha.cls.voc",
  water: "ha.cls.water",
  gas: "ha.cls.gas",
  distance: "ha.cls.distance",
};
const measureLabel = (measure: string) => capOf(measure)?.label ?? measure;

// Why an other sensor cannot serve this measure, "" if it can.
function otherProblem(o: HaOther, measure: string): string {
  if (o.measures.includes(measure)) return "";
  if (o.problem === "device_class")
    return Object.prototype.hasOwnProperty.call(CLASS_TEXT, o.deviceClass) ? t("ha.otherIs", { what: t(CLASS_TEXT[o.deviceClass]) }) : t("ha.otherClass");
  if (o.problem === "meter") return t("ha.otherMeter");
  if (o.problem === "unit_missing") return t("ha.otherUnitMissing");
  if (o.problem === "unit_unsupported" || o.measures.length === 0) return t("ha.otherUnit", { unit: o.unit });
  if (!o.unit) return t("ha.otherNoUnit");
  return t("ha.otherUnitFor", { unit: o.unit, fits: o.measures.map(measureLabel).join(", ") });
}

/** The other sensors, fetched only while shown, refreshed like the suggestions. */
function useOthers(on: boolean): { others: HaOther[] | null; failing: boolean } {
  const [others, setOthers] = useState<HaOther[] | null>(null);
  const [fails, setFails] = useState(0);
  useEffect(() => {
    if (!on) return;
    let alive = true;
    const load = () =>
      get<HaList>("/ha/candidates?others=1")
        .then((l) => {
          if (!alive) return;
          setOthers(l.others ?? []);
          setFails(0);
        })
        .catch(() => alive && setFails((n) => n + 1));
    load();
    const id = window.setInterval(load, 10000);
    return () => {
      alive = false;
      window.clearInterval(id);
    };
  }, [on]);
  return { others, failing: fails >= 2 };
}
const byLabel = (a: { name: string; entity: string }, b: { name: string; entity: string }) =>
  (a.name || a.entity).localeCompare(b.name || b.entity, undefined, { numeric: true }) || a.entity.localeCompare(b.entity);

/** Choosing the sensor for one role: search, live values, one tap saves. */
function HaPicker(p: { role: string; measure: string; list: HaList | null; failing: boolean; reload: () => Promise<void>; onClose: () => void }) {
  const [q, setQ] = useState("");
  const [saving, setSaving] = useState<string | null>(null); // the entity being saved, "" for "none"
  const search = useRef<HTMLInputElement>(null);
  const b = binding(p.role);
  const current = b?.device.startsWith("ha.") ? b.device.slice(3) : "";
  const ok = p.list?.connection === "ok";
  const all = (p.list?.candidates ?? []).filter((c) => c.measures.includes(p.measure)).sort(byName);
  const match = (c: { name: string; entity: string }) => fold(`${c.name} ${c.entity}`).includes(fold(q.trim()));
  const shown = q.trim() ? all.filter(match) : all;
  const fitCount = p.list?.otherFits?.[p.measure] ?? 0;
  // The other sensors show at once without a suggestion, and when the sensor in use is one of them
  const [showOthers, setShowOthers] = useState(all.length === 0 || (!!current && !all.some((c) => c.entity === current)));
  const [jumpToOthers, setJumpToOthers] = useState(false);
  const othersHead = useRef<HTMLHeadingElement>(null);
  const { others, failing: othersFailing } = useOthers(showOthers);
  const othersShown = (others ?? []).filter((o) => !q.trim() || match(o)).sort(byLabel);
  const othersFit = othersShown.filter((o) => o.measures.includes(p.measure));
  const othersNot = othersShown.filter((o) => !o.measures.includes(p.measure));
  const label = roleLabel(p.role);
  useEffect(() => {
    if ((all.length > 8 || showOthers) && window.matchMedia?.("(pointer: fine)").matches) search.current?.focus();
  }, []);
  useEffect(() => {
    if (!jumpToOthers || !othersHead.current) return;
    othersHead.current.focus();
    othersHead.current.scrollIntoView?.({ block: "start" });
    setJumpToOthers(false);
  }, [jumpToOthers, showOthers]);
  const openOthers = () => {
    setShowOthers(true);
    setJumpToOthers(true);
  };
  const choose = async (entity: string, name: string) => {
    if (entity && entity === current) return p.onClose(); // already this one
    setSaving(entity);
    try {
      await post("/ha/assign", { role: p.role, entity });
      await Promise.all([refreshConfig(), refreshState(), p.reload()]);
      toast(entity ? t("ha.saved", { name: name || entity, role: label }) : t("ha.removed", { role: label }));
      p.onClose();
    } catch (e: any) {
      toast(e.message, "error");
    } finally {
      setSaving(null);
    }
  };
  return (
    <Modal title={t("ha.pickerTitle", { role: label })} onClose={p.onClose} wide>
      <ConnectionNotice list={p.list} failing={p.failing} />
      {ok && all.length === 0 && <p class="muted small">{t("ha.noSuggestion")}</p>}
      {ok && (all.length > 8 || showOthers) && (
        <input
          ref={search}
          class="input ha-search"
          type="search"
          value={q}
          placeholder={t("ha.search")}
          aria-label={t("ha.searchLabel")}
          onInput={(e) => setQ((e.target as HTMLInputElement).value)}
          onKeyDown={(e) => {
            if (e.key === "Escape" && q) {
              e.stopPropagation(); // clears the search instead of closing the picker
              setQ("");
            }
          }}
        />
      )}
      {ok && all.length > 0 && (
        <p class="muted small">
          {q.trim() ? t("ha.countFiltered", { n: shown.length, total: all.length }) : all.length === 1 ? t("ha.countOne") : t("ha.count", { n: all.length })}
        </p>
      )}
      {ok && Array.isArray(p.list?.truncated) && p.list!.truncated.includes(kindOf(p.measure)) && <p class="muted small">{t("ha.truncated")}</p>}
      {ok && TIP.has(p.measure) && (all.length > 1 || showOthers) && <p class="muted small">{t("ha.tip")}</p>}
      {ok && ALSO_ELSEWHERE.has(p.measure) && <p class="muted small">{t("ha.alsoElsewhere")}</p>}
      <div class="ha-options" data-testid="ha-picker" aria-busy={saving !== null || undefined}>
        {ok &&
          shown.map((c) => {
            const elsewhere = boundElsewhere(c.entity, p.role);
            const converted = c.value !== null && c.raw !== null && c.value !== c.raw; // µS/cm → mS/cm, °F → °C
            return (
              <button
                class={`ha-option ${c.entity === current ? "current" : ""}`}
                disabled={saving !== null || !!elsewhere}
                aria-disabled={elsewhere ? true : undefined}
                aria-current={c.entity === current || undefined}
                onClick={() => choose(c.entity, c.name)}
              >
                <span class="grow">
                  <strong>{c.name || c.entity}</strong>
                  <span class="muted small mono ha-entity">{c.entity}</span>
                  {converted && <span class="muted small">{t("ha.inHa", { value: numUpTo(c.raw!, 2), unit: c.unit })}</span>}
                  {elsewhere && <span class="ha-used small">{t("ha.usedFor", { role: roleLabel(elsewhere) })}</span>}
                  {saving === c.entity && <span class="muted small">{t("ha.saving")}</span>}
                </span>
                {c.value !== null ? (
                  <span class="ha-value">{valueText(p.measure, c.value)}</span>
                ) : (
                  <span class="muted small ha-novalue">{unitProblem(c) || t("ha.noValue")}</span>
                )}
              </button>
            );
          })}
        {ok && q.trim() && all.length > 0 && shown.length === 0 && !showOthers && (
          <div class="row" style="gap:8px;flex-wrap:wrap">
            <span class="muted">{fitCount > 0 ? t("ha.noSuggestionMatch", { q: q.trim() }) : t("ha.noMatch", { q: q.trim() })}</span>
            {fitCount > 0 && (
              <Button size="sm" onClick={openOthers}>
                {t("ha.searchOthers")}
              </Button>
            )}
          </div>
        )}
        {current && (
          <button class="ha-option ha-none" disabled={saving !== null} onClick={() => choose("", "")}>
            <span class="grow">{saving === "" ? t("ha.saving") : t("ha.none")}</span>
          </button>
        )}
      </div>
      {ok && !showOthers && !(q.trim() && all.length > 0 && shown.length === 0 && fitCount > 0) && (
        <div class="row" style="gap:8px;flex-wrap:wrap">
          <span class="muted small">{t("ha.notListed")}</span>
          <Button size="sm" onClick={openOthers} data-testid="ha-show-others">
            {fitCount > 0 ? t("ha.showOthers", { n: fitCount }) : t("ha.showOthersPlain")}
          </Button>
        </div>
      )}
      {ok && showOthers && (
        <div class="stack-sm" data-testid="ha-others">
          <h3 ref={othersHead} tabIndex={-1} class="ha-others-title">
            {t("ha.othersTitle")}
          </h3>
          <p class="muted small">{t("ha.othersHelp", { role: label })}</p>
          {Array.isArray(p.list?.truncated) && p.list!.truncated.includes("other") && <p class="muted small">{t("ha.othersTruncated")}</p>}
          {others === null && !othersFailing && <p class="muted small">{t("ha.loading")}</p>}
          {othersFailing && <p class="muted small">{t("ha.othersFailed")}</p>}
          <div class="ha-options" aria-busy={saving !== null || undefined}>
            {othersFit.map((o) => (
              <OtherOption o={o} measure={p.measure} role={p.role} current={current} saving={saving} onChoose={choose} />
            ))}
            {others !== null && othersFit.length === 0 && (!q.trim() || othersNot.length === 0) && (
              <p class="muted">{q.trim() ? t("ha.noMatch", { q: q.trim() }) : t("ha.othersNone", { role: label })}</p>
            )}
          </div>
          {othersNot.length > 0 && (
            <details open={!!q.trim() || othersFit.length === 0} data-testid="ha-others-not">
              <summary class="small">{t("ha.othersNot", { role: label, n: othersNot.length })}</summary>
              <div class="ha-options" style="margin-top:8px">
                {othersNot.map((o) => (
                  <OtherOption o={o} measure={p.measure} role={p.role} current={current} saving={saving} onChoose={choose} />
                ))}
              </div>
            </details>
          )}
        </div>
      )}
    </Modal>
  );
}

/** One other sensor in the picker: chosen with a tap if its unit fits, otherwise greyed with the reason. */
function OtherOption(p: { o: HaOther; measure: string; role: string; current: string; saving: string | null; onChoose: (entity: string, name: string) => void }) {
  const { o } = p;
  const elsewhere = boundElsewhere(o.entity, p.role);
  const problem = otherProblem(o, p.measure);
  return (
    <button
      class={`ha-option ${o.entity === p.current ? "current" : ""}`}
      disabled={p.saving !== null || !!elsewhere || !!problem}
      aria-disabled={elsewhere || problem ? true : undefined}
      aria-current={o.entity === p.current || undefined}
      onClick={() => p.onChoose(o.entity, o.name)}
    >
      <span class="grow">
        <strong>{o.name || o.entity}</strong>
        <span class="muted small mono ha-entity">{o.entity}</span>
        {problem && <span class="muted small">{problem}</span>}
        {elsewhere && <span class="ha-used small">{t("ha.usedFor", { role: roleLabel(elsewhere) })}</span>}
        {p.saving === o.entity && <span class="muted small">{t("ha.saving")}</span>}
      </span>
      {o.raw !== null ? (
        <span class="ha-value">{`${numUpTo(o.raw, 2)}${o.unit ? ` ${o.unit}` : ""}`}</span>
      ) : !problem ? (
        <span class="muted small ha-novalue">{t("ha.noValue")}</span>
      ) : null}
    </button>
  );
}

/** The overview's notice on a Home Assistant hub: choose sensors, or what is wrong. */
export function HaBanner() {
  const { list } = useHaList(15000);
  if (!list) return null;
  const anyBound = ROLES.some(([role]) => binding(role));
  if (list.connection === "refused") return <Banner tone="bad">{t("ha.bannerRefused")}</Banner>;
  if (list.connection === "unreachable") return <Banner tone="warn">{t("ha.bannerUnreachable")}</Banner>;
  if (list.connection !== "ok" || anyBound) return null;
  const none = list.candidates.length === 0;
  return (
    <Banner tone="info">
      {none ? t("ha.bannerNone") : t("ha.bannerChoose")}{" "}
      <a href="#/geraete?tab=zuordnung" data-testid="ha-choose-link">
        {none ? t("ha.bannerNoneLink") : t("ha.bannerChooseLink")} →
      </a>
    </Banner>
  );
}
