// SPDX-License-Identifier: AGPL-3.0-or-later
// Rezepte und Kanister: Nährstoffe den Pumpen zuordnen, Paare (A:B), Vorrat,
// Rezepte mit Reihenfolge, Vorlagen.
import { useLayoutEffect, useRef, useState } from "preact/hooks";
import { ArrowDown, ArrowUp, FlaskConical, Pencil, Plus, RefreshCw, ScrollText, Trash2 } from "lucide-preact";
import { del, post, type Canister, type Recipe, type RecipeTemplate } from "../api";
import { lang, t, type TextKey } from "../i18n";
import { num, numUpTo } from "../format";
import { canisters, catalog, paramDef, readOnly, recipes, REF_EFFECT_BAND, refreshConfig, refreshState, state, toast } from "../store";
import { Banner, Button, Card, Empty, Field, Modal, NumberInput, navigate } from "../ui";

// At or below this, a stock counts as booked below zero, as on the hub
// (kStockBelowZeroMl, SD-066).
const STOCK_BELOW_ZERO_ML = -0.5;
const belowZero = (ml: number | null): boolean => ml !== null && ml <= STOCK_BELOW_ZERO_ML;
// A stock booked below zero is not offered back as a value: the field stays
// empty, so saving keeps it until the user enters what is there; a rounding
// difference just under zero is offered as 0.
const editableStock = (ml: number | null): number | null => (ml === null ? null : belowZero(ml) ? null : Math.max(0, ml));
const COLORS = ["#3f8f4a", "#c47a2c", "#5b7fb8", "#b8455b", "#8a5cc2", "#2f9aa0", "#9a8a2c", "#6b7280"];

const KIND: Record<Canister["kind"], TextKey> = { nutrient: "setup.nut.kindNutrient", ph_down: "setup.nut.kindPhDown", ph_up: "setup.nut.kindPhUp" };

const tName = (x: { name: string; nameEn?: string }) => (lang.value === "en" && x.nameEn ? x.nameEn : x.name);
const tNote = (x: RecipeTemplate) => (lang.value === "en" && x.noteEn ? x.noteEn : x.note);

/** Vorlage übernehmen: jeder Teil der Vorlage bekommt einen eigenen Kanister
 * (vorbelegt über den Namen), danach legt der Kern das Rezept an. */
