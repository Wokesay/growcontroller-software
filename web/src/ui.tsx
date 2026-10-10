// SPDX-License-Identifier: AGPL-3.0-or-later
// Bausteine der Oberfläche: Karten, Knöpfe, Felder, Dialoge, Status.
import { Component, type ComponentChildren, type JSX } from "preact";
import { useEffect, useLayoutEffect, useRef, useState } from "preact/hooks";
import { signal } from "@preact/signals";
import { AlertTriangle, Check, CheckCircle2, HelpCircle, Info, Minus, X, XCircle } from "lucide-preact";
import { lang, t, type TextKey } from "./i18n";
import { toastError, toasts } from "./store";

// ---------- Router (Hash, damit die App aus dem Flash ohne Server-Routing läuft)
function parseHash() {
  const h = location.hash.replace(/^#/, "") || "/";
  const [path, q = ""] = h.split("?");
  return { path, query: Object.fromEntries(new URLSearchParams(q)) as Record<string, string> };
}
export const route = signal(parseHash());
window.addEventListener("hashchange", () => (route.value = parseHash()));
export function navigate(to: string) {
  location.hash = to.startsWith("#") ? to : `#${to}`;
}

// Behebungsschritt des Resolvers → Ziel in der App
export function fixHref(fix: string): string | null {
  if (!fix) return null;
  if (fix === "expand") return "#/geraete?tab=erweitern";
  if (fix === "roles") return "#/geraete?tab=zuordnung";
  if (fix === "canisters" || fix === "recipes") return "#/rezepte";
  if (fix === "tank") return "#/tank";
  if (fix.startsWith("function:")) return `#/funktionen?f=${fix.slice(9)}`;
  if (fix.startsWith("calibrate-pump:")) return `#/geraete?pump=${fix.slice(15)}`;
  if (fix.startsWith("calibrate:")) return `#/geraete?cal=${encodeURIComponent(fix.slice(10))}`;
  return null;
}

// ---------- Grundbausteine
export function Card(p: { title?: ComponentChildren; icon?: ComponentChildren; actions?: ComponentChildren; children?: ComponentChildren; class?: string; id?: string }) {
  return (
    <section class={`card ${p.class ?? ""}`} id={p.id}>
      {(p.title || p.actions) && (
        <div class="card-h">
          <h2>
            {p.icon}
            {p.title}
          </h2>
          {p.actions && <div class="row">{p.actions}</div>}
        </div>
      )}
      {p.children}
    </section>
  );
}

type BtnProps = Omit<JSX.HTMLAttributes<HTMLButtonElement>, "onClick" | "size"> & {
  variant?: "primary" | "danger" | "danger-soft" | "ghost" | "default";
  size?: "sm" | "lg";
  block?: boolean;
  onClick?: () => unknown | Promise<unknown>;
  disabled?: boolean;
  type?: "button" | "submit";
};
export function Button({ variant = "default", size, block, onClick, children, disabled, type = "button", ...rest }: BtnProps) {
  const [busy, setBusy] = useState(false);
  const cls = ["btn", variant !== "default" ? variant : "", size ?? "", block ? "block" : ""].join(" ");
  return (
    <button
      {...rest}
      type={type}
      class={cls}
      disabled={disabled || busy}
      onClick={async () => {
        if (!onClick) return;
        try {
          setBusy(true);
          await onClick();
        } catch (e) {
          toastError(e);
        } finally {
          setBusy(false);
        }
      }}
    >
      {children}
    </button>
  );
}

export type Tone = "ok" | "bad" | "warn" | "info" | "neutral" | "accent";
export function Pill(p: { tone?: Tone; dot?: boolean; pulse?: boolean; wrap?: boolean; children: ComponentChildren; title?: string }) {
  return (
    <span class={`pill ${p.tone ?? ""}${p.wrap ? " wrap" : ""}`} title={p.title}>
      {p.dot && <span class={`dot ${p.pulse ? "pulse" : ""}`} />}
      {p.children}
    </span>
  );
}

// Erklärung zu einem Begriff: ⓘ öffnet Text und, falls vorhanden, „So misst du es“.
// Ein <span role="button"> statt <button>, damit das umgebende <label> weiter
// das Eingabefeld meint.
export type HelpTopic = "usableVolume" | "minLevel" | "baseVolume" | "bottle" | "pair" | "calibratePump" | "prime" | "calibrateProbe" | "levelCurve" | "tolerance" | "ecGate" | "waitTime" | "latched" | "jumpLock" | "maintenance" | "phase" | "vpd";
const MEASURE: Partial<Record<HelpTopic, TextKey>> = { usableVolume: "measure.usableVolume", minLevel: "measure.minLevel" };

export function HelpButton(p: { open: boolean; onToggle: () => void; topic: HelpTopic }) {
  const toggle = (e: Event) => {
    e.preventDefault();
    e.stopPropagation();
    p.onToggle();
  };
  return (
    <span
      class="help-btn"
      role="button"
      tabIndex={0}
      aria-expanded={p.open}
      aria-label={`${t("common.help")}: ${t(`term.${p.topic}` as TextKey)}`}
      onClick={toggle}
      onKeyDown={(e) => (e.key === "Enter" || e.key === " ") && toggle(e)}
    >
      <HelpCircle size={15} />
    </span>
  );
}

export function HelpBox(p: { topic: HelpTopic }) {
  const m = MEASURE[p.topic];
  return (
    <div class="help-box" role="note">
      <span>{t(`help.${p.topic}` as TextKey)}</span>
      {m && <span>{t(m)}</span>}
    </div>
  );
}

/** Begriff mit ⓘ und aufklappbarer Erklärung, z. B. in Überschriften und Listen. */
export function Term(p: { topic: HelpTopic; children?: ComponentChildren }) {
  const [open, setOpen] = useState(false);
  return (
    <span class="term">
      <span class="field-label">
        {p.children ?? t(`term.${p.topic}` as TextKey)}
        <HelpButton topic={p.topic} open={open} onToggle={() => setOpen(!open)} />
      </span>
      {open && <HelpBox topic={p.topic} />}
    </span>
  );
}

export function Field(p: { label: ComponentChildren; hint?: ComponentChildren; error?: string | null; help?: HelpTopic; children: ComponentChildren }) {
  const [open, setOpen] = useState(false);
  const field = (
    <label class="field">
      <span class="field-label">
        {p.label}
        {p.help && <HelpButton topic={p.help} open={open} onToggle={() => setOpen(!open)} />}
      </span>
      {p.children}
      {p.error ? <small class="err">{p.error}</small> : p.hint ? <small class="hint">{p.hint}</small> : null}
    </label>
  );
  if (!p.help) return field;
  return (
    <div class="field-wrap">
      {field}
      {open && <HelpBox topic={p.help} />}
    </div>
  );
}

// Decimal comma in German, decimal point in English (PD-035); typing accepts both.
const shown = (v: number) => (lang.value === "de" ? String(v).replace(".", ",") : String(v));

export function NumberInput(p: { value: number | null | undefined; onValue: (v: number | null) => void; unit?: string; min?: number; max?: number; step?: number; placeholder?: string; id?: string; name?: string }) {
  const [text, setText] = useState(p.value === null || p.value === undefined ? "" : shown(p.value));
  useEffect(() => {
    const cur = parseFloat(text.replace(",", "."));
    if (p.value !== null && p.value !== undefined && cur !== p.value) setText(shown(p.value));
    if ((p.value === null || p.value === undefined) && text !== "" && Number.isNaN(cur)) setText("");
  }, [p.value]);
  // A language switch shows the value again with the page's separator.
  useEffect(() => {
    if (p.value !== null && p.value !== undefined) setText(shown(p.value));
  }, [lang.value]);
  const input = (
    <input
      class="input"
      inputMode="decimal"
      id={p.id}
      name={p.name}
      value={text}
      placeholder={p.placeholder}
      onInput={(e) => {
        const t = (e.target as HTMLInputElement).value;
        setText(t);
        const v = parseFloat(t.replace(",", "."));
        p.onValue(t.trim() === "" || Number.isNaN(v) ? null : v);  // leer bleibt leer, nie 0
      }}
    />
  );
  return p.unit ? (
    <div class="input-unit">
      {input}
      <span class="u">{p.unit}</span>
    </div>
  ) : (
    input
  );
}

export function Toggle(p: { checked: boolean; onChange: (v: boolean) => void; disabled?: boolean; label?: string }) {
  return (
    <label class="toggle" title={p.label}>
      <input type="checkbox" checked={p.checked} disabled={p.disabled} aria-label={p.label} onChange={(e) => p.onChange((e.target as HTMLInputElement).checked)} />
      <span />
    </label>
  );
}

export function Seg<T extends string>(p: { value: T; options: [T, string][]; onChange: (v: T) => void }) {
  return (
    <div class="seg" role="tablist">
      {p.options.map(([v, l]) => (
        <button type="button" role="tab" aria-selected={p.value === v} class={p.value === v ? "on" : ""} onClick={() => p.onChange(v)}>
          {l}
        </button>
      ))}
    </div>
  );
}

export function Modal(p: { title: ComponentChildren; onClose: () => void; children: ComponentChildren; footer?: ComponentChildren; wide?: boolean }) {
  // Escape calls the current onClose, not the one from the first render. The key is
  // listened for as soon as the window is in the page, not only after the next frame.
  const onClose = useRef(p.onClose);
  onClose.current = p.onClose;
  useLayoutEffect(() => {
    const k = (e: KeyboardEvent) => e.key === "Escape" && onClose.current();
    window.addEventListener("keydown", k);
    return () => window.removeEventListener("keydown", k);
  }, []);
  return (
    <div class="overlay" onClick={(e) => e.target === e.currentTarget && p.onClose()}>
      <div class={`modal ${p.wide ? "wide" : ""}`} role="dialog" aria-modal="true">
        <div class="row-between">
          <h2>{p.title}</h2>
          <button class="btn ghost sm" onClick={p.onClose} aria-label={t("common.close")}>
            <X size={18} />
          </button>
        </div>
        {p.children}
        {p.footer && <div class="modal-foot">{p.footer}</div>}
      </div>
    </div>
  );
}

export function Toasts() {
  return (
    <div class="toasts" aria-live="polite">
      {toasts.value.map((t) => (
        <div class={`toast ${t.kind}`} key={t.id}>
          {t.kind === "error" ? <XCircle size={18} /> : t.kind === "ok" ? <CheckCircle2 size={18} /> : <Info size={18} />}
          <span>{t.text}</span>
        </div>
      ))}
    </div>
  );
}

export function Banner(p: { tone?: "info" | "warn" | "bad" | "ok"; children: ComponentChildren; icon?: ComponentChildren }) {
  const icon = p.icon ?? (p.tone === "bad" || p.tone === "warn" ? <AlertTriangle size={18} /> : p.tone === "ok" ? <CheckCircle2 size={18} /> : <Info size={18} />);
  return (
    <div class={`banner ${p.tone ?? ""}`}>
      {icon}
      <div>{p.children}</div>
    </div>
  );
}

/** Catches an error while drawing its part, so the rest of the page and the shell with STOP stay usable. */
export class ErrorBoundary extends Component<{ children: ComponentChildren }, { failed: boolean }> {
  state = { failed: false };
  static getDerivedStateFromError() {
    return { failed: true };
  }
  componentDidCatch(e: unknown) {
    console.error(e);  // still diagnosable in the browser console
  }
  render() {
    if (!this.state.failed) return this.props.children;
    return (
      <Banner tone="bad">
        <div class="stack-sm">
          <p>{t("common.renderError")}</p>
          <div>
            <button class="btn" onClick={() => location.reload()}>
              {t("common.reload")}
            </button>
          </div>
        </div>
      </Banner>
    );
  }
}

export function Empty(p: { icon?: ComponentChildren; title: string; text?: ComponentChildren; action?: ComponentChildren }) {
  return (
    <div class="empty">
      {p.icon}
      <h3>{p.title}</h3>
      {p.text && <p class="muted">{p.text}</p>}
      {p.action}
    </div>
  );
}

export function CheckRow(p: { ok: boolean; soft?: boolean; text: string; fix?: string | null; neutral?: boolean }) {
  const href = p.fix ? fixHref(p.fix) : null;
  return (
    <div class={`check ${p.ok ? "ok" : p.soft ? "soft" : "no"}`}>
      {p.neutral ? <Minus size={16} /> : p.ok ? <Check size={16} /> : p.soft ? <AlertTriangle size={16} /> : <X size={16} />}
      <span>{p.text}</span>
      {!p.ok && href && (
        <a class="fix" href={href}>
          {t("shell.fixNow")} →
        </a>
      )}
    </div>
  );
}

export function useNow(intervalMs = 1000) {
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), intervalMs);
    return () => clearInterval(t);
  }, [intervalMs]);
  return now;
}

export const ctlTone = (s: string): Tone =>
  s === "working" ? "info" : s === "blocked" || s === "latched" ? "bad" : s === "waiting" ? "warn" : s === "idle" ? "ok" : "neutral";
// Getters, so that each read gives the text in the current language.
export const ctlLabel: Record<string, string> = {
  get off() { return t("shell.ctl.off"); },
  get idle() { return t("shell.ctl.idle"); },
  get working() { return t("shell.ctl.working"); },
  get waiting() { return t("shell.ctl.waiting"); },
  get blocked() { return t("shell.ctl.blocked"); },
  get latched() { return t("shell.ctl.latched"); },
};
export const setupLabel: Record<string, [string, Tone]> = {
  get unavailable(): [string, Tone] { return [t("shell.setup.unavailable"), "neutral"]; },
  get needs_setup(): [string, Tone] { return [t("shell.setup.needsSetup"), "warn"]; },
  get limited(): [string, Tone] { return [t("shell.setup.limited"), "info"]; },
  get ready(): [string, Tone] { return [t("shell.setup.ready"), "ok"]; },
};
