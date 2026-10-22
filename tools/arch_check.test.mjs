// SPDX-License-Identifier: AGPL-3.0-or-later
// The architecture check finds what it guards against: here the options
// that burn eFuses, in sdkconfig.defaults and in a board profile (PD-022,
// SD-024), std::random_device where random numbers must come from the
// operating system (#79, #234), and the rules of the Home Assistant app
// behind Ingress (SD-077). It runs on a copy of itself in an otherwise
// empty tree.
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
    for (const folder of ["core", "sim", "ha", "firmware", "web/src"]) mkdirSync(join(dir, folder), { recursive: true });  // as in the repository
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

// #79, #234: session tokens, salts and the setup code come from the operating
// system; std::random_device tries RDSEED first on Linux x86-64
const randomBytes = (windows, others) =>
  `void randomBytes(std::uint8_t* p, size_t n) {\n#ifdef _WIN32\n${windows}\n#else\n${others}\n#endif\n}\n`;
const winRd = "  static std::random_device rd;  // MSVC: the system's generator (rand_s)";
const getentropy = "  if (getentropy(p, n) != 0) std::abort();";

test("std::random_device, RDSEED or RDRAND outside the Windows branch of sim::randomBytes fails the check", () => {
  const cases = [
    ["core/src/auth.cpp", "std::random_device rd;\n"],
    ["core/src/auth.cpp", "std::random_device rd;  // seeded once\n"],  // a comment after the code hides nothing
    ["ha/main.cpp", "  static std::random_device rd;\n  p[i] = static_cast<std::uint8_t>(rd());\n"],
    ["sim/server.cpp", "auto seed = std::random_device{}();\n"],
    ["sim/a b.cpp", "auto seed = std::random_device{}();\n"],  // a file name with a space
    ["firmware/main/main.cpp", "std::random_device rd;\n"],
    ["core/src/token.cpp", "unsigned long long v; _rdseed64_step(&v);\n"],
    ["sim/noise.cpp", "unsigned v; __builtin_ia32_rdrand32_step(&v);\n"],
    ["sim/scenario.cpp", randomBytes(winRd, "  static std::random_device rd;")],  // in the branch for the others
    ["sim/scenario.cpp", "#if !defined(_WIN32)\n  static std::random_device rd;\n#endif\n"],
    ["sim/scenario.cpp", "#ifdef _WIN32\n#include <io.h>\n#endif\nstatic std::random_device rd;\n"],
    ["sim/other.cpp", randomBytes(winRd, getentropy)],  // the exception is only sim/scenario.cpp
  ];
  for (const [file, text] of cases) {
    const r = check({ [file]: text });
    assert.equal(r.status, 1, `${file}: ${text}`);
    assert.match(r.stdout, /std::random_device, RDSEED or RDRAND outside the Windows branch/, file);
    assert.match(r.stdout, new RegExp(`^${file.replace(/[./]/g, "\\$&")}:\\d+:`, "m"), file);
  }
});

test("std::random_device right after #ifdef _WIN32 in sim/scenario.cpp, in a comment or in the tests passes", () => {
  const r = check({
    "sim/scenario.cpp": randomBytes(winRd, getentropy),
    "sim/scenario.hpp": "// Never std::random_device on the others: libstdc++ tries RDSEED first\n",
    "tests/core/test_net.cpp": 'auto dir = "gc-test-" + std::to_string(std::random_device{}());\n',
  });
  assert.equal(r.status, 0, r.stdout);
  assert.match(r.stdout, /Architekturregeln: ok/);
});

test("Home Assistant's user header outside ha/main.cpp, a forwarded header read, or HTML written into the web app fails the check (SD-077)", () => {
  for (const [file, text, message] of [
    ["sim/main.cpp", 'auto user = req.get_header_value("X-Remote-User-Id");\n', /user header outside ha\/main\.cpp/],
    ["core/src/api.cpp", 'const auto from = headers.find("X-Forwarded-For");\n', /a forwarded header read/],
    ["ha/main.cpp", 'const auto path = req.get_header_value("X-Ingress-Path");\n', /a forwarded header read/],
    ["web/src/pages/notes.tsx", "box.innerHTML = text;\n", /HTML written into the page/],
    ["web/src/widgets.tsx", "<div dangerouslySetInnerHTML={{ __html: text }} />\n", /HTML written into the page/],
    ["web/src/ui.tsx", "node.outerHTML = text;\n", /HTML written into the page/],
    ["web/src/chart.tsx", 'el.insertAdjacentHTML("beforeend", text);\n', /HTML written into the page/],
    ["web/src/main.tsx", "document.write(text);\n", /HTML written into the page/],
  ]) {
    const r = check({ [file]: text });
    assert.equal(r.status, 1, file);
    assert.match(r.stdout, message, file);
  }
  const r = check({
    "ha/main.cpp": 'const auto id = soleHeader(req, "X-Remote-User-Id");\n',
    "ha/app/DOCS.md": "The Supervisor names the user in X-Remote-User-Id.\n",  // the docs may name it
    "web/src/ui.tsx": "<p>{text}</p>\n",
  });
  assert.equal(r.status, 0, r.stdout);
});