function TemplateDialog(p: { tpl: RecipeTemplate; onClose: () => void }) {
  const nutrients = canisters.value.filter((k) => k.kind === "nutrient");
  const guess = (name: string) => nutrients.find((k) => k.name.trim().toLowerCase() === name.trim().toLowerCase())?.id ?? "";
  const [map, setMap] = useState<Record<string, string>>(() => Object.fromEntries(p.tpl.steps.map((s) => [s.role, guess(tName(s)) || guess(s.name) || guess(s.nameEn ?? "")])));
  const complete = p.tpl.steps.every((s) => map[s.role]);
  const twice = new Set(Object.values(map).filter(Boolean)).size < Object.values(map).filter(Boolean).length;
  return (
    <Modal
      title={tName(p.tpl)}
      onClose={p.onClose}
      footer={
        <>
          <Button onClick={p.onClose}>{t("common.cancel")}</Button>
          <Button
            variant="primary"
            disabled={!complete || twice}
            onClick={async () => {
              await post("/recipes/template", { id: p.tpl.id, map, lang: lang.value });
              await refreshConfig();
              toast(t("recipes.tpl.done", { name: tName(p.tpl) }));
              p.onClose();
            }}
          >
            {t("recipes.tpl.apply")}
          </Button>
        </>
      }
    >
      <div class="stack">
        {tNote(p.tpl) && <p class="muted">{tNote(p.tpl)}</p>}
        {nutrients.length === 0 ? (
          <Banner tone="warn">
            {t("recipes.tpl.noCanisters")}{" "}
            {!readOnly.value && (
              <a href="#/einrichtung?s=3" onClick={() => navigate("/einrichtung?s=3")}>
                {t("recipes.tpl.toSetup")}
              </a>
            )}
          </Banner>
        ) : (
          <table class="table">
            <thead>
              <tr>
                <th>{t("recipes.tpl.part")}</th>
                <th>ml/L</th>
                <th>{t("recipes.tpl.canister")}</th>
              </tr>
            </thead>
            <tbody>
              {p.tpl.steps.map((s) => (
                <tr>
                  <td>{tName(s)}</td>
                  <td class="num">{num(s.mlPerL, 1)}</td>
                  <td>
                    <select class="select" value={map[s.role] ?? ""} name={`map-${s.role}`} onChange={(e) => setMap({ ...map, [s.role]: (e.target as HTMLSelectElement).value })}>
                      <option value="">–</option>
                      {nutrients.map((k) => (
                        <option value={k.id}>{k.name}</option>
                      ))}
                    </select>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
        {twice && <Banner tone="warn">{t("recipes.tpl.twice")}</Banner>}
        {p.tpl.ec && <p class="faint small">{t("setup.nut.ec", { ec: num(p.tpl.ec, 1) })}</p>}
        {p.tpl.source && <p class="muted small">{t("setup.nut.source", { s: lang.value === "en" && p.tpl.sourceEn ? p.tpl.sourceEn : p.tpl.source })}</p>}
      </div>
    </Modal>
  );
}

// Names in quotes as a list: „A“, „B“ und „C“
function quotedList(names: string[]): string {
  const q = names.map((name) => t("recipes.quoted", { name }));
  return q.length < 2 ? q.join("") : t("recipes.listAnd", { list: q.slice(0, -1).join(", "), last: q[q.length - 1] });
}

// Removing a bottle erases its stock and cannot be undone, so it asks first
// (#120). The hub refuses it while a recipe uses the bottle, so the dialog says
// that up front, and how to take it out, instead of offering a removal that fails.
// A pair stays whole in a recipe (the hub refuses half of it), so both go.
function RemoveCanister(p: { can: Canister; flow: number | null; onClose: () => void; onEditRecipe: (r: Recipe) => void }) {
  const name = p.can.name;
  const using = recipes.value.filter((r) => r.steps.some((s) => s.canister === p.can.id));
  const partner = p.can.pair ? canisters.value.find((k) => k.id !== p.can.id && k.pair === p.can.pair) : undefined;
  if (using.length) {
    const list = quotedList(using.map((r) => r.name));
    const key = partner ? (using.length === 1 ? "recipes.removeUsedPair" : "recipes.removeUsedPairMany") : using.length === 1 ? "recipes.removeUsed" : "recipes.removeUsedMany";
    return (
      <Modal
        title={t("recipes.removeBlockedTitle")}
        message={t(key, { name, partner: partner?.name ?? "", recipes: list })}
        onClose={p.onClose}
        footer={
          using.length === 1 ? (
            <>
              <Button onClick={p.onClose}>{t("common.cancel")}</Button>
              <Button
                variant="primary"
                onClick={() => {
                  p.onClose();
                  p.onEditRecipe(using[0]);
                }}
              >
                {t("recipes.editRecipe")}
              </Button>
            </>
          ) : (
            <Button onClick={p.onClose}>{t("common.gotIt")}</Button>
          )
        }
      />
    );
  }
  const text = !p.can.pump ? "recipes.removeTextNoPump" : p.flow ? "recipes.removeText" : "recipes.removeTextUncal";
  // The last partner of a pair is no longer paired afterwards (#147)
  const unpairsPartner = partner && canisters.value.filter((k) => k.pair === p.can.pair).length === 2;
  return (
    <Modal
      title={t("recipes.removeTitle")}
      message={[t(text, { name }), unpairsPartner ? t("recipes.removeUnpairs", { partner: partner.name }) : "", t("recipes.removeUndo")].filter(Boolean).join(" ")}
      onClose={p.onClose}
      // The row is gone afterwards: the focus goes to adding a bottle
      returnFocus={() => document.getElementById("add-bottle")}
      footer={
        <>
          <Button onClick={p.onClose}>{t("common.cancel")}</Button>
          <Button
            variant="danger"
            onClick={async () => {
              await del(`/canisters/${p.can.id}`);
              toast(t("recipes.bottleRemoved", { name }));
              try {
                await refreshConfig();
              } finally {
                p.onClose();
              }
            }}
          >
            {t("recipes.remove")}
          </Button>
        </>
      }
    />
  );
}

function CanisterEditor(p: { can?: Canister; onClose: () => void }) {
  const st = state.value!;
  const caps = st.devices.filter((d) => d.class === "pump_cap" && d.configured);
  const used = new Set(canisters.value.filter((k) => k.id !== p.can?.id).map((k) => k.pump));
  const [v, setV] = useState({
    name: p.can?.name ?? "",
    kind: p.can?.kind ?? "nutrient",
    pump: p.can?.pump ?? caps.find((c) => !used.has(c.id))?.id ?? "",
    pair: p.can?.pair ?? "",
    color: p.can?.color ?? COLORS[canisters.value.length % COLORS.length],
    // An existing bottle without a size keeps none until someone enters it (RAT-015, #184)
    capacityMl: (p.can ? (p.can.capacityMl ?? null) : 1000) as number | null,
    stockMl: (p.can ? editableStock(st.stock[p.can.id] ?? null) : 1000) as number | null,
  });
  // The amount left is sent only when it was changed here: a dose booked while
  // the editor is open is not undone by saving a new name (SD-066)
  const [stockOpened] = useState(v.stockMl);
  const set = (k: string, val: unknown) => setV({ ...v, [k]: val });
  // A new bottle starts full: its stock follows its size until the stock itself is changed.
  // The size it last followed keys the amount field, so typing in the amount never rebuilds it (#184).
  const [stockTouched, setStockTouched] = useState(false);
  const [followed, setFollowed] = useState(v.capacityMl);
  // Leaving its pair takes the label from a partner left alone (#152), as removing does
  const oldPair = p.can?.pair ?? "";
  const others = oldPair && (v.kind === "nutrient" ? v.pair : "") !== oldPair ? canisters.value.filter((k) => k.id !== p.can?.id && k.pair === oldPair) : [];
  const unpairs = others.length === 1 ? others[0] : undefined;
  // What the hub would refuse is said before saving (#159): no name, joining a pair that a
  // recipe holds only part of (pairs stay whole, RAT-054), or a pH bottle a recipe still holds
  // (pH comes last, RAT-004). Leaving a pair never leaves half of it.
  const has = (r: Recipe, id: string) => r.steps.some((s) => s.canister === id);
  const newPair = v.kind === "nutrient" ? v.pair : "";
  const mates = newPair && newPair !== oldPair ? canisters.value.filter((k) => k.id !== p.can?.id && k.pair === newPair) : [];
  const half = mates.length
    ? recipes.value.filter((r) => {
        const self = !!p.can && has(r, p.can.id);
        const n = mates.filter((k) => has(r, k.id)).length;
        return (self || n > 0) && !(self && n === mates.length);
      })
    : [];
  const holding = p.can && v.kind !== "nutrient" ? recipes.value.filter((r) => has(r, p.can!.id)) : [];
  // A bottle a recipe holds as a fixed amount cannot join a pair (RAT-085, RAT-054)
  const fixedIn = p.can && newPair ? recipes.value.filter((r) => r.steps.some((s) => s.canister === p.can!.id && s.fixed)) : [];
  const problems: string[] = [];
  if (!v.name.trim()) problems.push(t("recipes.bottleNoName"));
  if (half.length) {
    // The recipe editor the text sends you to shows the stored name
    const args = { pair: newPair, recipes: quotedList(half.map((r) => r.name)), all: quotedList([...mates.map((k) => k.name), p.can?.name ?? v.name.trim()]) };
    if (!p.can) problems.push(half.length === 1 ? t("recipes.pairHalfNew", args) : t("recipes.pairHalfNewMany", args));
    else problems.push(half.length === 1 ? t("recipes.pairHalf", args) : t("recipes.pairHalfMany", args));
  }
  if (holding.length) {
    const args = { name: p.can!.name, recipes: quotedList(holding.map((r) => r.name)) };
    problems.push(holding.length === 1 ? t("recipes.phInRecipe", args) : t("recipes.phInRecipeMany", args));
  }
  if (fixedIn.length) problems.push(t("recipes.fixedInPair", { name: p.can!.name, recipes: quotedList(fixedIn.map((r) => r.name)) }));
  const conflict = half.length > 0 || holding.length > 0 || fixedIn.length > 0;
  const described = [unpairs && "bottle-unpairs", problems.length > 0 && "bottle-problems"].filter(Boolean).join(" ");
  return (
    <Modal
      title={p.can ? t("recipes.bottleTitle", { name: p.can.name }) : t("recipes.addBottle")}
      onClose={p.onClose}
      footer={
        <>
          <button class="btn" onClick={p.onClose}>
            {t("common.cancel")}
          </button>
          <Button
            variant="primary"
            disabled={problems.length > 0}
            aria-describedby={described || undefined}
            onClick={async () => {
              const stockMl = p.can && v.stockMl === stockOpened ? null : v.stockMl;
              await post("/canisters", { id: p.can?.id, ...v, stockMl, pair: v.kind === "nutrient" ? v.pair : "" });
              await refreshConfig();
              await refreshState();
              toast(t("recipes.bottleSaved"));
              p.onClose();
            }}
          >
            {t("common.save")}
          </Button>
        </>
      }
    >
      {/* Present from the start, so a screen reader reads each change (a region added with its text is often missed) */}
      <div class="sr-only" role="status">
        {[unpairs ? t("recipes.editUnpairs", { partner: unpairs.name }) : "", ...problems].filter(Boolean).join(" ")}
      </div>
      <div class="form-grid">
        <Field label={t("common.name")}>
          <input class="input" value={v.name} placeholder={t("recipes.nameHint")} onInput={(e) => set("name", (e.target as HTMLInputElement).value)} name="canister-name" />
        </Field>
        <Field label={t("recipes.type")}>
          <select class="select" value={v.kind} onChange={(e) => set("kind", (e.target as HTMLSelectElement).value)} aria-invalid={holding.length > 0 || undefined} aria-describedby={holding.length ? "bottle-problems" : undefined}>
            <option value="nutrient">{t("setup.nut.kindNutrient")}</option>
            <option value="ph_down">{t("recipes.phDownAcid")}</option>
            <option value="ph_up">{t("recipes.phUpBase")}</option>
          </select>
        </Field>
        <Field label={t("setup.nut.pump")} hint={t("recipes.pumpHint")}>
          <select class="select" value={v.pump} onChange={(e) => set("pump", (e.target as HTMLSelectElement).value)} name="canister-pump">
            <option value="">{t("recipes.noPumpOption")}</option>
            {caps.map((c) => (
              <option value={c.id} disabled={used.has(c.id)}>
                {t("recipes.pumpOption", { name: c.name || c.id, n: c.slot + 1 })}
              </option>
            ))}
          </select>
        </Field>
        {v.kind === "nutrient" && (
          <Field label={t("setup.nut.pair")} hint={t("recipes.pairHint")}>
            <input class="input" value={v.pair} placeholder={t("recipes.pairPlaceholder")} onInput={(e) => set("pair", (e.target as HTMLInputElement).value.toUpperCase().slice(0, 8))} name="canister-pair" aria-invalid={half.length > 0 || undefined} aria-describedby={half.length ? "bottle-problems" : undefined} />
          </Field>
        )}
        <Field label={t("recipes.bottleSize")}>
          <NumberInput
            value={v.capacityMl}
            onValue={(x) => {
              const follow = !p.can && !stockTouched;
              setV({ ...v, capacityMl: x, ...(follow ? { stockMl: x } : {}) });
              if (follow) setFollowed(x);
            }}
            unit="ml"
            name="canister-size"
          />
        </Field>
        <Field
          label={t("recipes.stockNow")}
          hint={
            !p.can
              ? t("recipes.stockNowHint")
              : belowZero(st.stock[p.can.id] ?? null)
                ? t("recipes.stockBelowZero", { ml: num(st.stock[p.can.id], 0) })
                : (st.stock[p.can.id] ?? null) === null
                  ? t("recipes.stockUnknownHint")
                  : t("recipes.stockCountedHint")
          }
        >
          <NumberInput
            // While it follows the size, it shows each new size, also an empty one
            key={!p.can ? `size-${followed}` : "own"}
            value={v.stockMl}
            onValue={(x) => {
              setStockTouched(true);
              set("stockMl", x);
            }}
            unit="ml"
            name="canister-stock"
          />
        </Field>
      </div>
      <div class="row">
        {COLORS.map((c) => (
          <button
            aria-label={t("recipes.colour", { c })}
            onClick={() => set("color", c)}
            style={`width:28px;height:28px;border-radius:8px;border:${v.color === c ? "3px solid var(--text)" : "1px solid var(--border)"};background:${c};cursor:pointer`}
          />
        ))}
      </div>
      {unpairs && (
        <p class="small muted" id="bottle-unpairs">
          {t("recipes.editUnpairs", { partner: unpairs.name })}
        </p>
      )}
      {problems.length > 0 && (
        <p class={`small ${conflict ? "warn-text" : "muted"}`} id="bottle-problems">
          {problems.join(" ")}
        </p>
      )}
    </Modal>
  );
}

type Step = { canister: string; mlPerL: number | null; fixed?: boolean };
// Only a bottle with an amount is in the recipe; the hub takes nothing else. An empty field or 0
// leaves it out; a negative or endless number is said to be invalid.
const filled = (s: Step) => s.mlPerL !== null && Number.isFinite(s.mlPerL) && s.mlPerL > 0;
const invalid = (s: Step) => s.mlPerL !== null && (!Number.isFinite(s.mlPerL) || s.mlPerL < 0);
// The next row with an amount above or below row i
function neighbour(list: Step[], i: number, d: number) {
  for (let j = i + d; j >= 0 && j < list.length; j += d) if (filled(list[j])) return j;
  return -1;
}

function RecipeEditor(p: { recipe?: Recipe; onClose: () => void; returnFocus?: () => HTMLElement | null }) {
  const cans = canisters.value.filter((k) => k.kind === "nutrient");
  const [name, setName] = useState(p.recipe?.name ?? "");
  const [note, setNote] = useState(p.recipe?.note ?? "");
  // The chart's EC at these amounts and its pH range: a reference and the grow's proposal (SD-057)
  const [refEc, setRefEc] = useState<number | null>(p.recipe?.refEc ?? null);
  const [phLow, setPhLow] = useState<number | null>(p.recipe?.ph?.[0] ?? null);
  const [phHigh, setPhHigh] = useState<number | null>(p.recipe?.ph?.[1] ?? null);
  // Every nutrient bottle has a row: the recipe's own first, in their order, then the others
  // empty. An amount puts a bottle in, clearing it takes it out (#159).
  const [steps, setSteps] = useState<Step[]>(() => {
    const own = p.recipe?.steps ?? [];
    return [...own, ...cans.filter((k) => !own.some((s) => s.canister === k.id)).map((k) => ({ canister: k.id, mlPerL: null }))];
  });
  // Up and down pass over the rows without an amount: only the dosing order counts
  const moved = useRef<{ id: string; dir: "up" | "down" } | null>(null);
  const move = (id: string, d: number) =>
    setSteps((cur) => {
      const i = cur.findIndex((x) => x.canister === id);
      const j = neighbour(cur, i, d);
      if (i < 0 || j < 0) return cur;
      const s = [...cur];
      const [row] = s.splice(i, 1);
      s.splice(j, 0, row);
      moved.current = { id, dir: d < 0 ? "up" : "down" };
      return s;
    });
  // The focus moves with the row, to its other button once it reaches an end
  const list = useRef<HTMLDivElement>(null);
  useLayoutEffect(() => {
    const m = moved.current;
    moved.current = null;
    if (!m || !list.current) return;
    const row = Array.from(list.current.querySelectorAll<HTMLElement>("[data-step]")).find((el) => el.dataset.step === m.id);
    const same = row?.querySelector<HTMLButtonElement>(`button[data-dir="${m.dir}"]`);
    const other = row?.querySelector<HTMLButtonElement>(`button[data-dir="${m.dir === "up" ? "down" : "up"}"]`);
    (same && !same.disabled ? same : other)?.focus();
  }, [steps]);
  // What the hub would refuse is said before saving, with the way out (#159); a pair goes in whole
  // or not at all (RAT-054)
  const inRecipe = new Set(steps.filter(filled).map((s) => s.canister));
  const pairs = [...new Set(cans.filter((k) => k.pair).map((k) => k.pair))];
  const problems: string[] = [];
  if (!name.trim()) problems.push(t("recipes.noName"));
  const bad = steps.filter(invalid).map((s) => cans.find((k) => k.id === s.canister)?.name ?? s.canister);
  if (bad.length) problems.push(bad.length === 1 ? t("recipes.amountInvalid", { names: quotedList(bad) }) : t("recipes.amountInvalidMany", { names: quotedList(bad) }));
  else if (!inRecipe.size) problems.push(t("recipes.noAmount"));
  let conflict = bad.length > 0;
  for (const pair of pairs) {
    const all = cans.filter((k) => k.pair === pair);
    const missing = all.filter((k) => !inRecipe.has(k.id));
    if (all.length < 2 || !missing.length || missing.length === all.length) continue;
    const args = { pair, missing: quotedList(missing.map((k) => k.name)) };
    problems.push(missing.length === 1 ? t("recipes.pairMissing", args) : t("recipes.pairMissingMany", args));
    conflict = true;
  }
  // The chart's EC lies in the target's range and fits the amounts; the pH range is low to high in the
  // pH target's range – as the hub checks it (SD-057)
  const ecDef = paramDef("ec_control", "ec_target");
  const phDef = paramDef("ph_control", "ph_target");
  if (refEc !== null) {
    // Over the parts EC control scales, as on the hub (RAT-085)
    const sum = steps.filter((s) => filled(s) && !s.fixed).reduce((a, s) => a + (s.mlPerL ?? 0), 0);
    const lo = ecDef?.min ?? 0, hi = ecDef?.max ?? Infinity;
    if (!Number.isFinite(refEc) || refEc <= 0 || refEc < lo || refEc > hi) {
      problems.push(t("recipes.refRange", { min: numUpTo(lo, 2), max: numUpTo(Number.isFinite(hi) ? hi : null, 2) }));
      conflict = true;
    } else if (sum > 0 && (refEc / sum < REF_EFFECT_BAND[0] || refEc / sum > REF_EFFECT_BAND[1])) {
      // The EC that would fit these amounts, within the target's range where it can be
      let [fitLo, fitHi] = [REF_EFFECT_BAND[0] * sum, REF_EFFECT_BAND[1] * sum];
      if (Math.max(fitLo, lo) <= Math.min(fitHi, hi)) [fitLo, fitHi] = [Math.max(fitLo, lo), Math.min(fitHi, hi)];
      const anyFixed = steps.some((s) => filled(s) && s.fixed);
      const amounts = t(anyFixed ? "recipes.amountsScaled" : "recipes.amounts", { sum: numUpTo(sum, 2) });
      const args = { ec: numUpTo(refEc, 2), amounts, lo: numUpTo(fitLo, 2), hi: numUpTo(fitHi, 2) };
      problems.push(refEc / sum < REF_EFFECT_BAND[0] ? t("recipes.refFar", args) : t("recipes.refStrong", args));
      conflict = true;
    }
  }
  // Amounts changed under a chart EC: does it still match? A note, not a problem
  const amountsOf = (list: Step[]) => JSON.stringify(list.filter(filled).map((s) => [s.canister, s.mlPerL, !!s.fixed]));
  const amountsChanged = !!p.recipe && refEc !== null && amountsOf(p.recipe.steps) !== amountsOf(steps);
  // Fixed amounts only: EC control could not raise the EC with this recipe. A note, Save stays on
  const onlyFixed = inRecipe.size > 0 && steps.filter(filled).every((s) => s.fixed);
  if (phLow !== null || phHigh !== null) {
    const lo = phDef?.min ?? 0, hi = phDef?.max ?? 14;
    if (phLow === null || phHigh === null || phLow > phHigh || phLow < lo || phHigh > hi) {
      problems.push(t("recipes.phBand", { min: numUpTo(lo, 2), max: numUpTo(hi, 2) }));
      conflict = true;
    }
  }
  return (
    <Modal
      wide
      title={p.recipe ? t("recipes.recipeTitle", { name: p.recipe.name }) : t("recipes.newRecipe")}
      onClose={p.onClose}
      returnFocus={p.returnFocus}
      footer={
        <>
          <button class="btn" onClick={p.onClose}>
            {t("common.cancel")}
          </button>
          <Button
            variant="primary"
            disabled={problems.length > 0}
            aria-describedby={problems.length ? "recipe-problems" : undefined}
            onClick={async () => {
              // A bottle of a pair is never a fixed amount (RAT-054): a flag from an import goes on saving
              const paired = new Set(cans.filter((k) => k.pair).map((k) => k.id));
              const saved = steps.filter(filled).map((s) => ({ ...s, fixed: !!s.fixed && !paired.has(s.canister) }));
              await post("/recipes", { id: p.recipe?.id, name, note, steps: saved, refEc, ph: phLow !== null && phHigh !== null ? [phLow, phHigh] : null });
              await refreshConfig();
              toast(t("recipes.recipeSaved"));
              p.onClose();
            }}
          >
            {t("common.save")}
          </Button>
        </>
      }
    >
      {/* Present from the start, so a screen reader reads each change */}
      <div class="sr-only" role="status">
        {[...problems, onlyFixed ? t("recipes.onlyFixed") : ""].filter(Boolean).join(" ")}
      </div>
      <div class="form-grid">
        <Field label={t("common.name")}>
          <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} placeholder={t("recipes.recipeNameHint")} />
        </Field>
        <Field label={t("recipes.note")}>
          <input class="input" value={note} onInput={(e) => setNote((e.target as HTMLInputElement).value)} />
        </Field>
      </div>
      <div>
        <div class="section-title">{t("recipes.order")}</div>
        <p class="muted small">{t("recipes.emptyLeftOut")}</p>
      </div>
      <div class="steps recipe-steps" ref={list}>
        {steps.map((s, i) => {
          const k = cans.find((c) => c.id === s.canister);
          const label = k?.name ?? s.canister;
          const on = filled(s);
          // Without an amount the number and the order buttons keep their place but are not there
          const hidden = on ? undefined : "visibility:hidden";
          return (
            <div class="step" key={s.canister} data-step={s.canister}>
              <span class="n" style={hidden}>
                {on ? steps.slice(0, i + 1).filter(filled).length : ""}
              </span>
              <div class="stack-sm" style="gap:2px">
                <div class="row">
                  <span class="swatch" style={`background:${k?.color}`} />
                  <strong>{label}</strong>
                  {k?.pair && <span class="muted small">{t("recipes.pairOf", { p: k.pair })}</span>}
                </div>
                {/* A fixed addition, never one of a pair: a pair goes in together (SD-057, RAT-054) */}
                {on && !k?.pair && (
                  <label class="row small" style="gap:6px;min-height:32px">
                    <input
                      type="checkbox"
                      aria-label={t("recipes.fixedOf", { name: label })}
                      aria-describedby="recipe-fixed-note"
                      checked={!!s.fixed}
                      onChange={(e) => setSteps((cur) => cur.map((x) => (x.canister === s.canister ? { ...x, fixed: (e.target as HTMLInputElement).checked } : x)))}
                    />
                    {t("recipes.fixed")}
                  </label>
                )}
              </div>
              <div class="row">
                <div style="width:130px">
                  <NumberInput ariaLabel={t("recipes.amountOf", { name: label })} value={s.mlPerL} onValue={(v) => setSteps((cur) => cur.map((x) => (x.canister === s.canister ? { ...x, mlPerL: v } : x)))} unit="ml/L" />
                </div>
                <button class="btn ghost sm" style={hidden} data-dir="up" disabled={!on || neighbour(steps, i, -1) < 0} onClick={() => move(s.canister, -1)} aria-label={t("recipes.moveUp", { name: label })}>
                  <ArrowUp size={15} />
                </button>
                <button class="btn ghost sm" style={hidden} data-dir="down" disabled={!on || neighbour(steps, i, 1) < 0} onClick={() => move(s.canister, 1)} aria-label={t("recipes.moveDown", { name: label })}>
                  <ArrowDown size={15} />
                </button>
              </div>
            </div>
          );
        })}
      </div>
      {/* Below the amounts, which "these amounts" points back to */}
      <div class="form-grid">
        <Field label={t("recipes.refEc")} hint={amountsChanged ? t("recipes.amountsChanged") : t("recipes.refEcHint")}>
          <NumberInput value={refEc} onValue={setRefEc} unit="mS/cm" />
        </Field>
        {/* Two inputs: a group, not one label, so each is named and the hint read with both */}
        <div class="field" role="group" aria-labelledby="recipe-ph-range" aria-describedby="recipe-ph-hint">
          <span class="field-label" id="recipe-ph-range">
            {t("recipes.phRange")}
          </span>
          <div class="row" style="flex-wrap:nowrap">
            <NumberInput ariaLabel={t("recipes.phLow")} value={phLow} onValue={setPhLow} />
            <span class="muted" aria-hidden="true">
              –
            </span>
            <NumberInput ariaLabel={t("recipes.phHigh")} value={phHigh} onValue={setPhHigh} />
          </div>
          <small class="hint" id="recipe-ph-hint">
            {t("recipes.phRangeHint")}
          </small>
        </div>
      </div>
      {onlyFixed && <p class="small warn-text">{t("recipes.onlyFixed")}</p>}
      <p class="muted small" id="recipe-fixed-note">
        {t("recipes.fixedNote")}
        {pairs.length > 0 && ` ${t("recipes.fixedNoPair")}`}
      </p>
      <p class="muted small">
        {t("recipes.mixNote")}
        {pairs.length > 0 && ` ${t("recipes.pairsComplete")}`}
      </p>
      {problems.length > 0 && (
        <p class={`small ${conflict ? "warn-text" : "muted"}`} id="recipe-problems">
          {problems.join(" ")}
        </p>
      )}
    </Modal>
  );
}

export function RecipesPage() {
  const st = state.value!;
  const [editCan, setEditCan] = useState<Canister | null | undefined>(undefined);
  const [removeCan, setRemoveCan] = useState<Canister | null>(null);
  const [editRec, setEditRec] = useState<Recipe | null | undefined>(undefined);
  // A recipe opened from "Bottle still in use" hands the focus back to that bottle's remove button
  const [editFrom, setEditFrom] = useState<string | null>(null);
  const cans = canisters.value;
  const rs = recipes.value;
  // A stock the hub does not know is said with the way to enter it, for example after a backup (#184)
  const unknownStock = cans.filter((k) => (st.stock[k.id] ?? null) === null);
  const templates = catalog.value?.templates?.recipes ?? [];
  const [tpl, setTpl] = useState<RecipeTemplate | null>(null);
  const pumpFlow = (id: string) => st.devices.find((d) => d.id === id)?.info?.flowMlPerMin ?? null;
  return (
    <div class="stack">
      <Card
        title={t("recipes.bottles")}
        icon={<FlaskConical size={18} />}
        actions={
          <Button size="sm" variant="primary" id="add-bottle" onClick={() => setEditCan(null)}>
            <Plus size={15} /> {t("term.bottle")}
          </Button>
        }
      >
        {!cans.length ? (
          <Empty icon={<FlaskConical size={34} />} title={t("recipes.noBottles")} text={t("recipes.noBottlesText")} />
        ) : (
          <>
            {unknownStock.length > 0 && (
              <Banner tone="info">
                {unknownStock.length === 1 ? t("recipes.stockUnknownOne", { name: unknownStock[0].name }) : t("recipes.stockUnknownMany", { n: unknownStock.length })}
              </Banner>
            )}
            <table class="table rtable">
              <thead>
                <tr>
                  <th>{t("term.bottle")}</th>
                  <th class="hide-sm">{t("recipes.type")}</th>
                  <th>{t("setup.nut.pump")}</th>
                  <th>{t("recipes.stock")}</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {cans.map((k) => {
                  const ml = st.stock[k.id] ?? null;
                  const flow = pumpFlow(k.pump);
                  return (
                    <tr key={k.id}>
                      <td class="full">
                        <span class="row" style="gap:8px">
                          <span class="swatch" style={`background:${k.color}`} />
                          <strong>{k.name}</strong>
                          {k.pair && <span class="muted small">{t("recipes.pairOf", { p: k.pair })}</span>}
                        </span>
                      </td>
                      <td class="hide-sm muted">{t(KIND[k.kind])}</td>
                      <td class="small" data-label={t("setup.nut.pump")}>
                        {k.pump ? (flow ? <span class="muted">{num(flow, 1)} ml/min</span> : <a href={`#/geraete?pump=${k.pump}`}>{t("recipes.calibrate")}</a>) : <span class="faint">{t("recipes.noPump")}</span>}
                      </td>
                      <td data-label={t("recipes.stock")}>
                        <div class="row" style="gap:8px;flex-wrap:nowrap">
                          <div class={`bar ${ml === null ? "unknown" : ml < (k.kind === "nutrient" ? 150 : 20) ? "low" : ""}`} style="width:80px">
                            <span style={`width:${k.capacityMl && ml !== null ? Math.max(2, Math.min(100, (ml / k.capacityMl) * 100)) : 0}%`} />
                          </div>
                          <span class="small num nowrap" style={belowZero(ml) ? "color:var(--bad)" : undefined}>
                            {ml === null ? t("area.unknown") : `${num(ml, 0)} ml`}
                          </span>
                        </div>
                      </td>
                      <td class="num nowrap full actions">
                        <Button
                          size="sm"
                          variant="ghost"
                          title={k.capacityMl ? t("recipes.bottleChanged") : t("recipes.bottleChangedNoSize")}
                          disabled={!k.capacityMl}
                          onClick={async () => {
                            await post(`/canisters/${k.id}/stock`, { ml: k.capacityMl });
                            await refreshState();
                            toast(t("recipes.newBottle", { name: k.name }));
                          }}
                        >
                          <RefreshCw size={15} />
                        </Button>
                        <Button size="sm" variant="ghost" onClick={() => setEditCan(k)} title={t("common.edit")}>
                          <Pencil size={15} />
                        </Button>
                        <Button size="sm" variant="ghost" title={t("recipes.remove")} aria-label={t("recipes.removeNamed", { name: k.name })} data-remove={k.id} onClick={() => setRemoveCan(k)}>
                          <Trash2 size={15} />
                        </Button>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </>
        )}
      </Card>
      <Card
        title={t("recipes.recipes")}
        icon={<ScrollText size={18} />}
        actions={
          <Button size="sm" variant="primary" onClick={() => setEditRec(null)} disabled={!cans.some((k) => k.kind === "nutrient")}>
            <Plus size={15} /> {t("recipes.recipe")}
          </Button>
        }
      >
        <div class="stack">
          {rs.length === 0 && <p class="muted">{t("setup.nut.none")}</p>}
          <div class="grid">
            {rs.map((r) => (
              <div class="card flat" key={r.id}>
                <div class="row-between">
                  <h3>{r.name}</h3>
                  <div class="row" style="gap:2px">
                    <Button size="sm" variant="ghost" onClick={() => setEditRec(r)} title={t("common.edit")}>
                      <Pencil size={15} />
                    </Button>
                    <Button
                      size="sm"
                      variant="ghost"
                      title={t("recipes.remove")}
                      onClick={async () => {
                        await del(`/recipes/${r.id}`);
                        await refreshConfig();
                      }}
                    >
                      <Trash2 size={15} />
                    </Button>
                  </div>
                </div>
                {r.note && <p class="muted small">{r.note}</p>}
                {(r.refEc != null || r.ph) && (
                  <p class="muted small" data-testid="recipe-chart">
                    {[r.refEc != null ? t("recipes.chartEc", { ec: numUpTo(r.refEc, 2) }) : "", r.ph ? t("recipes.chartPh", { lo: numUpTo(r.ph[0], 2), hi: numUpTo(r.ph[1], 2) }) : ""].filter(Boolean).join(" · ")}
                  </p>
                )}
                <div class="list">
                  {r.steps.map((s, i) => {
                    const k = cans.find((c) => c.id === s.canister);
                    return (
                      <div class="item">
                        <span class="faint small">{i + 1}</span>
                        <span class="swatch" style={`background:${k?.color}`} />
                        <span class="grow">
                          {k?.name ?? s.canister}
                          {s.fixed && <span class="faint small"> · {t("recipes.fixedTag")}</span>}
                        </span>
                        <span class="num">{num(s.mlPerL, 2)} ml/L</span>
                      </div>
                    );
                  })}
                </div>
              </div>
            ))}
          </div>
          {templates.length > 0 && (
            <div class="stack-sm" data-testid="templates">
              <div class="section-title">{t("recipes.tpl.title")}</div>
              <div class="tpl-grid">
                {templates.map((x) => (
                  <button type="button" class="tpl" onClick={() => setTpl(x)}>
                    <strong>{tName(x)}</strong>
                    <div class="tpl-rows">
                      {x.steps.map((s) => (
                        <div>
                          <span>{tName(s)}</span>
                          <span class="faint">{num(s.mlPerL, 1)} ml/L</span>
                        </div>
                      ))}
                    </div>
                    {x.ec && <span class="faint small">{t("setup.nut.ec", { ec: num(x.ec, 1) })}</span>}
                    <span class="link small">{t("recipes.tpl.use")} →</span>
                  </button>
                ))}
              </div>
              <p class="faint small">{t("recipes.tpl.hint")}</p>
            </div>
          )}
        </div>
      </Card>
      {editCan !== undefined && <CanisterEditor can={editCan ?? undefined} onClose={() => setEditCan(undefined)} />}
      {removeCan && (
        <RemoveCanister
          can={removeCan}
          flow={pumpFlow(removeCan.pump)}
          onClose={() => setRemoveCan(null)}
          onEditRecipe={(r) => {
            setEditFrom(removeCan.id);
            setEditRec(r);
          }}
        />
      )}
      {editRec !== undefined && (
        <RecipeEditor
          recipe={editRec ?? undefined}
          onClose={() => {
            setEditRec(undefined);
            setEditFrom(null);
          }}
          returnFocus={editFrom ? () => document.querySelector<HTMLElement>(`[data-remove="${CSS.escape(editFrom)}"]`) : undefined}
        />
      )}
      {tpl && <TemplateDialog tpl={tpl} onClose={() => setTpl(null)} />}
    </div>
  );
}
