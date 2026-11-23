"use strict";

const { accessSync, constants, readFileSync } = require("node:fs");
const { createServer } = require("node:http");
const path = require("node:path");
const { WebSocket, WebSocketServer } = require("ws");
const { maxGeneration, startLocalPipe } = require("./local-pipe");

const address = "127.0.0.1";
const maxSerialChunk = 256;
const serialQueueBytes = 8192;
const midiPeerLimit = 6;
const stallMs = 5000;

const [, , runnerArg, portArg = "8767"] = process.argv;
const port = Number(portArg);
if (!runnerArg || !/^\d+$/.test(portArg) || port > 65535 || process.argv.length > 4) {
  console.error("Usage: node connections/local-host.js <local-router> [port]");
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
function page(file, extra = "") {
  const html = readFileSync(path.join(__dirname, file), "utf8");
  if (!html.includes("</head>")) throw new Error("Local router HTML is missing </head>");
  return html.replace("</head>", script + extra + "\n</head>");
}
const assets = new Map([
  ["/apc/", ["text/html; charset=utf-8", page("../web/apc/index.html")]],
  ["/diagnostics/", ["text/html; charset=utf-8", page("diagnostics/index.html",
    '<script defer src="/local-diagnostics.js"></script>')]],
  ["/local-transport.js", ["text/javascript; charset=utf-8", readFileSync(path.join(__dirname, "local-transport.js"))]],
  ["/local-diagnostics.js", ["text/javascript; charset=utf-8", readFileSync(path.join(__dirname, "local-diagnostics.js"))]],
]);

const server = createServer((request, response) => {
  const asset = assets.get(request.url);
  if (request.headers.host !== `${address}:${request.socket.localPort}` || !asset) {
    response.writeHead(404).end();
    return;
  }
  if (request.method !== "GET" && request.method !== "HEAD") {
    response.writeHead(405, { Allow: "GET, HEAD" }).end();
    return;
  }
  const [type, body] = asset;
  response.writeHead(200, { "Content-Type": type, "Content-Length": Buffer.byteLength(body), "Cache-Control": "no-store" });
  response.end(request.method === "HEAD" ? undefined : body);
});
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
}

function removePeer(peer) {
  clearPeerTimers(peer);
  if (peers.get(peer.key) === peer) peers.delete(peer.key);
}

function retire(peer, { code = 1011, reason = "Binding closed", graceful = false, retired = false } = {}) {
  clearPeerTimers(peer);
  peer.closing = true;
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
      line = port === undefined ? `serial-bind ${generation}` : `bind ${port} ${generation}`;
      complete = (error) => {
        if (!error && port === undefined) {
          peer.bound = true;
          generations.set(peer.key, generation);
        }
        if (error || !peer.bound) {
          if (!peer.closing) reject(peer.raw, error === "INVALID" ? "404 Not Found" : "503 Service Unavailable");
          if (peer.bound) retire(peer);
          else removePeer(peer);
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

server.on("upgrade", (request, raw, head) => {
  const expectedHost = `${address}:${raw.localPort}`;
  if (request.headers.host !== expectedHost || request.headers.origin !== `http://${expectedHost}`) {
    reject(raw, "403 Forbidden");
    return;
  }

  const match = /^\/midi\/([1-9][0-9]*)$/.exec(request.url);
  const port = match ? Number(match[1]) : undefined;
  if (request.url !== "/serial" && (port === undefined || port > 65535)) {
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
    key, port, generation, request, raw, head,
    bound: false, closing: false, input: [], inputBytes: 0,
    beforeUpgrade: [], outputFrames: 0, outputBytes: 0,
  };
  peer.rawClosed = () => retire(peer);
  raw.once("close", peer.rawClosed);
  raw.once("error", () => retire(peer));
  peer.handshakeTimer = setTimeout(() => retire(peer), stallMs);
  peers.set(key, peer);
  pump();
});

function shutdown() {
  if (shuttingDown) return;
  shuttingDown = true;
  server.close();
  server.closeAllConnections();
  for (const peer of peers.values()) retire(peer, { code: 1001, reason: "Local router stopped" });
  pipe.stop(); // Runtime EOF retires inputs before outputs and drains accepted cleanup.
}

const pipe = startLocalPipe(runnerPath, {
  onReady() {
    server.listen(port, address, () => {
      const host = `${address}:${server.address().port}`;
      console.log(`Gridpico local router listening at http://${host}/apc/ and http://${host}/diagnostics/`);
    });
  },
  onRecord: handleRecord,
  onFailure(message) {
    console.error(message);
    process.exitCode = 1;
    shutdown();
  },
});

server.on("error", (error) => {
  console.error(error.message);
  process.exitCode = 1;
  shutdown();
});
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
