#!/usr/bin/env node
"use strict";

const assert = require("node:assert/strict");
const { spawn } = require("node:child_process");
const { once } = require("node:events");
const { mkdtemp, readFile, rm, writeFile } = require("node:fs/promises");
const { createInterface } = require("node:readline");
const { tmpdir } = require("node:os");
const { join } = require("node:path");
const { setTimeout: delay } = require("node:timers/promises");

async function waitFor(check, description) {
  for (let attempt = 0; attempt < 200; attempt++) {
    const result = await check();
    if (result) return result;
    await delay(10);
  }
  throw new Error("Timed out waiting for " + description);
}

async function check({ hostPath, fixturePath, WebSocket }, scenario) {
  const scratch = await mkdtemp(join(tmpdir(), "gridpico-forwarding-fixture-"));
  const host = spawn(process.execPath, [hostPath, "--ui-descriptor", fixturePath, "0"], {
    env: { ...process.env, GRIDPICO_BARRIER_TEST_ROOT: scratch,
      GRIDPICO_BARRIER_TEST_SCENARIO: scenario || "" },
    stdio: ["ignore", "pipe", "pipe"],
  });
  const exited = once(host, "exit");
  const sockets = [];
  let origin;
  let stderr = "";
  const lines = createInterface({ input: host.stdout });
  lines.on("line", line => {
    const match = /^Gridpico local router listening at (http:\/\/127\.0\.0\.1:\d+)\/$/.exec(line);
    if (match) origin = match[1];
  });
  host.stderr.on("data", chunk => { stderr += chunk.toString(); });

  async function open(path) {
    const socket = new WebSocket(origin + path, { origin, handshakeTimeout: 2000 });
    sockets.push(socket);
    const received = [];
    socket.on("message", (bytes, binary) => {
      assert(binary);
      received.push(bytes);
    });
    await once(socket, "open");
    return { socket, received };
  }

  async function submit(peer, frames) {
    for (const frame of frames) peer.socket.send(frame);
    // The server reads WebSocket messages in TCP order before this Ping.
    const pong = once(peer.socket, "pong");
    peer.socket.ping();
    await pong;
  }

  const bytes = (...frames) => frames.map(hex => Buffer.from(hex, "hex"));
  const hex = peer => peer.received.map(frame => frame.toString("hex").toUpperCase());
  const serial = peer => Buffer.concat(peer.received).toString();
  const trace = () => readFile(join(scratch, "private.log"), "utf8");
  const release = name => writeFile(join(scratch, "release-" + name), "");

  const failures = {
    "bad-init": /Invalid router readiness/,
    "missing-class": /Unknown runner record/,
    "missing-epoch": /Unknown runner record/,
    "future-epoch": /preceded its forwarding barrier/,
  };
  try {
    if (scenario === "bad-init") {
      await waitFor(() => host.exitCode !== null, "startup rejection");
      assert.equal(origin, undefined);
      return;
    }
    await waitFor(() => origin, "loopback readiness");
    if (scenario === "missing-class") {
      await assert.rejects(open("/midi/5"));
      return;
    }
    const monitor = await open("/midi/5");
    if (scenario === "missing-epoch" || scenario === "future-epoch") {
      const keybed = await open("/midi/1");
      keybed.socket.send(Buffer.from("903C60", "hex"));
      await waitFor(() => host.exitCode !== null, "missing ordinary epoch rejection");
      assert.deepEqual(monitor.received, []);
      return;
    }
    if (scenario === "legacy") {
      const keybed = await open("/midi/1");
      await submit(keybed, bytes("903C60", "803C00"));
      await waitFor(() => monitor.received.length === 2, "legacy delivery");
      assert.deepEqual(hex(monitor), ["903C60", "803C00"]);
      assert.match(await trace(), /midi 1 [0-9]+ 903C60/);
      return;
    }
    if (scenario === "pending-out") {
      await waitFor(() => monitor.received.length === 4, "staged output after the fence");
      assert.deepEqual(hex(monitor), ["B07B00", "B17B00", "B27B00", "B37B00"]);
      assert.equal(monitor.socket.readyState, WebSocket.OPEN);
      return;
    }
    const led = await open("/midi/3");
    const keybed = await open("/midi/1");
    const aux = await open("/midi/17");
    const panel = await open("/midi/2");
    const raw = await open("/midi/4");
    const terminal = await open("/serial");
    await submit(keybed, bytes("903C60", "803C00"));
    await waitFor(() => monitor.received.length === 2, "pre-Pause delivery");
    assert.deepEqual(hex(monitor), ["903C60", "803C00"]);

    // Already-issued TCP writes may still arrive. Delay reader consumption
    // until after the barrier while leaving earlier sends intact.
    monitor.socket.pause();
    await submit(keybed, bytes("903F60", "803F00"));
    await waitFor(async () => (await trace()).includes("803F00"), "issued Note Off");
    await submit(terminal, [Buffer.from("pause\n")]);
    await waitFor(async () => (await trace()).includes("70617573650A"), "private Pause dispatch in progress");
    await submit(keybed, bytes("904060", "804000"));
    await submit(aux, bytes("924462", "824402"));
    await submit(panel, bytes("90007F"));
    await submit(raw, bytes("960015"));
    assert(!serial(terminal).includes("FORWARDING PAUSED"));
    await release("pause");
    await waitFor(() => serial(terminal).includes("FORWARDING PAUSED 2\r\nOK\r\n"), "committed Pause reply");
    await waitFor(() => led.received.length >= 2, "Panel and Raw across barrier");
    assert.deepEqual(hex(led), ["90007F", "960015"]);
    monitor.socket.resume();
    await waitFor(() => monitor.received.length >= 4, "already-issued output");
    await waitFor(() => monitor.received.length === 5, "explicit Panic while paused");
    const issued = ["903C60", "803C00", "903F60", "803F00", "B07B00"];
    assert.deepEqual(hex(monitor), issued);

    await submit(terminal, [Buffer.from("hold\n")]);
    await waitFor(async () => (await trace()).includes("686F6C640A"), "paused serial control");
    await submit(keybed, Array(96).fill(Buffer.from("905460", "hex")));
    await submit(aux, Array(96).fill(Buffer.from("925560", "hex")));
    await release("hold");
    await waitFor(() => serial(terminal).includes("STILL ALIVE"), "serial while paused");
    assert.equal(keybed.socket.readyState, WebSocket.OPEN);
    assert.equal(aux.socket.readyState, WebSocket.OPEN);

    await submit(terminal, [Buffer.from("resume\n")]);
    await waitFor(() => serial(terminal).includes("FORWARDING RUNNING 3\r\nOK\r\n"), "committed Resume reply");
    await submit(aux, bytes("924562", "824520"));
    await waitFor(() => monitor.received.length === 7, "fresh post-Resume notes");
    assert.deepEqual(hex(monitor), [...issued, "924562", "824520"]);
    const log = await trace();
    assert(!/904060|804000|924462|824402|905460|925560/.test(log), log);
    assert(log.includes("90007F") && log.includes("960015"));

    await submit(terminal, [Buffer.from("pause\n")]);
    await waitFor(() => serial(terminal).includes("FORWARDING PAUSED 4\r\nOK\r\n"), "second Pause");
    const rejected = once(aux.socket, "close");
    aux.socket.send(Buffer.from("B27B00", "hex")); // Unsupported Managed ingress, even while paused.
    assert.equal((await rejected)[0], 1008);
    assert((await trace()).includes("B27B00"));
    assert.equal(keybed.socket.readyState, WebSocket.OPEN);
    assert.deepEqual(hex(monitor), [...issued, "924562", "824520"]);
    assert.equal(stderr, "");
  } finally {
    for (const socket of sockets) socket.terminate();
    if (host.exitCode === null && host.signalCode === null) host.kill("SIGTERM");
    const [code] = await exited;
    await rm(scratch, { recursive: true, force: true });
    assert.equal(code, failures[scenario] ? 1 : 0, stderr);
    if (failures[scenario]) assert.match(stderr, failures[scenario]);
  }
}


