// SPDX-License-Identifier: AGPL-3.0-or-later
// Home Assistant as the device layer (docs/HOME_ASSISTANT.md): the user picks
// for each measuring role a sensor from Home Assistant. gc_ha_server offers
// the candidates (/ha/candidates) and picks, accepts and binds in one step
// (/ha/assign). Read-only: nothing is switched from here.
import { useCallback, useEffect, useRef, useState } from "preact/hooks";
import { computed } from "@preact/signals";
import { get, post } from "./api";
import { ago, num, numUpTo } from "./format";
import { msg, t } from "./i18n";
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
type HaList = { connection: Connection; truncated: string[]; candidates: HaCandidate[] };

// The measuring roles and the measure each one reads ("zone.air_temp" → "air_temp").
const ROLES: [string, string][] = [
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
  const without = ROLES.filter(([role, m]) => !binding(role) && fits(m).length === 0).map(([role]) => roleLabel(role));
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
      {ok && rows.length > 0 && without.length > 0 && <p class="muted small">{t("ha.noneFor", { list: without.join(", ") })}</p>}
      <details>
        <summary>{t("ha.why")}</summary>
        <p class="muted small">{t("ha.whyText")}</p>
      </details>
      {pick && <HaPicker role={pick.role} measure={pick.measure} list={list} failing={failing} reload={reload} onClose={() => setPick(null)} />}
    </div>
  );
}

/** Choosing the sensor for one role: search, live values, one tap saves. */
function HaPicker(p: { role: string; measure: string; list: HaList | null; failing: boolean; reload: () => Promise<void>; onClose: () => void }) {
  const [q, setQ] = useState("");
  const [saving, setSaving] = useState<string | null>(null); // the entity being saved, "" for "none"
  const search = useRef<HTMLInputElement>(null);
  const b = binding(p.role);
  const current = b?.device.startsWith("ha.") ? b.device.slice(3) : "";
  const ok = p.list?.connection === "ok";
  const all = (p.list?.candidates ?? []).filter((c) => c.measures.includes(p.measure)).sort(byName);
  const shown = q.trim() ? all.filter((c) => fold(`${c.name} ${c.entity}`).includes(fold(q.trim()))) : all;
  const label = roleLabel(p.role);
  useEffect(() => {
    if (all.length > 8 && window.matchMedia?.("(pointer: fine)").matches) search.current?.focus();
  }, []);
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
      {ok && all.length > 8 && (
        <input
          ref={search}
          class="input"
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
      {ok && (
        <p class="muted small">
          {q.trim() ? t("ha.countFiltered", { n: shown.length, total: all.length }) : all.length === 1 ? t("ha.countOne") : t("ha.count", { n: all.length })}
        </p>
      )}
      {ok && Array.isArray(p.list?.truncated) && p.list!.truncated.includes(kindOf(p.measure)) && <p class="muted small">{t("ha.truncated")}</p>}
      {ok && TIP.has(p.measure) && all.length > 1 && <p class="muted small">{t("ha.tip")}</p>}
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
        {ok && q.trim() && shown.length === 0 && <p class="muted">{t("ha.noMatch", { q: q.trim() })}</p>}
        {current && (
          <button class="ha-option ha-none" disabled={saving !== null} onClick={() => choose("", "")}>
            <span class="grow">{saving === "" ? t("ha.saving") : t("ha.none")}</span>
          </button>
        )}
      </div>
    </Modal>
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
