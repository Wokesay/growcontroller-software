// SPDX-License-Identifier: AGPL-3.0-or-later
// gc_sim_server and a --web folder whose status it cannot read: that is no
// web app, so it starts without it (with the embedded web app or the "not
// built" page) instead of ending at the start. Runs the built server
// (tools/ci.sh builds it first).
//   node --test tools/sim_server.test.mjs      (GC_SIM_SERVER=<binary> for another build)
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { chmodSync, existsSync, mkdirSync, mkdtempSync, readdirSync, rmSync, symlinkSync } from "node:fs";
import { get } from "node:http";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, test } from "node:test";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const bin = process.env.GC_SIM_SERVER ?? join(root, "build", "gc_sim_server");
const tmp = mkdtempSync(join(tmpdir(), "gc-sim-server-"));
const locked = join(tmp, "locked");
after(() => {
  if (existsSync(locked)) chmodSync(locked, 0o700);  // so it can be removed
  rmSync(tmp, { recursive: true, force: true });
});

function freePort() {
  return new Promise((resolve) => {
    const s = createServer().listen(0, "127.0.0.1", () => {
      const { port } = s.address();
      s.close(() => resolve(port));
    });
  });
}
// A GET from the server; null if it does not answer.
function fetchStatus(port, path) {
  return new Promise((resolve) => {
    get({ host: "127.0.0.1", port, path, timeout: 1000 }, (res) => {
      let body = "";
      res.on("data", (c) => (body += c));
      res.on("end", () => resolve({ status: res.statusCode, body }));
    })
      .on("timeout", function () {
        this.destroy(); // a server that accepts but never answers
      })
      .on("error", () => resolve(null));
  });
}

// Starts the server with this --web, in its own folder and without --data,
// so it writes nothing; it must answer and end cleanly on SIGTERM.
async function startsWith(web) {
  const child = spawn(bin, ["--host", "127.0.0.1", "--port", String(await freePort()), "--scenario", "neu", "--prefill", "0", "--web", web], {
    cwd: tmp,
    stdio: ["ignore", "pipe", "pipe"],
  });
  let out = "";
  child.stdout.on("data", (c) => (out += c));
  child.stderr.on("data", (c) => (out += c));
  let exit = null;
  // close, not exit: its output is complete by then
  const ended = new Promise((resolve) => child.on("close", (code, signal) => resolve((exit = { code, signal }))));
  try {
    // the address it bound (the next port if that one was taken meanwhile)
    let port = null;
    for (let i = 0; i < 120 && !port && !exit; i++) {
      port = /http:\/\/127\.0\.0\.1:(\d+)\n/.exec(out)?.[1];
      if (!port) await new Promise((r) => setTimeout(r, 250));
    }
    assert.ok(port, `the server starts (exit ${JSON.stringify(exit)}):\n${out}`);
    let info = null;
    for (let i = 0; i < 40 && info?.status !== 200 && !exit; i++) {
      info = await fetchStatus(port, "/api/v1/info");
      if (info?.status !== 200) await new Promise((r) => setTimeout(r, 250));
    }
    assert.equal(info?.status, 200, `/api/v1/info answers:\n${out}`);
    assert.equal(JSON.parse(info.body).platform?.simulated, true, info.body);
    assert.equal((await fetchStatus(port, "/"))?.status, 200, `the start page answers:\n${out}`);
  } finally {
    child.kill("SIGTERM");
  }
  assert.deepEqual(await ended, { code: 0, signal: null }, out);
}

// tools/ci.sh builds the server first: in CI a missing one is a failure,
// not a skip.
if (process.env.CI && !existsSync(bin)) throw new Error(`${bin} is not built`);
const skip = !existsSync(bin) ? `${bin} is not built` : process.platform === "win32" ? "POSIX only" : false;

test("--web below a folder it may not enter: it starts without it", { skip, timeout: 120000 }, async (t) => {
  mkdirSync(locked);
  chmodSync(locked, 0o000);
  // root (or a process with the right to read any folder) enters it anyway
  try {
    readdirSync(locked);
    return t.skip("this process enters any folder");
  } catch {}
  await startsWith(join(locked, "dist"));
});

test("--web through a link to itself: it starts without it", { skip, timeout: 120000 }, async () => {
  // Reading its status fails (too many levels of links), for root too.
  const loop = join(tmp, "loop");
  symlinkSync("loop", loop);
  await startsWith(loop);
});
