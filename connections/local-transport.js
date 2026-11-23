(() => {
  "use strict";

  const protocol = window.location.protocol;
  const hasSocketMidi = protocol === "http:" || protocol === "https:";
  const midi = window.navigator;
  const hasWebMidi = typeof midi?.requestMIDIAccess === "function";
  const midiProviders = Object.freeze([
    ...(hasSocketMidi ? ["websocket"] : []),
    ...(hasWebMidi ? ["web-midi"] : []),
  ]);
  const boundPorts = new Set();
  let midiAccess;
  let requestingAccess;

  async function requestMidiAccess() {
    if (!hasWebMidi) throw new Error("Web MIDI is unavailable in this browser or context.");
    if (midiAccess) return;
    requestingAccess ??= midi.requestMIDIAccess({ sysex: false }).then((access) => {
      midiAccess = access;
    }).finally(() => {
      requestingAccess = undefined;
    });
    await requestingAccess;
  }

  function portsFor(direction) {
    if (direction === "send") return midiAccess?.outputs;
    if (direction === "receive") return midiAccess?.inputs;
    throw new TypeError("Choose a MIDI direction.");
  }

  function listMidiPorts(direction) {
    const compatible = portsFor(direction);
    const ports = [];
    for (const port of compatible?.values() ?? []) {
      if (port.state === "connected") ports.push({ id: port.id, name: port.name || port.id });
    }
    return ports;
  }

  function completeMessage(bytes) {
    if (!(bytes instanceof Uint8Array) || bytes.length < 1 || bytes.length > 3) return false;
    const status = bytes[0];
    if (status < 0x80 || bytes[1] >= 0x80 || bytes[2] >= 0x80) return false;
    if (status < 0xf0) return bytes.length === (status >= 0xc0 && status < 0xe0 ? 2 : 3);
    switch (status) {
      case 0xf1:
      case 0xf3:
        return bytes.length === 2;
      case 0xf2:
        return bytes.length === 3;
      case 0xf6:
      case 0xf8:
      case 0xfa:
      case 0xfb:
      case 0xfc:
      case 0xfe:
      case 0xff:
        return bytes.length === 1;
      default:
        return false;
    }
  }

  function openWebMidi(id, { direction, onData, onState }) {
    if (!midiAccess) throw new Error("Allow Web MIDI access before choosing a port.");
    const port = typeof id === "string" && portsFor(direction).get(id);
    if (!port || port.state !== "connected") throw new Error("The selected Web MIDI port is unavailable.");
    if (boundPorts.has(port)) throw new Error("The selected Web MIDI port is already in use on this page.");
    boundPorts.add(port);
    let connected = false;
    let retired = false;

    function close(reason) {
      if (retired) return;
      retired = true;
      connected = false;
      port.removeEventListener("statechange", onPortState);
      if (direction === "receive") port.removeEventListener("midimessage", receive);
      // An open already in progress must settle before releasing this port to a new binding.
      void opening.then(() => port.close()).catch(() => {}).finally(() => {
        boundPorts.delete(port);
      });
      queueMicrotask(() => onState?.("disconnected", reason));
    }

    function onPortState() {
      if (port.state !== "connected" || (connected && port.connection !== "open")) {
        close("The selected Web MIDI port disconnected.");
      }
    }

    function receive({ data }) {
      if (connected && completeMessage(data)) onData?.(new Uint8Array(data));
    }

    port.addEventListener("statechange", onPortState);
    const opening = Promise.resolve().then(async () => {
      if (retired) return;
      onState?.("connecting");
      if (retired) return;
      await port.open();
      if (retired) return;
      if (port.state !== "connected" || port.connection !== "open") {
        close("The selected Web MIDI port is unavailable.");
        return;
      }
      connected = true;
      if (direction === "receive") port.addEventListener("midimessage", receive);
      onState?.("connected");
    }).catch(() => close("Could not open the selected Web MIDI port."));

    return Object.freeze({
      send(bytes) {
        if (direction !== "send" || !connected || !completeMessage(bytes)) return false;
        if (port.state !== "connected" || port.connection !== "open") {
          close("The selected Web MIDI port disconnected.");
          return false;
        }
        try {
          port.send(bytes);
          return true;
        } catch {
          close("Could not send to the selected Web MIDI port.");
          return false;
        }
      },
      close() { close(); },
    });
  }

  function openBytes(path, { direction, onData, onState }, frameLimit, bufferLimit) {
    const url = new URL(path, window.location.href);
    url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    const socket = new WebSocket(url);
    socket.binaryType = "arraybuffer";
    let closing = false;

    function close(code, reason) {
      if (closing) return;
      closing = true;
      socket.close(code, reason);
    }
    queueMicrotask(() => { if (!closing) onState?.("connecting"); });
    socket.addEventListener("open", () => { if (!closing) onState?.("connected"); });
    socket.addEventListener("message", ({ data }) => {
      if (closing) return;
      if (direction === "send" || !(data instanceof ArrayBuffer) ||
          data.byteLength === 0 || !frameLimit(data.byteLength)) {
        close(4000, "Invalid byte frame");
        return;
      }
      onData?.(new Uint8Array(data));
    });
    socket.addEventListener("close", ({ code }) => {
      closing = true;
      onState?.("disconnected", code === 1000 ? undefined : "The Gridpico connection closed.");
    });

    return Object.freeze({
      send(bytes) {
        if (closing || direction === "receive" || socket.readyState !== WebSocket.OPEN ||
            !(bytes instanceof Uint8Array) || bytes.byteLength === 0 || !frameLimit(bytes.byteLength)) {
          return false;
        }
        if (socket.bufferedAmount + bytes.byteLength > bufferLimit) {
          close(4000, "Too much queued input");
          return false;
        }
        socket.send(bytes);
        return true;
      },
      close() {
        close(1000, "Binding closed");
      },
    });
  }

  const transport = {
    midiProviders,
    requestMidiAccess,
    listMidiPorts,
    openMidi(portId, options) {
      if (options?.direction !== "send" && options?.direction !== "receive") {
        throw new TypeError("Choose a valid MIDI port and direction.");
      }
      const provider = options.provider ?? midiProviders[0];
      if (provider === "web-midi") return openWebMidi(portId, options);
      if (provider !== "websocket" || !hasSocketMidi) throw new Error("The selected MIDI provider is unavailable.");
      if (!Number.isInteger(portId) || portId < 1 || portId > 65535) {
        throw new TypeError("Choose a valid router MIDI port ID.");
      }
      return openBytes(`/midi/${portId}`, options, (length) => length === 3, 128 * 3);
    },
  };
  if (protocol === "http:") {
    transport.openSerial = (options) => openBytes("/serial", options, (length) => length <= 256, 8192);
  }
  window.gridpicoLocal = Object.freeze(transport);
})();
