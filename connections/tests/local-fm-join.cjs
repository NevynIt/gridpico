#!/usr/bin/env node
"use strict";

const assert = require("node:assert/strict");
const { spawn } = require("node:child_process");
const { randomBytes } = require("node:crypto");
const { once } = require("node:events");
const { appendFileSync, existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } = require("node:fs");
const { request: httpRequest } = require("node:http");
const { createConnection } = require("node:net");
const { tmpdir } = require("node:os");
const { join } = require("node:path");
const { createInterface } = require("node:readline");
const { setTimeout: delay } = require("node:timers/promises");
const WebSocket = require("ws");

async function waitFor(check, label, timeout = 2500) {
  const deadline = performance.now() + timeout;
  do {
    const value = check();
    if (value) return value;
    await delay(10);
  } while (performance.now() < deadline);
  throw new Error("Timed out waiting for " + label);
}

const role = {role: "fm", direction: "receive", capability: "managed"};
const midiProtocol = "gridpico-midi-v1";
const protocol = lease => "gridpico-join." + lease.ticket;

async function run(scenario, check) {
  const scratch = mkdtempSync(join(tmpdir(), "fm-join-fixture-"));
  const host = spawn(process.execPath, [
    join(__dirname, "../local-host.js"), "--web-ui", "--ui-descriptor",
    "--profile", "app/profiles/desktop.conf", __filename, "0",
  ], {env: {...process.env, GRIDPICO_FM_TEST: scratch, GRIDPICO_FM_SCENARIO: scenario}});
  const exited = once(host, "exit");
  const sockets = [];
  let stdout = "", stderr = "";
  host.stdout.on("data", data => { stdout += data; });
  host.stderr.on("data", data => { stderr += data; });
  const trace = () => existsSync(join(scratch,"trace")) ? readFileSync(join(scratch, "trace"), "utf8") : "";
  const signal = name => writeFileSync(join(scratch, name), "");
  const expectExit = scenario.startsWith("startup-") || scenario.startsWith("violation-");
  try {
    if (scenario.startsWith("startup-")) {
      await waitFor(() => host.exitCode !== null, "malformed startup exit");
      assert.equal(host.exitCode, 1);
      assert.doesNotMatch(stdout, /listening at/);
      assert.match(stderr, /FM join negotiation failed|Unexpected router record before startup completion/);
      return;
    }
    const origin = await waitFor(() => /listening at (http:\/\/127\.0\.0\.1:[0-9]+)/.exec(stdout)?.[1], "listener");
    const desc = await (await fetch(origin + "/ui-capabilities.json")).json();
    const address = new URL(origin);
    const body = (name = "FM Instrument") => ({version: 1, runtime: desc.instance, tool: "fm", name, roles: [role]});
    const post = (value, changedHeaders = {}) => fetch(origin + "/ui-join", {
      method: "POST", headers: {Origin: origin, "Content-Type": "application/json", ...changedHeaders},
      body: JSON.stringify(value),
    });
    async function reserve(name) {
      const response = await post(body(name));
      assert.equal(response.status, 200, await response.clone().text());
      const lease = await response.json();
      assert.equal(lease.name, name ?? "FM Instrument");
      assert.equal(lease.runtime, desc.instance);
      assert.equal(lease.version, 1);
      assert.equal(lease.roles.length, 1);
      assert.deepEqual(lease.roles[0], {...role, port: lease.roles[0].port});
      assert.equal(lease.cue, "ID " + lease.roles[0].port);
      assert.match(lease.ticket, /^[0-9a-f]{64}$/);
      return lease;
    }
    async function ws(port, protocols, status = 101) {
      const socket = new WebSocket(origin + "/midi/" + port, protocols, {origin, handshakeTimeout: 1700});
      sockets.push(socket);
      const received = [];
      socket.on("message", (data, binary) => { assert(binary); received.push(data.toString("hex")); });
      if (status === 101) await once(socket, "open");
      else await assert.rejects(once(socket, "open"), new RegExp("Unexpected server response: " + status));
      return {socket, received};
    }
    async function rawUpgrade(lease, changes = {}, early = "") {
      const socket = createConnection({host: "127.0.0.1", port: Number(address.port)});
      sockets.push(socket);
      let reply = "";
      socket.on("data", value => { reply += value.toString("ascii"); });
      socket.on("error", () => {});
      await once(socket, "connect");
      const headers = {Host: address.host, Origin: origin,
        "Sec-WebSocket-Key": randomBytes(16).toString("base64"), "Sec-WebSocket-Version": "13",
        Connection: "Upgrade", Upgrade: "websocket", "Sec-WebSocket-Protocol": midiProtocol + ", " + protocol(lease),
        ...changes};
      socket.write("GET /midi/" + lease.roles[0].port + " HTTP/1.1\r\n" +
        Object.entries(headers).map(([key, value]) => key + ": " + value + "\r\n").join("") + "\r\n" + early);
      return {socket, reply: () => reply};
    }
    if (scenario === "legacy") {
      assert.equal(desc.actions.join, undefined);
      assert.equal((await post(body())).status, 404);
      assert.match(trace(), /forwarding-bind forwarding-v1\nfm-join-bind fm-v1 [1-9][0-9]*\n/);
    } else {
      assert.equal(desc.actions.join, true);
      await check({origin, trace, signal, reserve, body, post, ws, rawUpgrade, address,
        hostExited: () => host.exitCode !== null, errorText: () => stderr});
    }
    if (!expectExit) assert.equal(stderr, "");
  } finally {
    for (const socket of sockets) WebSocket.prototype.isPrototypeOf(socket) ? socket.terminate() : socket.destroy();
    if (host.exitCode === null && host.signalCode === null) host.kill("SIGTERM");
    const [code] = await exited;
    assert.equal(code, expectExit ? 1 : 0, stderr);
    rmSync(scratch, {recursive: true, force: true});
  }
}

