"use strict";

const { accessSync, constants, readFileSync, realpathSync, statSync } = require("node:fs");
const { createHash, randomBytes, X509Certificate } = require("node:crypto");
const { createServer } = require("node:https");
const { networkInterfaces } = require("node:os");
const path = require("node:path");
const { WebSocketServer } = require("ws");
const { loadWebUiBundle } = require("./web-ui-bundle");
const { reject, relayPico } = require("./pico-relay");

const address = "192.168.0.223";
const port = 19445;
const host = `${address}:${port}`;
const origin = `https://${host}`;
const interfaceName = "wlan0";
const root = realpathSync(path.join(__dirname, ".."));
const slots = new Map([
  [1, { role: "keybed", direction: "send" }],
  [2, { role: "panel", direction: "send" }],
  [3, { role: "led", direction: "receive" }],
  [5, { role: "monitor", direction: "receive" }],
]);

function externalFile(filename, label, limit = 4096) {
  try {
    if (!path.isAbsolute(filename)) throw new Error();
    const resolved = realpathSync(filename);
    const relative = path.relative(root, resolved);
    const info = statSync(resolved);
    if (!relative.startsWith(`..${path.sep}`) || !info.isFile() || info.size > limit) throw new Error();
    return { resolved, bytes: readFileSync(resolved) };
  } catch {
    throw new Error(`${label} must be a readable absolute file outside the checkout`);
  }
}

function object(bytes, label) {
  let value;
  try { value = JSON.parse(bytes.toString("utf8")); } catch { throw new Error(`Invalid ${label} JSON`); }
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error(`Invalid ${label}`);
  return value;
}

function enrolled(list, { allowEmpty = false } = {}) {
  if (!Array.isArray(list) || (!allowEmpty && !list.length) || list.length > 64 ||
      list.some(item => typeof item !== "string" || !/^[a-f0-9]{64}$/.test(item))) {
    throw new Error("Expected lowercase SHA-256 client leaf enrollments");
  }
  const set = new Set(list);
  if (set.size !== list.length) throw new Error("Duplicate Pico client leaf enrollment");
  return set;
}

function helper(filename) {
  if (!path.isAbsolute(filename) || !statSync(filename).isFile()) throw new Error("Expected absolute helper paths");
  accessSync(filename, constants.X_OK);
  return filename;
}

function physicalTarget(bytes) {
  const selector = object(bytes, "Pico selector");
  const { mode, usbPath, vid, pid, bcdDevice, product, serial, cdcInterface, midiInterface } = selector;
  const hex = value => typeof value === "string" && /^[0-9a-f]{4}$/.test(value);
  const text = value => typeof value === "string" && value.length > 0 && value.length <= 128 &&
    !/[\p{Cc}\p{Cf}]/u.test(value) && value === value.trim();
  const number = value => Number.isInteger(value) && value >= 0 && value <= 255;
  const serialMatches = mode === "usb-serial" ? text(serial) : mode === "ram-bench" && serial === null;
  if (Object.keys(selector).length !== 9 || !serialMatches ||
      typeof usbPath !== "string" || usbPath.length > 48 || !/^[1-9]\d*-[1-9]\d*(?:\.[1-9]\d*)*$/.test(usbPath) ||
      !hex(vid) || !hex(pid) || !hex(bcdDevice) || !text(product) ||
      !number(cdcInterface) || !number(midiInterface) || cdcInterface === midiInterface) {
    throw new Error("Invalid Pico selector identity");
  }
  return `${product} · USB ${usbPath}`;
}

function interfaceActive() {
  return networkInterfaces()[interfaceName]?.some(entry =>
    entry.family === "IPv4" && !entry.internal && entry.address === address);
}

