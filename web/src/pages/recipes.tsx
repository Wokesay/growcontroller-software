// Rezepte und Kanister: Nährstoffe den Pumpen zuordnen, Paare (A:B), Vorrat,
// Rezepte mit Reihenfolge, Vorlagen.
import { useState } from "preact/hooks";
import { ArrowDown, ArrowUp, FlaskConical, Pencil, Plus, RefreshCw, ScrollText, Trash2 } from "lucide-preact";
import { del, post, type Canister, type Recipe, type RecipeTemplate } from "../api";
import { lang, t } from "../i18n";
import { num } from "../format";
import { canisters, catalog, recipes, refreshConfig, refreshState, state, toast } from "../store";
import { Banner, Button, Card, Empty, Field, Modal, NumberInput, navigate } from "../ui";

const COLORS = ["#3f8f4a", "#c47a2c", "#5b7fb8", "#b8455b", "#8a5cc2", "#2f9aa0", "#9a8a2c", "#6b7280"];

const tName = (x: { name: string; nameEn?: string }) => (lang.value === "en" && x.nameEn ? x.nameEn : x.name);
const tNote = (x: RecipeTemplate) => (lang.value === "en" && x.noteEn ? x.noteEn : x.note);

/** Vorlage übernehmen: jeder Teil der Vorlage bekommt einen eigenen Kanister
 * (vorbelegt über den Namen), danach legt der Kern das Rezept an. */
function TemplateDialog(p: { tpl: RecipeTemplate; onClose: () => void }) {
  const nutrients = canisters.value.filter((k) => k.kind === "nutrient");
  const guess = (name: string) => nutrients.find((k) => k.name.trim().toLowerCase() === name.trim().toLowerCase())?.id ?? "";
  const [map, setMap] = useState<Record<string, string>>(() => Object.fromEntries(p.tpl.steps.map((s) => [s.role, guess(s.name) || guess(s.nameEn ?? "")])));
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
              await post("/recipes/template", { id: p.tpl.id, map });
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
            <a href="#/einrichtung?s=3" onClick={() => navigate("/einrichtung?s=3")}>
              {t("recipes.tpl.toSetup")}
            </a>
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
        {p.tpl.source && <p class="faint small">{t("setup.nut.source", { s: lang.value === "en" && p.tpl.sourceEn ? p.tpl.sourceEn : p.tpl.source })}</p>}
      </div>
    </Modal>
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
    capacityMl: p.can?.capacityMl ?? (1000 as number | null),
    stockMl: (p.can ? st.stock[p.can.id] : 1000) as number | null,
  });
  const set = (k: string, val: unknown) => setV({ ...v, [k]: val });
  return (
    <Modal
      title={p.can ? `Kanister „${p.can.name}“` : "Kanister anlegen"}
      onClose={p.onClose}
      footer={
        <>
          <button class="btn" onClick={p.onClose}>
            Abbrechen
          </button>
          <Button
            variant="primary"
            onClick={async () => {
              await post("/canisters", { id: p.can?.id, ...v, pair: v.kind === "nutrient" ? v.pair : "" });
              await refreshConfig();
              await refreshState();
              toast("Kanister gespeichert");
              p.onClose();
            }}
          >
            Speichern
          </Button>
        </>
      }
    >
      <div class="form-grid">
        <Field label="Name">
          <input class="input" value={v.name} placeholder="z. B. Teil A" onInput={(e) => set("name", (e.target as HTMLInputElement).value)} name="canister-name" />
        </Field>
        <Field label="Typ">
          <select class="select" value={v.kind} onChange={(e) => set("kind", (e.target as HTMLSelectElement).value)}>
            <option value="nutrient">Nährstoff</option>
            <option value="ph_down">pH− (Säure)</option>
            <option value="ph_up">pH+ (Lauge)</option>
          </select>
        </Field>
        <Field label="Pumpe" hint="Die Pumpe sitzt auf diesem Kanister">
          <select class="select" value={v.pump} onChange={(e) => set("pump", (e.target as HTMLSelectElement).value)} name="canister-pump">
            <option value="">– keine –</option>
            {caps.map((c) => (
              <option value={c.id} disabled={used.has(c.id)}>
                {c.name || c.id} · Dosierblock Port {c.slot + 1}
              </option>
            ))}
          </select>
        </Field>
        {v.kind === "nutrient" && (
          <Field label="Paar" hint="Gleiche Kennung = werden immer gemeinsam skaliert (A:B)">
            <input class="input" value={v.pair} placeholder="z. B. AB" onInput={(e) => set("pair", (e.target as HTMLInputElement).value.toUpperCase().slice(0, 8))} />
          </Field>
        )}
        <Field label="Kanistergröße">
          <NumberInput value={v.capacityMl} onValue={(x) => set("capacityMl", x)} unit="ml" />
        </Field>
        <Field label="Inhalt jetzt" hint="Leer lassen, wenn unbekannt">
          <NumberInput value={v.stockMl} onValue={(x) => set("stockMl", x)} unit="ml" />
        </Field>
      </div>
      <div class="row">
        {COLORS.map((c) => (
          <button
            aria-label={`Farbe ${c}`}
            onClick={() => set("color", c)}
            style={`width:28px;height:28px;border-radius:8px;border:${v.color === c ? "3px solid var(--text)" : "1px solid var(--border)"};background:${c};cursor:pointer`}
          />
        ))}
      </div>
    </Modal>
  );
}

