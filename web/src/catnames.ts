// SPDX-License-Identifier: AGPL-3.0-or-later
// Catalog names in the page's language (SD-083 point 3): the catalog is
// English, the German comes from lang/cat.ts by the entry's key.
import type { Device } from "./api";
import { catText } from "./i18n";
import { catalog, config, state } from "./store";

export const roleName = (id: string) => catText(`cat.role.${id}`, catalog.value?.roles[id]?.label, id);
export const capName = (id: string) => catText(`cat.cap.${id}`, catalog.value?.capabilities[id]?.label, id);
export const className = (cls: string, english?: string) => catText(`cat.class.${cls}`, english || catalog.value?.deviceClasses[cls]?.label, cls);
export const classText = (cls: string) => catText(`cat.class.${cls}.text`, catalog.value?.deviceClasses[cls]?.text);
export const fnName = (id: string, english?: string) => catText(`cat.fn.${id}`, english ?? catalog.value?.functions.find((f) => f.id === id)?.label, id);
export const fnText = (id: string, english?: string) => catText(`cat.fn.${id}.text`, english ?? catalog.value?.functions.find((f) => f.id === id)?.text);
export const paramName = (fn: string, key: string, english: string) => catText(`cat.param.${fn}.${key}`, english, key);
export const optionName = (fn: string, key: string, value: string, english: string) => catText(`cat.option.${fn}.${key}.${value}`, english, value);
/** A device by its own name, else by its class in the page's language; the hub
 *  sends no name for one that has none of its own (SD-083). */
export const deviceName = (d: Pick<Device, "name" | "class" | "classLabel">) => d.name || className(d.class, d.classLabel);
/** A device of the configuration by its id, named as the hub shows it: the
 *  configuration keeps a name as stored, the state leaves out a class name. */
export const deviceNameById = (id: string) => {
  const d = state.value?.devices.find((x) => x.id === id);
  if (d) return deviceName(d);
  const c = config.value?.devices.find((x) => x.id === id);
  return c ? c.name || className(c.class) : id;
};
