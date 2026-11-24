"use strict";

const { accessSync, constants, readFileSync, realpathSync, statSync } = require("node:fs");
const { createHash } = require("node:crypto");
const path = require("node:path");
const { reject, relayPico } = require("./pico-relay");

const checkout = realpathSync(path.join(__dirname, ".."));

function privateJsonFile(filename) {
  if (typeof filename !== "string" || !path.isAbsolute(filename)) throw new Error("Expected an absolute private file");
  const resolved = realpathSync(filename);
  const info = statSync(resolved);
  if (!path.relative(checkout, resolved).startsWith(`..${path.sep}`) || !info.isFile() || info.size > 4096) {
    throw new Error("Expected a small private file outside the checkout");
  }
  const bytes = readFileSync(resolved);
  const value = JSON.parse(bytes.toString("utf8"));
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("Expected a JSON object");
  return { resolved, bytes, value };
}

function createLanPicoCdc(filename, { origin }, exactHeader) {
  const { value: config } = privateJsonFile(filename);
  const { target, cdcHelper } = config;
  if (Object.keys(config).length !== 2 || typeof cdcHelper !== "string" ||
      !path.isAbsolute(cdcHelper) || !statSync(cdcHelper).isFile()) throw new Error("Expected target and an absolute CDC helper");
  accessSync(cdcHelper, constants.X_OK);
  const selector = privateJsonFile(target);
  const { mode, usbPath, vid, pid, bcdDevice, product, serial, cdcInterface, midiInterface } = selector.value;
  const text = value => typeof value === "string" && value.length > 0 && value.length <= 128 &&
    value === value.trim() && !/[\p{Cc}\p{Cf}]/u.test(value);
  const hex = value => typeof value === "string" && /^[0-9a-f]{4}$/.test(value);
  const number = value => Number.isInteger(value) && value >= 0 && value <= 255;
  if (Object.keys(selector.value).length !== 9 || mode !== "usb-serial" ||
      typeof usbPath !== "string" || usbPath.length > 48 || !/^[1-9]\d*-[1-9]\d*(?:\.[1-9]\d*)*$/.test(usbPath) ||
      !text(product) || !text(serial) || !hex(vid) || !hex(pid) || !hex(bcdDevice) ||
      !number(cdcInterface) || !number(midiInterface) || cdcInterface === midiInterface) {
    throw new Error("Expected an exact Pico usb-serial selector");
  }
  const label = `${product} · USB ${usbPath}`.replace(/[&<>]/g, char =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[char]);
  const html = readFileSync(path.join(__dirname, "pico-cdc.html"), "utf8")
    .replace("PICO_TARGET_LABEL", () => label)
    .replace("authenticated WSS CDC", "Private LAN WebSocket CDC")
    .replace("This console sends commands directly to the Pico.",
      "Commands go to the attached Pico through this Pi.");
  const sourceHash = createHash("sha256").update(html.match(/<script>([\s\S]*?)<\/script>/)[1]).digest("base64");
  let release;

  return {
    serve(request, response) {
      const site = request.headers["sec-fetch-site"];
      if ((request.headers.origin !== undefined && !exactHeader(request, "origin", origin)) ||
          (site !== undefined && !["same-origin", "none"].some(value => exactHeader(request, "sec-fetch-site", value)))) {
        response.writeHead(403).end();
        return;
      }
      if (request.method !== "GET") {
        response.writeHead(405, { Allow: "GET" }).end();
        return;
      }
      response.writeHead(200, {
        "Content-Type": "text/html; charset=utf-8", "Content-Length": Buffer.byteLength(html),
        "Cache-Control": "no-store", "Cross-Origin-Resource-Policy": "same-origin",
        "X-Content-Type-Options": "nosniff", "X-Frame-Options": "DENY",
        "Content-Security-Policy": `default-src 'none'; script-src 'sha256-${sourceHash}'; style-src 'unsafe-inline'; connect-src ${origin.replace(/^http:/, "ws:")}; frame-ancestors 'none'; base-uri 'none'; form-action 'none'`,
      }).end(html);
    },

    upgrade(request, raw, head, server) {
      if (!exactHeader(request, "origin", origin)) return reject(raw, "403 Forbidden");
      const key = request.headers["sec-websocket-key"];
      if (request.method !== "GET" || request.headers.upgrade?.toLowerCase() !== "websocket" ||
          !exactHeader(request, "sec-websocket-version", "13") ||
          typeof key !== "string" || !/^[+/A-Za-z0-9]{22}==$/.test(key) ||
          !exactHeader(request, "sec-websocket-key", key) || request.headers["sec-websocket-protocol"] !== undefined) {
        return reject(raw, "400 Bad Request");
      }
      if (release) return reject(raw, "409 Conflict");
      try {
        if (!privateJsonFile(selector.resolved).bytes.equals(selector.bytes)) throw new Error();
        release = relayPico({
          request, raw, head, server, executable: cdcHelper, args: [selector.resolved],
          direction: "serial", onClose() { release = undefined; },
        });
      } catch {
        reject(raw, "503 Service Unavailable");
      }
    },

    close() { release?.(); },
  };
}

module.exports = { createLanPicoCdc };