async function admission() {
  await run("admission", async ({trace, reserve, body, post, ws, rawUpgrade}) => {
    assert.equal((await post(body(), {Origin: "http://other.invalid"})).status, 403);
    assert.equal((await post({...body(), runtime:"stale"})).status, 503);
    assert.equal((await post({...body(), roles:[{...role, direction:"send"}]})).status, 400);
    assert.equal((await post({...body(), name:"a".repeat(1100)})).status, 400);
    assert.equal((await post({...body(), name:"\ud800"})).status, 400);
    assert.doesNotMatch(trace(), /fm-reserve/);
    const a=await reserve();
    const b=await reserve();
    assert.notEqual(a.pageInstance,b.pageInstance);
    assert.notEqual(a.roles[0].port,b.roles[0].port);
    assert.notEqual(a.cue,b.cue);
    const capacity = await post(body("CAPACITY"));
    assert.equal(capacity.status, 409);
    assert.deepEqual(await capacity.json(), {error: "CAPACITY"});
    const at=a.roles[0].port, bt=b.roles[0].port;
    const malformed=await rawUpgrade(a, {"Sec-WebSocket-Version": "8"});
    await waitFor(()=>malformed.reply().startsWith("HTTP/1.1 403"),"invalid opening version");
    const duplicate=await rawUpgrade(a, {Origin: "http://wrong.invalid"});
    await waitFor(()=>duplicate.reply().startsWith("HTTP/1.1 403"),"wrong opening Origin");
    const nonAsciiWhitespace = await rawUpgrade(a,
      {"Sec-WebSocket-Protocol": midiProtocol + ",\xa0" + protocol(a)});
    await waitFor(()=>nonAsciiWhitespace.reply().startsWith("HTTP/1.1 403"),"non-HTTP whitespace refused");
    const badKey = await rawUpgrade(a, {"Sec-WebSocket-Key": "invalid"});
    await waitFor(()=>badKey.reply().startsWith("HTTP/1.1 403"),"bad opening key");
    await ws(at, [], 403);
    await ws(at, [midiProtocol, protocol(b)], 403);
    assert.doesNotMatch(trace(), /fm-publish/);
    const first=await ws(at,[protocol(a),midiProtocol]);
    const other=await ws(bt,[midiProtocol,protocol(b)]);
    assert.equal(first.socket.protocol,midiProtocol);
    assert.equal(other.socket.protocol,midiProtocol);
    await waitFor(()=>first.received.length && other.received.length, "matched pre-upgrade egress");
    assert.deepEqual(first.received,["903c60"]);
    assert.deepEqual(other.received,["903c60"]);
    const closed=once(first.socket,"close");
    first.socket.close(1000);
    await closed;
    await waitFor(()=>new RegExp("fm-retire [0-9]+ 1 "+at+" 2 1").test(trace()),"scoped exact retirement");
    assert.equal(other.socket.readyState, WebSocket.OPEN);
    await ws(at,[midiProtocol,protocol(a)],403);
    assert.doesNotMatch(trace(), new RegExp("^bind "+at+" ", "m"));
    other.socket.send(Buffer.from("903c60","hex"));
    assert.equal((await once(other.socket,"close"))[0],1008);
    assert.doesNotMatch(trace(), /^midi [0-9]+ 2 (ordinary .* )?903C60$/m);
  });
}

