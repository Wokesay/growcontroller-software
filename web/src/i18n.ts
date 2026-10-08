// SPDX-License-Identifier: AGPL-3.0-or-later
// Sprachen der App: Deutsch und Englisch. Texte stehen unter festen Schlüsseln
// in lang/de.ts und lang/en.ts; die englische Tabelle muss jeden deutschen
// Schlüssel haben (prüft der Compiler).
//
// Meldungen des Kerns kommen als {key, text, args}: Deutsch zeigt den Text des
// Kerns, Englisch übersetzt über den Schlüssel „msg.<key>“ und fällt sonst auf
// den deutschen Text zurück.
import { signal } from "@preact/signals";
import { de } from "./lang/de";
import { en } from "./lang/en";

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

function fill(text: string, vars?: Vars) {
  if (!vars) return text;
  return text.replace(/\{(\w+)\}/g, (_, k: string) => {
    const v = vars[k];
    return v === null || v === undefined ? "–" : String(v);
  });
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

/** Meldung des Kerns ({key, text, args}) in der aktuellen Sprache. */
export function msg(m: { key?: string; text?: string; args?: Vars } | null | undefined): string {
  if (!m) return "";
  if (lang.value !== "de" && m.key) {
    const text = tables[lang.value][`msg.${m.key}`];
    if (text) return fill(text, m.args);
  }
  return m.text ?? "";
}

/** Gebietsschema für Zahlen und Datum. */
export const locale = () => (lang.value === "de" ? "de-DE" : "en-GB");
