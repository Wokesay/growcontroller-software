// SPDX-License-Identifier: AGPL-3.0-or-later
// Texts of one area of the app (#18), German first. de.ts and en.ts merge
// all areas; en must have every key of de (checked by the compiler).
export const de = {
  "login.tagline": "Fertigations-Hub",
  "login.mismatch": "Die Passwörter stimmen nicht überein.",
  "login.firstText": "Lege zuerst ein eigenes Passwort fest. Es schützt Pumpen und Einstellungen in deinem Netz.",
  "login.newPassword": "Neues Passwort",
  "login.minLength": "Mindestens 8 Zeichen",
  "login.repeatPassword": "Passwort wiederholen",
  "login.code": "Einrichtungscode",
  "login.forgotHa": "Passwort vergessen? Öffne growcontroller in der Seitenleiste von Home Assistant und leg unter Einstellungen › Zugang ein neues fest.",
  "login.codeHintHa": "Du findest ihn in der Ausgabe des Servers (Terminal), in der neuesten Zeile „Setup code for the first password: …“. Jeder Neustart macht einen neuen.",
  "login.password": "Passwort",
  "login.setPassword": "Passwort festlegen",
  "login.signIn": "Anmelden",
  "login.expired": "Deine Anmeldung ist abgelaufen. Hast du gerade etwas angeklickt, hat der Hub es nicht ausgeführt – melde dich an und wiederhole es.",
  "login.localNote": "Die Web-App läuft auf dem Hub in deinem Heimnetz – ohne Cloud und ohne Konto. Die Steuerung läuft weiter, auch wenn diese Seite zu ist.",
} as const;

export const en: Record<keyof typeof de, string> = {
  "login.tagline": "Fertigation hub",
  "login.mismatch": "The passwords do not match.",
  "login.firstText": "First set your own password. It protects pumps and settings on your network.",
  "login.newPassword": "New password",
  "login.minLength": "At least 8 characters",
  "login.repeatPassword": "Repeat password",
  "login.code": "Setup code",
  "login.forgotHa": "Forgot the password? Open growcontroller in Home Assistant's sidebar and set a new one under Settings › Access.",
  "login.codeHintHa": "Find it in the server's output (terminal), in the newest line \"Setup code for the first password: …\". Every restart makes a new one.",
  "login.password": "Password",
  "login.setPassword": "Set password",
  "login.signIn": "Sign in",
  "login.expired": "Your session has expired. If you just clicked something, the hub did not carry it out – sign in and do it again.",
  "login.localNote": "The web app runs on the hub on your home network – no cloud, no account. Control keeps running even when this page is closed.",
};
