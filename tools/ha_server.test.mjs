// SPDX-License-Identifier: AGPL-3.0-or-later
// gc_ha_server as a Home Assistant app (SD-034): --app refuses a mapping
// file and a token file, takes the token only from SUPERVISOR_TOKEN and
// writes nothing without it, is not switched on by an option's value, and
// still honours --data, --host, --web and --port. Started as root, it gives
// up root before it opens its port (#80). The first password needs
// the setup code the start prints (#79). Values that a stand-in Home
// Assistant reports reach the hub's readings (#112), with the session
// taken from the one cookie named exactly gc_session. A --config it cannot
// read or a number out of range ends with a message, not a crash (#213). Runs the built
// server (tools/ci.sh builds it first; the app image's own server in
// .github/workflows/app.yml).
//   node --test tools/ha_server.test.mjs      (GC_HA_SERVER=<binary> for another build)
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { chmodSync, existsSync, lstatSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync, symlinkSync, writeFileSync } from "node:fs";
import { createServer as httpServer, get, request } from "node:http";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, test } from "node:test";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const bin = process.env.GC_HA_SERVER ?? join(root, "build", "gc_ha_server");
const tmp = mkdtempSync(join(tmpdir(), "gc-ha-server-"));
chmodSync(tmp, 0o755); // run as root, --app becomes 65532 (#80) and must still reach its folders
after(() => rmSync(tmp, { recursive: true, force: true }));

// The environment without either token, plus what a case sets.
function env(extra = {}) {
  const e = { ...process.env, ...extra };
  for (const k of ["SUPERVISOR_TOKEN", "HASSIO_TOKEN", "GC_HA_TOKEN"]) if (!(k in extra)) delete e[k];
  return e;
}
// A call that must end at once; one that starts the server instead times
// out. In its own folder, so a mistaken start writes nothing into the
// repository.
function run(args, extra) {
  return spawnSync(bin, args, { cwd: tmp, env: env(extra), encoding: "utf8", timeout: 15000 });
}
function freePort() {
  return new Promise((resolve) => {
    const s = createServer().listen(0, "127.0.0.1", () => {
      const { port } = s.address();
      s.close(() => resolve(port));
    });
  });
}
function info(port) {
  return new Promise((resolve) => {
    get({ host: "127.0.0.1", port, path: "/api/v1/info", timeout: 1000 }, (res) => {
      let body = "";
      res.on("data", (c) => (body += c));
      res.on("end", () => resolve(res.statusCode === 200 ? JSON.parse(body) : null));
    })
      .on("timeout", function () {
        this.destroy(); // a server that accepts but never answers
      })
      .on("error", () => resolve(null));
  });
}

// The setup code the start printed (#79), once it is out.
async function setupCode(output) {
  for (let i = 0; i < 60; i++) {
    const m = /Setup code for the first password: ([2-9A-HJ-NP-Z]{4}(?:-[2-9A-HJ-NP-Z]{4}){3})\n/.exec(output());
    if (m) return m[1];
    await new Promise((r) => setTimeout(r, 250));
  }
  assert.fail(`no setup code in the output:\n${output()}`);
}

// tools/ci.sh builds the server first: in CI a missing one is a failure,
// not a skip.
if (process.env.CI && !existsSync(bin)) throw new Error(`${bin} is not built`);
const skip = !existsSync(bin) ? `${bin} is not built` : process.platform === "win32" ? "POSIX only" : false;

test("--app refuses a mapping file and a token file", { skip }, () => {
  for (const args of [["--app", "--config", "x.json"], ["--config", "x.json", "--app"], ["--app", "--token-file", "t"]]) {
    const r = run(args, { SUPERVISOR_TOKEN: "t" });
    assert.equal(r.status, 2, args.join(" "));
    assert.match(r.stderr, /leave out --config and --token-file/);
  }
});