async function race(scenario, success) {
  await run(scenario, async ({trace,signal,reserve,rawUpgrade})=>{
    const lease=await reserve();
    const port=lease.roles[0].port;
    const offered=await rawUpgrade(lease, {}, "early bytes");
    await waitFor(()=>trace().includes("fm-publish"),"in-flight publish");
    assert.equal(offered.reply(),"");
    offered.socket.end();
    await waitFor(()=>offered.reply().startsWith("HTTP/1.1 503"),"host observed opening FIN");
    signal("release");
    const retire=new RegExp("fm-retire [0-9]+ 1 " + port + " 2 " + (success ? "1" : "-") + "\\n");
    await waitFor(()=>retire.test(trace()),"matched late publish cleanup");
    assert.doesNotMatch(offered.reply(), /101/);
  });
}

async function waitForOkay() {
  await run("publish-ack", async ({trace,signal,reserve,rawUpgrade})=>{
    const lease=await reserve();
    const offered=await rawUpgrade(lease);
    await waitFor(()=>trace().includes("publish-metadata-sent"),"publish metadata");
    await delay(60);
    assert.equal(offered.reply(),"");
    signal("release");
    await waitFor(()=>offered.reply().startsWith("HTTP/1.1 101"),"upgrade only after terminal acknowledgement");
  });
}

async function faultBeforeOkay() {
  await run("publish-fault", async ({trace, reserve, rawUpgrade}) => {
    const lease = await reserve();
    const offered = await rawUpgrade(lease);
    await waitFor(() => offered.reply().startsWith("HTTP/1.1 503"), "fault must prevent upgrade");
    assert.doesNotMatch(offered.reply(), /101/);
    await delay(40);
    assert.doesNotMatch(trace(), /fm-retire/);
    const next = await reserve();
    assert.notEqual(lease.roles[0].port, next.roles[0].port);
  });
}

async function malformedTransaction() {
  await run("violation-publish", async ({reserve, rawUpgrade, hostExited, errorText}) => {
    const lease = await reserve();
    const offered = await rawUpgrade(lease);
    await waitFor(hostExited, "mismatched publication metadata fails pipe");
    assert.match(errorText(), /Unexpected router FM publication/);
    assert.doesNotMatch(offered.reply(), /101/);
  });
}

async function abandoned() {
  await run("abandoned", async ({trace,signal,body,origin,address})=>{
    const request=httpRequest(origin+"/ui-join", {method:"POST",headers:{
      Host:address.host, Origin:origin,"Content-Type":"application/json",
    }});
    request.on("error",()=>{});
    let responded=false;
    request.on("response",()=>{responded=true;});
    request.end(JSON.stringify(body()));
    await waitFor(()=>trace().includes("fm-reserve"),"in-flight HTTP reserve");
    const closed=once(request,"close").catch(()=>{});
    request.destroy();
    await closed;
    signal("release");
    await waitFor(()=>/fm-retire [0-9]+ 1 99 2 -/.test(trace()),"late reserve acknowledgement cleaned up");
    assert(!responded);
  });
}

