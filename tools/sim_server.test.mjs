// SPDX-License-Identifier: AGPL-3.0-or-later
// gc_sim_server and a folder whose status it cannot read, instead of ending
// at the start: a --web folder is no web app, so it starts without it (with
// the embedded web app or the "not built" page); a --data folder runs it in
// memory only and is left alone (SD-074). The session comes from the one
// cookie named exactly gc_session. Runs the built server (tools/ci.sh
// builds it first).
//   node --test tools/sim_server.test.mjs      (GC_SIM_SERVER=<binary> for another build)
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { chmodSync, existsSync, lstatSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, symlinkSync, writeFileSync } from "node:fs";
import { get } from "node:http";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, test } from "node:test";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const bin = process.env.GC_SIM_SERVER ?? join(root, "build", "gc_sim_server");
const tmp = mkdtempSync(join(tmpdir(), "gc-sim-server-"));
const locked = [];  // folders it may not enter
after(() => {
  for (const dir of locked) if (existsSync(dir)) chmodSync(dir, 0o700);  // so they can be removed
  rmSync(tmp, { recursive: true, force: true });
});

// A folder it may not enter, unless this process enters any folder (root,
// or one with the right to read any folder): then the case is skipped.
function lock(dir) {
  chmodSync(dir, 0o000);
  locked.push(dir);
  try {
    readdirSync(dir);
    return false;
  } catch {
    return true;
  }
}

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

// Starts the server with these options, in its own folder; it must answer
// and end cleanly on SIGTERM. `check` runs while it is up, with its port.
// Gives what it printed.
async function startsWith(args, check) {
  const child = spawn(bin, ["--host", "127.0.0.1", "--port", String(await freePort()), "--scenario", "neu", "--prefill", "0", ...args], {
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
    if (check) await check(port);
  } finally {
    child.kill("SIGTERM");
  }
  assert.deepEqual(await ended, { code: 0, signal: null }, out);
  return out;
}
const webSaysWhy = /The web app in .* cannot be read; the simulator (runs without it|serves its built-in one)\. Detail: /;
// said once, and not the message for a folder it cannot create
function dataSaysWhy(out) {
  assert.equal(out.match(/Data cannot be read \(.+: .+\)\. Running in memory only\./g)?.length, 1, out);
  assert.doesNotMatch(out, /Data cannot be saved/);
}

// tools/ci.sh builds the server first: in CI a missing one is a failure,
// not a skip.
if (process.env.CI && !existsSync(bin)) throw new Error(`${bin} is not built`);
const skip = !existsSync(bin) ? `${bin} is not built` : process.platform === "win32" ? "POSIX only" : false;

test("--web below a folder it may not enter: it starts without it", { skip, timeout: 120000 }, async (t) => {
  const dir = join(tmp, "locked-web");
  mkdirSync(dir);
  if (!lock(dir)) return t.skip("this process enters any folder");
  assert.match(await startsWith(["--web", join(dir, "dist")]), webSaysWhy);
});

test("--web through a link to itself: it starts without it", { skip, timeout: 120000 }, async () => {
  // Reading its status fails (too many levels of links), for root too.
  const loop = join(mkdtempSync(join(tmp, "web-")), "loop");
  symlinkSync("loop", loop);
  assert.match(await startsWith(["--web", loop]), webSaysWhy);
});

test("--data below a folder it may not enter: memory only, and its data stays as it was (SD-074)", { skip, timeout: 120000 }, async (t) => {
  const dir = join(tmp, "locked-data");
  const data = join(dir, "data");
  mkdirSync(data, { recursive: true });
  writeFileSync(join(data, "config.json"), '{"kept":true}');
  if (!lock(dir)) return t.skip("this process enters any folder");
  const out = await startsWith(["--data", data]);
  dataSaysWhy(out);
  assert.doesNotMatch(out, /^Daten: /m, "not named as its data folder");
  chmodSync(dir, 0o700);
  assert.deepEqual(readdirSync(data), ["config.json"]);
  assert.equal(readFileSync(join(data, "config.json"), "utf8"), '{"kept":true}');
});

test("--data through a link to itself: memory only, and the link stays (SD-074)", { skip, timeout: 120000 }, async () => {
  const dir = mkdtempSync(join(tmp, "data-"));
  const loop = join(dir, "loop");
  symlinkSync("loop", loop);
  const out = await startsWith(["--data", loop]);
  dataSaysWhy(out);
  assert.doesNotMatch(out, /^Daten: /m, "not named as its data folder");
  assert.deepEqual(readdirSync(dir), ["loop"]);
  assert.ok(lstatSync(loop).isSymbolicLink());
});

test("--data holding a file it cannot read: memory only, and every file stays as it was (SD-074)", { skip, timeout: 120000 }, async (t) => {
  // e.g. root's files after a start with sudo, in a folder it can read
  const data = mkdtempSync(join(tmp, "data-"));
  for (const f of ["config.json", "state.json"]) writeFileSync(join(data, f), '{"kept":true}');
  chmodSync(join(data, "state.json"), 0o000);
  try {
    readFileSync(join(data, "state.json"));
    return t.skip("this process reads any file");
  } catch {}
  const out = await startsWith(["--data", data]);
  dataSaysWhy(out);
  assert.match(out, /state\.json: /, "names the file");
  assert.doesNotMatch(out, /^Daten: /m, "not named as its data folder");
  chmodSync(join(data, "state.json"), 0o600);
  assert.deepEqual(readdirSync(data).sort(), ["config.json", "state.json"]);
  for (const f of ["config.json", "state.json"]) assert.equal(readFileSync(join(data, f), "utf8"), '{"kept":true}', f);
});

test("--data it can use is named as its data folder, and nothing is said about it", { skip, timeout: 120000 }, async () => {
  // what the cases above rule out
  const data = join(mkdtempSync(join(tmp, "data-")), "data");
  const out = await startsWith(["--data", data]);
  assert.ok(out.split("\n").includes(`Daten: ${data}`), out);
  assert.doesNotMatch(out, /Data cannot be/);
});

test("the session comes from the one cookie named exactly gc_session", { skip, timeout: 120000 }, async () => {
  await startsWith([], async (port) => {
    const base = `http://127.0.0.1:${port}`;
    const login = await fetch(`${base}/api/v1/auth/setup`, {
      method: "POST",
      headers: { "Content-Type": "application/json", Origin: base },
      body: JSON.stringify({ password: "sim-password-1" }),
    });
    assert.equal(login.status, 200, await login.text());
    const cookie = (login.headers.get("set-cookie") ?? "").split(";")[0];
    assert.match(cookie, /^gc_session=[0-9a-f]+$/);
    const state = async (c) => (await fetch(`${base}/api/v1/state`, { headers: { Cookie: c } })).status;
    assert.equal(await state(cookie), 200);
    assert.equal(await state(`theme=dark; ${cookie}; lang=en`), 200);
    assert.equal(await state(`x${cookie}`), 401);
    assert.equal(await state(`note=${cookie}`), 401);
    assert.equal(await state(`${cookie}; ${cookie}`), 401);
  });
});
