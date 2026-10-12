// SPDX-License-Identifier: AGPL-3.0-or-later
// gc_ha_server as a Home Assistant app (SD-034): --app refuses a mapping
// file and a token file, takes the token only from SUPERVISOR_TOKEN and
// writes nothing without it, is not switched on by an option's value, and
// still honours --data, --host, --web and --port. Values that a stand-in
// Home Assistant reports reach the hub's readings (#112). Runs the built
// server (tools/ci.sh builds it first; the app image's own server in
// .github/workflows/app.yml).
//   node --test tools/ha_server.test.mjs      (GC_HA_SERVER=<binary> for another build)
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs";
import { createServer as httpServer, get } from "node:http";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, test } from "node:test";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("..", import.meta.url));
const bin = process.env.GC_HA_SERVER ?? join(root, "build", "gc_ha_server");
const tmp = mkdtempSync(join(tmpdir(), "gc-ha-server-"));
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

// A stand-in for Home Assistant: every read gets these states and a Date
// header, as Home Assistant sends one.
async function standIn(states) {
  const seen = [];
  const server = httpServer((req, res) => {
    seen.push(`${req.method} ${req.url} ${req.headers.authorization ?? ""}`);
    res.writeHead(200, { "Content-Type": "application/json", Date: new Date().toUTCString() });
    res.end(JSON.stringify(states));
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  return { url: `http://127.0.0.1:${server.address().port}`, seen, close: () => server.close() };
}

test("values from Home Assistant reach the hub: a number, an unavailable sensor, a unit to convert (#112)", { skip }, async () => {
  const now = new Date().toISOString();
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
  ]);
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
    for (let i = 0; i < 60 && !(await info(port)); i++) await new Promise((r) => setTimeout(r, 250));
    // as the web app does it: set the password, then pick a sensor per measurement
    const json = { "Content-Type": "application/json", Origin: base };
    const setup = await fetch(`${base}/api/v1/auth/setup`, { method: "POST", headers: json, body: JSON.stringify({ password: "stand-in-password-1" }) });
    assert.equal(setup.status, 200, out);
    const cookie = (setup.headers.get("set-cookie") ?? "").split(";")[0];
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
    assert.equal(readings["zone.humidity"]?.value, null, "unavailable stays missing, never 0 (R5)");
    assert.ok(ha.seen.some((r) => r === "GET /api/states Bearer stand-in-token"), ha.seen.join("\n"));
    assert.doesNotMatch(out, /no Date header/, "the stand-in's Date header was read");
  } finally {
    child.kill("SIGTERM");
    ha.close();
  }
  assert.deepEqual(await ended, { code: 0, signal: null }, out);
});
