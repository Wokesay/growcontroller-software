// SPDX-License-Identifier: AGPL-3.0-or-later
// Languages of the app: German and English. Texts live under fixed keys in
// lang/de.ts and lang/en.ts (base and one file per area); the English table
// must have every German key (checked by the compiler).
//
// Messages of the hub come as {key, text, args} with an English text
// (SD-032): English shows that text, German fills the template from
// lang/msg.ts with the arguments and falls back to the hub's text.
import { signal } from "@preact/signals";
import { de } from "./lang/de";
import { en } from "./lang/en";
import { msgDe } from "./lang/msg";

export type Lang = "de" | "en";
export type TextKey = keyof typeof de;
type Vars = Record<string, unknown>;

const tables: Record<Lang, Record<string, string>> = { de, en };
const STORE = "gc.lang";

function initial(): Lang {
  try {
    const saved = localStorage.getItem(STORE);
    if (saved === "de" || saved === "en") return saved;
  } catch {
    // privater Modus: Browser-Sprache
  }
  return (navigator.language || "de").toLowerCase().startsWith("de") ? "de" : "en";
}

export const lang = signal<Lang>(initial());

/** Hat dieser Browser schon eine eigene Wahl? Sonst gilt die Sprache des Hubs. */
export function hasOwnLang(): boolean {
  try {
    return localStorage.getItem(STORE) !== null;
  } catch {
    return false;
  }
}
document.documentElement.lang = lang.value;

/** Sprache wechseln; `remember` merkt die Wahl in diesem Browser. */
export function setLang(l: Lang, remember = true) {
  lang.value = l;
  document.documentElement.lang = l;
  if (!remember) return;
  try {
    localStorage.setItem(STORE, l);
  } catch {
    // ohne Speicher gilt die Wahl bis zum Neuladen
  }
}

type Msg = { key?: string; text?: string; args?: Vars };

// {name} or {name:N}: a number in the page language, with N decimals if
// given, without a thousands separator; missing is "–", never 0 (R5); a
// hub message inside is shown in the page language too.
function fill(text: string, vars?: Vars) {
  if (!vars) return text;
  return text.replace(/\{(\w+)(?::(\d))?\}/g, (_, k: string, d?: string) => show(vars[k], d === undefined ? undefined : Number(d)));
}

function show(v: unknown, decimals?: number): string {
  if (v === null || v === undefined) return "–";
  if (typeof v === "number") {
    if (!Number.isFinite(v)) return "–";
    if (decimals === undefined) return v.toLocaleString(locale(), { maximumFractionDigits: 6, useGrouping: false });
    // toFixed rounds the exact value like the hub does, so both languages
    // show the same digits (20.65 → 20.6 / 20,6).
    const s = v.toFixed(decimals).replace(/^-(0\.?0*)$/, "$1");
    return lang.value === "de" ? s.replace(".", ",") : s;
  }
  if (typeof v === "object") {
    const m = v as Msg;
    return !Array.isArray(v) && (m.key || m.text) ? msg(m) : "–";
  }
  return typeof v === "string" ? v : "–";
}

/** Text zum Schlüssel in der aktuellen Sprache, mit {platzhaltern}. */
export function t(key: TextKey, vars?: Vars): string {
  return tIn(lang.value, key, vars);
}

/** Text in a given language, e.g. for an issue in the English repository (PD-034). */
export function tIn(l: Lang, key: TextKey, vars?: Vars): string {
  const text = tables[l][key] ?? de[key] ?? key;
  return fill(text, vars);
}

/** A hub message ({key, text, args}) in the current language (SD-032). */
export function msg(m: Msg | null | undefined): string {
  if (!m) return "";
  if (lang.value === "de" && m.key) {
    const text = msgDe[m.key];
    if (text) return fill(text, m.args ?? {});
  }
  return m.text ?? "";
}

/** Gebietsschema für Zahlen und Datum. */
export const locale = () => (lang.value === "de" ? "de-DE" : "en-GB");
