"use strict";

const { spawn } = require("node:child_process");
const { accessSync, constants, readFileSync } = require("node:fs");
const { createServer } = require("node:http");
const path = require("node:path");
const { WebSocket, WebSocketServer } = require("ws");
const { parseRequest, PatchResponse } = require("./patch-wire");

const address = "127.0.0.1";
const maxMessageBytes = 256;
const maxQueuedRequests = 16;
const maxRunnerLine = 128;
const maxBufferedOutput = 8192;
const runnerTimeoutMs = 5000;
const readerTimeoutMs = 5000;

const [, , runnerArg, portArg = "8766"] = process.argv;
const port = Number(portArg);
if (!runnerArg || !/^\d+$/.test(portArg) || port > 65535 || process.argv.length > 4) {
  console.error("Usage: node connections/patch-host.js <patch-runner> [port]");
  process.exit(1);
}
const runnerPath = path.resolve(runnerArg);
try {
  accessSync(runnerPath, constants.X_OK);
} catch (error) {
  console.error(`Cannot execute Patch runner ${runnerPath}: ${error.message}`);
  process.exit(1);
}

function attach(socket, onRetired) {
  // Keep terminal Ctrl+C in the host so the runner receives EOF and can release notes.
  const runner = spawn(runnerPath, [], {
    stdio: ["pipe", "pipe", "inherit"], detached: process.platform !== "win32",
  });
  const pending = [];
  let ready = false;
  let usesPageMarkers;
  let response;
  let retiring = false;
  let partialLine = "";
  let runnerTimer;
  let readerTimer;

  function armRunnerTimer(action) {
    clearTimeout(runnerTimer);
    runnerTimer = setTimeout(action, runnerTimeoutMs);
  }

  function retire(code, reason) {
    if (retiring) return;
    retiring = true;
    pending.length = 0;
    response = undefined;
    partialLine = "";
    clearTimeout(readerTimer);

    // Leave the stdout listener attached until exit, even with no live browser.
    runner.stdin.end();
    armRunnerTimer(() => {
      console.error("Patch runner did not exit after stdin closed");
      runner.kill("SIGKILL");
    });
    if (code !== undefined && socket.readyState === WebSocket.OPEN) socket.close(code, reason);
  }

  function closeStalledReader() {
    retire();
    socket.terminate();
  }

  function sendBatch(frames) {
    if (socket.readyState !== WebSocket.OPEN) {
      retire();
      return;
    }
    const messages = frames.map((frame) => JSON.stringify(frame));
    let bytes = socket.bufferedAmount;
    for (const message of messages) {
      const size = Buffer.byteLength(message);
      if (size > maxMessageBytes) throw new Error("Oversized Patch output frame");
      bytes += size + 4; // Server frame header for payloads up to 256 bytes.
    }
    if (bytes > maxBufferedOutput) {
      closeStalledReader();
      return;
    }

    for (const message of messages) {
      if (retiring) break;
      socket.send(message, (error) => {
        if (error) retire(1011, "Patch delivery failed");
        if (socket.bufferedAmount === 0) {
          clearTimeout(readerTimer);
          readerTimer = undefined;
        }
      });
    }
    if (!retiring && socket.bufferedAmount > 0 && readerTimer === undefined) {
      readerTimer = setTimeout(closeStalledReader, readerTimeoutMs);
    }
  }

  function dispatch({ kind, line }) {
    if (socket.readyState !== WebSocket.OPEN) {
      retire();
      return;
    }
    response = new PatchResponse(kind);
    armRunnerTimer(() => retire(1011, "Patch dispatch timed out"));
    runner.stdin.write(`${line}\n`);
  }

  function dispatchNext() {
    if (!retiring && ready && !response && pending.length > 0) dispatch(pending.shift());
  }

  function readLine(line) {
    if (line.length > maxRunnerLine) throw new Error("Patch runner line too long");
    if (!response) {
      if (ready || line !== "ready") throw new Error("Unexpected Patch runner output");
      dispatch({ kind: "startup", line: "surface-ready" });
      return;
    }
    if (line !== "ok" && !line.startsWith("err ")) {
      response.add(line);
      return;
    }

    const code = line === "ok" ? undefined : line.slice(4);
    const frames = response.finish(code, usesPageMarkers);
    if (response.kind === "startup") {
      if (code !== undefined) throw new Error(`Patch startup rejected: ${code}`);
      ready = true;
      usesPageMarkers = Boolean(response.ledPage);
      frames.unshift({ type: "ready" });
    }
    clearTimeout(runnerTimer);
    response = undefined;
    sendBatch(frames);
  }

  armRunnerTimer(() => retire(1011, "Patch startup timed out"));
  runner.stdout.on("data", (chunk) => {
    if (retiring) return;
    const lines = (partialLine + chunk.toString("latin1")).split("\n");
    partialLine = lines.pop();
    try {
      for (const line of lines) {
        if (retiring) return;
        readLine(line);
      }
      if (partialLine.length > maxRunnerLine || (partialLine && ready && !response)) {
        throw new Error("Unexpected partial Patch runner line");
      }
      dispatchNext();
    } catch (error) {
      console.error(`Patch runner protocol: ${error.message}`);
      retire(1011, "Invalid Patch runner response");
    }
  });
  runner.stdout.on("error", () => retire(1011, "Patch stdout failed"));
  runner.stdin.on("error", () => retire(1011, "Patch stdin failed"));
  runner.on("error", (error) => {
    console.error(`Patch runner: ${error.message}`);
    retire(1011, "Patch runner failed");
  });
  runner.on("close", (code, signal) => {
    if (code !== 0) console.error(`Patch runner exited with ${signal ?? code}`);
    retire(1011, "Patch runner exited");
    clearTimeout(runnerTimer);
    onRetired();
  });

  socket.on("message", (data, binary) => {
    if (retiring) return;
    const request = ready && !binary && parseRequest(data);
    if (!request) {
      retire(1008, "Invalid Patch message");
    } else if (pending.length === maxQueuedRequests) {
      retire(1013, "Patch queue full");
    } else {
      pending.push(request);
      dispatchNext();
    }
  });
  socket.on("error", () => retire(1011, "Patch WebSocket error"));
  socket.on("close", () => retire());
  return retire;
}

const page = readFileSync(path.join(__dirname, "../web/apc/index.html"), "utf8");
if (!page.includes("</head>")) throw new Error("Web APC page is missing </head>");
const connectedPage = page.replace("</head>", '<script defer src="/patch/midi.js"></script>\n</head>');
const adapter = readFileSync(path.join(__dirname, "patch-browser.js"));
const assets = new Map([
  ["/patch/", ["text/html; charset=utf-8", connectedPage]],
  ["/patch/midi.js", ["text/javascript; charset=utf-8", adapter]],
]);
const server = createServer((request, response) => {
  const asset = assets.get(request.url);
  const host = `${address}:${request.socket.localPort}`;
  if (!asset || request.headers.host !== host) {
    response.writeHead(404).end();
    return;
  }
  if (request.method !== "GET" && request.method !== "HEAD") {
    response.writeHead(405, { Allow: "GET, HEAD" }).end();
    return;
  }

  const [type, body] = asset;
  response.writeHead(200, {
    "Content-Type": type,
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
  if (request.url !== "/patch/midi") rejection = "404 Not Found";
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
  if (retireFixture) retireFixture(1001, "Patch host stopped");
}
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);

server.listen(port, address, () => {
  console.log(`Gridpico Web APC Patch listening at http://${address}:${server.address().port}/patch/`);
});
