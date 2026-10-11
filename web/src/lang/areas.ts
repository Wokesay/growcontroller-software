// SPDX-License-Identifier: AGPL-3.0-or-later
// Texts of one area of the app (#18), German first. de.ts and en.ts merge
// all areas; en must have every key of de (checked by the compiler).
export const de = {
  // What the app is to do per area (SD-037)
  "control.climate": "Klima",
  "control.climate.what": "Abluft, Umluft, Befeuchter und Entfeuchter",
  "control.light": "Licht",
  "control.light.what": "Licht an und aus nach Plan",
  "control.irrigation": "Bewässerung",
  "control.irrigation.what": "Gießen aus dem Tank",
  "control.refill": "Nachfüllen",
  "control.refill.what": "Wasser in den Tank nachfüllen",
  "control.circulation": "Umwälzen",
  "control.circulation.what": "Nährlösung im Tank bewegen",
  "control.dosing": "Dosieren",
  "control.dosing.what": "pH und EC im Tank halten",
  "control.mode.auto": "Automatisch",
  "control.mode.outside": "Außerhalb der Steuerung",
  "control.mode.none": "Noch nicht gewählt",
  "control.page.outside": "Du schaltest das in Home Assistant oder am Gerät. Diese App schaltet hier nichts.",
  "control.page.auto": "„Automatisch“ ist vorgemerkt. Diese Version schaltet noch nichts – lass deine Automatisierungen in Home Assistant an.",
  "control.page.none": "Diese App schaltet hier nichts. Wähle, was sie künftig automatisch machen soll.",
  // Climate page without a sensor in the Home Assistant app: nothing to buy there
  "area.noClimateSensorHa": "Kein Klimasensor gewählt",
  "area.noClimateSensorHaText": "Wähle unter Geräte › Zuordnung einen Sensor für Lufttemperatur und Luftfeuchte aus Home Assistant.",
  "area.chooseSensor": "Sensor wählen",
} as const;

export const en: Record<keyof typeof de, string> = {
  "control.climate": "Climate",
  "control.climate.what": "Exhaust, circulation fan, humidifier and dehumidifier",
  "control.light": "Light",
  "control.light.what": "Light on and off on a schedule",
  "control.irrigation": "Watering",
  "control.irrigation.what": "Watering from the tank",
  "control.refill": "Refill",
  "control.refill.what": "Topping up the tank with water",
  "control.circulation": "Tank circulation",
  "control.circulation.what": "Keeping the nutrient solution in the tank moving",
  "control.dosing": "Dosing",
  "control.dosing.what": "Keeping pH and EC in the tank",
  "control.mode.auto": "Automatic",
  "control.mode.outside": "Handled elsewhere",
  "control.mode.none": "Not chosen yet",
  "control.page.outside": "You switch this in Home Assistant or at the device. This app switches nothing here.",
  "control.page.auto": "\"Automatic\" is noted. This version switches nothing yet – keep your Home Assistant automations on.",
  "control.page.none": "This app switches nothing here. Choose what it should do automatically later on.",
  "area.noClimateSensorHa": "No climate sensor chosen",
  "area.noClimateSensorHaText": "Choose a Home Assistant sensor for air temperature and humidity under Devices › Assignment.",
  "area.chooseSensor": "Choose sensor",
};
