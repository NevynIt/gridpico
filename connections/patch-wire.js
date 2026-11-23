"use strict";

const monitorPort = 6;
const maxGeneration = 0xffffffff;
const rows = 5;
const columns = 8;
const ledCount = rows * columns;
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

function isNote(event, maxChannel = 1) {
  return event && (event.type === "note-on" || event.type === "note-off") &&
    integer(event.channel, 1, maxChannel) && integer(event.note, 0, 127) &&
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
  if ((event.type === "monitor-subscribe" || event.type === "monitor-unsubscribe") && fields === 1) {
    return { kind: "monitor", attached: event.type === "monitor-subscribe" };
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

function parseInventoryBegin([revision, devices, ports], monitorProfile) {
  const deviceCount = decimal(devices, 0, monitorProfile ? 3 : 2);
  const portCount = decimal(ports, 0, monitorProfile ? 6 : 5);
  if (deviceCount === undefined || portCount === undefined ||
      !/^(0|[1-9][0-9]{0,19})$/.test(revision) || BigInt(revision) > maxRevision) {
    throw new Error("Invalid inventory header");
  }
  return { type: "patch-inventory-begin", revision, deviceCount, portCount };
}

class PatchResponse {
  constructor(kind, { monitorProfile = false, registeredOutputs, monitorChange } = {}) {
    this.kind = kind;
    this.refresh = kind === "startup" || kind === "refresh";
    this.monitorProfile = monitorProfile;
    this.registeredOutputs = registeredOutputs;
    this.monitorChange = monitorChange;
    this.monitorFrame = undefined;
    this.inventory = [];
    this.devices = new Set();
    this.ports = new Set();
    this.outputPorts = new Set();
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
      this.inventory.push(parseInventoryBegin(values, this.monitorProfile));
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
      if (direction === "out") this.outputPorts.add(id);
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
    const record = fields[0];
    if (record === "monitor-state") return this.addMonitorState(fields);
    if (record === "on" || record === "off") return this.addCapture(fields);
    if ((this.refresh || this.monitorFrame) && !this.inventoryComplete) {
      this.addInventory(fields);
      return;
    }

    const [, first, second, third] = fields;
    if (this.monitorChange && !this.inventoryComplete) {
      throw new Error("Missing Monitor state or inventory");
    }
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
    throw new Error("Unexpected fixture record");
  }

  addMonitorState([, port, generation, attached, ...extra]) {
    const expected = this.monitorChange;
    if (!expected || this.monitorFrame || this.captureCount || this.inventory.length || extra.length ||
        port !== String(monitorPort) || generation !== String(expected.generation) ||
        attached !== (expected.attached ? "1" : "0")) {
      throw new Error("Invalid or out-of-order Monitor state");
    }
    this.monitorFrame = { type: "patch-monitor", portId: monitorPort, ...expected };
    return this.monitorFrame;
  }

  addCapture([record, ...values]) {
    if (this.refresh || this.inventory.length || this.ledPage || this.leds.length ||
        this.captureCount === (this.monitorProfile ? 75 : 50) ||
        ((this.kind === "surface" || this.monitorChange) && record !== "off")) {
      throw new Error("Unexpected capture record");
    }
    if (values.length !== (this.monitorProfile ? 5 : 3)) {
      throw new Error("Invalid capture framing");
    }
    const [channel, note, velocity] = values.slice(-3);
    const event = {
      type: `note-${record}`, channel: decimal(channel, 1, this.monitorProfile ? 16 : 1),
      note: decimal(note, 0, 127), velocity: decimal(velocity, 0, 127),
    };
    if (!isNote(event, this.monitorProfile ? 16 : 1)) throw new Error("Invalid capture note");
    if (this.monitorProfile) {
      const destinationPort = decimal(values[0], 1, 65535);
      const destinationGeneration = decimal(values[1], 1, maxGeneration);
      if (!this.registeredOutputs?.has(destinationPort) || destinationGeneration === undefined ||
          (this.monitorChange && (!this.monitorFrame?.attached || destinationPort !== monitorPort ||
            destinationGeneration !== this.monitorFrame.generation))) {
        throw new Error("Invalid capture destination");
      }
      Object.assign(event, { destinationPort, destinationGeneration });
    }
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
    if (this.monitorChange) {
      const snapshotStarted = this.inventory.length > 0 || this.ledPage || this.leds.length > 0;
      if (this.monitorFrame && (!rejected || snapshotStarted) && (!this.inventoryComplete || !completePage)) {
        throw new Error("Incomplete Monitor snapshot");
      }
      if (!this.monitorFrame && !rejected) throw new Error("Missing Monitor state");
    }
    if (this.monitorProfile && this.inventoryComplete) {
      const port = this.inventory.find((record) => record.type === "patch-port" && record.id === monitorPort);
      if (!port || port.deviceId !== 3 || port.direction !== "out" || !port.eligible || port.slot !== 3 ||
          (this.kind === "startup" && port.state !== "disconnected") || (this.monitorFrame &&
            port.state !== (this.monitorFrame.attached ? "connected" : "disconnected"))) {
        throw new Error("Invalid Monitor inventory port");
      }
    }
    const frames = [...this.inventory];
    if (this.ledPage) frames.push(this.ledPage);
    frames.push(...this.leds);
    if (rejected) frames.push({ type: "error", code });
    return frames;
  }
}

module.exports = { maxGeneration, parseRequest, PatchResponse };