function serverConfig(filename) {
  const config = object(externalFile(filename, "Pico service config").bytes, "Pico service config");
  const hasCombined = Object.hasOwn(config, "midiCdcClientFingerprints");
  if (Object.keys(config).length !== 10 + Number(hasCombined)) throw new Error("Invalid Pico service config fields");
  if (config.address !== address || config.port !== port || config.interface !== interfaceName || config.origin !== origin) {
    throw new Error(`Expected ${origin} on ${interfaceName}`);
  }
  if (!interfaceActive()) throw new Error("Configured Pico address is unavailable on wlan0");
  const midi = enrolled(config.midiClientFingerprints, { allowEmpty: true });
  const combined = enrolled(hasCombined ? config.midiCdcClientFingerprints : [], { allowEmpty: true });
  const maintenance = enrolled(config.maintenanceClientFingerprints, { allowEmpty: combined.size > 0 });
  const grants = new Map();
  for (const [fingerprints, grant] of [
    [midi, { midi: true, cdc: false }],
    [maintenance, { midi: false, cdc: true }],
    [combined, { midi: true, cdc: true }],
  ]) {
    for (const fingerprint of fingerprints) {
      if (grants.has(fingerprint)) throw new Error("Pico client leaf enrollments must be disjoint");
      grants.set(fingerprint, grant);
    }
  }
  const selector = externalFile(config.target, "Pico selector");
  const targetLabel = physicalTarget(selector.bytes);
  const cert = externalFile(config.certificate, "Pico TLS certificate", 65536).bytes;
  const key = externalFile(config.key, "Pico TLS key", 65536).bytes;
  const ca = externalFile(config.clientCA, "Pico client CA", 65536).bytes;
  let tls;
  try {
    if (!new X509Certificate(cert).checkIP(address)) throw new Error();
    tls = createServer({ cert, key, ca, minVersion: "TLSv1.2", requestCert: true, rejectUnauthorized: true });
  } catch {
    throw new Error("Pico TLS requires a matching key and certificate with the configured IP SAN");
  }
  return { selector, targetLabel, tls, grants };
}

function exact(request, name, expected) {
  if (request.headers[name] !== expected) return false;
  let count = 0;
  for (let index = 0; index < request.rawHeaders.length; index += 2) {
    if (request.rawHeaders[index].toLowerCase() === name) count++;
  }
  return count === 1;
}

