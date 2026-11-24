"use strict";

const { accessSync, constants, existsSync, readFileSync } = require("node:fs");
const { createServer } = require("node:http");
const { randomBytes } = require("node:crypto");
const path = require("node:path");
const { WebSocket, WebSocketServer } = require("ws");
const { maxGeneration, startLocalPipe } = require("./local-pipe");
const { createWssListener } = require("./wss-listener");
const { createTrustedLanListener } = require("./trusted-lan-listener");
const { createRecorder, recorderPort } = require("./recording");
const { createRecordingsHttp } = require("./recordings-http");
const { loadWebUiBundle } = require("./web-ui-bundle");

const address = "127.0.0.1";
const maxSerialChunk = 256;
const serialQueueBytes = 8192;
const stallMs = 5000;
const heartbeatMs = 5000;

const args = process.argv.slice(2);
let webUi = false;
let uiDescriptor = false;
let wssConfig;
let profile;
let lanConfig;
let recorderConfig;
let invalidOptions = false;
while (args[0]?.startsWith("--")) {
  const flag = args.shift();
  if (flag === "--web-ui" && !webUi) webUi = true;
  else if (flag === "--ui-descriptor" && !uiDescriptor) uiDescriptor = true;
  else if (flag === "--wss-config" && wssConfig === undefined && args[0] && !args[0].startsWith("--")) {
    wssConfig = args.shift();
  } else if (flag === "--profile" && profile === undefined && args[0] && !args[0].startsWith("--")) {
    profile = args.shift();
  } else if (flag === "--trusted-lan-config" && lanConfig === undefined && args[0] && !args[0].startsWith("--")) {
    lanConfig = args.shift();
  } else if (flag === "--recorder-config" && recorderConfig === undefined && args[0] && !args[0].startsWith("--")) {
    recorderConfig = args.shift();
  } else {
    invalidOptions = true;
    break;
  }
}
const [runnerArg, portArg = "8767"] = args;
const port = Number(portArg);
if (invalidOptions || !runnerArg || !/^\d+$/.test(portArg) || port > 65535 || args.length > 2) {
  console.error("Usage: node connections/local-host.js [--web-ui] [--ui-descriptor] [--profile <file>] [--wss-config <file>] [--trusted-lan-config <file>] [--recorder-config <file>] <local-router> [port]");
  process.exit(1);
}
const runnerPath = path.resolve(runnerArg);
try {
  accessSync(runnerPath, constants.X_OK);
} catch (error) {
  console.error(`Cannot execute local router ${runnerPath}: ${error.message}`);
  process.exit(1);
}

const script = '<script defer src="/local-transport.js"></script>';
let recorder;
let lan;
let bundle;
try {
  if (uiDescriptor) bundle = loadWebUiBundle();
  if (lanConfig !== undefined) lan = createTrustedLanListener(lanConfig, uiDescriptor);
  if (recorderConfig !== undefined) recorder = createRecorder(recorderConfig);
} catch (error) {
  console.error(`Invalid browser bundle, recorder or trusted LAN setup: ${error.message}`);
  process.exit(1);
}
const recordings = recorder && createRecordingsHttp(recorder, matchesHeader, bundle?.assets.get("/recordings/")[1]);
const wssPages = new Set(["/apc/", "/diagnostics/", "/fm/", "/midi-file/", "/local-transport.js",
  "/local-diagnostics.js", "/local-routing.js", "/local-led-test.js"]);
