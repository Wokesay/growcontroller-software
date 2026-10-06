// Bausteine der Oberfläche: Karten, Knöpfe, Felder, Dialoge, Status.
import type { ComponentChildren, JSX } from "preact";
import { useEffect, useState } from "preact/hooks";
import { signal } from "@preact/signals";
import { AlertTriangle, Check, CheckCircle2, Info, Minus, X, XCircle } from "lucide-preact";
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
export function Pill(p: { tone?: Tone; dot?: boolean; pulse?: boolean; children: ComponentChildren; title?: string }) {
  return (
    <span class={`pill ${p.tone ?? ""}`} title={p.title}>
      {p.dot && <span class={`dot ${p.pulse ? "pulse" : ""}`} />}
      {p.children}
    </span>
  );
}

export function Field(p: { label: ComponentChildren; hint?: ComponentChildren; error?: string | null; children: ComponentChildren }) {
  return (
    <label class="field">
      <span>{p.label}</span>
      {p.children}
      {p.error ? <small class="err">{p.error}</small> : p.hint ? <small class="hint">{p.hint}</small> : null}
    </label>
  );
}

export function NumberInput(p: { value: number | null | undefined; onValue: (v: number | null) => void; unit?: string; min?: number; max?: number; step?: number; placeholder?: string; id?: string; name?: string }) {
  const [text, setText] = useState(p.value === null || p.value === undefined ? "" : String(p.value).replace(".", ","));
  useEffect(() => {
    const cur = parseFloat(text.replace(",", "."));
    if (p.value !== null && p.value !== undefined && cur !== p.value) setText(String(p.value).replace(".", ","));
    if ((p.value === null || p.value === undefined) && text !== "" && Number.isNaN(cur)) setText("");
  }, [p.value]);
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
  useEffect(() => {
    const k = (e: KeyboardEvent) => e.key === "Escape" && p.onClose();
    window.addEventListener("keydown", k);
    return () => window.removeEventListener("keydown", k);
  }, []);
  return (
    <div class="overlay" onClick={(e) => e.target === e.currentTarget && p.onClose()}>
      <div class={`modal ${p.wide ? "wide" : ""}`} role="dialog" aria-modal="true">
        <div class="row-between">
          <h2>{p.title}</h2>
          <button class="btn ghost sm" onClick={p.onClose} aria-label="Schließen">
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
          Jetzt erledigen →
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
export const ctlLabel: Record<string, string> = {
  off: "Aus", idle: "Ruht", working: "Arbeitet", waiting: "Wartet", blocked: "Gesperrt", latched: "Gerastet",
};
export const setupLabel: Record<string, [string, Tone]> = {
  unavailable: ["Nicht verfügbar", "neutral"],
  needs_setup: ["Einzurichten", "warn"],
  limited: ["Eingeschränkt", "info"],
  ready: ["Bereit", "ok"],
};
