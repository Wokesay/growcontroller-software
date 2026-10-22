// SPDX-License-Identifier: AGPL-3.0-or-later
// gc_ha_server as a Home Assistant app (SD-034): --app refuses a mapping
// file and a token file, takes the token only from SUPERVISOR_TOKEN and
// writes nothing without it, is not switched on by an option's value, and
// still honours --data, --host, --web and --port. Started as root, it gives
// up root before it opens its port (#80). The first password needs
// the setup code the start prints (#79). Values that a stand-in Home
// Assistant reports reach the hub's readings (#112), with the session
// taken from the one cookie named exactly gc_session. A --config or
// --token-file it cannot read, or that is a folder, or a number out of range
// ends with a message naming it and why, not a crash or "No token" (#213); a
// token file named wins over GC_HA_TOKEN, a named pipe is read once, and a
// token given in place of its file is not shown. A --data it cannot create
// ends with a message, and a --web it cannot read leaves it running without
// the web app. Runs the built server (tools/ci.sh builds it first; the app
// image's own server in .github/workflows/app.yml).
//   node --test tools/ha_server.test.mjs      (GC_HA_SERVER=<binary> for another build)
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { chmodSync, existsSync, lstatSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, readlinkSync, rmSync, statSync, symlinkSync, writeFileSync } from "node:fs";
import { createServer as httpServer, get, request } from "node:http";
import { connect, createServer } from "node:net";
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

// A request from a given local address, with headers given as a flat list
// of names and values: a name may come twice, or spelt otherwise, as a
// browser or another container could send it.
function call(port, path, { method = "GET", body, headers = [], from = "127.0.0.1" } = {}) {
  return new Promise((resolve, reject) => {
    const data = body === undefined ? undefined : JSON.stringify(body);
    const raw = [...headers];
    if (data !== undefined) raw.push("Content-Type", "application/json", "Content-Length", String(Buffer.byteLength(data)));
    const req = request({ host: "127.0.0.1", port, path, method, localAddress: from, headers: raw }, (res) => {
      let text = "";
      res.setEncoding("utf8");
      res.on("data", (c) => (text += c));
      res.on("end", () => resolve({ status: res.statusCode, headers: res.headers, text, json: () => JSON.parse(text) }));
    });
    req.on("error", reject);
    req.end(data);
  });
}
// A request written byte for byte, for header values Node will not send as
// they are (UTF-8 in a header, as Home Assistant sends a user's name).
function rawCall(port, method, path, headers, body, host = "127.0.0.1") {
  return new Promise((resolve, reject) => {
    const data = Buffer.from(body === undefined ? "" : JSON.stringify(body), "utf8");
    const lines = [`${method} ${path} HTTP/1.1`, `Host: ${host}`, "Connection: close"];
    if (body !== undefined) lines.push("Content-Type: application/json", `Content-Length: ${data.length}`);
    for (let i = 0; i < headers.length; i += 2) lines.push(`${headers[i]}: ${headers[i + 1]}`);
    const socket = connect({ host: "127.0.0.1", port, localAddress: "127.0.0.1" });
    const parts = [];
    socket.on("data", (c) => parts.push(c));
    socket.on("end", () => resolve(Number(/^HTTP\/1\.1 (\d{3})/.exec(Buffer.concat(parts).toString("utf8"))?.[1])));
    socket.on("error", reject);
    socket.end(Buffer.concat([Buffer.from(`${lines.join("\r\n")}\r\n\r\n`, "utf8"), data]));
  });
}
// The user Home Assistant's Supervisor names on a request through Ingress (SD-077)
const ALEX = ["X-Remote-User-Id", "0123456789abcdef0123456789abcdef", "X-Remote-User-Display-Name", "Alex"];
// The app as the Supervisor starts it, with its Ingress listener answering
// this computer instead of the Supervisor; ready once /info answers there.
async function startApp(data, { web = join(tmp, "no-web"), extraEnv = {} } = {}) {
  const port = await freePort();
  let ingressPort = await freePort();
  while (ingressPort === port) ingressPort = await freePort();
  const child = spawn(
    bin,
    ["--app", "--data", data, "--host", "127.0.0.1", "--web", web, "--port", String(port), "--ingress-port", String(ingressPort), "--ingress-from", "127.0.0.1"],
    { env: env({ SUPERVISOR_TOKEN: "test-token", ...extraEnv }), stdio: ["ignore", "pipe", "pipe"] },
  );
  const app = { port, ingressPort, child, out: "" };
  child.stdout.on("data", (c) => (app.out += c));
  child.stderr.on("data", (c) => (app.out += c));
  app.ended = new Promise((resolve) => child.on("exit", (code, signal) => resolve({ code, signal })));
  for (let i = 0; i < 60 && !(await info(ingressPort)); i++) await new Promise((r) => setTimeout(r, 250));
  return app;
}