function RecipeEditor(p: { recipe?: Recipe; onClose: () => void }) {
  const cans = canisters.value.filter((k) => k.kind === "nutrient");
  const [name, setName] = useState(p.recipe?.name ?? "");
  const [note, setNote] = useState(p.recipe?.note ?? "");
  const [steps, setSteps] = useState<{ canister: string; mlPerL: number | null }[]>(p.recipe?.steps ?? cans.map((k) => ({ canister: k.id, mlPerL: null })));
  const move = (i: number, d: number) => {
    const s = [...steps];
    [s[i], s[i + d]] = [s[i + d], s[i]];
    setSteps(s);
  };
  const pairs = new Set(cans.filter((k) => k.pair).map((k) => k.pair));
  return (
    <Modal
      wide
      title={p.recipe ? `Rezept „${p.recipe.name}“` : "Rezept anlegen"}
      onClose={p.onClose}
      footer={
        <>
          <button class="btn" onClick={p.onClose}>
            Abbrechen
          </button>
          <Button
            variant="primary"
            onClick={async () => {
              await post("/recipes", { id: p.recipe?.id, name, note, steps: steps.filter((s) => s.mlPerL) });
              await refreshConfig();
              toast("Rezept gespeichert");
              p.onClose();
            }}
          >
            Speichern
          </Button>
        </>
      }
    >
      <div class="form-grid">
        <Field label="Name">
          <input class="input" value={name} onInput={(e) => setName((e.target as HTMLInputElement).value)} placeholder="z. B. Wachstum Woche 1–2" />
        </Field>
        <Field label="Notiz">
          <input class="input" value={note} onInput={(e) => setNote((e.target as HTMLInputElement).value)} />
        </Field>
      </div>
      <div class="section-title">Reihenfolge = Dosierreihenfolge</div>
      <div class="steps">
        {steps.map((s, i) => {
          const k = cans.find((c) => c.id === s.canister);
          return (
            <div class="step">
              <span class="n">{i + 1}</span>
              <div class="row">
                <span class="swatch" style={`background:${k?.color}`} />
                <strong>{k?.name ?? s.canister}</strong>
                {k?.pair && <span class="faint small">Paar {k.pair}</span>}
              </div>
              <div class="row">
                <div style="width:130px">
                  <NumberInput value={s.mlPerL} onValue={(v) => setSteps(steps.map((x, j) => (j === i ? { ...x, mlPerL: v } : x)))} unit="ml/L" />
                </div>
                <button class="btn ghost sm" disabled={i === 0} onClick={() => move(i, -1)} aria-label="nach oben">
                  <ArrowUp size={15} />
                </button>
                <button class="btn ghost sm" disabled={i === steps.length - 1} onClick={() => move(i, 1)} aria-label="nach unten">
                  <ArrowDown size={15} />
                </button>
              </div>
            </div>
          );
        })}
      </div>
      <p class="muted small">
        Konzentrate nie direkt zusammengeben: Der Hub dosiert jeden Teil einzeln ins Wasser und lässt dazwischen durchmischen. pH-Korrektur gehört nicht ins Rezept – sie kommt immer zuletzt.
        {pairs.size > 0 && " Paare müssen vollständig im Rezept stehen."}
      </p>
    </Modal>
  );
}