async function idleExpiry() {
  await run("expiry", async ({trace,reserve,ws})=>{
    const a=await reserve();
    await waitFor(()=>/fm-retire [0-9]+ 1 99 2 -/.test(trace()),"pending expiry while idle",6200);
    await ws(a.roles[0].port,[midiProtocol,protocol(a)],403);
    const b=await reserve();
    assert.notEqual(a.roles[0].port,b.roles[0].port);
    await ws(b.roles[0].port,[midiProtocol,protocol(b)]);
  });
}

async function fixture(scratch) {
  const scenario=process.env.GRIDPICO_FM_SCENARIO;
  const emit=(...records)=>console.log(records.join("\n"));
  const trace=line=>appendFileSync(join(scratch,"trace"),line+"\n");
  const release=()=>waitFor(()=>existsSync(join(scratch,"release")),"fixture interlock");
  emit("ui-roles keybed 1 in panel 2 in led 3 out monitor 5 out log 6 out", "ready 18");
  let nextPort=99, epoch;
  for await (const line of createInterface({input:process.stdin})) {
    trace(line);
    const [command, a,b,c,d,e]=line.split(" ");
    switch (command) {
      case "forwarding-bind": emit("forwarding-state 1 running","ok"); break;
      case "fm-join-bind":
        assert.equal(a,"fm-v1");
        assert.match(b,/^[1-9][0-9]*$/);
        epoch=b;
        if (scenario==="legacy") emit("err INVALID");
        else if (scenario==="startup-bare-ok") emit("ok");
        else if (scenario==="startup-wrong-marker") emit("fm-join-ready fm-v1 0","ok");
        else if (scenario==="startup-after-marker") emit(`fm-join-ready fm-v1 ${epoch}`,"err INVALID");
        else if (scenario==="startup-trailing") emit(`fm-join-ready fm-v1 ${epoch}`,"ok","event","end");
        else emit(`fm-join-ready fm-v1 ${epoch}`,"ok");
        break;
      case "fm-reserve":
        assert.equal(a,epoch);
        if (Buffer.from(c,"hex").toString()==="CAPACITY") {emit("err CAPACITY");break;}
        if (scenario==="abandoned") await release();
        emit(`fm-reserved ${epoch} ${b} ${nextPort} ${nextPort++} 2 5000`,"ok");
        break;
      case "fm-publish":
        assert.equal(a,epoch);
        if (scenario.startsWith("publish-close")) await release();
        if (scenario==="publish-close-error") {emit("err UNAVAILABLE");break;}
        if (scenario==="violation-publish") {
          emit(`fm-published ${a} ${b} ${c} ${d} 999 64`, "ok");
          break;
        }
        emit(`fm-published ${a} ${b} ${c} ${d} ${e} 64`);
        if (scenario==="publish-ack") {trace("publish-metadata-sent");await release();}
        if (scenario==="publish-fault") {emit(`fault midi ${c} ${d}`, "ok");break;}
        emit(`midi ${c} ${d} ordinary 1 903C60`,"ok");
        break;
      case "fm-retire":
        assert.equal(a,epoch);
        emit(scenario==="expiry" ? "err UNAVAILABLE" : "ok");
        break;
      default: throw new Error("Unexpected fixture dispatch: "+line);
    }
  }
}

async function main() {
  if (process.env.GRIDPICO_FM_TEST) return fixture(process.env.GRIDPICO_FM_TEST);
  for (const scenario of ["startup-bare-ok","startup-wrong-marker","startup-after-marker","startup-trailing","legacy"]) {
    await run(scenario);
  }
  await admission();
  await waitForOkay();
  await faultBeforeOkay();
  await malformedTransaction();
  await abandoned();
  await race("publish-close-ok",true);
  await race("publish-close-error",false);
  await idleExpiry();
  console.log("PASS FM join authority and protocol validation, matching publication/OK, bounded expiry and close races");
}

main().catch(error=>{console.error(error);process.exitCode=1;});
