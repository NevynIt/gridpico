(() => {
  "use strict";

  const protocol = window.location.protocol;
  const hasSocketMidi = protocol === "http:" || protocol === "https:";
  const profiles = document.currentScript?.dataset;
  const loopback = protocol === "http:" && window.location.hostname === "127.0.0.1";
  const trustedLan = protocol === "http:" && window.location.host === "192.168.0.223:18768" &&
    profiles?.gridpicoConnection === "trusted-lan";
  const startupPorts = trustedLan ? Object.freeze({ keybed: 1, panel: 2, led: 3 }) :
    loopback && profiles?.gridpicoProfile === "web-ui" ?
      Object.freeze({ keybed: 1, panel: 2, led: 3, sequence: 4, monitor: 5, log: 6 }) : undefined;
  const midi = window.navigator;
  const hasWebMidi = !trustedLan && typeof midi?.requestMIDIAccess === "function";
  const midiProviders = Object.freeze([
    ...(hasSocketMidi ? ["websocket"] : []),
    ...(hasWebMidi ? ["web-midi"] : []),
  ]);
  const boundPorts = new Set();
  const socketBindings = new Set();
  let midiAccess;
  let requestingAccess;

  async function requestMidiAccess() {
    if (!hasWebMidi) throw new Error("Web MIDI is unavailable in this browser or context.");
    if (midiAccess) return;
    try {
      requestingAccess ??= midi.requestMIDIAccess({ sysex: false }).then((access) => {
        midiAccess = access;
      }).finally(() => {
        requestingAccess = undefined;
      });
      await requestingAccess;
    } catch (error) {
      if (["NotAllowedError", "SecurityError", "AbortError"].includes(error.name)) {
        throw new Error("Web MIDI permission was denied or blocked by the browser.");
      }
      if (error.name === "NotSupportedError") {
        throw new Error("Web MIDI is unsupported by this browser or system.");
      }
      throw new Error("Could not open Web MIDI access.");
    }
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
    const reservation = `${direction}:${id}`;
    if (boundPorts.has(reservation)) throw new Error(`MIDI port ${port.name || id} is busy on this page.`);
    boundPorts.add(reservation);
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
        boundPorts.delete(reservation);
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

  function openBytes(path, { direction, onData, onState }, frameLimit, bufferLimit, onClosed) {
    const url = new URL(path, window.location.href);
    url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    const socket = new WebSocket(url);
    socket.binaryType = "arraybuffer";
    let closing = false;
    let opened = false;
    let requestedClose = false;

    function close(code, reason) {
      if (closing) return;
      closing = true;
      socket.close(code, reason);
    }
    queueMicrotask(() => { if (!closing) onState?.("connecting"); });
    socket.addEventListener("open", () => {
      if (closing) return;
      opened = true;
      onState?.("connected");
    });
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
      const reason = onClosed?.({ opened, requestedClose, code });
      onState?.("disconnected", reason ?? (code === 1000 || requestedClose ? undefined : "The Gridpico connection closed."));
    }, { once: true });

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
        requestedClose = true;
        close(1000, "Binding closed");
      },
    });
  }

  function serverOrigin(value) {
    const match = /^(https?|wss?):\/\/([^\s\\@/?#]+)\/?$/i.exec(value.trim());
    if (!match) throw new Error("Enter an origin only, for example https://router.example:9443.");
    if (match[2].endsWith(":")) throw new Error("Enter a valid server address and port.");
    let url;
    try { url = new URL(match[0]); } catch {
      throw new Error("Enter a valid server address and port.");
    }
    if (!url.hostname || url.port === "0") throw new Error("Enter a valid server address and port.");
    if (url.protocol === "ws:" || url.protocol === "http:") {
      url.protocol = "http:";
      if (!/^127\.0\.0\.1(?::\d+)?$/.test(match[2]) && !(trustedLan && url.origin === window.location.origin)) {
        throw new Error("Plain HTTP or WS is available only on 127.0.0.1. Use HTTPS or WSS for a remote server.");
      }
    } else {
      url.protocol = "https:";
    }
    return url.origin;
  }

  function mountServerSelection(container, tool) {
    if (!["/apc/", "/diagnostics/", "/fm/"].includes(tool)) throw new TypeError("Choose a Gridpico tool.");
    const form = document.createElement("form");
    const label = document.createElement("label");
    const field = document.createElement("input");
    const button = document.createElement("button");
    const status = document.createElement("p");
    field.id = "midi-server-origin";
    field.autocomplete = "off";
    field.spellcheck = false;
    field.placeholder = "http://127.0.0.1:8767";
    field.value = hasSocketMidi ? window.location.origin : "";
    label.htmlFor = field.id;
    label.textContent = "MIDI server origin";
    button.id = "midi-server-open";
    button.type = "submit";
    button.textContent = "Open server";
    status.id = "midi-server-status";
    status.setAttribute("role", "status");
    field.setAttribute("aria-describedby", status.id);
    const actions = document.createElement("div");
    actions.className = "midi-server-actions";
    actions.append(field, button);
    form.append(label, actions, status);
    form.addEventListener("submit", (event) => {
      event.preventDefault();
      try {
        const origin = serverOrigin(field.value);
        status.textContent = `Opening ${origin}…`;
        status.removeAttribute("data-error");
        window.location.assign(origin + tool);
      } catch (error) {
        status.textContent = error.message;
        status.setAttribute("data-error", "");
      }
    });
    container.append(form);
  }

  const transport = {
    midiProviders,
    trustedLan,
    startupPorts,
    requestMidiAccess,
    listMidiPorts,
    mountServerSelection,
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
      const reservation = `${options.direction}:${portId}`;
      if (socketBindings.has(reservation)) throw new Error(`Port ${portId} is busy on this page.`);
      const handle = openBytes(`/midi/${portId}`, options, (length) => length === 3, 128 * 3,
        ({ opened, requestedClose, code }) => {
          socketBindings.delete(reservation);
          if (requestedClose) return;
          if (!opened) return `Could not connect to MIDI port ${portId}. The port may be busy or unavailable.`;
          if (code !== 1000) return `MIDI port ${portId} disconnected.`;
        });
      socketBindings.add(reservation);
      return handle;
    },
  };
  if (loopback) {
    transport.openSerial = (options) => openBytes("/serial", options, (length) => length <= 256, 8192);
  }
  window.gridpicoLocal = Object.freeze(transport);
})();
