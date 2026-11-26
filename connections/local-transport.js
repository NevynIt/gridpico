(() => {
  "use strict";

  const protocol = window.location.protocol;
  const documentOrigin = window.location.origin;
  const switchSource = "http://127.0.0.1:8767";
  const switchDestination = "http://192.168.0.223:18768";
  const hasSocketMidi = protocol === "http:" || protocol === "https:";
  const profiles = document.currentScript?.dataset;
  const loopback = protocol === "http:" && window.location.hostname === "127.0.0.1";
  const legacyLan = protocol === "http:" && window.location.host === "192.168.0.223:18768" &&
    profiles?.gridpicoConnection === "trusted-lan";
  const legacyPorts = legacyLan ? Object.freeze({ keybed: 1, panel: 2, led: 3 }) :
    loopback && profiles?.gridpicoProfile === "web-ui" ?
      Object.freeze({ keybed: 1, panel: 2, led: 3, sequence: 4, monitor: 5, log: 6 }) : undefined;
  const midi = window.navigator;
  const roleDirection = new Map([["keybed", "send"], ["panel", "send"], ["led", "receive"],
    ["sequence", "send"], ["monitor", "receive"], ["log", "receive"], ["fm", "receive"]]);
  let capabilityStatus = hasSocketMidi ? "loading" : "offline";
  let capabilityRetry;
  let capabilities;
  let startupPorts;
  let trustedLan = false;
  let target = Object.freeze({ origin: documentOrigin, capabilities: undefined, epoch: 0 });
  const selectedLan = () => trustedLan || (target.origin === switchDestination && target.origin !== documentOrigin);
  const hasWebMidi = () => !selectedLan() && typeof midi?.requestMIDIAccess === "function";
  const availableMidiProviders = () => [
    ...(hasSocketMidi ? ["websocket"] : []),
    ...(hasWebMidi() ? ["web-midi"] : []),
  ];
  const boundPorts = new Set();
  const socketBindings = new Map();
  const handles = new Set();
  let midiAccess;
  let requestingAccess;

  function keysOf(value, names) {
    return value && typeof value === "object" && !Array.isArray(value) &&
      Object.keys(value).length === names.length && names.every(name => Object.hasOwn(value, name));
  }

  function validateCapabilities(value) {
    function requireValid(ok) { if (!ok) throw new Error("Invalid router capabilities"); }
    requireValid(keysOf(value, ["version", "release", "instance", "roles", "autoBind", "actions"]));
    const { version, release, instance, roles, autoBind, actions } = value;
    requireValid(version === 1 && typeof release === "string" && /^[a-f0-9]{64}$/.test(release) &&
      typeof instance === "string" && /^[A-Za-z0-9_-]{1,64}$/.test(instance));
    requireValid(roles && typeof roles === "object" && !Array.isArray(roles) &&
      Object.keys(roles).length <= roleDirection.size);
    const ports = new Set();
    for (const [role, binding] of Object.entries(roles)) {
      requireValid(roleDirection.has(role) && keysOf(binding, ["port", "direction"]) &&
        roleDirection.get(role) === binding.direction && Number.isInteger(binding.port) &&
        binding.port >= 1 && binding.port <= 65535 && !ports.has(binding.port));
      ports.add(binding.port);
      Object.freeze(binding);
    }
    requireValid(Array.isArray(autoBind) && autoBind.length <= 6 && new Set(autoBind).size === autoBind.length &&
      autoBind.every(role => typeof role === "string" && role !== "sequence" && Object.hasOwn(roles, role)));
    const actionNames = ["cli", "ledTest", "recording"];
    if (actions && Object.hasOwn(actions, "picoCdc")) actionNames.push("picoCdc");
    if (actions && Object.hasOwn(actions, "join")) actionNames.push("join");
    requireValid(keysOf(actions, actionNames) &&
      Object.values(actions).every(available => typeof available === "boolean"));
    Object.freeze(roles);
    Object.freeze(autoBind);
    Object.freeze(actions);
    return Object.freeze(value);
  }

  async function readCapabilities(origin, signal) {
    const sameOrigin = origin === location.origin;
    const response = await fetch(new URL("/ui-capabilities.json", origin), {
      cache: "no-store", mode: sameOrigin ? "same-origin" : "cors",
      credentials: sameOrigin ? "same-origin" : "omit", redirect: "error", signal,
    });
    if (response.status === 404) return;
    if (!response.ok || !/^application\/json(?:\s*;|$)/i.test(response.headers.get("Content-Type"))) {
      throw new Error("Router capabilities unavailable");
    }
    const bytes = await readBounded(response, 2048);
    return validateCapabilities(JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes)));
  }

  async function readBounded(response, limit) {
    const reader = response.body.getReader();
    const bytes = new Uint8Array(limit);
    let size = 0;
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) return bytes.subarray(0, size);
        if (size + value.byteLength > limit) throw new Error("Router response exceeds its bound");
        bytes.set(value, size);
        size += value.byteLength;
      }
    } catch (error) {
      void reader.cancel().catch(() => {});
      throw error;
    } finally {
      reader.releaseLock();
    }
  }

  async function verifyFmRelease(description, signal) {
    if (!loopback || !description || description !== capabilities) return false;
    try {
      const [current, response] = await Promise.all([
        readCapabilities(documentOrigin, signal),
        fetch("/web-ui/SHA256SUMS", { cache: "no-store", mode: "same-origin", redirect: "error", signal }),
      ]);
      if (!response.ok || !current || current.instance !== description.instance || current.release !== description.release ||
          current.roles.fm?.port !== description.roles.fm?.port || current.actions.join !== description.actions.join) {
        void response.body?.cancel().catch(() => {});
        return false;
      }
      const bytes = await readBounded(response, 8192);
      const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
      const release = Array.from(digest, byte => byte.toString(16).padStart(2, "0")).join("");
      return description === capabilities && release === description.release;
    } catch {
      return false;
    }
  }

  function verifySameOriginFm(description) {
    return description?.roles.fm ? verifyFmRelease(description, AbortSignal.timeout(5000)) : false;
  }

  function joinError(message, kind) {
    return Object.assign(new Error(message), { kind });
  }

  async function joinFm(options, signal) {
    const owner = target;
    const description = owner.capabilities;
    if (!loopback || location.pathname !== "/fm/" || owner.origin !== documentOrigin ||
        description?.actions.join !== true) throw joinError("FM join is unavailable on this connection.");
    const deadline = AbortSignal.any([signal, AbortSignal.timeout(5000)]);
    if (!(await verifyFmRelease(description, deadline))) {
      deadline.throwIfAborted();
      throw joinError("The selected router could not be verified. Reopen FM Instrument on this router.", "stale");
    }
    deadline.throwIfAborted();
    if (target !== owner) throw joinError("The selected router changed.", "stale");

    const name = "FM Instrument";
    const role = { role: "fm", direction: "receive", capability: "managed" };
    const response = await fetch("/ui-join", {
      method: "POST", mode: "same-origin", credentials: "same-origin", redirect: "error", cache: "no-store",
      headers: { "Content-Type": "application/json" }, signal: deadline,
      body: JSON.stringify({ version: 1, runtime: description.instance, tool: "fm", name, roles: [role] }),
    });
    if (!/^application\/json(?:\s*;|$)/i.test(response.headers.get("Content-Type") ?? "")) {
      void response.body?.cancel().catch(() => {});
      throw joinError("Couldn’t join FM Instrument.");
    }
    const bytes = await readBounded(response, 2048);
    const result = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    deadline.throwIfAborted();
    if (target !== owner) throw joinError("The selected router changed.", "stale");
    if (response.status === 409 && keysOf(result, ["error"]) && result.error === "CAPACITY") {
      throw joinError("Capacity full.", "capacity");
    }
    if (!response.ok) throw joinError("Couldn’t join FM Instrument.");
    if (!keysOf(result, ["version", "runtime", "pageInstance", "name", "cue", "roles", "ticket"]) ||
        result.version !== 1 || result.name !== name ||
        typeof result.pageInstance !== "string" || !/^[0-9a-f]{32}$/.test(result.pageInstance) ||
        typeof result.ticket !== "string" || !/^[0-9a-f]{64}$/.test(result.ticket) ||
        result.pageInstance === result.runtime || !Array.isArray(result.roles) || result.roles.length !== 1) {
      throw joinError("Couldn’t verify the FM Instrument allocation.");
    }
    if (result.runtime !== description.instance) throw joinError("The selected router changed.", "stale");
    const [binding] = result.roles;
    if (!keysOf(binding, ["role", "port", "direction", "capability"]) ||
        binding.role !== role.role || binding.direction !== role.direction || binding.capability !== role.capability ||
        !Number.isInteger(binding.port) || binding.port < 1 || binding.port > 65535 || result.cue !== `ID ${binding.port}`) {
      throw joinError("Couldn’t verify the FM Instrument receive role.");
    }
    const midiProtocol = "gridpico-midi-v1";
    const handle = track({ ...options, direction: "receive" }, callbacks =>
      openBytes(`/midi/${binding.port}`, callbacks, length => length === 3, 128 * 3,
        ({ opened, requestedClose }) => {
          if (requestedClose) return;
          return opened ? "FM connection stale. Rejoin to reconnect." : "Couldn’t join FM Instrument.";
        }, owner.origin, undefined, [midiProtocol, "gridpico-join." + result.ticket]));
    return Object.freeze({ handle, runtime: result.runtime, pageInstance: result.pageInstance, cue: result.cue });
  }

  function initialPorts(description) {
    if (!description.autoBind.length) return;
    return Object.freeze(Object.fromEntries(description.autoBind.map(role => [role, description.roles[role].port])));
  }

  async function discover() {
    if (!hasSocketMidi) return;
    try {
      capabilities = await readCapabilities(location.origin, AbortSignal.timeout(5000));
      if (!capabilities) {
        capabilityStatus = "legacy";
        trustedLan = legacyLan;
        startupPorts = legacyPorts;
        return;
      }
      capabilityStatus = "ready";
      target = Object.freeze({ origin: documentOrigin, capabilities, epoch: 0 });
      trustedLan = protocol === "http:" && !loopback && !capabilities.actions.cli &&
        capabilities.autoBind.includes("panel");
      startupPorts = initialPorts(capabilities);
    } catch {
      capabilityStatus = "unavailable";
    }
  }

  async function requestMidiAccess() {
    if (!hasWebMidi()) throw new Error("Web MIDI is unavailable in this browser or context.");
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

  function openBytes(path, { direction, onData, onState }, frameLimit, bufferLimit, onClosed,
      origin = location.origin, sendChunkSize, protocols) {
    const url = new URL(path, origin);
    url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    const socket = new WebSocket(url, protocols);
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
      if (protocols && socket.protocol !== protocols[0]) {
        close(4000, "Unexpected Gridpico MIDI protocol");
        return;
      }
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
            !(bytes instanceof Uint8Array) || bytes.byteLength === 0 ||
            (!sendChunkSize && !frameLimit(bytes.byteLength))) {
          return false;
        }
        if (socket.bufferedAmount + bytes.byteLength > bufferLimit) {
          close(4000, "Too much queued input");
          return false;
        }
        try {
          const chunkSize = sendChunkSize ?? bytes.byteLength;
          for (let offset = 0; offset < bytes.byteLength; offset += chunkSize) {
            socket.send(bytes.subarray(offset, offset + chunkSize));
          }
          return true;
        } catch {
          close(4000, "Could not send bytes");
          return false;
        }
      },
      close() {
        requestedClose = true;
        close(1000, "Binding closed");
      },
    });
  }

  function track(options, open) {
    const owner = target;
    const entry = { owner, active: true };
    const acceptsCallbacks = () => entry.active && target === owner;
    const handle = open({
      ...options,
      onState(state, message) {
        const report = acceptsCallbacks();
        if (state === "disconnected") {
          entry.active = false;
          handles.delete(entry);
        }
        if (report) options.onState?.(state, message);
      },
      onData(bytes) {
        if (acceptsCallbacks()) options.onData?.(bytes);
      },
    });
    entry.retire = () => {
      entry.active = false;
      handles.delete(entry);
      handle.close();
    };
    handles.add(entry);
    return Object.freeze({
      send(bytes) { return acceptsCallbacks() && handle.send(bytes); },
      close() { handle.close(); },
    });
  }

  function checkRemoteRole(port, options) {
    const binding = target.capabilities?.roles[options.role];
    if (target.origin !== switchDestination || !["keybed", "panel", "led"].includes(options.role) ||
        binding?.port !== port || binding.direction !== options.direction) {
      throw new Error("Select a published APC role on this router.");
    }
  }

  function socketMidi(port, options) {
    const owner = target;
    if (owner.origin !== documentOrigin) checkRemoteRole(port, options);
    if (options.role === "fm" &&
        (!loopback || options.direction !== "receive" || owner.capabilities?.roles.fm?.port !== port)) {
      throw new Error("This router does not publish that FM Instrument input.");
    }
    function reportFailure() {
      if (target === owner && owner.epoch > 0) {
        window.dispatchEvent(new CustomEvent("gridpico-target-bind-failed", { detail: owner }));
      }
    }
    const key = `${owner.origin}/${options.direction}:${port}`;
    if (socketBindings.has(key)) {
      reportFailure();
      throw new Error(`Port ${port} is busy on this page.`);
    }
    const reservation = {};
    try {
      const handle = track(options, callbacks => openBytes(`/midi/${port}`, callbacks,
        length => length === 3, 128 * 3, ({ opened, requestedClose, code }) => {
          if (socketBindings.get(key) === reservation) socketBindings.delete(key);
          if (requestedClose) return;
          if (!opened) {
            reportFailure();
            return `Could not connect to MIDI port ${port}. The port may be busy or unavailable.`;
          }
          if (code !== 1000) return `MIDI port ${port} disconnected.`;
        }, owner.origin));
      socketBindings.set(key, reservation);
      return handle;
    } catch (error) {
      reportFailure();
      throw error;
    }
  }

  function serverOrigin(value, inPlace) {
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
      const local = /^127\.0\.0\.1(?::\d+)?$/.test(match[2]);
      const exactCandidate = inPlace && match[2] === "192.168.0.223:18768" && url.origin === switchDestination;
      if (!local && !exactCandidate && !(trustedLan && url.origin === documentOrigin)) {
        throw new Error("Plain HTTP or WS is available only on 127.0.0.1. Use HTTPS or WSS for a remote server.");
      }
    } else {
      url.protocol = "https:";
    }
    return url.origin;
  }

  async function prepareTarget(origin, signal) {
    const description = await readCapabilities(origin, signal);
    const apcRoles = ["keybed", "panel", "led"];
    if (!description || description.release !== capabilities?.release ||
        !apcRoles.every(role => Object.hasOwn(description.roles, role))) {
      throw new Error("Router release or APC roles could not be verified.");
    }
    if (origin === switchDestination &&
        (Object.keys(description.roles).length !== 3 || description.roles.keybed.port !== 1 ||
          description.roles.panel.port !== 2 || description.roles.led.port !== 3 ||
          !apcRoles.every(role => description.autoBind.includes(role)) ||
          Object.values(description.actions).some(Boolean))) {
      throw new Error("The router did not publish the isolated Web APC profile.");
    }
    return description;
  }

  function commitTarget(origin, description) {
    const previous = target;
    const next = Object.freeze({ origin, capabilities: description, epoch: previous.epoch + 1 });
    const detail = Object.freeze({ previous, target: next });
    window.dispatchEvent(new CustomEvent("gridpico-target-changing", { detail }));
    for (const entry of handles) {
      if (entry.owner === previous) entry.retire();
    }
    startupPorts = initialPorts(description);
    target = next;
    window.dispatchEvent(new CustomEvent("gridpico-target-changed", { detail }));
  }

  function mountServerSelection(container, tool, { inPlace = false, preservePage = false } = {}) {
    if (!["/apc/", "/diagnostics/", "/fm/", "/midi-file/"].includes(tool)) throw new TypeError("Choose a Gridpico tool.");
    const offerSwitch = inPlace && tool === "/apc/" && documentOrigin === switchSource;
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
    label.textContent = offerSwitch ? "Server address" : "MIDI server origin";
    button.id = "midi-server-open";
    button.type = "submit";
    button.textContent = offerSwitch ? "Switch router" : preservePage ? "Choose server" : "Open server";
    status.id = "midi-server-status";
    status.setAttribute("role", "status");
    field.setAttribute("aria-describedby", status.id);
    const actions = document.createElement("div");
    actions.className = "midi-server-actions";
    actions.append(field, button);
    form.append(label, actions, status);
    const fallback = document.createElement("a");
    fallback.id = "midi-server-fallback";
    fallback.hidden = true;
    if (preservePage) {
      fallback.target = "_blank";
      fallback.rel = "noopener";
    }
    if (offerSwitch || preservePage) form.append(fallback);
    function offerNavigation(origin) {
      fallback.href = origin + tool;
      fallback.textContent = `Open Web APC on ${new URL(origin).host}`;
      fallback.hidden = false;
    }
    function showBindingFailure() {
      status.textContent = `Selected ${target.origin}. Some MIDI roles could not connect. Retry a role or open Web APC on that server.`;
      status.setAttribute("data-error", "");
      offerNavigation(target.origin);
    }
    let pending;
    let diagnosticsProbe;
    let failedBindings;
    if (offerSwitch || preservePage) {
      field.addEventListener("input", () => {
        fallback.hidden = true;
        if (preservePage) {
          diagnosticsProbe?.abort();
          status.textContent = "";
          status.removeAttribute("data-error");
        }
      });
    }
    if (preservePage) window.addEventListener("pagehide", () => diagnosticsProbe?.abort());
    if (offerSwitch) {
      window.addEventListener("pagehide", () => pending?.controller.abort());
      window.addEventListener("gridpico-target-bind-failed", event => {
        if (event.detail !== target || pending?.previous === target) return;
        failedBindings = target;
        showBindingFailure();
      });
      window.addEventListener("gridpico-target-roles-ready", event => {
        if (event.detail !== target || failedBindings !== target || pending?.previous === target) return;
        failedBindings = undefined;
        fallback.hidden = true;
        status.removeAttribute("data-error");
        status.textContent = `Selected ${target.origin}.`;
      });
    }
    function publishesDiagnosticsMidi(description) {
      return ["sequence", "monitor", "log"].some(role => Object.hasOwn(description.roles, role));
    }

    async function checkDiagnosticsServer(origin) {
      const unavailable = "This router does not publish Diagnostics MIDI roles to this page.";
      const unverified = "Could not verify Diagnostics MIDI access from this page.";
      status.removeAttribute("data-error");
      if (origin === documentOrigin) {
        status.textContent = trustedLan && capabilities && !publishesDiagnosticsMidi(capabilities) ?
          unavailable : `Already using ${origin}.`;
        return;
      }

      let result = unverified;
      if (documentOrigin === switchSource && origin === switchDestination) {
        const attempt = new AbortController();
        diagnosticsProbe = attempt;
        status.textContent = `Checking MIDI roles on ${origin}…`;
        const signal = AbortSignal.any([attempt.signal, AbortSignal.timeout(5000)]);
        try {
          const description = await readCapabilities(origin, signal);
          if (description && !Object.values(description.actions).some(Boolean) && !publishesDiagnosticsMidi(description)) {
            result = unavailable;
          }
        } catch {
          // A denied or malformed descriptor establishes no MIDI access.
        } finally {
          if (diagnosticsProbe === attempt) diagnosticsProbe = undefined;
        }
        if (attempt.signal.aborted) return;
      }
      status.textContent = result;
      fallback.href = origin + tool;
      fallback.textContent = `Open Diagnostics on ${new URL(origin).host} in a new tab`;
      fallback.hidden = false;
    }

    form.addEventListener("submit", async (event) => {
      event.preventDefault();
      if (pending) return;
      if (preservePage) {
        diagnosticsProbe?.abort();
        fallback.hidden = true;
      }
      let origin;
      try {
        origin = serverOrigin(field.value, offerSwitch || (preservePage && documentOrigin === switchSource));
        const exact = /^(?:http|ws):\/\/(?:127\.0\.0\.1:8767|192\.168\.0\.223:18768)\/?$/i.test(field.value.trim());
        if (offerSwitch && exact) {
          if (origin === target.origin) {
            if (failedBindings === target) showBindingFailure();
            else {
              status.textContent = `Already using ${origin}.`;
              status.removeAttribute("data-error");
            }
            return;
          }
          if (!capabilities) throw new Error("The browser could not verify this page’s router release.");
          const attempt = new AbortController();
          pending = { controller: attempt, previous: target };
          failedBindings = undefined;
          field.disabled = button.disabled = true;
          fallback.hidden = true;
          status.removeAttribute("data-error");
          status.textContent = `Checking router ${origin}…`;
          const signal = AbortSignal.any([attempt.signal, AbortSignal.timeout(5000)]);
          try {
            const description = await prepareTarget(origin, signal);
            signal.throwIfAborted();
            field.value = origin;
            status.textContent = `Selected ${origin}.`;
            commitTarget(origin, description);
          } catch (error) {
            if (!attempt.signal.aborted) throw error;
          }
          return;
        }
        if (preservePage) {
          await checkDiagnosticsServer(origin);
          return;
        }
        status.textContent = `Opening ${origin}…`;
        status.removeAttribute("data-error");
        window.location.assign(origin + tool);
      } catch (error) {
        const switchFailed = offerSwitch && [switchSource, switchDestination].includes(origin);
        status.textContent = switchFailed ?
          `Couldn’t switch to ${origin}. Still using ${target.origin}.` : error.message;
        if (switchFailed) offerNavigation(origin);
        status.setAttribute("data-error", "");
      } finally {
        if (pending) {
          pending = undefined;
          field.disabled = button.disabled = false;
        }
      }
    });
    container.append(form);
  }

  const transport = {
    ready: discover(),
    retryCapabilities() {
      if (capabilityStatus !== "unavailable") return Promise.resolve();
      capabilityRetry ??= discover().then(() => {
        if (capabilityStatus !== "unavailable") window.dispatchEvent(new Event("gridpico-capabilities-updated"));
      }).finally(() => { capabilityRetry = undefined; });
      return capabilityRetry;
    },
    get capabilities() { return capabilities; },
    get capabilityStatus() { return capabilityStatus; },
    get target() { return target; },
    get trustedLan() { return selectedLan(); },
    get startupPorts() { return startupPorts; },
    get midiProviders() { return availableMidiProviders(); },
    requestMidiAccess,
    listMidiPorts,
    verifySameOriginFm,
    joinFm,
    mountServerSelection,
    openMidi(portId, options) {
      if (options?.direction !== "send" && options?.direction !== "receive") {
        throw new TypeError("Choose a valid MIDI port and direction.");
      }
      const provider = options.provider ?? availableMidiProviders()[0];
      if (provider === "web-midi" && target.origin === documentOrigin) {
        return track(options, callbacks => openWebMidi(portId, callbacks));
      }
      if (provider !== "websocket" || !hasSocketMidi) throw new Error("The selected MIDI provider is unavailable.");
      if (!Number.isInteger(portId) || portId < 1 || portId > 65535) {
        throw new TypeError("Choose a valid router MIDI port ID.");
      }
      return socketMidi(portId, options);
    },
  };
  const serialAdmitted = () => loopback && (capabilityStatus === "legacy" || capabilities?.actions.cli) &&
    (location.pathname === "/diagnostics/" || target.origin === documentOrigin);
  function serial(options) {
    if (!serialAdmitted()) throw new Error("The selected router has no local CLI connection.");
    const open = callbacks => openBytes("/serial", callbacks, length => length <= 256, 8192);
    return location.pathname === "/diagnostics/" ? open(options) : track(options, open);
  }
  Object.defineProperty(transport, "openSerial", {
    get() {
      if (serialAdmitted()) return serial;
    },
  });
  const picoCdcAdmitted = () => documentOrigin === switchDestination &&
    location.pathname === "/diagnostics/" && capabilities?.actions.picoCdc === true;
  function openPicoCdc(options) {
    if (!picoCdcAdmitted()) throw new Error("Pico CDC is unavailable on this page.");
    return openBytes("/cdc", options, length => length <= 256, 8192, undefined, documentOrigin, 256);
  }
  Object.defineProperty(transport, "openPicoCdc", {
    get() { return picoCdcAdmitted() ? openPicoCdc : undefined; },
  });
  window.gridpicoLocal = Object.freeze(transport);
})();