const recordingsLink = '<li><a href="/recordings/"><strong>Recordings</strong><span>Record a take and download the WAV.</span></a></li>';
function legacyAssets() {
  const read = file => readFileSync(path.join(__dirname, file), "utf8");
  const html = body => ["text/html; charset=utf-8", body];
  const common = new Map([
    ["/apc/", html(read("../web/apc/index.html"))],
    ["/diagnostics/", html(read("diagnostics/index.html"))],
  ]);
  for (const tool of ["fm", "midi-file"]) {
    const source = `../web/${tool}/index.html`;
    if (existsSync(path.join(__dirname, source))) common.set(`/${tool}/`, html(read(source)));
  }
  for (const url of ["/local-transport.js", "/local-diagnostics.js", "/local-routing.js", "/local-led-test.js"]) {
    common.set(url, ["text/javascript; charset=utf-8", read(url.slice(1))]);
  }
  const sourceHome = read("home.html").replace(/^.*<script defer .*<\/script>\n/gm, "")
    .replace(/^.*data-bundle-(?:only|download).*\n/gm, line =>
      common.has("/midi-file/") && line.includes('data-optional-tool="midi-file"') ?
        line.replace(" data-bundle-only", "").replace(" hidden>", ">") : "");
  const hostedHome = sourceHome.replace('href="/diagnostics/#routing"', 'href="/routing/"');
  const localHome = common.has("/fm/") ? hostedHome : hostedHome.replace(/^ *<li data-optional-tool="fm">.*<\/li>\n/m, "");
  const local = new Map(common);
  local.set("/", html(recorder ? localHome.replace("</ul>", `${recordingsLink}\n</ul>`) : localHome));
  local.set("/routing/", common.get("/diagnostics/"));
  if (webUi) {
    for (const url of ["/apc/", "/diagnostics/"]) {
      local.set(url, html(common.get(url)[1].replace(script,
        '<script defer src="/local-transport.js" data-gridpico-profile="web-ui"></script>')));
    }
  }
  const lanPages = new Map([
    ["/", html(sourceHome.replace(/<ul>[\s\S]*?<\/ul>/,
      `<ul><li><a href="/apc/"><strong>Web APC</strong><span>Play the keybed and use the APC controls.</span></a></li>${recorder ? recordingsLink : ""}</ul>`))],
    ["/apc/", html(common.get("/apc/")[1].replace(script,
      '<script defer src="/local-transport.js" data-gridpico-connection="trusted-lan"></script>'))],
    ["/local-transport.js", common.get("/local-transport.js")],
  ]);
  return { local, wss: common, lan: lanPages };
}
const legacy = bundle ? undefined : legacyAssets();
const capabilityBodies = new Map();
let alternateCapabilities;

function prepareCapabilities(runtimeRoles) {
  const instance = randomBytes(16).toString("hex");
  if (lan) {
    for (const [role, port, direction] of [["keybed", 1, "in"], ["panel", 2, "in"], ["led", 3, "out"]]) {
      const published = runtimeRoles.get(role);
      if (published?.port !== port || published.direction !== direction) {
        throw new Error(`Trusted LAN needs registered UI role ${role} at port ${port} (${direction})`);
      }
    }
  }
  for (const listener of [undefined, ...[lan, network].filter(Boolean)]) {
    const roles = Object.fromEntries([...runtimeRoles].filter(([, { port, direction }]) =>
      !listener || listener.ports.get(port) === direction).map(([role, { port, direction }]) =>
      [role, { port, direction: direction === "in" ? "send" : "receive" }]));
    const autoBind = (listener?.trustedLan ? ["keybed", "panel", "led"] :
      !listener && webUi ? ["keybed", "panel", "led", "monitor", "log"] : [])
      .filter(role => Object.hasOwn(roles, role));
    const actions = {
      cli: !listener,
      ledTest: !listener && webUi && roles.sequence?.port === 4 && roles.led?.port === 3,
      recording: Boolean(recorder && (!listener || listener.trustedLan)),
    };
    const descriptor = { version: 1, release: bundle.release, instance, roles, autoBind, actions };
    const body = Buffer.from(JSON.stringify(descriptor));
    if (body.length > 2048) throw new Error("Browser capability descriptor exceeds its bound");
    capabilityBodies.set(listener, body);
    if (listener?.uiOrigin) {
      alternateCapabilities = Buffer.from(JSON.stringify({
        ...descriptor, actions: { cli: false, ledTest: false, recording: false },
      }));
    }
  }
}

const server = createServer();
let loopbackPort;
let network;
if (wssConfig !== undefined) {
  try {
    network = createWssListener(wssConfig);
  } catch (error) {
    console.error(`WSS disabled: ${error.message}`);
  }
}

function matchesHeader(request, name, expected) {
  if (request.headers[name] !== expected) return false;
  let count = 0;
  for (let index = 0; index < request.rawHeaders.length; index += 2) {
    if (request.rawHeaders[index].toLowerCase() === name) count++;
  }
  return count === 1;
}

