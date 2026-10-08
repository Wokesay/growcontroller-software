// SPDX-License-Identifier: AGPL-3.0-or-later
// Texts of one area of the app (#18), German first. de.ts and en.ts merge
// all areas; en must have every key of de (checked by the compiler).
export const de = {
  // State of a controller
  "shell.ctl.off": "Aus",
  "shell.ctl.idle": "Ruht",
  "shell.ctl.working": "Arbeitet",
  "shell.ctl.waiting": "Wartet",
  "shell.ctl.blocked": "Gesperrt",
  "shell.ctl.latched": "Gerastet",

  // State of a function
  "shell.setup.unavailable": "Nicht verfügbar",
  "shell.setup.needsSetup": "Einzurichten",
  "shell.setup.limited": "Eingeschränkt",
  "shell.setup.ready": "Bereit",

  "shell.fixNow": "Jetzt erledigen",
  "shell.noAnswer": "Der Hub antwortet nicht. Ist er eingeschaltet und im selben Netz?",
  "shell.httpError": "Fehler {status}",
} as const;

export const en: Record<keyof typeof de, string> = {
  "shell.ctl.off": "Off",
  "shell.ctl.idle": "Resting",
  "shell.ctl.working": "Controlling",
  "shell.ctl.waiting": "Waiting",
  "shell.ctl.blocked": "Blocked",
  "shell.ctl.latched": "Needs release",

  "shell.setup.unavailable": "Not available",
  "shell.setup.needsSetup": "Needs setup",
  "shell.setup.limited": "Limited",
  "shell.setup.ready": "Ready",

  "shell.fixNow": "Fix now",
  "shell.noAnswer": "The hub isn't responding. Is it switched on and on the same network as this device?",
  "shell.httpError": "Error {status}",
};
