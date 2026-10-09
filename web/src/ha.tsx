// SPDX-License-Identifier: AGPL-3.0-or-later
// Home Assistant as the device layer (docs/HOME_ASSISTANT.md): the user picks
// for each measuring role a sensor from Home Assistant. gc_ha_server offers
// the candidates (/ha/candidates) and picks, accepts and binds in one step
// (/ha/assign). Read-only: nothing is switched from here.
import { useEffect, useRef, useState } from "preact/hooks";
import { computed } from "@preact/signals";
import { get, post } from "./api";
import { ago, num } from "./format";
import { t } from "./i18n";
import { binding, catalog, config, info, refreshConfig, refreshState, state, toast } from "./store";
import { Banner, Button, Modal, Pill } from "./ui";

export const isHa = computed(() => info.value?.platform?.kind === "home-assistant");

type Connection = "starting" | "ok" | "unreachable" | "refused";
export type HaCandidate = { entity: string; name: string; kind: string; measures: string[]; value: number | null; used: string | null };
type HaList = { connection: Connection; candidates: HaCandidate[] };

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
const DISPLAY_ONLY = new Set(["ph", "ec", "level"]); // calibrated outside the hub (RAT-025)
const TIP = new Set(["water_temp", "air_temp", "humidity", "co2"]); // warm it by hand or breath to find it

/** The candidates, refreshed every few seconds while a view needs them. */
export function useHaList(): HaList | null {
  const [list, setList] = useState<HaList | null>(null);
  useEffect(() => {
    let alive = true;
    const load = () =>
      get<HaList>("/ha/candidates")
        .then((l) => alive && setList(l))
        .catch(() => {});
    load();
    const id = window.setInterval(load, 5000);
    return () => {
      alive = false;
      window.clearInterval(id);
    };
  }, []);
  return list;
}

const roleLabel = (role: string) => catalog.value?.roles[role]?.label ?? role;
const capOf = (measure: string) => catalog.value?.capabilities[`measure.${measure}`];
const valueText = (measure: string, v: number | null | undefined) => {
  const cap = capOf(measure);
  return v === null || v === undefined ? "" : `${num(v, cap?.decimals ?? 1)}${cap?.unit ? ` ${cap.unit}` : ""}`;
};
const byName = (a: HaCandidate, b: HaCandidate) =>
  (a.value === null ? 1 : 0) - (b.value === null ? 1 : 0) || (a.name || a.entity).localeCompare(b.name || b.entity);
const fold = (s: string) => s.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();