function serve(request, response, listener) {
  const host = listener?.host ?? `${address}:${request.socket.localPort}`;
  const identity = listener?.identity(request.socket);
  if (listener && (!identity || !matchesHeader(request, "host", host))) {
    response.writeHead(403).end();
    return;
  }
  if (identity === "synth") {
    response.writeHead(request.url === "/serial" ? 404 : 403).end();
    return;
  }
  if (request.headers.host !== host) {
    response.writeHead(404).end();
    return;
  }
  const origin = listener?.origin ?? `http://${host}`;
  const sameOrigin = request.headers.origin === undefined || matchesHeader(request, "origin", origin);
  const capabilityPath = request.url === "/ui-capabilities.json";
  const alternate = listener?.uiOrigin && capabilityPath && request.method === "GET" &&
    matchesHeader(request, "origin", listener.uiOrigin);
  if (listener?.trustedLan && !sameOrigin && !alternate) {
    response.writeHead(403).end();
    return;
  }
  const capability = capabilityPath && (alternate ? alternateCapabilities : capabilityBodies.get(listener));
  if (capability) {
    const site = request.headers["sec-fetch-site"];
    const sameOriginFetch = site === undefined || ["same-origin", "none"].some(value =>
      matchesHeader(request, "sec-fetch-site", value));
    const credentials = request.headers.cookie !== undefined || request.headers.authorization !== undefined;
    if (!matchesHeader(request, "host", host) || (alternate ? credentials : !sameOrigin || !sameOriginFetch)) {
      response.writeHead(403).end();
      return;
    }
    if (request.method !== "GET") {
      response.writeHead(405, { Allow: "GET" }).end();
      return;
    }
    const headers = {
      "Content-Type": "application/json; charset=utf-8", "Content-Length": capability.length,
      "Cache-Control": "no-store", "Cross-Origin-Resource-Policy": "same-origin", "X-Content-Type-Options": "nosniff",
    };
    if (listener?.uiOrigin) headers.Vary = "Origin";
    if (alternate) headers["Access-Control-Allow-Origin"] = listener.uiOrigin;
    response.writeHead(200, headers).end(capability);
    return;
  }
  if (recordings && !listener && request.url.startsWith("/recordings/") && !matchesHeader(request, "host", host)) {
    response.writeHead(403).end();
    return;
  }
  if ((!listener || listener.trustedLan) &&
      recordings?.(request, response, origin, origin, loopbackPort)) return;
  const wss = listener && !listener.trustedLan;
  const asset = bundle ? (!wss || wssPages.has(request.url)) && bundle.assets.get(request.url) :
    (listener?.trustedLan ? legacy.lan : listener ? legacy.wss : legacy.local).get(request.url);
  if (!asset) {
    response.writeHead(404).end();
    return;
  }
  if (request.method !== "GET" && request.method !== "HEAD") {
    response.writeHead(405, { Allow: "GET, HEAD" }).end();
    return;
  }
  const [type, body] = asset;
  const headers = { "Content-Type": type, "Content-Length": Buffer.byteLength(body), "Cache-Control": "no-store" };
  if (request.url === "/web-ui.zip") headers["Content-Disposition"] = 'attachment; filename="gridpico-web-ui.zip"';
  response.writeHead(200, headers);
  response.end(request.method === "HEAD" ? undefined : body);
}
const websockets = new WebSocketServer({
  noServer: true,
  maxPayload: maxSerialChunk,
  maxFragments: 256,
  maxBufferedChunks: 256,
  maxPendingAutoPongs: 16,
  closeTimeout: 1000,
  perMessageDeflate: false,
});

const peers = new Map();
const generations = new Map();
let midiPeerLimit;
let busy;
let shuttingDown = false;

