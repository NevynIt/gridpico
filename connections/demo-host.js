"use strict";

const { spawn } = require("node:child_process");
const { accessSync, constants, readFileSync } = require("node:fs");
const http = require("node:http");
const path = require("node:path");
const { WebSocket, WebSocketServer } = require("ws");

const address = "127.0.0.1";
const maxMessageBytes = 256;
const maxPendingNotes = 16;
const maxBufferedOutput = 8192;
const maxRunnerLine = 32;
const runnerTimeoutMs = 5000;
const readerTimeoutMs = 5000;

const [, , runnerArg, portArg = "8765"] = process.argv;
const port = Number(portArg);
if (!runnerArg || !/^\d+$/.test(portArg) || port > 65535 || process.argv.length > 4) {
  console.error("Usage: node connections/demo-host.js <fixture-runner> [port]");
  process.exit(1);
}
const runnerPath = path.resolve(runnerArg);
try {
  accessSync(runnerPath, constants.X_OK);
} catch (error) {
  console.error(`Cannot execute fixture runner ${runnerPath}: ${error.message}`);
  process.exit(1);
}

const standalonePage = readFileSync(path.join(__dirname, "../web/apc/index.html"), "utf8");
if (!standalonePage.includes("</head>")) throw new Error("Web APC page is missing </head>");
const connectedPage = standalonePage.replace(
  "</head>", '<script defer src="/demo/midi.js"></script>\n</head>'
);
const browserAdapter = readFileSync(path.join(__dirname, "demo-browser.js"));

function isNote(event) {
  return event !== null && typeof event === "object" &&
    (event.type === "note-on" || event.type === "note-off") && event.channel === 1 &&
    Number.isInteger(event.note) && event.note >= 0 && event.note <= 127 &&
    Number.isInteger(event.velocity) && event.velocity <= 127 &&
    event.velocity >= (event.type === "note-on" ? 1 : 0);
}

function parseInput(data) {
  try {
    const event = JSON.parse(data.toString("utf8"));
    if (isNote(event) && Object.keys(event).length === 4) return event;
  } catch {
    // Invalid JSON is handled like any other malformed input.
  }
  return null;
}

function parseCapture(line) {
  const match = /^(on|off) (1) (\d{1,3}) (\d{1,3})$/.exec(line);
  if (!match) return null;
  const [, kind, channel, note, velocity] = match;
  const event = { type: `note-${kind}`, channel: +channel, note: +note, velocity: +velocity };
  return isNote(event) ? event : null;
}

