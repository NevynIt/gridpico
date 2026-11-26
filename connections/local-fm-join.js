"use strict";

const { randomBytes } = require("node:crypto");

const protocol = "gridpico-midi-v1";
const ticketPrefix = "gridpico-join.";
const maxBody = 1024;
const ttl = 5000;
const maxOwner = 0xffffffffffffffffn;

function exactKeys(value, keys) {
  return value && typeof value === "object" && !Array.isArray(value) &&
    Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
}

function requestName(value) {
  if (!exactKeys(value, ["version", "runtime", "tool", "name", "roles"]) ||
      value.version !== 1 || typeof value.runtime !== "string" || value.tool !== "fm" ||
      typeof value.name !== "string" || /[\x00-\x1f\x7f]/.test(value.name) ||
      !Array.isArray(value.roles) || value.roles.length !== 1) return;
  const [role] = value.roles;
  if (!exactKeys(role, ["role", "direction", "capability"]) ||
      role.role !== "fm" || role.direction !== "receive" || role.capability !== "managed") return;
  const bytes = Buffer.from(value.name);
  if (bytes.length < 1 || bytes.length > 31 || bytes.toString() !== value.name) return;
  return bytes;
}

function sendJson(response, status, value, headers = {}) {
  if (response.destroyed || response.writableEnded) return;
  const body = JSON.stringify(value);
  if (Buffer.byteLength(body) > 2048) throw new Error("FM join reply exceeds its bound");
  response.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store",
    "Cross-Origin-Resource-Policy": "same-origin", "X-Content-Type-Options": "nosniff",
    "Content-Length": Buffer.byteLength(body), ...headers,
  }).end(body);
}

function readJoin(request, response, done) {
  const chunks = [];
  let count = 0;
  let finished = false;
  const timer = setTimeout(() => finish(), ttl);
  function finish(value) {
    if (finished) return;
    finished = true;
    clearTimeout(timer);
    done(value);
  }
  request.on("data", chunk => {
    if (finished) return;
    count += chunk.length;
    if (count > maxBody) finish();
    else chunks.push(chunk);
  });
  request.once("end", () => {
    if (finished) return;
    let value;
    try {
      value = JSON.parse(new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(Buffer.concat(chunks)));
    } catch { /* Invalid body. */ }
    finish(value);
  });
  request.once("error", () => finish());
  response.once("close", () => finish());
}

function openProtocol(request, matchesHeader) {
  const headers = request.headers;
  const key = headers["sec-websocket-key"];
  const offer = headers["sec-websocket-protocol"];
  const connection = headers.connection;
  const upgrade = headers.upgrade;
  if (request.method !== "GET" || upgrade?.toLowerCase() !== "websocket" ||
      !matchesHeader(request, "upgrade", upgrade) ||
      !/(?:^|,)[ 	]*upgrade[ 	]*(?:,|$)/i.test(connection ?? "") ||
      !matchesHeader(request, "sec-websocket-version", "13") ||
      !matchesHeader(request, "sec-websocket-key", key) || !/^[A-Za-z0-9+/]{22}==$/.test(key) ||
      Buffer.from(key, "base64").toString("base64") !== key ||
      !matchesHeader(request, "sec-websocket-protocol", offer)) return;
  const choices = /^(gridpico-midi-v1|gridpico-join\.[0-9a-f]{64})[ 	]*,[ 	]*(gridpico-midi-v1|gridpico-join\.[0-9a-f]{64})$/.exec(offer);
  if (!choices || choices[1] === choices[2]) return;
  const ticket = choices[1] === protocol ? choices[2] : choices[1];
  if (choices.includes(protocol)) return ticket.slice(ticketPrefix.length);
}