function reject(socket, status) {
  if (socket.destroyed || socket.writableEnded) return;
  socket.once("error", () => socket.destroy());
  socket.end(`HTTP/1.1 ${status}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
  socket.resume();
}

function clearPeerTimers(peer) {
  clearTimeout(peer.handshakeTimer);
  clearTimeout(peer.readerTimer);
  clearTimeout(peer.heartbeatTimer);
}

function removePeer(peer) {
  clearPeerTimers(peer);
  if (peers.get(peer.key) === peer) peers.delete(peer.key);
}

function retire(peer, { code = 1011, reason = "Binding closed", graceful = false, retired = false } = {}) {
  clearPeerTimers(peer);
  peer.closing = true;
  peer.beforeUpgrade.length = 0;
  if (!graceful || retired) {
    peer.input.length = 0;
    peer.inputBytes = 0;
  }
  if (peer.socket?.readyState === WebSocket.OPEN) peer.socket.close(code, reason);
  else if (!peer.socket) reject(peer.raw, "503 Service Unavailable");

  // The runner fault already retired this exact generation.
  if (retired || (!peer.bound && busy !== peer)) removePeer(peer);
  queueMicrotask(pump);
}

function monitorHeartbeat(peer) {
  let expected;
  let pongDeadline;
  peer.socket.on("pong", (data) => {
    if (expected?.equals(data) && performance.now() <= pongDeadline) expected = undefined;
  });
  function ping() {
    if (peer.closing || peer.socket.readyState !== WebSocket.OPEN) return;
    if (expected) {
      retire(peer, { reason: "MIDI heartbeat expired" });
      return;
    }
    expected = randomBytes(8);
    pongDeadline = performance.now() + heartbeatMs;
    peer.heartbeatTimer = setTimeout(ping, heartbeatMs);
    peer.socket.ping(expected, (error) => {
      if (error) retire(peer, { reason: "MIDI heartbeat failed" });
    });
  }
  peer.heartbeatTimer = setTimeout(ping, heartbeatMs);
}

function output(peer, data) {
  if (peer.closing || (peer.socket && peer.socket.readyState !== WebSocket.OPEN)) return;
  const full = peer.port === undefined
    ? peer.outputBytes + data.length > serialQueueBytes : peer.outputFrames === peer.limit;
  if (full) {
    retire(peer, { code: 1013, reason: "Output queue full" });
    return;
  }

  peer.outputFrames++;
  peer.outputBytes += data.length;
  if (!peer.socket) {
    peer.beforeUpgrade.push(data);
    return;
  }
  peer.socket.send(data, { binary: true }, (error) => {
    peer.outputFrames--;
    peer.outputBytes -= data.length;
    if (error) retire(peer, { reason: "Delivery failed" });
    if (peer.outputFrames === 0) {
      clearTimeout(peer.readerTimer);
      peer.readerTimer = undefined;
    }
  });
  if (!peer.closing && peer.outputFrames > 0 && peer.readerTimer === undefined) {
    peer.readerTimer = setTimeout(() =>
      retire(peer, { code: 1013, reason: "Reader stalled" }), stallMs);
  }
}

function handleRecord(record) {
  const peer = peers.get(record.port ?? "serial");
  if (record.kind === "bound") {
    if (!peer || peer.generation !== record.generation || busy !== peer || peer.bound) {
      throw new Error("Unexpected binding confirmation");
    }
    peer.direction = record.direction;
    peer.limit = record.limit;
    peer.bound = true;
    generations.set(peer.key, peer.generation);
    return;
  }
  if (!peer || peer.generation !== record.generation) return;
  if (record.kind === "fault") {
    generations.set(peer.key, peer.generation);
    retire(peer, { retired: true, reason: "Router retired the binding" });
    return;
  }
  if (peer.port !== undefined && (!peer.bound || peer.direction !== "out")) {
    throw new Error("Unexpected MIDI output direction");
  }
  output(peer, record.data);
}

function completeUpgrade(peer) {
  clearTimeout(peer.handshakeTimer);
  if (peer.closing || peer.raw.destroyed) {
    retire(peer);
    return;
  }

  websockets.handleUpgrade(peer.request, peer.raw, peer.head, (socket) => {
    peer.raw.removeListener("close", peer.rawClosed);
    peer.socket = socket;
    socket.on("message", (data, binary) => {
      if (peer.closing) return;
      const valid = binary && (peer.port === undefined
        ? data.length > 0 && data.length <= maxSerialChunk : peer.direction === "in" && data.length === 3);
      if (!valid) {
        retire(peer, { code: 1008, reason: "Invalid byte frame or direction" });
        return;
      }

      const full = peer.port === undefined
        ? peer.inputBytes + data.length > serialQueueBytes : peer.input.length === peer.limit;
      if (full) {
        retire(peer, { code: 1013, reason: "Input queue full" });
        return;
      }
      peer.input.push(data);
      peer.inputBytes += data.length;
      pump();
    });
    socket.on("error", () => retire(peer, { reason: "Socket failed" }));
    socket.on("close", (code) => {
      if (!peer.closing) retire(peer, { graceful: code === 1000 });
    });

    const staged = peer.beforeUpgrade;
    peer.beforeUpgrade = [];
    peer.outputFrames = 0;
    peer.outputBytes = 0;
    for (const data of staged) output(peer, data);
    if (peer.expectedDirection && !peer.closing) monitorHeartbeat(peer);
    peer.raw.resume();
  });
}

function pump() {
  if (shuttingDown || busy) return;
  for (const peer of peers.values()) {
    const { port, generation } = peer;
    let line;
    let complete;
    if (!peer.bound) {
      const verb = peer.synthReceive ? "bind-managed-out" : "bind";
      line = port === undefined ? `serial-bind ${generation}` : `${verb} ${port} ${generation}`;
      complete = (error) => {
        if (!error && port === undefined) {
          peer.bound = true;
          generations.set(peer.key, generation);
        }
        if (error || !peer.bound) {
          if (!peer.closing) reject(peer.raw, error === "INVALID" ? "404 Not Found" : "503 Service Unavailable");
          if (peer.bound) retire(peer);
          else removePeer(peer);
        } else if (peer.expectedDirection && peer.direction !== peer.expectedDirection) {
          reject(peer.raw, "403 Forbidden");
          retire(peer);
        } else if (peers.get(peer.key) === peer) {
          completeUpgrade(peer);
        }
      };
    } else if (peer.input.length > 0) {
      const data = peer.input.shift();
      if (port !== undefined) peer.inputBytes -= data.length;
      const hex = data.toString("hex").toUpperCase();
      line = port === undefined ? `serial ${generation} ${hex}` : `midi ${port} ${generation} ${hex}`;
      complete = (error) => {
        if (port === undefined && !peer.closing) peer.inputBytes -= data.length;
        if (error && peers.get(peer.key) === peer) retire(peer, { code: 1008, reason: "Router rejected the frame" });
      };
    } else if (peer.closing) {
      line = port === undefined ? `serial-detach ${generation}` : `detach ${port} ${generation}`;
      complete = () => removePeer(peer);
    }
    if (!line) continue;

    // Rotate each dispatched peer behind its peers while preserving its own FIFO.
    peers.delete(peer.key);
    peers.set(peer.key, peer);
    busy = peer;
    pipe.dispatch(line, (code) => {
      busy = undefined;
      complete(code);
      queueMicrotask(pump);
    });
    return;
  }
}

function acceptUpgrade(request, raw, head, listener) {
  const expectedHost = listener?.host ?? `${address}:${raw.localPort}`;
  const identity = listener?.identity(raw);
  const synthReceive = identity === "synth" ? listener.synthReceive : undefined;
  const expectedOrigin = synthReceive?.origin ?? listener?.origin ?? `http://${expectedHost}`;
  const alternate = listener?.uiOrigin && /^\/midi\/[123]$/.test(request.url) &&
    matchesHeader(request, "origin", listener.uiOrigin);
  const admitted = listener
    ? identity && matchesHeader(request, "host", expectedHost) &&
      (matchesHeader(request, "origin", expectedOrigin) || alternate)
    : request.headers.host === expectedHost && request.headers.origin === expectedOrigin;
  if (!admitted) {
    reject(raw, "403 Forbidden");
    return;
  }

  const match = /^\/midi\/([1-9][0-9]*)$/.exec(request.url);
  const port = match ? Number(match[1]) : undefined;
  if (request.url !== "/serial" && (port === undefined || port > 65535)) {
    reject(raw, "404 Not Found");
    return;
  }
  if (recorder && port === recorderPort && (listener || !recorder.claim(raw))) {
    reject(raw, "404 Not Found");
    return;
  }
  const expectedDirection = synthReceive
    ? (port === synthReceive.portId ? "out" : undefined) : listener?.ports.get(port);
  if (listener && !expectedDirection) {
    reject(raw, "404 Not Found");
    return;
  }
  const key = port ?? "serial";
  const generation = (generations.get(key) ?? (port === undefined ? 0 : 1)) + 1;
  const midiPeers = peers.size - Number(peers.has("serial"));
  if (shuttingDown || peers.has(key) || generation > maxGeneration ||
      (port !== undefined && midiPeers >= midiPeerLimit)) {
    reject(raw, "503 Service Unavailable");
    return;
  }

  raw.pause();
  const peer = {
    key, port, generation, request, raw, head, expectedDirection, synthReceive: Boolean(synthReceive),
    bound: false, closing: false, input: [], inputBytes: 0,
    beforeUpgrade: [], outputFrames: 0, outputBytes: 0,
  };
  peer.rawClosed = () => retire(peer);
  raw.once("close", peer.rawClosed);
  raw.once("error", () => retire(peer));
  peer.handshakeTimer = setTimeout(() => retire(peer), stallMs);
  peers.set(key, peer);
  pump();
}

server.on("request", (request, response) => serve(request, response));
server.on("upgrade", (request, raw, head) => acceptUpgrade(request, raw, head));
if (network) {
  network.server.on("request", (request, response) => serve(request, response, network));
  network.server.on("upgrade", (request, raw, head) => acceptUpgrade(request, raw, head, network));
  network.server.on("error", (error) => console.error(`WSS listener error: ${error.code ?? "unknown"}`));
}
if (lan) {
  lan.server.on("request", (request, response) => serve(request, response, lan));
  lan.server.on("upgrade", (request, raw, head) => acceptUpgrade(request, raw, head, lan));
  lan.server.on("error", error => failLan(error.code ?? "listener error"));
}

let lanWatch;
function failLan(message) {
  console.error(`Trusted LAN startup or interface failed: ${message}`);
  process.exitCode = 1;
  void shutdown();
}

async function shutdown() {
  if (shuttingDown) return;
  shuttingDown = true;
  clearInterval(lanWatch);
  await recorder?.shutdown();
  server.close();
  server.closeAllConnections();
  network?.server.close();
  network?.server.closeAllConnections();
  lan?.server.close();
  lan?.server.closeAllConnections();
  for (const peer of peers.values()) retire(peer, { code: 1001, reason: "Local router stopped" });
  pipe.stop(); // Runtime EOF retires inputs before outputs and drains accepted cleanup.
}

const pipe = startLocalPipe(runnerPath, {
  onReady(peerLimit, runtimeRoles) {
    if (bundle) prepareCapabilities(runtimeRoles);
    midiPeerLimit = peerLimit;
    server.listen(port, address, () => {
      loopbackPort = server.address().port;
      const host = `${address}:${loopbackPort}`;
      const announce = () => console.log(`Gridpico local router listening at http://${host}/`);
      if (lan && !shuttingDown) {
        lan.server.listen(lan.port, lan.address, () => {
          announce();
          console.log(`Gridpico trusted LAN listening at ${lan.origin}/`);
          lanWatch = setInterval(() => {
            if (!lan.available()) failLan("the configured IPv4 interface is no longer active");
          }, 5000);
        });
      } else announce();
      if (network && !shuttingDown) {
        network.server.listen(network.port, network.address, () => {
          console.log(`Gridpico WSS MIDI listening at ${network.origin}/apc/ and ${network.origin}/diagnostics/`);
        });
      }
    });
  },
  onRecord: handleRecord,
  onFailure(message) {
    console.error(message);
    process.exitCode = 1;
    shutdown();
  },
}, [...(webUi ? ["--web-ui"] : []), ...(profile ? ["--profile", profile] : []),
  ...(recorder ? ["--recorder"] : []), ...(uiDescriptor ? ["--ui-descriptor"] : [])], uiDescriptor);

server.on("error", (error) => {
  console.error(error.message);
  process.exitCode = 1;
  shutdown();
});
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