function attach(socket, onRetired) {
  const runner = spawn(runnerPath, [], { stdio: ["pipe", "pipe", "inherit"] });
  const pending = [];
  let ready = false;
  let inFlight = false;
  let retiring = false;
  let partialLine = "";
  let runnerTimer;
  let readerTimer;

  function deadline(action) {
    clearTimeout(runnerTimer);
    runnerTimer = setTimeout(action, runnerTimeoutMs);
  }

  function retire(code, reason) {
    if (retiring) return;
    retiring = true;
    pending.length = 0;
    clearTimeout(readerTimer);

    // An already-dispatched line precedes EOF in the pipe. Keep stdout flowing
    // through process exit so dispatch and disconnect releases can finish.
    runner.stdin.end();
    deadline(() => {
      console.error("Fixture runner did not exit after stdin closed");
      runner.kill("SIGKILL");
    });

    if (code !== undefined && socket.readyState === WebSocket.OPEN) socket.close(code, reason);
  }

  function closeStalledReader() {
    retire();
    socket.terminate();
  }

  function send(event) {
    if (socket.readyState !== WebSocket.OPEN) {
      retire();
      return;
    }
    const message = JSON.stringify(event);
    if (socket.bufferedAmount + Buffer.byteLength(message) > maxBufferedOutput) {
      closeStalledReader();
      return;
    }
    socket.send(message, (error) => {
      if (error) retire(1011, "Capture delivery failed");
      if (socket.bufferedAmount === 0) {
        clearTimeout(readerTimer);
        readerTimer = undefined;
      }
    });
    if (socket.bufferedAmount > 0 && readerTimer === undefined) {
      readerTimer = setTimeout(closeStalledReader, readerTimeoutMs);
    }
  }

  function dispatchNext() {
    if (retiring || !ready || inFlight || pending.length === 0) return;
    if (socket.readyState !== WebSocket.OPEN) {
      retire();
      return;
    }

    const { type, channel, note, velocity } = pending.shift();
    const kind = type === "note-on" ? "on" : "off";
    inFlight = true;
    deadline(() => retire(1011, "Fixture dispatch timed out"));
    runner.stdin.write(`${kind} ${channel} ${note} ${velocity}\n`);
  }

  function readRunnerLine(line) {
    if (!ready && line === "ready") {
      clearTimeout(runnerTimer);
      ready = true;
      send({ type: "ready" });
    } else if (inFlight && line === "ok") {
      clearTimeout(runnerTimer);
      inFlight = false;
      dispatchNext();
    } else if (inFlight && line === "error") {
      retire(1011, "Fixture dispatch failed");
    } else {
      const event = inFlight && parseCapture(line);
      if (event) send(event);
      else retire(1011, "Invalid fixture response");
    }
  }

  deadline(() => retire(1011, "Fixture startup timed out"));
  runner.stdout.on("data", (chunk) => {
    if (retiring) return;
    // Split LF records across arbitrary pipe reads with bounded partial storage.
    const lines = (partialLine + chunk.toString("latin1")).split("\n");
    partialLine = lines.pop();
    for (const line of lines) {
      if (line.length > maxRunnerLine) retire(1011, "Fixture line too long");
      if (retiring) return;
      readRunnerLine(line);
    }
    if (partialLine.length > maxRunnerLine) retire(1011, "Fixture line too long");
  });
  runner.stdout.on("error", () => retire(1011, "Fixture stdout failed"));
  runner.stdin.on("error", () => retire(1011, "Fixture stdin failed"));
  runner.on("error", (error) => {
    console.error(`Fixture runner: ${error.message}`);
    retire(1011, "Fixture process failed");
  });
  runner.on("close", (code, signal) => {
    if (code !== 0) console.error(`Fixture runner exited with ${signal ?? code}`);
    retire(1011, "Fixture process exited");
    clearTimeout(runnerTimer);
    onRetired();
  });

  socket.on("message", (data, isBinary) => {
    if (retiring) return;
    const event = !isBinary && parseInput(data);
    if (!ready || !event) {
      retire(1008, "Invalid keybed message");
    } else if (pending.length === maxPendingNotes) {
      retire(1013, "Keybed queue full");
    } else {
      pending.push(event);
      dispatchNext();
    }
  });
  socket.on("error", () => retire(1011, "WebSocket error"));
  socket.on("close", () => retire());

  return retire;
}

const assets = new Map([
  ["/", ["text/html; charset=utf-8", connectedPage]],
  ["/index.html", ["text/html; charset=utf-8", connectedPage]],
  ["/demo/midi.js", ["text/javascript; charset=utf-8", browserAdapter]],
]);
const server = http.createServer((request, response) => {
  const asset = assets.get(request.url);
  if (request.headers.host !== `${address}:${request.socket.localPort}` || !asset) {
    response.writeHead(404).end();
    return;
  }
  if (request.method !== "GET" && request.method !== "HEAD") {
    response.writeHead(405, { Allow: "GET, HEAD" }).end();
    return;
  }

  const [contentType, body] = asset;
  response.writeHead(200, {
    "Content-Type": contentType,
    "Content-Length": Buffer.byteLength(body),
    "Cache-Control": "no-store",
  });
  response.end(request.method === "HEAD" ? undefined : body);
});

const websockets = new WebSocketServer({
  noServer: true,
  maxPayload: maxMessageBytes,
  maxFragments: 256,
  maxBufferedChunks: 256,
  maxPendingAutoPongs: 16,
  closeTimeout: 1000,
  perMessageDeflate: false,
});

let retireFixture;
let shuttingDown = false;
server.on("upgrade", (request, socket, head) => {
  const host = `${address}:${socket.localPort}`;
  let rejection;
  if (request.url !== "/demo/midi") rejection = "404 Not Found";
  else if (request.headers.host !== host || request.headers.origin !== `http://${host}`) {
    rejection = "403 Forbidden";
  } else if (shuttingDown || retireFixture) rejection = "503 Service Unavailable";

  if (rejection) {
    socket.on("error", () => socket.destroy());
    socket.end(`HTTP/1.1 ${rejection}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
    return;
  }

  websockets.handleUpgrade(request, socket, head, (connection) => {
    retireFixture = attach(connection, () => { retireFixture = undefined; });
  });
});

function shutdown() {
  if (shuttingDown) return;
  shuttingDown = true;
  server.close();
  server.closeAllConnections();
  if (retireFixture) retireFixture(1001, "Demo host stopped");
}
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);

server.listen(port, address, () => {
  const host = `${address}:${server.address().port}`;
  console.log(`Gridpico Web APC listening at http://${host}/`);
});
