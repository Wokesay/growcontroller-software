// SPDX-License-Identifier: AGPL-3.0-or-later
// The files to flash a board, and the manifest ESP Web Tools reads to flash
// them from the browser (SD-043). From a firmware build it copies the images
// ESP-IDF flashes (bootloader, partition table, OTA data, app) into one
// folder and writes next to them:
//   manifest.json  one ESP32-S3 build, each image at its own offset, so NVS
//                  and the data partitions stay as they are (unless the user
//                  chooses to erase, or the partition table changed);
//   flash_args     the same for esptool: write_flash @flash_args.
// One folder and manifest per board, as ESP Web Tools tells builds apart
// only by chip family, not by flash size. The version comes from VERSION.
//   node tools/flash_manifest.mjs <board> <build folder> <out folder>
import { copyFileSync, existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync } from "node:fs";
import { basename, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));

// The boards the firmware is built for (firmware/boards/, PD-162)
// with the settings the build must end up with: Kconfig drops a default
// whose conditions do not hold without a word
export const boards = {
  n32r16v: {
    name: "ESP32-S3-DevKitC-1-N32R16V",
    flashSize: "32MB",
    sdkconfig: [
      "CONFIG_ESPTOOLPY_OCT_FLASH=y",
      'CONFIG_ESPTOOLPY_FLASHSIZE="32MB"',
      "CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_OCTAL_FLASH=y",
      "CONFIG_SPIRAM_MODE_OCT=y",
      'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="boards/n32r16v.csv"',
    ],
  },
  n8r8: {
    name: "ESP32-S3-DevKitC-1-N8R8",
    flashSize: "8MB",
    sdkconfig: [
      "CONFIG_ESPTOOLPY_FLASHMODE_QIO=y",
      'CONFIG_ESPTOOLPY_FLASHSIZE="8MB"',
      "CONFIG_SPIRAM_MODE_OCT=y",
      'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="boards/n8r8.csv"',
    ],
  },
};

// The board's settings that the build's sdkconfig lacks
export function missingSettings(board, sdkconfig) {
  const lines = new Set(sdkconfig.split("\n").map((l) => l.trim()));
  return boards[board].sdkconfig.filter((s) => !lines.has(s));
}

const kPartitionTable = 0x8000; // ESP-IDF's default offset; the partitions follow it

// A board's partitions from firmware/boards/<board>.csv, placed as ESP-IDF
// places them: one after the other, apps on 64 KB boundaries; "readonly" is
// the only flag a partition may carry (the factory data, SD-076)
export function layout(board, csv) {
  if (csv === undefined) {
    if (!boards[board]) throw new Error(`unknown board "${board}": ${Object.keys(boards).join(" or ")}`);
    csv = readFileSync(join(root, "firmware", "boards", `${board}.csv`), "utf8");
  }
  const size = (s) => {
    const m = /^(?:0x([0-9a-f]+)|(\d+)([KM]?))$/i.exec(s);
    if (!m) throw new Error(`not a size: "${s}"`);
    return m[1] ? parseInt(m[1], 16) : Number(m[2]) * ({ K: 1024, M: 1024 * 1024 }[m[3].toUpperCase()] ?? 1);
  };
  let at = kPartitionTable + 0x1000;
  const parts = [];
  for (const line of csv.split("\n")) {
    if (!line.trim() || line.trimStart().startsWith("#")) continue;
    const [name, type, subtype, offset, len, flags = ""] = line.split(",").map((x) => x.trim());
    if (offset) throw new Error(`${board}: ${name} has an offset; ESP-IDF places the partitions`);
    const flagList = flags.split(":").filter(Boolean);
    for (const f of flagList) if (f !== "readonly") throw new Error(`${board}: ${name}: not a flag here: "${f}"`);
    if (type === "app") at = Math.ceil(at / 0x10000) * 0x10000;
    parts.push({ name, type, subtype, offset: at, size: size(len), flags: flagList });
    at += size(len);
  }
  return parts;
}