function createLocalFmJoin({ epoch, runtime, limit, matchesHeader, enqueue, createPeer, closePeer, removePeer,
  finishUpgrade, reject }) {
  const reservations = new Set();
  const ports = new Map();
  let nextReservation = 1n;
  let nextSocket = 1n;
  let reading = 0;
  let stopped = false;

  function exactOrigin(request, host, origin) {
    return matchesHeader(request, "host", host) && matchesHeader(request, "origin", origin);
  }

  function finish(lease) {
    clearTimeout(lease.timer);
    reservations.delete(lease);
    if (ports.get(lease.port) === lease) ports.delete(lease.port);
    if (lease.peer) removePeer(lease.peer);
  }

  function queue(lease, start, complete, onRecord) {
    lease.working = true;
    enqueue(start, error => {
      lease.working = false;
      complete(error);
    }, onRecord);
  }

  function cancel(lease) {
    if (!lease.closed || lease.working || !reservations.has(lease)) return;
    if (!lease.port || lease.faulted || stopped) {
      finish(lease);
      return;
    }
    const sid = lease.published ? lease.sid : "-";
    queue(lease, () => lease.faulted ? undefined :
      `fm-retire ${epoch} ${lease.rid} ${lease.port} ${lease.generation} ${sid}`, error => {
      if (error === "INVALID" || (error === "UNAVAILABLE" && lease.published && !lease.faulted)) {
        throw new Error("Router could not fence the FM owner");
      }
      finish(lease);
    });
  }

  function close(lease, faulted = false) {
    lease.closed = true;
    lease.ticket = undefined;
    lease.faulted ||= faulted;
    clearTimeout(lease.timer);
    cancel(lease);
  }

  function expire(lease) {
    if (lease.response) sendJson(lease.response, 503, { error: "UNAVAILABLE" });
    if (lease.peer && !lease.peer.closing) closePeer(lease.peer);
    else close(lease);
  }

  function reserve(value, bytes, response) {
    const lease = { name: value.name, response, deadline: performance.now() + ttl };
    reservations.add(lease);
    response.once("close", () => { if (!response.writableFinished) close(lease); });
    lease.timer = setTimeout(() => expire(lease), ttl);
    let record;
    queue(lease, () => {
      if (lease.closed) return;
      if (nextReservation > maxOwner) return;
      lease.rid = nextReservation++;
      return `fm-reserve ${epoch} ${lease.rid} ${bytes.toString("hex").toUpperCase()}`;
    }, error => {
      if (record && error) throw new Error("Router FM reserve returned both metadata and error");
      if (!error && !record) throw new Error("Missing router FM reservation");
      if (error) {
        if (!lease.closed) sendJson(response, error === "CAPACITY" ? 409 : error === "INVALID" ? 400 : 503,
          { error: error === "SKIPPED" ? "UNAVAILABLE" : error });
        finish(lease);
        return;
      }
      const [, , , device, port, generation] = record;
      lease.device = Number(device);
      lease.port = Number(port);
      lease.generation = Number(generation);
      if (ports.has(lease.port)) throw new Error("Duplicate router FM port");
      ports.set(lease.port, lease);
      if (lease.closed || response.destroyed || performance.now() >= lease.deadline) {
        close(lease);
        return;
      }
      lease.ticket = randomBytes(32).toString("hex");
      sendJson(response, 200, {
        version: 1, runtime, pageInstance: randomBytes(16).toString("hex"), name: lease.name,
        cue: `ID ${lease.port}`, roles: [{ role: "fm", port: lease.port, direction: "receive", capability: "managed" }],
        ticket: lease.ticket,
      });
      lease.response = undefined;
    }, line => {
      const match = /^fm-reserved ([1-9][0-9]*) ([1-9][0-9]*) ([1-9][0-9]*) ([1-9][0-9]*) ([1-9][0-9]*) 5000$/.exec(line);
      if (record || !match || match[1] !== String(epoch) || match[2] !== String(lease.rid) ||
          +match[3] > 65535 || +match[4] > 65535 || +match[5] <= 1 || +match[5] > 0xffffffff) {
        throw new Error("Unexpected router FM reservation");
      }
      record = match;
    });
  }

  function serve(request, response, host, origin) {
    if (!exactOrigin(request, host, origin)) return sendJson(response, 403, { error: "INVALID" });
    if (request.method !== "POST") return sendJson(response, 405, { error: "INVALID" }, { Allow: "POST" });
    if (!/^application\/json(?:\s*;\s*charset=utf-8)?$/i.test(request.headers["content-type"] ?? "") ||
        Number(request.headers["content-length"]) > maxBody) {
      return sendJson(response, 400, { error: "INVALID" }, { Connection: "close" });
    }
    // Allow one bounded reserve attempt at full size so Runtime can refuse capacity.
    if (stopped || reading + reservations.size > limit) {
      return sendJson(response, 503, { error: "UNAVAILABLE" }, { Connection: "close" });
    }
    reading++;
    readJoin(request, response, value => {
      reading--;
      if (response.destroyed) return;
      const name = requestName(value);
      if (!name) return sendJson(response, 400, { error: "INVALID" }, { Connection: "close" });
      if (value.runtime !== runtime || stopped) return sendJson(response, 503, { error: "UNAVAILABLE" });
      reserve(value, name, response);
    });
  }

  function upgrade(request, raw, port, host, origin) {
    const lease = ports.get(port);
    const offer = request.headers["sec-websocket-protocol"] ?? "";
    if (!lease && !offer.includes(ticketPrefix)) return false;
    if (!lease || !lease.ticket || lease.closed || lease.peer || stopped ||
        !exactOrigin(request, host, origin) || openProtocol(request, matchesHeader) !== lease.ticket) {
      reject(raw, "403 Forbidden");
      return true;
    }
    if (performance.now() >= lease.deadline) {
      reject(raw, "403 Forbidden");
      expire(lease);
      return true;
    }
    if (nextSocket > maxOwner) {
      reject(raw, "503 Service Unavailable");
      return true;
    }
    lease.sid = nextSocket++;
    lease.ticket = undefined;
    lease.peer = createPeer(lease, request, raw);
    const tuple = `${epoch} ${lease.rid} ${port} ${lease.generation} ${lease.sid}`;
    let record = false;
    queue(lease, () => lease.closed ? undefined : `fm-publish ${tuple}`, error => {
      if (record !== (error === undefined)) throw new Error("Inconsistent router FM publication");
      if (error || lease.closed || raw.destroyed || performance.now() >= lease.deadline) {
        expire(lease);
        return;
      }
      clearTimeout(lease.timer);
      finishUpgrade(lease.peer);
    }, line => {
      if (record || line !== `fm-published ${tuple} 64`) throw new Error("Unexpected router FM publication");
      record = lease.published = true;
      lease.peer.bound = true;
    });
    return true;
  }

  return {
    serve, upgrade, close,
    pendingCount: () => [...reservations].filter(lease => !lease.peer).length,
    stop() {
      stopped = true;
      for (const lease of reservations) {
        clearTimeout(lease.timer);
        if (lease.response && !lease.response.writableEnded) lease.response.destroy();
      }
      reservations.clear();
      ports.clear();
    },
  };
}

module.exports = { createLocalFmJoin, fmJoinProtocol: protocol };
