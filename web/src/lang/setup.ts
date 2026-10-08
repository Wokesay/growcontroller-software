// SPDX-License-Identifier: AGPL-3.0-or-later
// Texts of one area of the app (#18), German first. de.ts and en.ts merge
// all areas; en must have every key of de (checked by the compiler).
export const de = {
  "setup.tank.defaultName": "Tank 1",
} as const;

export const en: Record<keyof typeof de, string> = {
  "setup.tank.defaultName": "Tank 1",
};