// What to flash where, from ESP-IDF's flasher_args.json. Refuses a build for
// another chip or board, so a board never gets the other one's images, and
// any image outside bootloader, partition table, OTA data and the first app:
// NVS and the data partitions are never written
export function flashFiles(board, args) {
  const b = boards[board];
  if (!b) throw new Error(`unknown board "${board}": ${Object.keys(boards).join(" or ")}`);
  const parts = layout(board);
  if (args?.extra_esptool_args?.chip !== "esp32s3") throw new Error(`not a build for the ESP32-S3: ${args?.extra_esptool_args?.chip}`);
  const size = args.flash_settings?.flash_size;
  if (size !== b.flashSize) throw new Error(`built for ${size} of flash, the ${b.name} has ${b.flashSize}: wrong GC_BOARD?`);
  const allowed = [0, kPartitionTable, ...parts.filter((p) => p.subtype === "ota" || p.subtype === "ota_0").map((p) => p.offset)];
  const files = Object.entries(args.flash_files ?? {}).map(([offset, path]) => {
    if (!/^0x[0-9a-f]+$/i.test(offset)) throw new Error(`not an offset: "${offset}"`);
    if (!/^[\w./-]+\.bin$/.test(path) || path.split("/").includes("..")) throw new Error(`not an image in the build: "${path}"`);
    if (!allowed.includes(parseInt(offset, 16))) throw new Error(`${path} at ${offset} is not bootloader, partition table, OTA data or the first app`);
    return { offset: parseInt(offset, 16), path, name: basename(path) };
  });
  files.sort((x, y) => x.offset - y.offset);
  if (files[0]?.offset !== 0) throw new Error("no bootloader at offset 0");
  if (new Set(files.map((f) => f.name)).size !== files.length) throw new Error("two images with the same name");
  return files;
}

export function manifest(board, version, files) {
  if (!/^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$/.test(version)) throw new Error(`not a version: "${version}"`);
  return {
    name: `growcontroller hub for ${boards[board].name}`,
    version,
    // Ask before erasing: without the prompt, ESP Web Tools erases a new
    // install unasked. Its erase (this prompt, and "Erase User Data" once
    // Improv runs) is the whole chip, factory data included; the flash page
    // for pre-flashed devices needs a patched copy (SD-076)
    new_install_prompt_erase: true,
    // No Wi-Fi setup over Improv yet (SD-044): do not wait for it after flashing
    new_install_improv_wait_time: 0,
    builds: [{ chipFamily: "ESP32-S3", parts: files.map((f) => ({ path: f.name, offset: f.offset })) }],
  };
}

// esptool's flash settings, then each image at its offset; only the three
// settings ESP-IDF writes, with plain values, go into the file
export function flashArgs(args, files) {
  const settings = args.write_flash_args ?? [];
  if (settings.length % 2) throw new Error("flash settings come in pairs");
  for (let i = 0; i < settings.length; i += 2) {
    if (!["--flash_mode", "--flash_freq", "--flash_size"].includes(settings[i])) throw new Error(`not a flash setting: "${settings[i]}"`);
    if (!/^[\w]+$/.test(settings[i + 1])) throw new Error(`not a value for ${settings[i]}: "${settings[i + 1]}"`);
  }
  return `${settings.join(" ")}\n${files.map((f) => `0x${f.offset.toString(16)} ${f.name}`).join("\n")}\n`;
}

export function writeFlashFolder(board, buildDir, outDir, version) {
  const args = JSON.parse(readFileSync(join(buildDir, "flasher_args.json"), "utf8"));
  const files = flashFiles(board, args);
  const missing = missingSettings(board, readFileSync(join(buildDir, "sdkconfig"), "utf8"));
  if (missing.length) throw new Error(`the build lacks the ${boards[board].name}'s settings: ${missing.join(", ")}`);
  mkdirSync(outDir, { recursive: true });
  for (const f of files) {
    const from = join(buildDir, f.path);
    if (!existsSync(from)) throw new Error(`missing in the build: ${f.path}`);
    copyFileSync(from, join(outDir, f.name));
  }
  writeFileSync(join(outDir, "manifest.json"), `${JSON.stringify(manifest(board, version, files), null, 2)}\n`);
  writeFileSync(join(outDir, "flash_args"), flashArgs(args, files));
  return files;
}

// realpath: Node resolves symlinks for import.meta.url but not in argv.
if (process.argv[1] && import.meta.url === pathToFileURL(realpathSync(process.argv[1])).href) {
  const [board, buildDir, outDir] = process.argv.slice(2);
  try {
    if (!outDir) throw new Error("usage: node tools/flash_manifest.mjs <board> <build folder> <out folder>");
    const version = readFileSync(join(root, "VERSION"), "utf8").trim();
    for (const f of writeFlashFolder(board, buildDir, outDir, version)) console.log(`0x${f.offset.toString(16)} ${f.name}`);
  } catch (e) {
    console.error(e.message);
    process.exit(1);
  }
}