async function fixture(scratch) {
  const { appendFileSync, existsSync } = require("node:fs");
  const roles = new Map([
    [1, ["in", "managed"]], [17, ["in", "managed"]],
    [2, ["in", "control"]], [4, ["in", "control"]],
    [3, ["out", "control"]], [5, ["out", "managed"]],
  ]);
  const bound = new Map();
  const scenario = process.env.GRIDPICO_BARRIER_TEST_SCENARIO;
  let revision = 1;
  let running = true;
  const emit = (...lines) => console.log(lines.join("\n"));
  const serial = (generation, text) =>
    `serial ${generation} ${Buffer.from(text).toString("hex").toUpperCase()}`;
  const dest = port => `${port} ${bound.get(port)}`;
  async function stopPoint(name) {
    await waitFor(() => existsSync(join(scratch, "release-" + name)), name + " release");
  }
  const legacy = scenario === "legacy";
  const ready = legacy ? "ready 6" : scenario === "bad-init" ? "ready 6 forwarding-v2" : "ready 6 forwarding-v1";
  emit("ui-roles keybed 1 in panel 2 in led 3 out sequence 4 in monitor 5 out", ready);
  const input = createInterface({ input: process.stdin });
  for await (const line of input) {
    appendFileSync(join(scratch, "private.log"), line + "\n");
    const [action, portText, generation, tag, interval, payload] = line.split(" ");
    const port = Number(portText);
    if (action === "bind") {
      const [direction, classification] = roles.get(port);
      bound.set(port, generation);
      const suffix = legacy || scenario === "missing-class" ? "" : " " + classification;
      const limit = scenario === "pending-out" ? 4 : 64;
      emit(`bound ${port} ${generation} ${direction} ${limit}${suffix}`);
      if (scenario === "pending-out") {
        emit(`midi ${dest(5)} ordinary 1 903C60`, `midi ${dest(5)} panic - B07B00`,
          "forwarding-state 2 paused", `midi ${dest(5)} control - B17B00`,
          `midi ${dest(5)} panic - B27B00`, `midi ${dest(5)} panic - B37B00`,
          `midi ${dest(5)} panic - B47B00`);
      }
      emit("ok");
    } else if (action === "midi") {
      const managed = roles.get(port)[1] === "managed";
      const data = payload ?? tag;
      if (data === "B27B00" && managed) {
        assert.equal(tag, data);
        emit("err INVALID");
        continue;
      }
      if (!managed) {
        assert.equal(tag, data);
        emit(`midi ${dest(3)} control - ${data}`);
      } else if (running) {
        if (!legacy) {
          assert.equal(tag, "ordinary");
          assert.equal(interval, String(revision));
        }
        const provenance = legacy || scenario === "missing-epoch" ? "" :
          `ordinary ${scenario === "future-epoch" ? revision + 1 : revision} `;
        emit(`midi ${dest(5)} ${provenance}${data}`);
      }
      emit("ok");
    } else if (action === "serial") {
      switch (Buffer.from(generation, "hex").toString()) {
        case "pause\n":
          await stopPoint("pause");
          revision += 1;
          running = false;
          emit(`forwarding-state ${revision} paused`, `midi ${dest(5)} ordinary 1 904660`);
          if (revision === 2) emit(`midi ${dest(5)} panic - B07B00`);
          emit(serial(port, `FORWARDING PAUSED ${revision}\r\nOK\r\n`), "ok");
          break;
        case "resume\n":
          revision += 1;
          running = true;
          emit(`forwarding-state ${revision} running`, `midi ${dest(5)} ordinary 1 904760`,
            serial(port, "FORWARDING RUNNING 3\r\nOK\r\n"), "ok");
          break;
        case "hold\n":
          await stopPoint("hold");
          emit(serial(port, "STILL ALIVE\r\n"), "ok");
          break;
        default: throw Error("Unexpected serial fixture command");
      }
    } else if (action === "serial-bind" || action === "serial-detach") emit("ok");
    else if (action === "detach") {
      assert.equal(bound.get(port), generation);
      bound.delete(port);
      emit("ok");
    } else throw Error("Unexpected private input: " + line);
  }
}

