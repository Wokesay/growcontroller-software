// SPDX-License-Identifier: AGPL-3.0-or-later
// Texts of one area of the app (#18), German first. de.ts and en.ts merge
// all areas; en must have every key of de (checked by the compiler).
export const de = {
  "history.range6h": "6 h",
  "history.range24h": "24 h",
  "history.range7d": "7 T",
  "history.range30d": "30 T",
  "history.range1y": "1 J",
  "history.filterDose": "Dosierung",
  "history.filterMix": "Mischläufe",
  "history.filterAlarm": "Alarme",
  "history.filterCalibration": "Kalibrierung",
  "history.filterAuth": "Anmeldung",
  "history.intro": "Gestrichelte Linien zeigen Dosierungen. Der grüne Streifen ist das Zielband. Lücken sind fehlende Werte, nicht 0.",
  "history.waterTemp": "Wassertemperatur",
  "history.volumeFromMixes": "Volumen (aus Mischläufen)",
  "history.volume": "Volumen",
  "history.airTemp": "Lufttemperatur",
  "history.humidity": "Luftfeuchte",
  "history.vpd": "VPD (Luft)",
  "history.events": "Ereignisse",
} as const;

export const en: Record<keyof typeof de, string> = {
  "history.range6h": "6 h",
  "history.range24h": "24 h",
  "history.range7d": "7 d",
  "history.range30d": "30 d",
  "history.range1y": "1 y",
  "history.filterDose": "Dosing",
  "history.filterMix": "Mixes",
  "history.filterAlarm": "Alarms",
  "history.filterCalibration": "Calibration",
  "history.filterAuth": "Sign-in",
  "history.intro": "Dashed lines show doses. The green strip is the target band. Gaps are missing values, not 0.",
  "history.waterTemp": "Water temperature",
  "history.volumeFromMixes": "Volume (from mixes)",
  "history.volume": "Volume",
  "history.airTemp": "Air temperature",
  "history.humidity": "Humidity",
  "history.vpd": "VPD (air)",
  "history.events": "Events",
};
