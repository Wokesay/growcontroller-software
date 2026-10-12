// SPDX-License-Identifier: AGPL-3.0-or-later
// The files to flash a board and their manifest, and the board profiles:
// every board has its files in firmware/boards/, CMake accepts it, CI
// builds it, and its partitions fill its flash (SD-043).
//   node --test tools/flash_manifest.test.mjs
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { boards, flashArgs, flashFiles, layout, manifest } from "./flash_manifest.mjs";

const root = fileURLToPath(new URL("..", import.meta.url));
const read = (f) => readFileSync(join(root, f), "utf8");
const script = join(root, "tools", "flash_manifest.mjs");

// flasher_args.json as ESP-IDF v5.4 writes it for a build with A/B apps
// (octal flash: dout in the header, quad flash: dio)
const built = (size = "32MB", chip = "esp32s3", mode = size === "32MB" ? "dout" : "dio") => ({
  write_flash_args: ["--flash_mode", mode, "--flash_size", size, "--flash_freq", "80m"],
  flash_settings: { flash_mode: mode, flash_size: size, flash_freq: "80m" },
  flash_files: {
    "0x20000": "growcontroller_hub.bin",
    "0x0": "bootloader/bootloader.bin",
    "0x19000": "ota_data_initial.bin",
    "0x8000": "partition_table/partition-table.bin",
  },
  extra_esptool_args: { after: "hard_reset", before: "default_reset", stub: true, chip },
});
const temp = (prefix) => mkdtempSync(join(tmpdir(), prefix));

test("the manifest names one ESP32-S3 build, each image at its own offset", () => {
  const files = flashFiles("n32r16v", built());
  const m = manifest("n32r16v", "0.1.0-proto.4", files);
  assert.equal(m.name, "growcontroller hub for ESP32-S3-DevKitC-1-N32R16V");
  assert.equal(m.version, "0.1.0-proto.4");
  assert.equal(m.new_install_prompt_erase, true);
  assert.equal(m.new_install_improv_wait_time, 0);
  // NVS (0x9000) and the data partitions after the app are not written
  assert.deepEqual(m.builds, [{
    chipFamily: "ESP32-S3",
    parts: [
      { path: "bootloader.bin", offset: 0 },
      { path: "partition-table.bin", offset: 0x8000 },
      { path: "ota_data_initial.bin", offset: 0x19000 },
      { path: "growcontroller_hub.bin", offset: 0x20000 },
    ],
  }]);
  assert.equal(flashArgs(built(), files), "--flash_mode dout --flash_size 32MB --flash_freq 80m\n0x0 bootloader.bin\n0x8000 partition-table.bin\n0x19000 ota_data_initial.bin\n0x20000 growcontroller_hub.bin\n");
});

test("a build for another board or chip, or with odd files, gives no manifest", () => {
  assert.throws(() => flashFiles("n16r8", built()), /unknown board/);
  assert.throws(() => flashFiles("n8r8", built("32MB")), /wrong GC_BOARD/);
  assert.throws(() => flashFiles("n32r16v", built("8MB")), /wrong GC_BOARD/);
  assert.throws(() => flashFiles("n32r16v", built("32MB", "esp32")), /not a build for the ESP32-S3/);
  const odd = (files) => ({ ...built(), flash_files: files });
  assert.throws(() => flashFiles("n32r16v", odd({ "0x8000": "partition_table/partition-table.bin" })), /no bootloader at offset 0/);
  assert.throws(() => flashFiles("n32r16v", odd({ "0x0": "../secret.bin" })), /not an image in the build/);
  assert.throws(() => flashFiles("n32r16v", odd({ "0x0": "app.elf" })), /not an image in the build/);
  assert.throws(() => flashFiles("n32r16v", odd({ "10": "bootloader/bootloader.bin" })), /not an offset/);
  assert.throws(() => flashFiles("n32r16v", odd({ "0x0": "a/x.bin", "0x8000": "b/x.bin" })), /same name/);
  assert.throws(() => manifest("n8r8", "v1.2", flashFiles("n8r8", built("8MB"))), /not a version/);
  // Never into NVS, the second app or the data partitions
  for (const offset of ["0x9000", "0x420000", "0x820000", "0xa20000"])
    assert.throws(() => flashFiles("n32r16v", odd({ "0x0": "bootloader/bootloader.bin", [offset]: "x.bin" })), /is not bootloader, partition table, OTA data or the first app/, offset);
  // flash_args takes only the three flash settings, with plain values
  const files = flashFiles("n8r8", built("8MB"));
  for (const extra of [["--before", "no_reset"], ["--flash_mode"], ["--flash_size", "8MB;rm"]])
    assert.throws(() => flashArgs({ write_flash_args: extra }, files), /flash setting|pairs|not a value/, extra.join(" "));
});

