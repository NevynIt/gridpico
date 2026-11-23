"use strict";

const maxDevices = 2;
const maxPorts = 5;
const rows = 5;
const columns = 8;
const ledCount = rows * columns;
const maxCaptures = 50;
const maxRevision = (1n << 64n) - 1n;
const states = new Set(["connected", "disconnected", "ambiguous"]);
const errorCodes = new Set(["INVALID", "CAPACITY", "UNAVAILABLE"]);
const nameDecoder = new TextDecoder("utf-8", { fatal: true });

function integer(value, minimum, maximum) {
  return Number.isInteger(value) && value >= minimum && value <= maximum;
}

function decimal(text, minimum, maximum) {
  if (!/^(0|[1-9][0-9]*)$/.test(text)) return undefined;
  const value = Number(text);
  return integer(value, minimum, maximum) ? value : undefined;
}

function isNote(event) {
  return event && (event.type === "note-on" || event.type === "note-off") &&
    event.channel === 1 && integer(event.note, 0, 127) &&
    integer(event.velocity, event.type === "note-on" ? 1 : 0, 127);
}

function parseRequest(data) {
  let event;
  try {
    event = JSON.parse(data.toString("utf8"));
  } catch {
    return undefined;
  }
  if (event === null || typeof event !== "object") return undefined;

  const fields = Object.keys(event).length;
  if (event.type === "patch-refresh" && fields === 1) {
    return { kind: "refresh", line: "surface-ready" };
  }
  if (isNote(event) && fields === 4) {
    const direction = event.type === "note-on" ? "on" : "off";
    return { kind: `keybed-${direction}`, line: `${direction} 1 ${event.note} ${event.velocity}` };
  }
  const { bytes } = event;
  if (event.type === "apc-surface" && fields === 2 && Array.isArray(bytes) &&
      bytes.length === 3 && integer(bytes[0], 128, 239) &&
      integer(bytes[1], 0, 127) && integer(bytes[2], 0, 127)) {
    return { kind: "surface", line: `surface ${bytes.join(" ")}` };
  }
  return undefined;
}

function decodeName(nameHex) {
  if (nameHex === "-") return "";
  if (!/^(?:[0-9A-F]{2}){1,31}$/.test(nameHex)) throw new Error("Invalid inventory name");
  const bytes = Buffer.from(nameHex, "hex");
  if (bytes.includes(0)) throw new Error("Invalid inventory name");
  nameDecoder.decode(bytes);
  return nameHex;
}

function parseInventoryBegin([revision, devices, ports]) {
  const deviceCount = decimal(devices, 0, maxDevices);
  const portCount = decimal(ports, 0, maxPorts);
  if (deviceCount === undefined || portCount === undefined ||
      !/^(0|[1-9][0-9]{0,19})$/.test(revision) || BigInt(revision) > maxRevision) {
    throw new Error("Invalid inventory header");
  }
  return { type: "patch-inventory-begin", revision, deviceCount, portCount };
}

class PatchResponse {
  constructor(kind) {
    this.kind = kind;
    this.refresh = kind === "startup" || kind === "refresh";
    this.inventory = [];
    this.devices = new Set();
    this.ports = new Set();
    this.slots = new Set();
    this.inventoryComplete = false;
    this.ledPage = undefined;
    this.leds = [];
    this.captureCount = 0;
  }