test("--app takes the token only from SUPERVISOR_TOKEN and writes nothing without it", { skip }, () => {
  const data = join(tmp, "no-token");
  for (const extra of [{ GC_HA_TOKEN: "user-token" }, { HASSIO_TOKEN: "old-name" }, { SUPERVISOR_TOKEN: " \n" }]) {
    const r = run(["--app", "--data", data], extra);
    assert.equal(r.status, 2, JSON.stringify(Object.keys(extra)));
    assert.match(r.stderr, /No SUPERVISOR_TOKEN/);
    assert.ok(!existsSync(data), "no data folder without a token");
  }
  // Outside --app the Supervisor's token is not taken either.
  const config = join(tmp, "mapping.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  const r = run(["--config", config, "--data", data], { SUPERVISOR_TOKEN: "t" });
  assert.equal(r.status, 2);
  assert.match(r.stderr, /No token/);
  assert.ok(!existsSync(data));
});

test("a --config through a link to itself ends with a message, not a crash (#213)", { skip }, () => {
  // its status cannot be read, for root too
  const dir = mkdtempSync(join(tmp, "config-"));
  symlinkSync("loop", join(dir, "loop"));
  const r = run(["--config", join(dir, "loop", "ha.json"), "--data", join(dir, "hub")], { GC_HA_TOKEN: "t" });
  assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
  assert.match(r.stderr, /ha\.json: cannot be read \(.+\)\n/);
  assert.ok(!existsSync(join(dir, "hub")), "no data folder");
});

test("a --config below a folder it may not enter ends with a message, not a crash (#213)", { skip }, (t) => {
  // the file is there, but its status cannot be read
  const dir = mkdtempSync(join(tmp, "config-"));
  const locked = join(dir, "locked");
  mkdirSync(locked);
  writeFileSync(join(locked, "ha.json"), JSON.stringify({ url: "http://127.0.0.1:9" }));
  chmodSync(locked, 0o000);
  try {
    try {
      statSync(join(locked, "ha.json"));
      return t.skip("this process enters any folder");
    } catch {}
    const r = run(["--config", join(locked, "ha.json"), "--data", join(dir, "hub")], { GC_HA_TOKEN: "t" });
    assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
    assert.match(r.stderr, /ha\.json: cannot be read \(Permission denied\)\n/);
    assert.ok(!existsSync(join(dir, "hub")), "no data folder");
  } finally {
    chmodSync(locked, 0o700); // so it can be removed
  }
});

test("a --config that is there but cannot be opened says so, not that it holds no mapping (#213)", { skip }, (t) => {
  const dir = mkdtempSync(join(tmp, "config-"));
  writeFileSync(join(dir, "ha.json"), JSON.stringify({ url: "http://127.0.0.1:9" }));
  chmodSync(join(dir, "ha.json"), 0o000);
  try {
    readFileSync(join(dir, "ha.json"));
    return t.skip("this process reads any file");
  } catch {}
  const r = run(["--config", join(dir, "ha.json"), "--data", join(dir, "hub")], { GC_HA_TOKEN: "t" });
  assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
  assert.match(r.stderr, /ha\.json: cannot be read \(.+\)\n/);
  assert.ok(!existsSync(join(dir, "hub")), "no data folder");
});

test("a number option out of range names the range (#213)", { skip }, () => {
  for (const [option, value] of [["--port", "70000"], ["--every", "0"], ["--port", "x"]]) {
    const r = run(["--config", "x.json", option, value], { GC_HA_TOKEN: "t" });
    assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
    assert.ok(r.stderr.split("\n").includes(`${option} must be a whole number from 1 to 65535, not "${value}"`), r.stderr);
  }
});

test("a state.json it cannot read is kept: the hub starts and never writes over it (SD-081)", { skip }, async () => {
  const dir = mkdtempSync(join(tmp, "kept-"));
  const config = join(dir, "ha.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  const data = join(dir, "hub");
  mkdirSync(data, { mode: 0o700 });
  symlinkSync("state.json", join(data, "state.json"));  // a link to itself: it cannot be read, also as root
  const port = await freePort();
  const child = spawn(bin, ["--config", config, "--data", data, "--host", "127.0.0.1", "--port", String(port)], {
    env: env({ GC_HA_TOKEN: "t" }),
    stdio: ["ignore", "pipe", "pipe"],
  });
  let out = "";
  child.stdout.on("data", (c) => (out += c));
  child.stderr.on("data", (c) => (out += c));
  const ended = new Promise((resolve) => child.on("exit", (code, signal) => resolve({ code, signal })));
  try {
    let body = null;
    for (let i = 0; i < 60 && !body; i++) {
      body = await info(port);
      if (!body) await new Promise((r) => setTimeout(r, 250));
    }
    assert.ok(body, `the server answers on ${port}:\n${out}`);
  } finally {
    child.kill("SIGTERM");
  }
  assert.deepEqual(await ended, { code: 0, signal: null }, out);
  // said where who runs it looks, with the reason (its wording is the C library's)
  const kept = `${join(data, "state.json")}: cannot be read (`;
  assert.ok(out.split("\n").some((l) => l.startsWith(kept) && /\(.+\), so it is kept as it is/.test(l)), out);
  assert.ok(lstatSync(join(data, "state.json")).isSymbolicLink(), "state.json is still the link it was");
});

test("an option's value --app does not switch to the app", { skip }, () => {
  // Taken for the app, this would start with the Supervisor's token.
  const r = run(["--data", "--app"], { SUPERVISOR_TOKEN: "t" });
  assert.equal(r.status, 2, "usage: no --config");
  assert.ok(!existsSync(join(tmp, "--app")), "no data folder named --app");
});

test("--app starts with the Supervisor's token and honours --data, --host, --web and --port", { skip }, async () => {
  const port = await freePort();
  const data = join(tmp, "hub");
  const child = spawn(bin, ["--app", "--data", data, "--host", "127.0.0.1", "--web", join(tmp, "no-web"), "--port", String(port)], {
    env: env({ SUPERVISOR_TOKEN: "test-token" }),
    stdio: ["ignore", "pipe", "pipe"],
  });
  let out = "";
  child.stdout.on("data", (c) => (out += c));
  child.stderr.on("data", (c) => (out += c));
  const ended = new Promise((resolve) => child.on("exit", (code, signal) => resolve({ code, signal })));
  try {
    let body = null;
    for (let i = 0; i < 60 && !body; i++) {
      body = await info(port);
      if (!body) await new Promise((r) => setTimeout(r, 250));
    }
    assert.ok(body, `the server answers on ${port}:\n${out}`);
    assert.deepEqual(body.platform, { kind: "home-assistant", readOnly: true, simulated: false });
    assert.match(out, /http:\/\/127\.0\.0\.1:\d+/);
    assert.match(out, /from http:\/\/supervisor\/core every 5 s/);
  } finally {
    child.kill("SIGTERM");
  }
  assert.deepEqual(await ended, { code: 0, signal: null }, out);
  assert.equal(statSync(data).mode & 0o777, 0o700, "the data folder");
  const files = readdirSync(data);
  assert.ok(files.length > 0, "the hub wrote its data folder");
  for (const f of files) assert.equal(statSync(join(data, f)).mode & 0o777, 0o600, f);
});

test("the first password needs the setup code the start printed; a restart makes a new one; once set, a start prints none (#79)", { skip }, async () => {
  const data = join(tmp, "code");
  const start = async () => {
    const port = await freePort();
    const child = spawn(bin, ["--app", "--data", data, "--host", "127.0.0.1", "--web", join(tmp, "no-web"), "--port", String(port)], {
      env: env({ SUPERVISOR_TOKEN: "test-token" }),
      stdio: ["ignore", "pipe", "pipe"],
    });
    const run = { port, child, out: "" };
    child.stdout.on("data", (c) => (run.out += c));
    child.stderr.on("data", (c) => (run.out += c));
    run.ended = new Promise((resolve) => child.on("exit", (code, signal) => resolve({ code, signal })));
    for (let i = 0; i < 60 && !(await info(port)); i++) await new Promise((r) => setTimeout(r, 250));
    return run;
  };
  const before = await start();
  let old;
  try {
    old = await setupCode(() => before.out);
  } finally {
    before.child.kill("SIGTERM");
  }
  assert.deepEqual(await before.ended, { code: 0, signal: null }, before.out);
  const first = await start();
  try {
    const code = await setupCode(() => first.out);
    assert.notEqual(code, old, "a restart makes a new code");
    assert.equal(await post(first.port, "/api/v1/auth/setup", { password: "uebernahme-123", code: old }, "127.0.0.1"), 403, "the code of the start before");
    assert.equal((await info(first.port)).setupCode, true);
    assert.equal(await post(first.port, "/api/v1/auth/setup", { password: "uebernahme-123" }, "127.0.0.1"), 403, "without the code");
    assert.equal(await post(first.port, "/api/v1/auth/setup", { password: "uebernahme-123", code: "2222-2222-2222-2222" }, "127.0.0.1"), 403);
    assert.equal(await post(first.port, "/api/v1/auth/setup", { password: "mein-passwort", code: code.toLowerCase() }, "127.0.0.1"), 200);
    assert.equal((await info(first.port)).setupCode, false);
    for (const f of readdirSync(data)) assert.doesNotMatch(readFileSync(join(data, f), "utf8"), new RegExp(code.replaceAll("-", "-?")), f);
  } finally {
    first.child.kill("SIGTERM");
  }
  assert.deepEqual(await first.ended, { code: 0, signal: null }, first.out);
  const again = await start();
  try {
    assert.equal((await info(again.port)).setupCode, false);
    for (let i = 0; i < 60 && !/Reading \d+ picked sensors/.test(again.out); i++) await new Promise((r) => setTimeout(r, 250));
    assert.match(again.out, /Reading \d+ picked sensors/, "the start's output is complete");  // a code would come right after it
    assert.doesNotMatch(again.out, /Setup code/);
  } finally {
    again.child.kill("SIGTERM");
  }
  assert.deepEqual(await again.ended, { code: 0, signal: null }, again.out);
});

// A stand-in for Home Assistant: every read gets these states and a Date
// header by its own clock, as Home Assistant sends one.
async function standIn(states, clock) {
  const seen = [];
  const server = httpServer((req, res) => {
    seen.push(`${req.method} ${req.url} ${req.headers.authorization ?? ""}`);
    res.writeHead(200, { "Content-Type": "application/json", Date: new Date(clock()).toUTCString() });
    res.end(JSON.stringify(states));
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  return { url: `http://127.0.0.1:${server.address().port}`, seen, close: () => server.close() };
}

// As the Supervisor starts it: as root. The app job in
// .github/workflows/app.yml runs this file as root with the image's binary
// and sets GC_HA_ROOT_TEST, so there it cannot be skipped.
const asRoot = skip || (process.env.GC_HA_ROOT_TEST ? false : process.platform !== "linux" ? "Linux only" : process.getuid() !== 0 ? "needs to run as root" : false);
test("started as root, --app gives its data folder to 65532 and runs as it, without capabilities and new privileges (#80)", { skip: asRoot }, async () => {
  assert.equal(process.getuid?.(), 0, "GC_HA_ROOT_TEST is set: this must run as root");
  const parent = mkdtempSync(join(tmpdir(), "gc-ha-root-"));
  try {
    chmodSync(parent, 0o755); // as /data: the account the app becomes must reach its folder
    // Root's own, outside the data folder: a link to them is given away
    // itself, never what it points to, and the walk does not enter it
    const outside = join(parent, "outside");
    mkdirSync(outside, { mode: 0o700 });
    writeFileSync(join(outside, "file"), "root's", { mode: 0o600 });
    // A web folder the account may not read: no web app, not a crash at the start
    const web = join(parent, "web");
    mkdirSync(web, { mode: 0o700 });
    writeFileSync(join(web, "index.html"), '<div id="app"></div>');
    // The data folder as a version before #80 left it: root's; closed, as a
    // run before could have left it
    const data = join(parent, "hub");
    mkdirSync(join(data, "deeper"), { recursive: true, mode: 0o700 });
    writeFileSync(join(data, "auth.json"), "{}", { mode: 0o600 });
    writeFileSync(join(data, "deeper", "file"), "", { mode: 0o600 });
    chmodSync(data, 0o000);
    symlinkSync(join(outside, "file"), join(data, "link"));
    symlinkSync(outside, join(data, "folder-link"));
    const port = await freePort();
    const child = spawn(bin, ["--app", "--data", data, "--host", "127.0.0.1", "--web", web, "--port", String(port)], {
      env: env({ SUPERVISOR_TOKEN: "test-token" }),
      stdio: ["ignore", "pipe", "pipe"],
    });
    let out = "";
    child.stdout.on("data", (c) => (out += c));
    child.stderr.on("data", (c) => (out += c));
    const ended = new Promise((resolve) => child.on("exit", (code, signal) => resolve({ code, signal })));
    try {
      for (let i = 0; i < 60 && !(await info(port)); i++) await new Promise((r) => setTimeout(r, 250));
      assert.ok(await info(port), out);
      const status = Object.fromEntries(
        readFileSync(`/proc/${child.pid}/status`, "utf8").split("\n").filter((l) => l.includes(":")).map((l) => l.split(/:\s*/, 2)),
      );
      assert.equal(status.Uid, "65532\t65532\t65532\t65532");
      assert.equal(status.Gid, "65532\t65532\t65532\t65532");
      assert.equal(status.Groups, "");
      assert.equal(status.CapPrm, "0000000000000000");
      assert.equal(status.CapEff, "0000000000000000");
      assert.equal(status.NoNewPrivs, "1");
      const page = await fetch(`http://127.0.0.1:${port}/`);
      assert.match(await page.text(), /^growcontroller's web app cannot be read\./);
      assert.match(out, new RegExp(`The web app in ${web} cannot be read; growcontroller runs without it\\. Detail: Permission denied`));
    } finally {
      child.kill("SIGTERM");
    }
    assert.deepEqual(await ended, { code: 0, signal: null }, out);
    // The folder, private again, and what is in it are the account's, lstat:
    // links themselves. The store keeps no subfolders: one is handed over,
    // not entered
    assert.equal(statSync(data).mode & 0o777, 0o700, "the data folder");
    const entries = [data, ...readdirSync(data).map((f) => join(data, f))];
    for (const f of ["auth.json", "deeper", "link", "folder-link"]) assert.ok(entries.includes(join(data, f)), f);
    for (const p of entries) {
      const st = lstatSync(p);
      assert.deepEqual([st.uid, st.gid], [65532, 65532], p);
    }
    for (const p of [join(data, "deeper", "file"), outside, join(outside, "file"), web, join(web, "index.html")]) {
      const st = statSync(p);
      assert.deepEqual([st.uid, st.gid], [0, 0], `${p} is left alone`);
    }
  } finally {
    rmSync(parent, { recursive: true, force: true });
  }
});

test("started as root, --app refuses a data folder that is a link or a file and ends, its data kept (#80)", { skip: asRoot }, async () => {
  const parent = mkdtempSync(join(tmpdir(), "gc-ha-root-"));
  try {
    const target = join(parent, "elsewhere");
    mkdirSync(target, { mode: 0o700 });
    writeFileSync(join(target, "file"), "root's", { mode: 0o600 });
    symlinkSync(target, join(parent, "hub"));
    writeFileSync(join(parent, "a-file"), "root's", { mode: 0o600 });
    for (const [data, why] of [
      [join(parent, "hub"), /hub is a link/],
      [join(parent, "hub") + "/", /hub is a link/], // a slash at the end would follow the link
      [join(parent, "a-file"), /cannot create .*a-file/],
    ]) {
      const r = spawnSync(bin, ["--app", "--data", data, "--host", "127.0.0.1", "--port", String(await freePort())], {
        env: env({ SUPERVISOR_TOKEN: "test-token" }),
        encoding: "utf8",
        timeout: 15000,
      });
      assert.equal(r.status, 2, `${data}: ${r.stderr}`);
      assert.match(r.stderr, /^growcontroller did not start: it could not give up its administrator rights \(root\)\. Detail: .*\.\nYour data is kept\. Start the app again\./m, data);
      assert.match(r.stderr, why, data);
      assert.doesNotMatch(r.stderr, /reinstall/i, "reinstalling deletes the app's data");
      for (const p of [target, join(target, "file"), join(parent, "a-file")]) assert.equal(statSync(p).uid, 0, `${data}: ${p} is left alone`);
    }
  } finally {
    rmSync(parent, { recursive: true, force: true });
  }
});

test("values from Home Assistant reach the hub: a number, an unavailable sensor, a unit to convert (#112)", { skip, timeout: 60000 }, async () => {
  // Home Assistant's clock an hour ahead of this computer's, its values
  // 120 s old by it: ages come from its clock, not from this one
  const clock = () => Date.now() + 3600e3;
  // in Home Assistant's format: microseconds and an offset
  const now = new Date(clock() - 120e3).toISOString().replace("Z", "000+00:00");
  const state = (entity_id, value, unit, device_class) => ({
    entity_id,
    state: value,
    attributes: { unit_of_measurement: unit, device_class },
    last_changed: now,
    last_updated: now,
    last_reported: now,
  });
  const ha = await standIn([
    state("sensor.grow_ph", "6.2", "pH", "ph"),
    state("sensor.grow_rh", "unavailable", "%", "humidity"),
    state("sensor.grow_ec", "5", "µS/cm", "conductivity"),
  ], clock);
  const dir = mkdtempSync(join(tmp, "values-"));
  const config = join(dir, "ha.json");
  writeFileSync(config, JSON.stringify({ url: ha.url }));
  const port = await freePort();
  const base = `http://127.0.0.1:${port}`;
  const child = spawn(bin, ["--config", config, "--data", join(dir, "hub"), "--host", "127.0.0.1", "--port", String(port), "--every", "1"], {
    env: env({ GC_HA_TOKEN: "stand-in-token" }),
    stdio: ["ignore", "pipe", "pipe"],
  });
  let out = "";
  child.stdout.on("data", (c) => (out += c));
  child.stderr.on("data", (c) => (out += c));
  const ended = new Promise((resolve) => child.on("exit", (code, signal) => resolve({ code, signal })));
  try {
    let up = null;
    for (let i = 0; i < 60 && !up; i++) {
      up = await info(port);
      if (!up) await new Promise((r) => setTimeout(r, 250));
    }
    assert.ok(up, `the server answers on ${port}:\n${out}`);
    // as the web app does it: set the password, then pick a sensor per measurement
    const json = { "Content-Type": "application/json", Origin: base };
    const code = await setupCode(() => out);
    const setup = await fetch(`${base}/api/v1/auth/setup`, { method: "POST", headers: json, body: JSON.stringify({ password: "stand-in-password-1", code }) });
    assert.equal(setup.status, 200, out);
    const cookie = (setup.headers.get("set-cookie") ?? "").split(";")[0];
    // the session comes from the one cookie named exactly gc_session
    const status = async (c) => (await fetch(`${base}/api/v1/state`, { headers: { Cookie: c } })).status;
    assert.equal(await status(`theme=dark; ${cookie}; lang=en`), 200);
    assert.equal(await status(`x${cookie}`), 401);
    assert.equal(await status(`note=${cookie}`), 401);
    assert.equal(await status(`${cookie}; ${cookie}`), 401);
    const token = cookie.slice("gc_session=".length);
    const bearer = await fetch(`${base}/api/v1/state`, { headers: { Authorization: `Bearer ${token}`, Cookie: "gc_session=other" } });
    assert.equal(bearer.status, 200);
    // a sensor can be picked once the hub has read it from Home Assistant
    const wanted = ["sensor.grow_ph", "sensor.grow_rh", "sensor.grow_ec"];
    let listed = [];
    for (let i = 0; i < 40 && !wanted.every((e) => listed.includes(e)); i++) {
      const c = await (await fetch(`${base}/api/v1/ha/candidates?others=1`, { headers: { Cookie: cookie } })).json();
      listed = [...(c.candidates ?? []), ...(c.others ?? [])].map((x) => x.entity);
      if (!wanted.every((e) => listed.includes(e))) await new Promise((r) => setTimeout(r, 250));
    }
    assert.deepEqual(wanted.filter((e) => !listed.includes(e)), [], `read from the stand-in:\n${out}`);
    for (const [role, entity] of [["tank.ph", "sensor.grow_ph"], ["zone.humidity", "sensor.grow_rh"], ["tank.ec", "sensor.grow_ec"]]) {
      const r = await fetch(`${base}/api/v1/ha/assign`, { method: "POST", headers: { ...json, Cookie: cookie }, body: JSON.stringify({ role, entity }) });
      assert.equal(r.status, 200, `${role}: ${await r.text()}\n${out}`);
    }
    let readings = {};
    for (let i = 0; i < 40; i++) {
      readings = (await (await fetch(`${base}/api/v1/state`, { headers: { Cookie: cookie } })).json()).readings;
      if (readings["tank.ph"]?.value != null && readings["tank.ec"]?.value != null) break;
      await new Promise((r) => setTimeout(r, 250));
    }
    assert.equal(readings["tank.ph"]?.value, 6.2, JSON.stringify(readings["tank.ph"]));
    assert.equal(readings["tank.ec"]?.value, 0.005, JSON.stringify(readings["tank.ec"]));
    assert.equal(readings["tank.ec"]?.unit, "mS/cm");
    // taken as values calibrated elsewhere, 120 s old plus the test's run
    // time: a time read wrongly, or this computer's clock, gives another age
    for (const role of ["tank.ph", "tank.ec"]) {
      const r = readings[role];
      assert.equal(r?.quality, "uncalibrated", `${role}: ${JSON.stringify(r)}`);
      assert.ok(r.ageMs >= 118000 && r.ageMs < 150000, `${role}: ${JSON.stringify(r)}`);
    }
    assert.equal(readings["zone.humidity"]?.value, null, "unavailable stays missing, never 0 (R5)");
    assert.equal(readings["zone.humidity"]?.quality, "offline", JSON.stringify(readings["zone.humidity"]));
    assert.ok(ha.seen.some((r) => r === "GET /api/states Bearer stand-in-token"), ha.seen.join("\n"));
    assert.doesNotMatch(out, /no Date header/, "the stand-in's Date header was read");
  } finally {
    child.kill("SIGTERM");
    ha.close();
  }
  assert.deepEqual(await ended, { code: 0, signal: null }, out);
});

// A JSON POST from a given local address: Linux has all of 127.0.0.0/8 on its
// loopback, so 127.0.0.2 is another device to the server.
function post(port, path, body, localAddress) {
  return new Promise((resolve, reject) => {
    const data = JSON.stringify(body);
    const req = request(
      { host: "127.0.0.1", port, path, method: "POST", localAddress, headers: { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(data) } },
      (res) => {
        res.resume();
        res.on("end", () => resolve(res.statusCode));
      },
    );
    req.on("error", reject);
    req.end(data);
  });
}

test("the lockout tells devices apart by the connection's address (SD-051)", { skip: skip || (process.platform !== "linux" && "needs 127.0.0.2 on the loopback (Linux)") }, async () => {
  const port = await freePort();
  const child = spawn(bin, ["--app", "--data", join(tmp, "lockout"), "--host", "127.0.0.1", "--web", join(tmp, "no-web"), "--port", String(port)], {
    env: env({ SUPERVISOR_TOKEN: "test-token" }),
    stdio: ["ignore", "pipe", "ignore"],
  });
  let out = "";
  child.stdout.on("data", (c) => (out += c));
  const ended = new Promise((resolve) => child.on("exit", resolve));
  try {
    for (let i = 0; i < 60 && !(await info(port)); i++) await new Promise((r) => setTimeout(r, 250));
    const code = await setupCode(() => out);
    assert.equal(await post(port, "/api/v1/auth/setup", { password: "mein-passwort", code }, "127.0.0.1"), 200);  // signed in from here
    const tries = [];
    for (let i = 0; i < 6; i++) tries.push(await post(port, "/api/v1/auth/login", { password: "falsch" }, "127.0.0.2"));
    assert.deepEqual(tries, [401, 401, 401, 401, 401, 429]);
    assert.equal(await post(port, "/api/v1/auth/login", { password: "mein-passwort" }, "127.0.0.3"), 429, "a device that never signed in shares the lockout");
    assert.equal(await post(port, "/api/v1/auth/login", { password: "mein-passwort" }, "127.0.0.1"), 200, "the device that signed in keeps its own count");
  } finally {
    child.kill("SIGTERM");
  }
  await ended;
});