test("command line: copies the images and writes manifest and flash_args; a missing image stops it", () => {
  const build = temp("gc-build-");
  const outRoot = temp("gc-flash-");
  try {
    mkdirSync(join(build, "bootloader"));
    mkdirSync(join(build, "partition_table"));
    writeFileSync(join(build, "flasher_args.json"), JSON.stringify(built("8MB")));
    for (const f of ["bootloader/bootloader.bin", "partition_table/partition-table.bin", "ota_data_initial.bin"]) writeFileSync(join(build, f), f);
    const out = join(outRoot, "n8r8");
    const missing = spawnSync(process.execPath, [script, "n8r8", build, out], { encoding: "utf8" });
    assert.notEqual(missing.status, 0);
    assert.match(missing.stderr, /missing in the build: growcontroller_hub.bin/);
    writeFileSync(join(build, "growcontroller_hub.bin"), "app");
    execFileSync(process.execPath, [script, "n8r8", build, out], { encoding: "utf8" });
    const m = JSON.parse(readFileSync(join(out, "manifest.json"), "utf8"));
    assert.equal(m.version, read("VERSION").trim());
    for (const p of m.builds[0].parts) assert.ok(existsSync(join(out, p.path)), p.path);
    assert.equal(readFileSync(join(out, "growcontroller_hub.bin"), "utf8"), "app");
    assert.match(readFileSync(join(out, "flash_args"), "utf8"), /^--flash_mode dio --flash_size 8MB/);
    assert.notEqual(spawnSync(process.execPath, [script, "n32r16v", build, out], { encoding: "utf8" }).status, 0, "the other board");
  } finally {
    rmSync(build, { recursive: true, force: true });
    rmSync(outRoot, { recursive: true, force: true });
  }
});

test("every board has its profile and partition table, CMake takes it and CI builds it", () => {
  const files = readdirSync(join(root, "firmware", "boards")).sort();
  const ids = Object.keys(boards).sort();
  assert.deepEqual(files, ids.flatMap((b) => [`${b}.csv`, `${b}.defaults`]).sort());
  const cmake = read("firmware/CMakeLists.txt");
  const accepted = /GC_BOARD MATCHES "\^\(([^)]*)\)\$"/.exec(cmake);
  assert.ok(accepted, "CMakeLists.txt checks GC_BOARD");
  assert.deepEqual(accepted[1].split("|").sort(), ids);
  const ci = read(".github/workflows/ci.yml");
  const matrix = /board: \[([^\]]*)\]/.exec(ci);
  assert.ok(matrix, "ci.yml builds the firmware per board");
  assert.deepEqual(matrix[1].split(",").map((s) => s.trim()).sort(), ids);
  for (const b of ids) {
    const defaults = read(`firmware/boards/${b}.defaults`);
    assert.ok(defaults.includes(`CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="boards/${b}.csv"`), `${b}: its partition table`);
    assert.match(defaults, /^CONFIG_ESPTOOLPY_FLASHSIZE_\d+MB=y$/m, `${b}: its flash size`);
  }
  assert.doesNotMatch(read("firmware/sdkconfig.defaults"), /FLASHSIZE|PARTITION_TABLE_CUSTOM_FILENAME/, "flash and partitions only per board");
});

test("each partition table fills its board's flash exactly, sizes on 4 KB, apps on 64 KB", () => {
  for (const b of Object.keys(boards)) {
    const flash = Number(/FLASHSIZE_(\d+)MB=y/.exec(read(`firmware/boards/${b}.defaults`))[1]) * 1024 * 1024;
    const parts = layout(b);
    const end = parts.at(-1).offset + parts.at(-1).size;
    assert.equal(end, flash, `${b}: partitions end at the end of the flash`);
    for (const p of parts) assert.equal(p.size % 0x1000, 0, `${b}: ${p.name} is a whole number of 4 KB sectors`);
    const apps = parts.filter((p) => p.type === "app");
    assert.deepEqual(apps.map((p) => p.subtype), ["ota_0", "ota_1"], `${b}: two app partitions (A/B)`);
    assert.equal(apps[0].size, apps[1].size, `${b}: A and B the same size`);
    for (const p of apps) assert.equal(p.offset % 0x10000, 0, `${b}: ${p.name} on a 64 KB boundary`);
    assert.ok(parts.find((p) => p.name === "nvs").size >= 64 * 1024, `${b}: NVS has room for the encrypted secrets (SD-024)`);
  }
  assert.throws(() => layout("x", "nvs, data, nvs, 0x9000, 0x6000\n"), /has an offset/);
  assert.throws(() => layout("x", "nvs, data, nvs, , 6 MB\n"), /not a size/);
});