/** The connection state as one notice; null while all is well. */
function ConnectionNotice(p: { list: HaList | null }) {
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
  const list = useHaList();
  const [pick, setPick] = useState<{ role: string; measure: string } | null>(null);
  const st = state.value!;
  const cands = list?.candidates ?? [];
  const fits = (measure: string) => cands.filter((c) => c.measures.includes(measure));
  const rows = ROLES.filter(([role, m]) => binding(role) || fits(m).length > 0);
  const without = ROLES.filter(([role, m]) => !binding(role) && fits(m).length === 0).map(([role]) => roleLabel(role));
  const ok = list?.connection === "ok";
  return (
    <div class="stack">
      <p class="muted">{t("ha.help")}</p>
      <ConnectionNotice list={list} />
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
            return (
              <div class="item" data-testid={`ha-role-${role}`}>
                <span class="grow">
                  <span class="row" style="gap:8px">
                    <strong>{roleLabel(role)}</strong>
                    {b && DISPLAY_ONLY.has(measure) && <Pill tone="warn">{t("widgets.quality.external")}</Pill>}
                    {b && live === false && <Pill tone="bad">{t("widgets.quality.offline")}</Pill>}
                  </span>
                  {b && (
                    <div class="muted small" style="overflow-wrap:anywhere">
                      {[device?.name || entity, valueText(measure, reading?.value), reading?.ageS != null ? ago(reading.ageS) : ""].filter(Boolean).join(" · ")}
                      {live === false && entity && <div>{t("ha.missing", { entity })}</div>}
                    </div>
                  )}
                </span>
                <Button size="sm" variant={b ? "default" : "primary"} onClick={() => setPick({ role, measure })} aria-label={t("ha.chooseFor", { role: roleLabel(role) })}>
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
      {pick && <HaPicker role={pick.role} measure={pick.measure} list={list} onClose={() => setPick(null)} />}
    </div>
  );
}

/** Choosing the sensor for one role: search, live values, one tap saves. */
function HaPicker(p: { role: string; measure: string; list: HaList | null; onClose: () => void }) {
  const [q, setQ] = useState("");
  const [busy, setBusy] = useState(false);
  const search = useRef<HTMLInputElement>(null);
  const b = binding(p.role);
  const current = b?.device.startsWith("ha.") ? b.device.slice(3) : "";
  const all = (p.list?.candidates ?? []).filter((c) => c.measures.includes(p.measure)).sort(byName);
  const shown = q.trim() ? all.filter((c) => fold(`${c.name} ${c.entity}`).includes(fold(q.trim()))) : all;
  const label = roleLabel(p.role);
  useEffect(() => {
    if (all.length > 8 && window.matchMedia?.("(pointer: fine)").matches) search.current?.focus();
  }, []);
  const choose = async (entity: string, name: string) => {
    setBusy(true);
    try {
      await post("/ha/assign", { role: p.role, entity });
      await refreshConfig();
      await refreshState();
      toast(entity ? t("ha.saved", { name: name || entity, role: label }) : t("ha.removed", { role: label }));
      p.onClose();
    } catch (e: any) {
      toast(e.message, "error");
    } finally {
      setBusy(false);
    }
  };
  return (
    <Modal title={t("ha.pickerTitle", { role: label })} onClose={p.onClose} wide>
      {all.length > 8 && (
        <input ref={search} class="input" type="search" value={q} placeholder={t("ha.search")} aria-label={t("ha.searchLabel")} onInput={(e) => setQ((e.target as HTMLInputElement).value)} />
      )}
      <p class="muted small">{q.trim() ? t("ha.countFiltered", { n: shown.length, total: all.length }) : t("ha.count", { n: all.length })}</p>
      {TIP.has(p.measure) && all.length > 1 && <p class="muted small">{t("ha.tip")}</p>}
      <div class="list" data-testid="ha-picker">
        {current && (
          <button class="item ha-option" disabled={busy} onClick={() => choose("", "")}>
            <span class="grow">
              <strong>{t("ha.none")}</strong>
            </span>
          </button>
        )}
        {shown.map((c) => {
          const elsewhere = c.used !== null && c.entity !== current;
          const usedRole = elsewhere ? ROLES.find(([, m]) => m === c.used)?.[0] : undefined;
          return (
            <button
              class={`item ha-option ${c.entity === current ? "current" : ""}`}
              disabled={busy || elsewhere}
              aria-disabled={elsewhere || undefined}
              aria-current={c.entity === current || undefined}
              onClick={() => choose(c.entity, c.name)}
            >
              <span class="grow">
                <strong>{c.name || c.entity}</strong>
                <div class="muted small mono" style="overflow-wrap:anywhere">
                  {c.entity}
                </div>
                {elsewhere && <div class="muted small">{t("ha.usedFor", { role: usedRole ? roleLabel(usedRole) : c.used! })}</div>}
              </span>
              <span class="ha-value">{c.value === null ? t("widgets.quality.noData") : valueText(p.measure, c.value)}</span>
            </button>
          );
        })}
        {q.trim() && shown.length === 0 && <p class="muted">{t("ha.noMatch", { q: q.trim() })}</p>}
      </div>
    </Modal>
  );
}

/** The overview's notice on a Home Assistant hub: choose sensors, or what is wrong. */
export function HaBanner() {
  const list = useHaList();
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