// Started in dir with a mapping and a token, plus args: check(port) once
// it answers, then a clean stop on SIGTERM. What it printed.
async function startServer(dir, args, check = async () => {}) {
  const config = join(dir, "ha.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  const port = await freePort();
  const child = spawn(bin, ["--config", config, "--data", join(dir, "hub"), ...args, "--host", "127.0.0.1", "--port", String(port)], {
    env: env({ GC_HA_TOKEN: "t" }),
    stdio: ["ignore", "pipe", "pipe"],
  });
  let out = "";
  child.stdout.on("data", (c) => (out += c));
  child.stderr.on("data", (c) => (out += c));
  const ended = new Promise((resolve) => child.on("exit", (code, signal) => resolve({ code, signal })));
  try {
    for (let i = 0; i < 60 && !(await info(port)); i++) await new Promise((r) => setTimeout(r, 250));
    assert.ok(await info(port), `the server answers on ${port}:\n${out}`);
    await check(port);
  } finally {
    child.kill("SIGTERM");
  }
  assert.deepEqual(await ended, { code: 0, signal: null }, out);
  return out;
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
  for (const args of [["--app", "--config", "x.json"], ["--config", "x.json", "--app"], ["--app", "--token-file", "t"], ["--app", "--token-file", ""]]) {
    // a refusal that broke would start in the test's folder, not in /data/hub
    const r = run([...args, "--data", join(tmp, "refused")], { SUPERVISOR_TOKEN: "t" });
    assert.equal(r.status, 2, args.join(" "));
    assert.match(r.stderr, /leave out --config and --token-file/);
  }
  assert.ok(!existsSync(join(tmp, "refused")), "no data folder");
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

test("a --config that is not there, or a folder, says so, not that it holds no mapping", { skip }, () => {
  const dir = mkdtempSync(join(tmp, "config-"));
  mkdirSync(join(dir, "folder"));
  for (const [file, says] of [["missing", "file not found"], ["folder", "is a folder, not a file"]]) {
    const r = run(["--config", join(dir, file), "--data", join(dir, "hub")], { GC_HA_TOKEN: "t" });
    assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
    assert.ok(r.stderr.split("\n").includes(`${join(dir, file)}: ${says}`), r.stderr);
    assert.ok(!existsSync(join(dir, "hub")), "no data folder");
  }
});

// A --token-file it cannot use is named with the reason, never "No token":
// that would send whoever starts it looking for the token, not the file. It
// does not fall back to GC_HA_TOKEN either: the file named wins.
test("a --token-file it cannot read names it and why, not that there is no token", { skip }, () => {
  const dir = mkdtempSync(join(tmp, "token-"));
  const config = join(dir, "ha.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  symlinkSync("loop", join(dir, "loop"));
  mkdirSync(join(dir, "folder"));
  // each also as root
  for (const [file, says] of [
    ["missing", "file not found"],
    [join("loop", "token"), /^cannot be read \(.+\)$/],
    ["folder", "is a folder, not a file"],
  ]) {
    const r = run(["--config", config, "--token-file", join(dir, file), "--data", join(dir, "hub")], { GC_HA_TOKEN: "t" });
    assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
    const line = r.stderr.split("\n").find((l) => l.startsWith(`${join(dir, file)}: `));
    assert.ok(line, r.stderr);
    if (typeof says === "string") assert.equal(line.slice(join(dir, file).length + 2), says);
    else assert.match(line.slice(join(dir, file).length + 2), says);
    assert.doesNotMatch(r.stderr, /No token/);
    assert.ok(!existsSync(join(dir, "hub")), "no data folder");
  }
  // one it reads but that holds no token says so
  writeFileSync(join(dir, "empty"), "\n");
  const r = run(["--config", config, "--token-file", join(dir, "empty"), "--data", join(dir, "hub")], { GC_HA_TOKEN: "t" });
  assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
  assert.ok(r.stderr.split("\n").includes(`${join(dir, "empty")}: holds no token`), r.stderr);
  assert.ok(!existsSync(join(dir, "hub")), "no data folder");
});

test("a --token-file it may not read, or below a folder it may not enter, says so", { skip }, (t) => {
  const dir = mkdtempSync(join(tmp, "token-"));
  const config = join(dir, "ha.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  writeFileSync(join(dir, "closed"), "t\n");
  const locked = join(dir, "locked");
  mkdirSync(locked);
  writeFileSync(join(locked, "token"), "t\n");
  chmodSync(join(dir, "closed"), 0o000);
  chmodSync(locked, 0o000);
  try {
    try {
      readFileSync(join(dir, "closed"));
      return t.skip("this process reads any file");
    } catch {}
    try {
      statSync(join(locked, "token"));
      return t.skip("this process enters any folder");
    } catch {}
    for (const file of [join(dir, "closed"), join(locked, "token")]) {
      const r = run(["--config", config, "--token-file", file, "--data", join(dir, "hub")], { GC_HA_TOKEN: "t" });
      assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
      assert.ok(r.stderr.split("\n").includes(`${file}: cannot be read (Permission denied)`), r.stderr);
      assert.ok(!existsSync(join(dir, "hub")), "no data folder");
    }
  } finally {
    chmodSync(locked, 0o700); // so it can be removed
  }
});

test("a --token-file with no name, or the token in place of its file, ends without showing it", { skip }, () => {
  const dir = mkdtempSync(join(tmp, "token-"));
  const config = join(dir, "ha.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  // an empty name (an unset shell variable) is no reason to take GC_HA_TOKEN
  let r = run(["--config", config, "--token-file", "", "--data", join(dir, "hub")], { GC_HA_TOKEN: "t" });
  assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
  assert.ok(r.stderr.split("\n").includes("--token-file needs the name of a file that holds the token"), r.stderr);
  // a long-lived token is a JWT; this one is shaped like one, not one
  const token = "eyJstand-in.eyJstand-in.stand-in";
  r = run(["--config", config, "--token-file", token, "--data", join(dir, "hub")], { GC_HA_TOKEN: "t" });
  assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
  assert.ok(r.stderr.split("\n").includes("--token-file takes a file that holds the token, not the token (not shown here)"), r.stderr);
  assert.ok(!r.stderr.includes(token) && !r.stdout.includes(token), "the token is not shown");
  assert.ok(!existsSync(join(dir, "hub")), "no data folder");
});

test("a --config and --token-file that are named pipes are read once, not emptied by a look first", { skip, timeout: 120000 }, async () => {
  const dir = mkdtempSync(join(tmp, "pipe-"));
  const writers = [];
  // each waits until the server opens it, then writes and ends
  const pipe = (name, text) => {
    assert.equal(spawnSync("mkfifo", [join(dir, name)]).status, 0, "mkfifo");
    const w = spawn("sh", ["-c", 'printf "%s" "$1" > "$0"', join(dir, name), text], { stdio: "ignore" });
    writers.push({ w, ended: new Promise((resolve) => w.on("exit", (code, signal) => resolve({ code, signal }))) });
    return join(dir, name);
  };
  const within = (p, ms) => Promise.race([p, new Promise((r) => setTimeout(() => r("still waiting for a reader"), ms))]);
  try {
    // the last --config counts
    await startServer(dir, ["--config", pipe("ha-pipe", JSON.stringify({ url: "http://127.0.0.1:9" })), "--token-file", pipe("token", "t\n")], async () => {
      // both were opened: what it runs with came from them, not from ha.json or GC_HA_TOKEN
      for (const { ended } of writers) assert.deepEqual(await within(ended, 5000), { code: 0, signal: null });
    });
  } finally {
    for (const { w } of writers) w.kill();
  }
});

test("a --data it cannot create ends with a message, and a link stays as it was", { skip }, () => {
  // through a link to itself, for root too: the app job runs this file as root
  const dir = mkdtempSync(join(tmp, "data-"));
  const config = join(dir, "ha.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  symlinkSync("loop", join(dir, "loop"));
  const data = join(dir, "loop", "hub");
  const r = run(["--config", config, "--data", data], { GC_HA_TOKEN: "t" });
  assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
  assert.ok(r.stderr.split("\n").includes(`The data folder ${data} cannot be written. Choose a writable one with --data.`), r.stderr);
  assert.equal(readlinkSync(join(dir, "loop")), "loop");
});

test("a --data below a folder it may not enter ends with a message", { skip }, (t) => {
  const dir = mkdtempSync(join(tmp, "data-"));
  const config = join(dir, "ha.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  const locked = join(dir, "locked");
  mkdirSync(locked);
  chmodSync(locked, 0o000);
  try {
    try {
      readdirSync(locked);
      return t.skip("this process enters any folder");
    } catch {}
    const data = join(locked, "hub");
    const r = run(["--config", config, "--data", data], { GC_HA_TOKEN: "t" });
    assert.deepEqual([r.status, r.signal], [2, null], r.stderr);
    assert.ok(r.stderr.split("\n").includes(`The data folder ${data} cannot be written. Choose a writable one with --data.`), r.stderr);
  } finally {
    chmodSync(locked, 0o700); // so it can be removed
  }
  assert.deepEqual(readdirSync(locked), [], "nothing created in it");
});

// A --web whose status cannot be read is no web app, not a crash at the
// start: it runs without it, says why (its wording is the C library's) and
// stops cleanly.
const webSaysWhy = (web) => new RegExp(`The web app in ${web.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")} cannot be read; growcontroller runs without it\\. Detail: .+\\.\\n`);

test("a --web through a link to itself: it runs without the web app and says why", { skip, timeout: 120000 }, async () => {
  // for root too: the app job runs this file as root
  const dir = mkdtempSync(join(tmp, "web-"));
  symlinkSync("loop", join(dir, "loop"));
  const out = await startServer(dir, ["--web", join(dir, "loop")]);
  assert.match(out, webSaysWhy(join(dir, "loop")));
});

test("a --web below a folder it may not enter: it runs without the web app and says why", { skip, timeout: 120000 }, async (t) => {
  const dir = mkdtempSync(join(tmp, "web-"));
  const locked = join(dir, "locked");
  mkdirSync(join(locked, "dist"), { recursive: true });
  writeFileSync(join(locked, "dist", "index.html"), '<div id="app"></div>');
  chmodSync(locked, 0o000);
  try {
    try {
      statSync(join(locked, "dist", "index.html"));
      return t.skip("this process enters any folder");
    } catch {}
    const out = await startServer(dir, ["--web", join(locked, "dist")], async (port) => {
      assert.doesNotMatch(await (await fetch(`http://127.0.0.1:${port}/`)).text(), /id="app"/, "not its web app");
    });
    assert.match(out, webSaysWhy(join(locked, "dist")));
    assert.match(out, /Detail: Permission denied\.\n/);
  } finally {
    chmodSync(locked, 0o700); // so it can be removed
  }
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
  const data = join(tmp, "hub");
  const app = await startApp(data);
  try {
    const body = await info(app.ingressPort);
    assert.ok(body, `the server answers through Ingress on ${app.ingressPort}:\n${app.out}`);
    assert.deepEqual(body.platform, { kind: "home-assistant", readOnly: true, signIn: "home-assistant", simulated: false });
    assert.match(app.out, /http:\/\/127\.0\.0\.1:\d+/);
    assert.match((await call(app.port, "/")).text, /Direct access at this address is off/, `the direct port on ${app.port}`);
    assert.match(app.out, /from http:\/\/supervisor\/core every 5 s/);
  } finally {
    app.child.kill("SIGTERM");
  }
  assert.deepEqual(await app.ended, { code: 0, signal: null }, app.out);
  assert.equal(statSync(data).mode & 0o777, 0o700, "the data folder");
  const files = readdirSync(data);
  assert.ok(files.length > 0, "the hub wrote its data folder");
  for (const f of files) assert.equal(statSync(join(data, f)).mode & 0o777, 0o600, f);
});

test("without --app the first password needs the setup code the start printed; a restart makes a new one; once set, a start prints none (#79)", { skip }, async () => {
  const data = join(tmp, "code");
  const config = join(tmp, "code-mapping.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  const start = async () => {
    const port = await freePort();
    const child = spawn(bin, ["--config", config, "--data", data, "--host", "127.0.0.1", "--web", join(tmp, "no-web"), "--port", String(port)], {
      env: env({ GC_HA_TOKEN: "test-token" }),
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

test("--ingress-port and --ingress-from go with --app, one address and another port (SD-077)", { skip }, () => {
  for (const args of [
    ["--config", "x.json", "--ingress-port", "9000"],
    ["--config", "x.json", "--ingress-from", "127.0.0.1"],
  ]) {
    const r = run(args, { GC_HA_TOKEN: "t" });
    assert.deepEqual([r.status, r.signal], [2, null], args.join(" "));
    assert.match(r.stderr, /go with --app/);
  }
  // Only the Supervisor or this computer may name Home Assistant's user; a
  // proxy elsewhere on the network would be trusted with that
  for (const from of ["0.0.0.0/0", "supervisor", "192.168.1.5", "172.30.32.3", "127.0.0.01", "0:0:0:0:0:ffff:ac1e:2002", "::1"]) {
    const r = run(["--app", "--ingress-from", from], { SUPERVISOR_TOKEN: "t" });
    assert.deepEqual([r.status, r.signal], [2, null], from);
    assert.match(r.stderr, /--ingress-from takes the Supervisor's address \(172\.30\.32\.2\) or one of this computer's \(127\.x\.x\.x\)/, from);
  }
  const same = run(["--app", "--port", "8100"], { SUPERVISOR_TOKEN: "t" });
  assert.deepEqual([same.status, same.signal], [2, null]);
  assert.match(same.stderr, /--ingress-port must differ from --port/);
  // Without --app there is no Ingress listener: 8100 is a port like any other
  const alone = run(["--config", join(tmp, "missing.json"), "--port", "8100"], { GC_HA_TOKEN: "t" });
  assert.doesNotMatch(alone.stderr, /ingress/i);
  assert.match(alone.stderr, /file not found/);
});

const linuxLoopback = skip || (process.platform !== "linux" && "needs 127.0.0.2 on the loopback (Linux)");
test("as an app, Ingress answers only the Supervisor and only with the user it signed in; the direct port takes nothing from Home Assistant (SD-077)", { skip: linuxLoopback }, async () => {
  const app = await startApp(join(tmp, "ingress"));
  const ing = (path, opts) => call(app.ingressPort, path, opts);
  const direct = (path, opts) => call(app.port, path, opts);
  try {
    assert.doesNotMatch(app.out, /Setup code/, "no first password over the network");
    assert.match(app.out, /Opens in Home Assistant's sidebar \(Ingress, port \d+, only from 127\.0\.0\.1\); port \d+ shows only a notice/);
    // Ingress: the user the Supervisor names, nothing else
    const state = await ing("/api/v1/state", { headers: ALEX });
    assert.equal(state.status, 200, state.text);
    assert.equal(state.headers["x-frame-options"], "SAMEORIGIN");
    assert.match(state.headers["content-security-policy"], /frame-ancestors 'self'/);
    const session = (await ing("/api/v1/auth/session", { headers: ALEX })).json();
    assert.deepEqual([session.authenticated, session.platform, session.user], [true, true, "Alex"]);
    assert.equal((await ing("/api/v1/info")).json().platform.signIn, "home-assistant", "the web app knows its way in");
    assert.equal((await ing("/api/v1/info")).status, 200, "the watchdog reads /info without a user");
    for (const [why, headers] of [
      ["no user", []],
      ["a copy spelt otherwise", ["x-remote-user-id", ALEX[1]]],
      ["two copies", [...ALEX, "x-remote-user-id", "ffffffffffffffffffffffffffffffff"]],
      ["not 32 lowercase hex digits", ["X-Remote-User-Id", ALEX[1].toUpperCase()]],
      ["too short", ["X-Remote-User-Id", ALEX[1].slice(1)]],
    ]) {
      const r = await ing("/api/v1/state", { headers });
      assert.equal(r.status, 403, why);
      assert.equal(r.json().error.key, "api.ingress_user", why);
    }
    assert.equal((await ing("/", { headers: [] })).status, 403, "the page too needs the user");
    const other = await ing("/api/v1/state", { headers: ALEX, from: "127.0.0.2" });
    assert.equal(other.status, 403, "another container, with a user it made up");
    assert.equal(other.json().error.key, "api.ingress");
    assert.equal((await ing("/api/v1/info", { from: "127.0.0.2" })).status, 403);
    assert.equal((await ing("/api/v1/stop", { method: "POST", body: {}, headers: [...ALEX, "Sec-Fetch-Site", "cross-site"] })).status, 403, "cross-site");
    // Home Assistant passes on the browser's host name, which may be any of its own (a domain, its cloud address); only its form is checked
    assert.equal((await ing("/api/v1/state", { headers: [...ALEX, "Host", "my-home.example.org"] })).status, 200, "a name of Home Assistant's own");
    const long = await ing("/api/v1/state", { headers: [...ALEX, "Host", `${"a".repeat(260)}.example.org`] });
    assert.equal(long.status, 403, "a host name longer than any");
    assert.equal(long.json().error.key, "api.ingress");
    assert.equal(await rawCall(app.ingressPort, "GET", "/api/v1/state", ALEX, undefined, "a".repeat(255)), 200, "255 bytes, the longest a host name can be");
    assert.equal(await rawCall(app.ingressPort, "GET", "/api/v1/state", ALEX, undefined, "a".repeat(256)), 403, "256 bytes");
    assert.equal(await rawCall(app.ingressPort, "GET", "/api/v1/state", ALEX, undefined, "my home.example.org"), 403, "a space in the host name");
    // The direct port without a password: a notice, no API, whatever Home Assistant's headers say
    const notice = await direct("/");
    assert.equal(notice.status, 200);
    assert.match(notice.text, /Open growcontroller in Home Assistant:/);
    assert.match(notice.text, /Öffne growcontroller in Home Assistant:/);
    assert.match(notice.text, /https:\/\/github\.com\/Wokesay\/growcontroller-software\/tree\/v/);
    assert.equal(notice.headers["x-frame-options"], "DENY");
    assert.equal(notice.headers["content-security-policy"], "default-src 'none'; frame-ancestors 'none'", "one policy, the notice's own");
    for (const path of ["/api/v1/info", "/api/v1/state", "/api/v1/auth/session"]) {
      const r = await direct(path, { headers: ALEX });
      assert.equal(r.status, 404, path);
      assert.equal(r.json().error.key, "api.ingress_only", path);
    }
    assert.equal((await direct("/api/v1/auth/setup", { method: "POST", body: { password: "uebernahme-123" } })).status, 404);
    // The password for direct access, set through Ingress
    const set = await ing("/api/v1/auth/direct", { method: "PUT", body: { password: "direkt-passwort" }, headers: ALEX });
    assert.equal(set.status, 200, set.text);
    assert.equal((await direct("/api/v1/info")).json().hasPassword, true);
    assert.equal((await direct("/api/v1/state", { headers: ALEX })).status, 401, "Home Assistant's user counts for nothing there");
    const login = await direct("/api/v1/auth/login", { method: "POST", body: { password: "direkt-passwort" } });
    assert.equal(login.status, 200, login.text);
    assert.match(String(login.headers["set-cookie"]), /; Path=\/api\/v1\/;/, "the cookie goes only to the API, the only part that needs it");
    const cookie = String(login.headers["set-cookie"]).split(";")[0];
    const renewed = await direct("/api/v1/auth/session", { headers: ["Cookie", cookie] });
    assert.match(String(renewed.headers["set-cookie"]), /^gc_session=[^;]+; Path=\/api\/v1\/;/, "renewed with the same path");
    assert.equal((await direct("/api/v1/state", { headers: ["Cookie", cookie] })).status, 200);
    assert.equal((await direct("/api/v1/state", { headers: ["Cookie", `x${cookie}`] })).status, 401, "the cookie by its exact name");
    const change = await direct("/api/v1/auth/password", { method: "PUT", body: { old: "direkt-passwort", new: "anderes-passwort" }, headers: ["Cookie", cookie] });
    assert.equal(change.status, 403, "never changed on the direct port");
    assert.equal((await ing("/api/v1/state", { headers: ["Cookie", cookie] })).status, 403, "the direct port's session counts for nothing through Ingress");
    const logout = await direct("/api/v1/auth/logout", { method: "POST", body: {}, headers: ["Cookie", cookie] });
    assert.match(String(logout.headers["set-cookie"]), /^gc_session=; Path=\/api\/v1\/;.*Max-Age=0/, "signed out: the same cookie, removed");
    const again = await direct("/api/v1/auth/login", { method: "POST", body: { password: "direkt-passwort" } });
    const cookie2 = String(again.headers["set-cookie"]).split(";")[0];
    // Removed through Ingress: the direct port is a notice again at once
    assert.equal((await ing("/api/v1/auth/direct", { method: "DELETE", headers: ALEX })).status, 200);
    assert.equal((await direct("/api/v1/state", { headers: ["Cookie", cookie2] })).status, 404);
    assert.match((await direct("/")).text, /Direct access at this address is off/);
    const events = (await ing("/api/v1/events?type=auth&limit=50", { headers: ALEX })).json().events;
    assert.ok(events.some((e) => e.title?.key === "ev.auth.direct_set" && e.data?.by === "Alex (01234567)"), JSON.stringify(events));
    // The name Home Assistant sends goes into the log cleaned and short, with the ID's start (names need not be unique), or is made from the ID
    const id = ["X-Remote-User-Id", "0123456789abcdef0123456789abcdef"];
    for (const [headers, by] of [
      [[...id, "X-Remote-User-Display-Name", "\u202eAlex\u200b Example\u{E0041}"], "Alex Example (01234567)"],
      [[...id, "X-Remote-User-Display-Name", "\u200b", "X-Remote-User-Name", "alex"], "alex (01234567)"],
      [[...id, "X-Remote-User-Display-Name", "\u200b \u200b", "X-Remote-User-Name", "alex"], "alex (01234567)"],
      [[...id, "X-Remote-User-Display-Name", "\u00a0\u3000\u3164\u2800", "X-Remote-User-Name", "alex"], "alex (01234567)"],  // blank in other ways
      [[...id, "X-Remote-User-Display-Name", "Ä".repeat(40)], `${"Ä".repeat(32)} (01234567)`],
      [id, "Home Assistant 01234567"],
    ]) {
      assert.equal(await rawCall(app.ingressPort, "PUT", "/api/v1/auth/direct", headers, { password: "direkt-passwort" }), 200, JSON.stringify(headers));
      const [newest] = (await ing("/api/v1/events?type=auth&limit=1", { headers: ALEX })).json().events;
      assert.equal(newest.data?.by, by, JSON.stringify(headers));
    }
  } finally {
    app.child.kill("SIGTERM");
  }
  assert.deepEqual(await app.ended, { code: 0, signal: null }, app.out);
});

test("as an app, without its Ingress port it does not start, and the direct port does not stand in (SD-077)", { skip }, async () => {
  const port = await freePort();
  const taken = createServer().listen(0, "127.0.0.1");
  await new Promise((r) => taken.once("listening", r));
  try {
    const r = spawnSync(
      bin,
      ["--app", "--data", join(tmp, "no-ingress"), "--host", "127.0.0.1", "--web", join(tmp, "no-web"), "--port", String(port), "--ingress-port", String(taken.address().port), "--ingress-from", "127.0.0.1"],
      { env: env({ SUPERVISOR_TOKEN: "test-token" }), encoding: "utf8", timeout: 15000 },
    );
    assert.deepEqual([r.status, r.signal], [1, null], r.stderr);
    assert.match(r.stderr, /^growcontroller did not start: Port \d+ for Home Assistant's Ingress is not available\.\nYour data is kept\./m);
    assert.doesNotMatch(r.stdout, /Opens in Home Assistant's sidebar/, "it never said it was serving");
  } finally {
    taken.close();
  }
});

test("as an app, a password set before Ingress (0.1.0-proto.4) is kept as the password for direct access (SD-077)", { skip }, async () => {
  const data = join(tmp, "proto4");
  // 0.1.0-proto.4's app set its password over the network; the server without --app still does, with the setup code (#79)
  const config = join(tmp, "proto4-mapping.json");
  writeFileSync(config, JSON.stringify({ url: "http://127.0.0.1:9" }));
  const port = await freePort();
  const before = spawn(bin, ["--config", config, "--data", data, "--host", "127.0.0.1", "--web", join(tmp, "no-web"), "--port", String(port)], {
    env: env({ GC_HA_TOKEN: "test-token" }),
    stdio: ["ignore", "pipe", "pipe"],
  });
  let out = "";
  before.stdout.on("data", (c) => (out += c));
  before.stderr.on("data", (c) => (out += c));
  const stopped = new Promise((resolve) => before.on("exit", (code, signal) => resolve({ code, signal })));
  try {
    for (let i = 0; i < 60 && !(await info(port)); i++) await new Promise((r) => setTimeout(r, 250));
    const setup = await call(port, "/api/v1/auth/setup", { method: "POST", body: { password: "altes-passwort", code: await setupCode(() => out) } });
    assert.equal(setup.status, 200, setup.text);
  } finally {
    before.kill("SIGTERM");
  }
  assert.deepEqual(await stopped, { code: 0, signal: null }, out);
  const app = await startApp(data);
  try {
    assert.equal((await call(app.ingressPort, "/api/v1/auth/session", { headers: ALEX })).json().hasPassword, true, app.out);
    const login = await call(app.port, "/api/v1/auth/login", { method: "POST", body: { password: "altes-passwort" } });
    assert.equal(login.status, 200, login.text);
  } finally {
    app.child.kill("SIGTERM");
  }
  assert.deepEqual(await app.ended, { code: 0, signal: null }, app.out);
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
    const app = await startApp(data, { web });
    const { child } = app;
    const ended = app.ended;
    try {
      const out = app.out;
      assert.ok(await info(app.ingressPort), out);
      const status = Object.fromEntries(
        readFileSync(`/proc/${child.pid}/status`, "utf8").split("\n").filter((l) => l.includes(":")).map((l) => l.split(/:\s*/, 2)),
      );
      assert.equal(status.Uid, "65532\t65532\t65532\t65532");
      assert.equal(status.Gid, "65532\t65532\t65532\t65532");
      assert.equal(status.Groups, "");
      assert.equal(status.CapPrm, "0000000000000000");
      assert.equal(status.CapEff, "0000000000000000");
      assert.equal(status.NoNewPrivs, "1");
      const page = await call(app.ingressPort, "/", { headers: ALEX });
      assert.match(page.text, /^growcontroller's web app cannot be read\./);
      assert.match(app.out, new RegExp(`The web app in ${web} cannot be read; growcontroller runs without it\\. Detail: Permission denied`));
    } finally {
      child.kill("SIGTERM");
    }
    assert.deepEqual(await ended, { code: 0, signal: null }, app.out);
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
  // the direct port of the app, its password set through Ingress (SD-077)
  const app = await startApp(join(tmp, "lockout"));
  const { port, child } = app;
  const ended = app.ended;
  try {
    assert.equal((await call(app.ingressPort, "/api/v1/auth/direct", { method: "PUT", body: { password: "mein-passwort" }, headers: ALEX })).status, 200);
    assert.equal(await post(port, "/api/v1/auth/login", { password: "mein-passwort" }, "127.0.0.1"), 200);  // signed in from here
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
