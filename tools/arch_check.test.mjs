// SPDX-License-Identifier: AGPL-3.0-or-later
// The architecture check finds what it guards against: here the options
// that burn eFuses, in sdkconfig.defaults and in a board profile (PD-022,
// SD-024). It runs on a copy of itself in an otherwise empty tree.
//   node --test tools/arch_check.test.mjs
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { copyFileSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const script = fileURLToPath(new URL("arch_check.sh", import.meta.url));

function check(files) {
  const dir = mkdtempSync(join(tmpdir(), "gc-arch-"));
  try {
    mkdirSync(join(dir, "tools"));
    copyFileSync(script, join(dir, "tools", "arch_check.sh"));
    for (const [path, text] of Object.entries(files)) {
      mkdirSync(join(dir, path, ".."), { recursive: true });
      writeFileSync(join(dir, path), text);
    }
    return spawnSync("bash", [join(dir, "tools", "arch_check.sh")], { encoding: "utf8" });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

test("a board profile or sdkconfig.defaults that burns eFuses fails the check", () => {
  for (const option of ["SECURE_BOOT", "SECURE_FLASH_ENC_ENABLED", "SECURE_DISABLE_ROM_DL_MODE", "BOOTLOADER_APP_ANTI_ROLLBACK"]) {
    for (const file of ["firmware/boards/n32r16v.defaults", "firmware/sdkconfig.defaults"]) {
      const r = check({ [file]: `CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y\nCONFIG_${option}=y\n` });
      assert.equal(r.status, 1, `${option} in ${file}`);
      assert.match(r.stdout, /eFuse-burning security option/, `${option} in ${file}`);
    }
  }
});

test("profiles without such an option pass", () => {
  const r = check({
    "firmware/boards/n8r8.defaults": "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\n# CONFIG_SECURE_BOOT is not set\n",
    "firmware/sdkconfig.defaults": "CONFIG_SPIRAM=y\n",
  });
  assert.equal(r.status, 0, r.stdout);
  assert.match(r.stdout, /Architekturregeln: ok/);
});
