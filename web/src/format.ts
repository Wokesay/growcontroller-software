// Formatierung für Deutsch. Ein fehlender Wert wird als „–“ gezeigt, nie als 0.
const nf = new Map<number, Intl.NumberFormat>();
export function num(v: number | null | undefined, decimals = 1): string {
  if (v === null || v === undefined || !Number.isFinite(v)) return "–";
  let f = nf.get(decimals);
  if (!f) {
    f = new Intl.NumberFormat("de-DE", { minimumFractionDigits: decimals, maximumFractionDigits: decimals });
    nf.set(decimals, f);
  }
  return f.format(v);
}

export function ago(seconds: number | null | undefined): string {
  if (seconds === null || seconds === undefined) return "";
  if (seconds < 5) return "gerade eben";
  if (seconds < 60) return `vor ${Math.round(seconds)} s`;
  if (seconds < 3600) return `vor ${Math.round(seconds / 60)} min`;
  if (seconds < 86400) return `vor ${Math.round(seconds / 3600)} h`;
  return `vor ${Math.round(seconds / 86400)} Tagen`;
}

export function duration(ms: number): string {
  const s = Math.max(0, Math.round(ms / 1000));
  if (s < 60) return `${s} s`;
  const m = Math.floor(s / 60);
  const r = s % 60;
  if (m < 60) return r ? `${m} min ${r} s` : `${m} min`;
  return `${Math.floor(m / 60)} h ${m % 60} min`;
}

const dtf = new Intl.DateTimeFormat("de-DE", { day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" });
const tf = new Intl.DateTimeFormat("de-DE", { hour: "2-digit", minute: "2-digit" });
const df = new Intl.DateTimeFormat("de-DE", { weekday: "long", day: "numeric", month: "long" });
export const dateTime = (epoch: number) => dtf.format(new Date(epoch * 1000));
export const time = (epoch: number) => tf.format(new Date(epoch * 1000));
export const day = (epoch: number) => df.format(new Date(epoch * 1000));

export function plural(n: number, one: string, many: string) {
  return `${n} ${n === 1 ? one : many}`;
}