export function RecipesPage() {
  const st = state.value!;
  const [editCan, setEditCan] = useState<Canister | null | undefined>(undefined);
  const [editRec, setEditRec] = useState<Recipe | null | undefined>(undefined);
  const cans = canisters.value;
  const rs = recipes.value;
  const templates = catalog.value?.templates?.recipes ?? [];
  const [tpl, setTpl] = useState<RecipeTemplate | null>(null);
  const pumpFlow = (id: string) => st.devices.find((d) => d.id === id)?.info?.flowMlPerMin ?? null;
  return (
    <div class="stack">
      <Card
        title="Kanister"
        icon={<FlaskConical size={18} />}
        actions={
          <Button size="sm" variant="primary" onClick={() => setEditCan(null)}>
            <Plus size={15} /> Kanister
          </Button>
        }
      >
        {!cans.length ? (
          <Empty icon={<FlaskConical size={34} />} title="Noch keine Kanister" text="Lege für jede Pumpe den Nährstoff an, der darunter steht." />
        ) : (
          <table class="table">
            <thead>
              <tr>
                <th>Kanister</th>
                <th class="hide-sm">Typ</th>
                <th>Pumpe</th>
                <th>Vorrat</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {cans.map((k) => {
                const ml = st.stock[k.id] ?? null;
                const flow = pumpFlow(k.pump);
                return (
                  <tr>
                    <td>
                      <span class="row" style="gap:8px">
                        <span class="swatch" style={`background:${k.color}`} />
                        <strong>{k.name}</strong>
                        {k.pair && <span class="faint small">Paar {k.pair}</span>}
                      </span>
                    </td>
                    <td class="hide-sm muted">{{ nutrient: "Nährstoff", ph_down: "pH−", ph_up: "pH+" }[k.kind]}</td>
                    <td class="small">
                      {k.pump ? (flow ? <span class="muted">{num(flow, 1)} ml/min</span> : <a href={`#/geraete?pump=${k.pump}`}>einmessen</a>) : <span class="faint">keine</span>}
                    </td>
                    <td>
                      <div class="row" style="gap:8px;flex-wrap:nowrap">
                        <div class={`bar ${ml !== null && ml < (k.kind === "nutrient" ? 150 : 20) ? "low" : ""}`} style="width:80px">
                          <span style={`width:${k.capacityMl && ml !== null ? Math.min(100, (ml / k.capacityMl) * 100) : 0}%`} />
                        </div>
                        <span class="small num nowrap">{ml === null ? "?" : `${num(ml, 0)} ml`}</span>
                      </div>
                    </td>
                    <td class="num nowrap">
                      <Button
                        size="sm"
                        variant="ghost"
                        title="Kanister gewechselt (voll)"
                        disabled={!k.capacityMl}
                        onClick={async () => {
                          await post(`/canisters/${k.id}/stock`, { ml: k.capacityMl });
                          await refreshState();
                          toast(`${k.name}: neuer Kanister`);
                        }}
                      >
                        <RefreshCw size={15} />
                      </Button>
                      <Button size="sm" variant="ghost" onClick={() => setEditCan(k)} title="Bearbeiten">
                        <Pencil size={15} />
                      </Button>
                      <Button
                        size="sm"
                        variant="ghost"
                        title="Entfernen"
                        onClick={async () => {
                          await del(`/canisters/${k.id}`);
                          await refreshConfig();
                          toast("Kanister entfernt");
                        }}
                      >
                        <Trash2 size={15} />
                      </Button>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        )}
      </Card>
      <Card
        title="Rezepte"
        icon={<ScrollText size={18} />}
        actions={
          <Button size="sm" variant="primary" onClick={() => setEditRec(null)} disabled={!cans.some((k) => k.kind === "nutrient")}>
            <Plus size={15} /> Rezept
          </Button>
        }
      >
        <div class="stack">
          {rs.length === 0 && <p class="muted">Noch kein Rezept.</p>}
          <div class="grid">
            {rs.map((r) => (
              <div class="card flat">
                <div class="row-between">
                  <h3>{r.name}</h3>
                  <div class="row" style="gap:2px">
                    <Button size="sm" variant="ghost" onClick={() => setEditRec(r)} title="Bearbeiten">
                      <Pencil size={15} />
                    </Button>
                    <Button
                      size="sm"
                      variant="ghost"
                      title="Entfernen"
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
                <div class="list">
                  {r.steps.map((s, i) => {
                    const k = cans.find((c) => c.id === s.canister);
                    return (
                      <div class="item">
                        <span class="faint small">{i + 1}</span>
                        <span class="swatch" style={`background:${k?.color}`} />
                        <span class="grow">{k?.name ?? s.canister}</span>
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
      {editRec !== undefined && <RecipeEditor recipe={editRec ?? undefined} onClose={() => setEditRec(undefined)} />}
      {tpl && <TemplateDialog tpl={tpl} onClose={() => setTpl(null)} />}
    </div>
  );
}
