"use strict";

const { spawn } = require("node:child_process");

const deadlineMs = 5000;
const maxLineBytes = 544;
const errorCodes = new Set(["INVALID", "CAPACITY", "UNAVAILABLE"]);
const maxGeneration = 0xffffffff;
const defaultMidiPeers = 6;
const maxMidiPeers = 65535; // Directional port ID space.
const uiDirections = new Map([
  ["keybed", "in"], ["panel", "in"], ["led", "out"],
  ["sequence", "in"], ["monitor", "out"], ["log", "out"], ["fm", "out"],
]);

function unsigned(text, maximum) {
  if (!/^[1-9][0-9]*$/.test(text) || Number(text) > maximum) {
    throw new Error("Invalid runner identifier");
  }
  return Number(text);
}

function bytes(hex, maxBytes, exact = false) {
  if (!/^(?:[0-9A-F]{2})+$/.test(hex) || hex.length > 2 * maxBytes ||
      (exact && hex.length !== 2 * maxBytes)) {
    throw new Error("Invalid runner byte record");
  }
  return Buffer.from(hex, "hex");
}

function recordFrom(line) {
  const fields = line.split(" ");
  const [kind, first, second, third] = fields;
  if (kind === "bound" && fields.length === 5 && (third === "in" || third === "out")) {
    return {
      kind, port: unsigned(first, 65535), generation: unsigned(second, maxGeneration),
      direction: third, limit: unsigned(fields[4], 128),
    };
  }
  if (kind === "midi" && fields.length === 4) {
    return { kind, port: unsigned(first, 65535), generation: unsigned(second, maxGeneration), data: bytes(third, 3, true) };
  }
  if (kind === "serial" && fields.length === 3) {
    return { kind, generation: unsigned(first, maxGeneration), data: bytes(second, 256) };
  }
  if (kind === "fault" && first === "midi" && fields.length === 4) {
    return { kind, port: unsigned(second, 65535), generation: unsigned(third, maxGeneration) };
  }
  if (kind === "fault" && first === "serial" && fields.length === 3) {
    return { kind, generation: unsigned(second, maxGeneration) };
  }
  throw new Error("Unknown runner record");
}

function uiRolesFrom(line) {
  const fields = line.split(" ").slice(1);
  if (fields.length % 3 !== 0 || fields.length > uiDirections.size * 3) {
    throw new Error("Invalid router UI role advertisement");
  }
  const roles = new Map();
  const ports = new Set();
  const order = [...uiDirections.keys()];
  let previous = -1;
  for (let index = 0; index < fields.length; index += 3) {
    const [role, id, direction] = fields.slice(index, index + 3);
    const port = unsigned(id, 65535);
    const position = order.indexOf(role);
    if (uiDirections.get(role) !== direction || position <= previous || ports.has(port)) {
      throw new Error("Invalid router UI role advertisement");
    }
    previous = position;
    roles.set(role, { port, direction });
    ports.add(port);
  }
  return roles;
}

function startLocalPipe(executable, handlers, args = [], expectUiRoles = false) {
  const runner = spawn(executable, args, { stdio: ["pipe", "pipe", "inherit"], detached: process.platform !== "win32" });
  let ready = false;
  let uiRoles;
  let stopped = false;
  let response;
  let inEvent = false;
  let replyStarted = false;
  let partial = "";
  let timer = setTimeout(() => fail("Router startup timed out"), deadlineMs);

  function stop() {
    if (stopped) return;
    stopped = true;
    clearTimeout(timer);
    runner.stdin.end();
    timer = setTimeout(() => runner.kill("SIGKILL"), deadlineMs);
  }

  function fail(message) {
    if (stopped) return;
    stop();
    handlers.onFailure(message);
  }

  function readLine(line) {
    if (line.length > maxLineBytes) throw new Error("Router line too long");
    if (line === "ui-roles" || line.startsWith("ui-roles ")) {
      if (!expectUiRoles || ready || uiRoles) throw new Error("Unexpected router UI role advertisement");
      uiRoles = uiRolesFrom(line);
      return;
    }
    if (!ready) {
      const match = /^ready ([1-9][0-9]*)$/.exec(line);
      if (line !== "ready" && (!match || Number(match[1]) > maxMidiPeers)) {
        throw new Error("Invalid router readiness");
      }
      if (expectUiRoles && !uiRoles) throw new Error("Router did not advertise UI roles");
      ready = true;
      clearTimeout(timer);
      const limit = match ? Number(match[1]) : defaultMidiPeers;
      if (expectUiRoles) {
        // Parse the entire startup chunk before accepting a listener, including any late metadata.
        setImmediate(() => {
          if (stopped) return;
          try { handlers.onReady(limit, uiRoles); } catch (error) { fail(error.message); }
        });
      } else handlers.onReady(limit);
      return;
    }
    if (line === "event" && !inEvent && !replyStarted) {
      inEvent = true;
      return;
    }
    if (line === "end" && inEvent) {
      inEvent = false;
      return;
    }
    if (!inEvent && !response) throw new Error("Unsolicited router response");
    if (!inEvent && (line === "ok" || line.startsWith("err "))) {
      const code = line === "ok" ? undefined : line.slice(4);
      if (code !== undefined && !errorCodes.has(code)) throw new Error("Invalid router result");
      const finish = response;
      response = undefined;
      replyStarted = false;
      clearTimeout(timer);
      finish(code);
      return;
    }
    if (!inEvent) replyStarted = true;
    handlers.onRecord(recordFrom(line));
  }

  runner.stdout.on("data", (chunk) => {
    if (stopped) return; // Keep draining even after stdio retirement begins.
    const lines = (partial + chunk.toString("latin1")).split("\n");
    partial = lines.pop();
    try {
      for (const line of lines) {
        if (stopped) return;
        readLine(line);
      }
      if (partial.length > maxLineBytes) throw new Error("Router line too long");
    } catch (error) {
      fail(error.message);
    }
  });

  runner.on("error", () => fail("Could not start the local router"));
  runner.stdout.on("error", () => fail("Router output failed"));
  runner.stdin.on("error", () => fail("Router input failed"));
  runner.on("close", (code, signal) => {
    if (!stopped) fail(`Router exited with ${signal ?? code}`);
    clearTimeout(timer);
  });

  return {
    dispatch(line, complete) {
      if (stopped || !ready || response) throw new Error("Router dispatch is unavailable");
      response = complete;
      timer = setTimeout(() => fail("Router dispatch timed out"), deadlineMs);
      runner.stdin.write(line + "\n");
    },
    stop,
  };
}

module.exports = { maxGeneration, startLocalPipe };