function startup(configFile, cdcHelper, midiHelper) {
  const cdc = helper(cdcHelper);
  const midi = helper(midiHelper);
  const { selector, targetLabel, tls, grants } = serverConfig(configFile);
  const bundle = loadWebUiBundle();
  const publicPaths = new Set(["/apc/", "/fm/", "/midi-file/", "/local-transport.js"]);
  const home = readFileSync(path.join(__dirname, "pico-home.html"));
  const combinedHome = home.toString("utf8").replace("</ul>",
    '<li><a href="/pico-cdc/">Pico CDC maintenance</a></li>\n      </ul>');
  const html = "text/html; charset=utf-8";
  const cdcSource = readFileSync(path.join(__dirname, "pico-cdc.html"), "utf8");
  const cdcPage = cdcSource.replace("PICO_TARGET_LABEL", () => targetLabel.replace(/[&<>]/g, char =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[char]));
  const cdcScriptHash = createHash("sha256").update(cdcPage.match(/<script>([\s\S]*?)<\/script>/)[1]).digest("base64");
  const roles = Object.fromEntries([...slots].map(([id, { role, direction }]) => [role, { port: id, direction }]));
  const capability = JSON.stringify({
    version: 1, release: bundle.release, instance: randomBytes(16).toString("hex"), roles,
    autoBind: ["keybed", "panel", "led"], actions: { cli: false, ledTest: false, recording: false },
  });
  const websocket = new WebSocketServer({
    noServer: true, maxPayload: 256, maxFragments: 256, maxBufferedChunks: 256,
    maxPendingAutoPongs: 16, closeTimeout: 1000, perMessageDeflate: false,
  });
  const live = new Map();
  const unavailable = new Set();
  let stopping = false;

  function identity(request) {
    const { socket } = request;
    if (!socket.authorized || !exact(request, "host", host) || !interfaceActive()) return;
    const fingerprint = socket.getPeerCertificate().fingerprint256?.replaceAll(":", "").toLowerCase();
    return grants.get(fingerprint);
  }

  tls.on("request", (request, response) => {
    const grant = identity(request);
    if (!grant || (request.headers.origin !== undefined && !exact(request, "origin", origin))) {
      response.writeHead(403).end();
      return;
    }
    let asset;
    const maintenancePage = request.url === "/pico-cdc/";
    if (grant.cdc && maintenancePage) asset = [html, cdcPage];
    else if (grant.midi) {
      if (request.url === "/") asset = [html, grant.cdc ? combinedHome : home];
      else if (request.url === "/ui-capabilities.json") asset = ["application/json; charset=utf-8", capability];
      else if (publicPaths.has(request.url)) asset = bundle.assets.get(request.url);
    }
    if (!asset) {
      response.writeHead(404).end();
      return;
    }
    if (request.method !== "GET") {
      response.writeHead(405, { Allow: "GET" }).end();
      return;
    }
    const site = request.headers["sec-fetch-site"];
    if (request.url === "/ui-capabilities.json" && site !== undefined &&
        !["same-origin", "none"].some(value => exact(request, "sec-fetch-site", value))) {
      response.writeHead(403).end();
      return;
    }
    const [type, bytes] = asset;
    const headers = {
      "Content-Type": type, "Content-Length": Buffer.byteLength(bytes), "Cache-Control": "no-store",
      "Cross-Origin-Resource-Policy": "same-origin", "X-Content-Type-Options": "nosniff", "X-Frame-Options": "DENY",
    };
    if (maintenancePage) {
      headers["Content-Security-Policy"] = `default-src 'none'; script-src 'sha256-${cdcScriptHash}'; style-src 'unsafe-inline'; connect-src wss://${host}; frame-ancestors 'none'; base-uri 'none'; form-action 'none'`;
    }
    response.writeHead(200, headers).end(bytes);
  });

  function selectorUnchanged() {
    try { return externalFile(selector.resolved, "Pico selector").bytes.equals(selector.bytes); }
    catch { return false; }
  }

  tls.on("upgrade", (request, raw, head) => {
    const grant = identity(request);
    if (!grant || !exact(request, "origin", origin)) return reject(raw, "403 Forbidden");
    const match = /^\/midi\/([1235])$/.exec(request.url);
    const slot = match && Number(match[1]);
    const serial = grant.cdc && request.url === "/cdc";
    if (!serial && !(grant.midi && slot)) return reject(raw, "404 Not Found");
    const wsKey = request.headers["sec-websocket-key"];
    if (request.method !== "GET" || request.headers.upgrade?.toLowerCase() !== "websocket" ||
        !exact(request, "sec-websocket-version", "13") ||
        typeof wsKey !== "string" || !/^[+/A-Za-z0-9]{22}==$/.test(wsKey) ||
        !exact(request, "sec-websocket-key", wsKey) || request.headers["sec-websocket-protocol"] !== undefined) {
      return reject(raw, "400 Bad Request");
    }
    const binding = serial ? "cdc" : slot;
    if (live.has(binding)) return reject(raw, "409 Conflict");
    if (stopping || unavailable.has(binding) || !selectorUnchanged()) return reject(raw, "503 Service Unavailable");
    try {
      const close = relayPico({
        request, raw, head, server: websocket, executable: serial ? cdc : midi,
        args: serial ? [selector.resolved] : [selector.resolved, String(slot)],
        direction: serial ? "serial" : slots.get(slot).direction,
        notesOnly: slot === 1 || slot === 5,
        onClose({ cleanupUnconfirmed }) {
          if (cleanupUnconfirmed) {
            unavailable.add(binding);
            console.error(`Pico MIDI ${slot} cleanup unconfirmed; native recovery required`);
          }
          live.delete(binding);
        },
      });
      live.set(binding, close);
    } catch {
      reject(raw, "503 Service Unavailable");
    }
  });

  function shutdown() {
    if (stopping) return;
    stopping = true;
    tls.close();
    tls.closeAllConnections();
    for (const close of live.values()) close();
  }
  tls.on("error", error => {
    console.error(`Pico listener failed: ${error.code ?? "listener error"}`);
    process.exitCode = 1;
    shutdown();
  });
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
  tls.listen(port, address, () => console.log(`Gridpico Pico service listening at ${origin}/`));
}

if (process.argv.length !== 5) {
  console.error("Usage: node connections/pico-host.js CONFIG CDC_HELPER MIDI_HELPER");
  process.exitCode = 2;
} else {
  try { startup(...process.argv.slice(2)); }
  catch (error) {
    console.error(`Pico service configuration failed: ${error.message}`);
    process.exitCode = 1;
  }
}