  addInventory(fields) {
    const [record, ...values] = fields;
    const [header] = this.inventory;
    if (!header && record === "inventory" && values.length === 3) {
      this.inventory.push(parseInventoryBegin(values));
      return;
    }
    if (!header) throw new Error("Missing inventory header");

    if (record === "device" && values.length === 3) {
      const [idText, state, name] = values;
      const id = decimal(idText, 1, 65535);
      if (id === undefined || this.devices.has(id) || this.devices.size === header.deviceCount ||
          !states.has(state)) {
        throw new Error("Invalid inventory device");
      }
      this.devices.add(id);
      this.inventory.push({ type: "patch-device", id, state, nameHex: decodeName(name) });
      return;
    }
    if (record === "port" && values.length === 7) {
      const [idText, deviceText, direction, state, eligibility, slotText, name] = values;
      const id = decimal(idText, 1, 65535);
      const deviceId = decimal(deviceText, 1, 65535);
      const slot = decimal(slotText, 0, direction === "in" ? rows : columns);
      const position = `${direction}:${slot}`;
      if (id === undefined || this.ports.has(id) || this.ports.size === header.portCount ||
          this.devices.size !== header.deviceCount || !this.devices.has(deviceId) ||
          (direction !== "in" && direction !== "out") || !states.has(state) ||
          (eligibility !== "0" && eligibility !== "1") || slot === undefined ||
          (slot !== 0 && (eligibility === "0" || this.slots.has(position)))) {
        throw new Error("Invalid inventory port");
      }
      this.ports.add(id);
      if (slot !== 0) this.slots.add(position);
      this.inventory.push({
        type: "patch-port", id, deviceId, direction, state,
        eligible: eligibility === "1", slot, nameHex: decodeName(name),
      });
      return;
    }
    if (record === "inventory-end" && values.length === 0 &&
        this.devices.size === header.deviceCount && this.ports.size === header.portCount) {
      this.inventoryComplete = true;
      this.inventory.push({ type: "patch-inventory-end" });
      return;
    }
    throw new Error("Invalid or incomplete inventory block");
  }

  add(line) {
    const fields = line.split(" ");
    if (this.refresh && !this.inventoryComplete) {
      this.addInventory(fields);
      return;
    }

    const [record, first, second, third] = fields;
    if (record === "led-page") {
      if (this.ledPage || this.leds.length) throw new Error("Unexpected Patch LED page marker");
      if (first === "route" && fields.length === 2) {
        this.ledPage = { type: "patch-led-page", layer: "route" };
        return;
      }
      const deviceId = decimal(second, 1, 65535);
      if (first === "device-color" && fields.length === 3 && deviceId !== undefined) {
        this.ledPage = { type: "patch-led-page", layer: "device-color", deviceId };
        return;
      }
      throw new Error("Invalid Patch LED page marker");
    }
    if (fields.length !== 4) throw new Error("Invalid fixture record");
    if (record === "surface") {
      const index = this.leds.length;
      const pad = columns * (rows - 1 - Math.floor(index / columns)) + index % columns;
      const color = decimal(third, 0, 127);
      if (index >= ledCount || first !== "150" || second !== String(pad) || color === undefined) {
        throw new Error("Invalid or out-of-order Patch LED");
      }
      this.leds.push({ type: "apc-surface", bytes: [150, pad, color] });
      return;
    }
    if ((record !== "on" && record !== "off") || this.refresh || this.ledPage || this.leds.length ||
        this.captureCount === maxCaptures || (this.kind === "surface" && record !== "off")) {
      throw new Error("Unexpected capture record");
    }
    const event = {
      type: `note-${record}`, channel: decimal(first, 1, 1),
      note: decimal(second, 0, 127), velocity: decimal(third, 0, 127),
    };
    if (!isNote(event)) throw new Error("Invalid capture note");
    this.captureCount++;
    return event;
  }

  finish(code, usesPageMarkers) {
    const rejected = code !== undefined;
    if (rejected && !errorCodes.has(code)) throw new Error("Invalid fixture error code");
    const completePage = this.leds.length === ledCount;
    if ((this.ledPage || this.leds.length !== 0) && !completePage) {
      throw new Error("Incomplete Patch LED page");
    }
    if (completePage && usesPageMarkers !== undefined &&
        usesPageMarkers !== Boolean(this.ledPage)) {
      throw new Error("Inconsistent Patch LED page framing");
    }
    if (this.refresh) {
      if (rejected && (this.inventory.length !== 0 || completePage)) {
        throw new Error("Unexpected snapshot in a rejected Patch refresh");
      }
      if (!rejected && (!this.inventoryComplete || !completePage)) {
        throw new Error("Incomplete Patch refresh");
      }
    }
    const frames = [...this.inventory];
    if (this.ledPage) frames.push(this.ledPage);
    frames.push(...this.leds);
    if (rejected) frames.push({ type: "error", code });
    return frames;
  }
}

module.exports = { parseRequest, PatchResponse };
