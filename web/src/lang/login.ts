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
  "login.password": "Passwort",
  "login.setPassword": "Passwort festlegen",
  "login.signIn": "Anmelden",
  "login.localNote": "Die Web-App läuft auf dem Hub in deinem Heimnetz – ohne Cloud und ohne Konto. Die Steuerung läuft weiter, auch wenn diese Seite zu ist.",
} as const;

export const en: Record<keyof typeof de, string> = {
  "login.tagline": "Fertigation hub",
  "login.mismatch": "The passwords do not match.",
  "login.firstText": "First set your own password. It protects pumps and settings on your network.",
  "login.newPassword": "New password",
  "login.minLength": "At least 8 characters",
  "login.repeatPassword": "Repeat password",
  "login.password": "Password",
  "login.setPassword": "Set password",
  "login.signIn": "Sign in",
  "login.localNote": "The web app runs on the hub in your home network – no cloud, no account. Control keeps running even when this page is closed.",
};
