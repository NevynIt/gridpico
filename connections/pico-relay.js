"use strict";

const { spawn } = require("node:child_process");
const { randomBytes } = require("node:crypto");
const { WebSocket } = require("ws");

const ioLimit = 8192;
const cdcChunk = 256;
const midiQueuedBytes = 64 * 3;
const timeout = 5000;

function midiTriplet(data, notesOnly) {
  const status = data[0] & 0xf0;
  const supported = status === 0x80 || status === 0x90 ||
    (!notesOnly && (status === 0xa0 || status === 0xb0 || status === 0xe0));
  return data.length === 3 && supported &&
    data[1] < 0x80 && data[2] < 0x80;
}

function reject(socket, status) {
  if (socket.destroyed || socket.writableEnded) return;
  socket.once("error", () => socket.destroy());
  socket.end(`HTTP/1.1 ${status}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
}

function relayPico({ request, raw, head, server, executable, args, direction, notesOnly, onClose }) {
  const cdc = direction === "serial";
  const pendingLimit = cdc ? ioLimit : midiQueuedBytes + 3;
  const canSend = cdc || direction === "send";
  const canReceive = cdc || direction === "receive";
  const child = spawn(executable, args, { stdio: ["pipe", "pipe", "ignore", "pipe"] });
  let socket;
  let closing = false;
  let ready = false;
  let status = "";
  let partial = Buffer.alloc(0);
  let staged = [];
  let stagedBytes = 0;
  let pong;
  let heartbeat;
  let stopTimer;
  let killTimer;
  const pending = { input: { bytes: 0, items: [] }, output: { bytes: 0, items: [] } };
  const attachTimer = setTimeout(() => retire("Device attachment timed out"), timeout);

  function retire(reason, code = 1011) {
    if (closing) return;
    closing = true;
    clearTimeout(attachTimer);
    clearInterval(heartbeat);
    for (const queue of Object.values(pending)) {
      clearTimeout(queue.timer);
      queue.items.length = 0;
    }
    staged = [];
    stagedBytes = 0;
    if (socket?.readyState === WebSocket.OPEN) socket.close(code, reason);
    else if (!socket) reject(raw, "503 Service Unavailable");
    child.stdin?.end();
    stopTimer = setTimeout(() => {
      child.kill("SIGTERM");
      killTimer = setTimeout(() => child.kill("SIGKILL"), 1000);
    }, timeout);
  }

  function write(which, data, transmit) {
    const queue = pending[which];
    if (closing) return;
    if (queue.bytes + data.length > pendingLimit) {
      retire("Device relay queue full", 1013);
      return;
    }
    queue.bytes += data.length;
    queue.items.push({ length: data.length, transmit });
    drain(queue);
  }

  function drain(queue) {
    if (closing || queue.inFlight || !queue.items.length) return;
    const { length, transmit } = queue.items.shift();
    queue.inFlight = true;
    queue.timer = setTimeout(() => retire("Device relay stalled"), timeout);
    try {
      transmit(error => {
        clearTimeout(queue.timer);
        queue.inFlight = false;
        queue.bytes -= length;
        if (error) retire("Device relay write failed");
        else drain(queue);
      });
    } catch {
      retire("Device relay write failed");
    }
  }

  function emit(data) {
    if (closing) return;
    if (!cdc && !midiTriplet(data, notesOnly)) {
      retire("Invalid device MIDI frame");
      return;
    }
    if (socket) {
      write("output", data, done => socket.send(data, { binary: true }, done));
    } else if (stagedBytes + data.length > (cdc ? ioLimit : midiQueuedBytes)) {
      retire("Device relay queue full", 1013);
    } else {
      staged.push(data);
      stagedBytes += data.length;
    }
  }

  raw.once("close", () => retire("Connection ended", 1000));
  raw.once("error", () => retire("Connection failed"));
  raw.pause();
  child.on("error", () => retire("Device helper unavailable"));
  child.stdin?.on("error", () => retire("Device input ended"));
  child.stdout?.on("error", () => retire("Device output ended"));
  child.stdout?.on("data", data => {
    if (closing) return;
    if (!canReceive) {
      retire("Unexpected device output");
      return;
    }
    if (cdc) {
      for (let offset = 0; offset < data.length && !closing; offset += cdcChunk) {
        emit(data.subarray(offset, offset + cdcChunk));
      }
      return;
    }
    const bytes = partial.length ? Buffer.concat([partial, data]) : data;
    let offset = 0;
    for (; offset + 3 <= bytes.length && !closing; offset += 3) emit(bytes.subarray(offset, offset + 3));
    partial = Buffer.from(bytes.subarray(offset, Math.min(bytes.length, offset + 2)));
  });

  const statusPipe = child.stdio[3];
  statusPipe?.on("error", () => retire("Device verification failed"));
  statusPipe?.on("data", bytes => {
    if (closing) return;
    if (status.length + bytes.length > 6) {
      retire("Device verification failed");
      return;
    }
    status += bytes.toString("latin1");
  });
  statusPipe?.once("end", () => {
    if (closing) return;
    if (status !== "ready\n" || child.exitCode !== null || raw.destroyed) {
      retire("Device verification failed");
      return;
    }
    ready = true;
    clearTimeout(attachTimer);
    server.handleUpgrade(request, raw, head, ws => {
      socket = ws;
      socket.on("error", () => retire("Connection failed"));
      socket.on("close", () => retire("Connection ended", 1000));
      socket.on("message", (data, binary) => {
        if (closing) return;
        if (!canSend || !binary || (cdc ? data.length < 1 || data.length > cdcChunk : !midiTriplet(data, notesOnly))) {
          retire("Invalid device byte frame", 1008);
          return;
        }
        write("input", data, done => child.stdin.write(data, done));
      });
      socket.on("pong", bytes => {
        if (pong?.equals(bytes)) pong = undefined;
      });
      heartbeat = setInterval(() => {
        if (closing || ws.readyState !== WebSocket.OPEN) return;
        if (pong) {
          retire("Connection heartbeat expired");
          return;
        }
        pong = randomBytes(8);
        ws.ping(pong, error => { if (error) retire("Connection failed"); });
      }, timeout);
      const buffered = staged;
      staged = [];
      stagedBytes = 0;
      for (const data of buffered) emit(data);
      raw.resume();
    });
    if (!socket) retire("Connection rejected");
  });

  child.once("close", (code, signal) => {
    const confirmed = closing && code === 0 && signal === null;
    retire(partial.length ? "Truncated device output" : "Device attachment ended");
    clearTimeout(stopTimer);
    clearTimeout(killTimer);
    onClose({ cleanupUnconfirmed: ready && direction === "send" && !confirmed });
  });
  return () => retire("Pico service is stopping", 1001);
}

module.exports = { reject, relayPico };