async function checkInvalidRecords() {
  const { startLocalPipe } = require("../local-pipe");
  const invalid = [
    "forwarding-state 0 paused", "forwarding-state 02 paused",
    "forwarding-state 18446744073709551616 paused", "forwarding-state 2 PAUSED",
    "bound 1 2 in 64 unknown", "midi 5 2 ordinary 0 903C60",
    "midi 5 2 ordinary 18446744073709551616 903C60", "midi 5 2 panic 1 903C60",
    "midi 5 2 control - 903c60", "midi 5 2 other - 903C60", "unknown-record 1",
  ];
  for (const record of invalid) {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("Timed out rejecting " + record)), 1500);
      const pipe = startLocalPipe(process.execPath, {
        onReady() { pipe.dispatch("probe", () => reject(new Error("Invalid record was accepted: " + record))); },
        onRecord() { reject(new Error("Record streamed before rejection: " + record)); },
        onFailure(message) {
          clearTimeout(timer);
          if (!/^(Invalid router |Invalid runner |Unknown runner record)/.test(message)) reject(new Error(message));
          else resolve();
        },
      }, [__filename, "--invalid-record", record]);
    });
  }
}

async function emitInvalidRecord() {
  console.log("ready 6 forwarding-v1");
  const input = createInterface({ input: process.stdin });
  for await (const _ of input) console.log(process.argv[3] + "\nserial 1 41434B0A\nok");
}

async function main() {
  if (process.argv[2] === "--invalid-record") return emitInvalidRecord();
  const scratch = process.env.GRIDPICO_BARRIER_TEST_ROOT;
  if (scratch) return fixture(scratch);
  const options = {
    hostPath: join(__dirname, "..", "local-host.js"),
    fixturePath: __filename,
    WebSocket: require("ws"),
  };
  await check(options);
  for (const scenario of ["bad-init", "missing-class", "missing-epoch", "future-epoch", "pending-out", "legacy"]) {
    await check(options, scenario);
  }
  await checkInvalidRecords();
  console.log("PASS portable host forwarding fence, ordinary burst/drop, control and fresh delivery, malformed producer rejection");
}

const task = main();
task.catch(error => {
  console.error(error);
  process.exitCode = 1;
});
