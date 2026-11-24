"use strict";

const { readFileSync, realpathSync, statSync } = require("node:fs");
const { createServer } = require("node:https");
const { isIPv4 } = require("node:net");
const path = require("node:path");

const checkout = realpathSync(path.join(__dirname, ".."));
const fingerprintPattern = /^[0-9a-f]{64}$/;

function httpsOrigin(value) {
  try {
    const url = new URL(value);
    return url.protocol === "https:" && url.origin === value ? url : undefined;
  } catch {
    return undefined;
  }
}

function loadFile(filename, label, encoding) {
  try {
    if (typeof filename !== "string" || !path.isAbsolute(filename)) throw new Error();
    const resolved = realpathSync(filename);
    const relative = path.relative(checkout, resolved);
    if (relative === "" || !relative.startsWith(`..${path.sep}`) || !statSync(resolved).isFile()) {
      throw new Error();
    }
    return readFileSync(resolved, encoding);
  } catch {
    throw new Error(`${label} must be a readable absolute file outside the repository`);
  }
}

function readConfig(filename) {
  const source = loadFile(filename, "WSS configuration", "utf8");
  let config;
  try {
    config = JSON.parse(source);
  } catch {
    throw new Error("WSS configuration must be valid JSON");
  }
  if (!config || typeof config !== "object" || Array.isArray(config)) {
    throw new Error("WSS configuration must be an object");
  }

  const { address, port, origin, allowedClientFingerprints, midiPorts } = config;
  const firstOctet = typeof address === "string" ? Number(address.split(".")[0]) : 0;
  if (typeof address !== "string" || !isIPv4(address) || firstOctet === 0 ||
      firstOctet === 127 || firstOctet >= 224) {
    throw new Error("WSS requires an explicit non-loopback unicast IPv4 address");
  }
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error("WSS port must be an integer from 1 to 65535");
  }

  const url = httpsOrigin(origin);
  if (!url || Number(url.port || 443) !== port) {
    throw new Error("WSS requires a canonical HTTPS origin with the configured port");
  }

  if (!Array.isArray(allowedClientFingerprints) || allowedClientFingerprints.length === 0 ||
      allowedClientFingerprints.some((value) => typeof value !== "string" || !fingerprintPattern.test(value))) {
    throw new Error("WSS requires lowercase SHA-256 client leaf fingerprints");
  }
  if (!Array.isArray(midiPorts) || midiPorts.length === 0) {
    throw new Error("WSS requires published directional MIDI ports");
  }
  const ports = new Map();
  for (const binding of midiPorts) {
    if (!binding || !Number.isInteger(binding.id) || binding.id < 1 || binding.id > 65535 ||
        (binding.direction !== "in" && binding.direction !== "out") || ports.has(binding.id)) {
      throw new Error("WSS MIDI ports must have unique IDs and Graph directions in or out");
    }
    ports.set(binding.id, binding.direction);
  }

  const fingerprints = new Set(allowedClientFingerprints);
  let synthReceive;
  if (Object.hasOwn(config, "synthReceive")) {
    const grant = config.synthReceive;
    if (grant && httpsOrigin(grant.origin) && grant.origin !== origin &&
        typeof grant.clientFingerprint === "string" && fingerprintPattern.test(grant.clientFingerprint) &&
        !fingerprints.has(grant.clientFingerprint) && Number.isInteger(grant.portId) &&
        grant.portId > 0 && grant.portId <= 65535 && !ports.has(grant.portId)) {
      synthReceive = { origin: grant.origin, fingerprint: grant.clientFingerprint, portId: grant.portId };
    } else {
      console.error("WSS synth receive disabled: use a distinct HTTPS origin, client leaf and output port.");
    }
  }

  return { config, host: url.host, ports, fingerprints, synthReceive };
}

function createWssListener(filename) {
  const { config, host, ports, fingerprints, synthReceive } = readConfig(filename);
  const cert = loadFile(config.certificate, "Server certificate");
  const key = loadFile(config.key, "Server key");
  const ca = loadFile(config.clientCA, "Client CA");
  let server;
  try {
    server = createServer({ cert, key, ca, minVersion: "TLSv1.2", requestCert: true, rejectUnauthorized: true });
  } catch {
    throw new Error("Could not initialize WSS TLS credentials");
  }
  return {
    server,
    address: config.address,
    port: config.port,
    origin: config.origin,
    host,
    ports,
    synthReceive,
    identity(socket) {
      if (!socket.authorized) return;
      const fingerprint = socket.getPeerCertificate().fingerprint256;
      if (typeof fingerprint !== "string") return;
      const canonical = fingerprint.replaceAll(":", "").toLowerCase();
      if (fingerprints.has(canonical)) return "tools";
      if (canonical === synthReceive?.fingerprint) return "synth";
    },
  };
}

module.exports = { createWssListener };
