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
  "shell.ctl.latched": "Freigabe nötig",

  // State of a function
  "shell.setup.unavailable": "Nicht verfügbar",
  "shell.setup.needsSetup": "Einzurichten",
  "shell.setup.limited": "Eingeschränkt",
  "shell.setup.ready": "Bereit",

  "shell.fixNow": "Jetzt erledigen",
  "shell.noAnswer": "Der Hub antwortet nicht. Ist er eingeschaltet und im selben Netz?",
  "shell.httpError": "Fehler {status}",
  "shell.failed": "Das hat nicht geklappt. Lade die Seite neu und versuch es noch einmal.",
  // A Stop the hub did not confirm is never silent; unplugging the hub does not switch off smart plugs
  "shell.stopSending": "Stopp wird gesendet …",
  "shell.stopRetry": "Noch einmal versuchen",
  "shell.stopFailed": "Stopp nicht bestätigt – keine Antwort vom Hub.",
  "shell.stopRefused": "Stopp nicht bestätigt – {reason}",
  "shell.stopHint": "Zieh im Notfall den Netzstecker des Hubs.",
  "shell.stopHintPlugs": "Zieh im Notfall den Netzstecker des Hubs und schalte Geräte an Steckdosen direkt aus.",
  "shell.offlineBar": "Keine Verbindung zum Hub – die Werte sind nicht aktuell. Ist der Hub an, steuert er ohne die App weiter.",
  "shell.platformLost": "Deine Anmeldung über Home Assistant ist abgelaufen – die Werte sind nicht aktuell.",
  "shell.platformRefused": "Nicht ausgeführt – deine Anmeldung über Home Assistant ist abgelaufen. Versuch es gleich noch einmal; klappt es nicht, lade die Seite neu.",
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
  "shell.failed": "That didn't work. Reload the page and try again.",
  "shell.stopSending": "Sending Stop …",
  "shell.stopRetry": "Try again",
  "shell.stopFailed": "Stop not confirmed – no answer from the hub.",
  "shell.stopRefused": "Stop not confirmed – {reason}",
  "shell.stopHint": "In an emergency, unplug the hub.",
  "shell.stopHintPlugs": "In an emergency, unplug the hub and switch off devices on smart plugs directly.",
  "shell.offlineBar": "No connection to the hub – the values are not current. If the hub is on, it keeps controlling without the app.",
  "shell.platformLost": "Your session through Home Assistant has expired – the values are not current.",
  "shell.platformRefused": "Not carried out – your session through Home Assistant has expired. Try again in a moment; if that doesn't work, reload the page.",
};
