window.addEventListener("DOMContentLoaded", () => {
  "use strict";

  const adapter = window.gridpicoLocal;
  if (!adapter) return;
  const localTool = location.protocol === "http:" && location.hostname === "127.0.0.1";
  if (localTool) {
    const handoff = () => {
      if (location.hash === "#routing") location.replace("/routing/");
    };
    window.addEventListener("hashchange", handoff);
    if (location.hash === "#routing") {
      handoff();
      return;
    }
    const routing = document.querySelector("#routing-navigation");
    routing.querySelector("a").href = "/routing/";
    routing.hidden = false;
    document.querySelector("#diagnostics-home").hidden = false;
  }
  if (location.protocol === "file:") document.querySelector("#diagnostics-apc").href = "INDEX.HTM";
  document.querySelector("#diagnostics-navigation").hidden = false;
  const section = document.querySelector("#local-midi");
  const connection = document.querySelector("#midi-connection");
  const provider = document.querySelector("#local-midi-provider");
  const access = document.querySelector("#local-midi-access");
  const accessStatus = document.querySelector("#midi-access-status");
  const osPort = document.querySelector("#local-midi-os-port");
  const osControls = document.querySelector("#local-midi-web-midi");
  const routerPort = document.querySelector("#local-midi-router-port");
  const port = document.querySelector("#local-midi-port");
  const lease = document.querySelector("#local-midi-lease");
  const destination = document.querySelector("#local-midi-destination");
  const connect = document.querySelector("#local-midi-connect");
  const disconnect = document.querySelector("#local-midi-disconnect");
  const status = document.querySelector("#local-midi-status");
  const solid = document.querySelector("#local-midi-solid");
  const off = document.querySelector("#local-midi-off");
  const step = document.querySelector("#local-midi-step");
  const replay = document.querySelector("#local-midi-replay");
  const stop = document.querySelector("#local-midi-stop");
  const reset = document.querySelector("#local-midi-reset");
  const steps = document.querySelector("#local-midi-steps");
  const leaseOn = document.querySelector("#local-midi-lease-on");
  const leaseRead = document.querySelector("#local-midi-lease-read");
  const leaseOff = document.querySelector("#local-midi-lease-off");
  const routeStatus = document.querySelector("#local-midi-route-status");
  const manualCheck = document.querySelector("#local-midi-manual-check");
  const manualConfirm = document.querySelector("#local-midi-manual-confirm");
  const routerCli = window.gridpicoDiagnosticsCli;

  const runLimitMs = 20000;
  const sequence = [
    { bytes: [0x9a, 0x00, 0x15], dwell: 100, effect: "Pad 00 pulses green every 1,000 ms" },
    { bytes: [0x9e, 0x01, 0x2d], dwell: 1200, effect: "Pad 01 blinks blue every 500 ms" },
    { bytes: [0x96, 0x00, 0x05], dwell: 100, effect: "Pad 00 becomes solid red" },
    { bytes: [0x96, 0x01, 0x00], dwell: 100, effect: "Pad 01 turns off" },
    { bytes: [0x96, 0x00, 0x00], dwell: 0, effect: "Pad 00 turns off" },
  ];

  let current;
  let run;
  let checking;
  let position = 0;
  let requesting = false;
  let accessGranted = false;

  adapter.mountServerSelection(connection, "/diagnostics/");
  connection.hidden = false;
  if (!adapter.midiProviders.length) {
    provider.disabled = true;
    accessStatus.hidden = false;
    accessStatus.textContent = "No MIDI provider is available in this browser or context.";
    return;
  }
  function addProviders(select) {
    for (const id of adapter.midiProviders) {
      select.add(new Option(id === "web-midi" ? "Web MIDI" : "WebSocket", id));
    }
  }
  addProviders(provider);

  function directMidi() {
    return provider.value === "web-midi";
  }

  function refreshPorts() {
    const previous = osPort.value;
    osPort.replaceChildren(new Option("Choose a MIDI output", ""));
    const outputs = adapter.listMidiPorts("send");
    for (const { id, name } of outputs) osPort.add(new Option(name, id));
    if (outputs.some(({ id }) => id === previous)) osPort.value = previous;
    return outputs.length;
  }

  function hex(bytes) {
    return Array.from(bytes, (byte) => byte.toString(16).toUpperCase().padStart(2, "0")).join(" ");
  }

  function validPort(field) {
    return field.validity.valid && Number.isInteger(field.valueAsNumber);
  }

  function noteMessage(bytes) {
    const kind = bytes[0] & 0xf0;
    if (bytes.length !== 3 || (kind !== 0x80 && kind !== 0x90) || bytes[1] > 127 || bytes[2] > 127) return;
    return { on: kind === 0x90 && bytes[2] !== 0, channel: (bytes[0] & 0x0f) + 1, note: bytes[1], velocity: bytes[2] };
  }

  function mountMonitor() {
    const toggle = document.querySelector("#monitor-audio");
    const state = document.querySelector("#monitor-audio-status");
    const volume = document.querySelector("#monitor-volume");
    const filter = document.querySelector("#monitor-channel-filter");
    const choices = document.querySelector("#monitor-channels");
    const sounds = Array(16).fill("sine");
    const voices = new Set();
    const maxVoices = 25;
    let audio;

    function remove(voice) {
      if (!voices.delete(voice)) return;
      voice.oscillator.disconnect();
      voice.envelope.disconnect();
    }

    function stopVoice(voice) {
      remove(voice);
      voice.oscillator.stop();
    }

    function release(voice) {
      if (voice.releasing) return;
      voice.releasing = true;
      const now = audio.context.currentTime;
      voice.envelope.gain.setTargetAtTime(0, now, 0.006);
      voice.oscillator.stop(now + 0.035);
    }

    function silence() {
      for (const voice of voices) stopVoice(voice);
    }

    function audioState(on, message) {
      toggle.textContent = on ? "Disable audio" : "Enable audio";
      state.textContent = message;
    }

    function disable(message = "Audio off.") {
      silence();
      const context = audio?.context;
      audio = undefined;
      if (context && context.state !== "closed") void context.close().catch(() => {});
      audioState(false, message);
    }

    toggle.addEventListener("click", async () => {
      if (audio) {
        disable();
        return;
      }
      let context;
      try {
        context = new AudioContext();
        const active = { context, master: undefined, ready: false };
        audio = active;
        active.master = context.createGain();
        active.master.gain.value = Number(volume.value) / 100;
        active.master.connect(context.destination);
        audioState(true, "Starting audio…");
        await context.resume();
        if (audio !== active) return;
        active.ready = true;
        context.addEventListener("statechange", () => {
          if (audio === active && context.state !== "running") silence();
        });
        audioState(true, "Audio on.");
      } catch {
        if (audio?.context === context) disable("Audio unavailable.");
        else if (context && context.state !== "closed") void context.close().catch(() => {});
      }
    });
    volume.addEventListener("input", () => {
      document.querySelector("#monitor-volume-display").textContent = `${volume.value}%`;
      if (audio) audio.master.gain.setTargetAtTime(Number(volume.value) / 100, audio.context.currentTime, 0.01);
    });
    filter.addEventListener("change", () => {
      if (!filter.value) return;
      for (const voice of voices) {
        if (voice.channel !== Number(filter.value)) stopVoice(voice);
      }
    });

    for (let channel = 1; channel <= sounds.length; channel++) {
      filter.add(new Option(`MIDI ${channel}`, String(channel)));
      const label = document.createElement("label");
      const choice = document.createElement("select");
      choice.setAttribute("aria-label", `Channel ${channel} sound`);
      for (const [name, value] of [["Sine", "sine"], ["Triangle", "triangle"], ["Square", "square"], ["Mute", "mute"]]) {
        choice.add(new Option(name, value));
      }
      choice.addEventListener("change", () => {
        sounds[channel - 1] = choice.value;
        if (choice.value === "mute") {
          for (const voice of voices) {
            if (voice.channel === channel) release(voice);
          }
        }
      });
      label.append(String(channel).padStart(2, "0"), choice);
      choices.append(label);
    }

    function receive(bytes) {
      const message = noteMessage(bytes);
      if (!message || !audio?.ready || (filter.value && Number(filter.value) !== message.channel)) return;
      const { on, channel, note, velocity } = message;
      const held = [...voices].find(voice => !voice.releasing && voice.channel === channel && voice.note === note);
      if (!on) {
        if (held) release(held);
        return;
      }
      const { context, master } = audio;
      const sound = sounds[channel - 1];
      if (context.state !== "running" || sound === "mute") return;
      if (held) stopVoice(held);
      if (voices.size === maxVoices) stopVoice([...voices].find(voice => voice.releasing) ?? voices.values().next().value);
      const oscillator = context.createOscillator();
      const envelope = context.createGain();
      const voice = { channel, note, oscillator, envelope, releasing: false };
      oscillator.type = sound;
      oscillator.frequency.value = 440 * 2 ** ((note - 69) / 12);
      envelope.gain.value = 0;
      oscillator.connect(envelope);
      envelope.connect(master);
      oscillator.onended = () => remove(voice);
      oscillator.start();
      voices.add(voice);
      envelope.gain.setTargetAtTime(0.04 * velocity / 127, context.currentTime, 0.004);
    }
    return { receive, silence, disable };
  }

  function mountReceiver(role, receive, lost) {
    const root = document.querySelector(`#${role}-binding`);
    const select = root.querySelector(`#${role}-provider`);
    const field = root.querySelector(`#${role}-port`);
    const ports = root.querySelector(`#${role}-system-port`);
    const label = root.querySelector(`label[for="${role}-port"]`);
    const bind = root.querySelector("[data-bind]");
    const unbind = root.querySelector("[data-disconnect]");
    const indicator = root.querySelector("[role=status]");
    let currentBinding;
    addProviders(select);
    const isWebMidi = () => select.value === "web-midi";

    function report(state, message) {
      indicator.dataset.state = state;
      indicator.textContent = message;
      indicator.toggleAttribute("data-error", state === "error");
      bind.textContent = state === "error" ? "Retry" : "Connect MIDI";
    }

    function refresh() {
      const direct = isWebMidi();
      const previous = ports.value;
      const inputs = direct ? adapter.listMidiPorts("receive") : [];
      if (direct) {
        ports.replaceChildren(new Option(accessGranted && !inputs.length ? "No eligible MIDI inputs" : "Choose a MIDI input", ""));
        for (const { id, name } of inputs) ports.add(new Option(name, id));
        if (inputs.some(({ id }) => id === previous)) ports.value = previous;
      }
      label.htmlFor = direct ? ports.id : field.id;
      field.hidden = direct;
      ports.hidden = !direct;
      field.disabled = ports.disabled = Boolean(currentBinding);
      bind.disabled = Boolean(currentBinding || (direct && (!ports.value || requesting)));
      unbind.disabled = !currentBinding;
    }

    function retire(reason) {
      if (!currentBinding) return;
      const previous = currentBinding;
      currentBinding = undefined;
      previous.handle?.close();
      if (previous.direct) ports.value = "";
      lost?.();
      report(reason ? "error" : "disconnected", reason || "Disconnected.");
      refresh();
    }

    function open() {
      const direct = isWebMidi();
      if (currentBinding || (direct ? !ports.value : !field.reportValidity())) return;
      const id = direct ? ports.value : field.valueAsNumber;
      const name = direct ? ports.selectedOptions[0].text : `MIDI port ${id}`;
      const active = { ready: false, direct };
      currentBinding = active;
      report("connecting", `Connecting to ${name}…`);
      try {
        active.handle = adapter.openMidi(id, {
          provider: select.value,
          direction: "receive",
          onData(bytes) {
            if (currentBinding === active && active.ready) receive(bytes, name);
          },
          onState(state, reason) {
            if (currentBinding !== active) return;
            if (state === "connected") {
              active.ready = true;
              report("connected", `${name} connected.`);
            } else if (state === "disconnected") {
              retire(reason);
            }
          },
        });
      } catch (error) {
        retire(error.message);
      }
      refresh();
    }

    bind.addEventListener("click", open);
    unbind.addEventListener("click", () => retire());
    select.addEventListener("change", () => {
      retire();
      ports.value = "";
      refresh();
      updateMidiAccess();
    });
    ports.addEventListener("change", () => { bind.disabled = !ports.value; });
    const defaultPort = adapter.startupPorts?.[role];
    if (defaultPort) {
      field.value = defaultPort;
      open();
    }
    refresh();
    return { refresh, retire, isWebMidi };
  }

  const monitor = mountMonitor();
  const log = document.querySelector("#log-rows");
  const receivers = [
    mountReceiver("monitor", (bytes) => {
      try { monitor.receive(bytes); } catch { monitor.disable("Audio unavailable."); }
    }, monitor.silence),
    mountReceiver("log", (bytes, source) => {
      const note = noteMessage(bytes);
      const summary = note ? `${note.on ? "Note On" : "Note Off"} · Ch ${note.channel} · ${note.note} · Vel ${note.velocity}` :
        (bytes[0] & 0xf0) === 0xb0 && bytes.length === 3 ? `CC ${bytes[1]} · Ch ${(bytes[0] & 0x0f) + 1} · ${bytes[2]}` : "MIDI";
      const row = log.insertRow();
      for (const cell of [source, performance.now().toFixed(1), hex(bytes), summary]) row.insertCell().textContent = cell;
      if (log.rows.length > 100) log.deleteRow(0);
      const scroller = log.closest(".receipts");
      scroller.scrollTop = scroller.scrollHeight;
    }),
  ];
  document.querySelector("#log-clear").addEventListener("click", () => log.replaceChildren());
  if (adapter.startupPorts) {
    port.value = adapter.startupPorts.sequence;
    destination.value = adapter.startupPorts.led;
  }

  function updateMidiAccess() {
    access.hidden = accessStatus.hidden = !directMidi() && !receivers.some(receiver => receiver.isWebMidi());
    access.disabled = requesting;
  }

  function renderSteps() {
    const source = validPort(port) ? port.valueAsNumber : "SRC";
    const sink = validPort(destination) ? destination.valueAsNumber : "DST";
    leaseOn.textContent = `route ${source} ${sink} raw on`;
    leaseRead.textContent = `route ${source} ${sink} raw`;
    leaseOff.textContent = `route ${source} ${sink} raw off`;
    const direction = directMidi()
      ? osPort.selectedOptions[0]?.text || "MIDI output"
      : `TX ${source} → LED RX ${sink}`;
    steps.replaceChildren();
    for (const [index, frame] of sequence.entries()) {
      const row = steps.insertRow();
      if (index === position) row.setAttribute("aria-current", "step");
      for (const value of [
        index + 1, direction, hex(frame.bytes),
        frame.dwell ? `${frame.dwell} ms` : "End", frame.effect,
      ]) row.insertCell().textContent = value;
    }
  }

  function clearRouteCheck() {
    checking = undefined;
    manualConfirm.checked = false;
    routeStatus.textContent = "Raw route not verified here.";
  }

  function updateControls() {
    const direct = directMidi();
    updateMidiAccess();
    osControls.hidden = !direct;
    routerPort.hidden = direct;
    lease.hidden = direct;
    manualCheck.hidden = Boolean(routerCli?.loopback);
    manualConfirm.disabled = Boolean(run || checking);
    provider.disabled = requesting;
    osPort.disabled = Boolean(current || requesting);
    connect.disabled = Boolean(current || requesting || (direct && !osPort.value));
    port.disabled = Boolean(current);
    disconnect.disabled = !current || current.closing;
    const available = Boolean(current?.ready && !current.closing);
    const readyToSend = available && !checking &&
      (direct || (validPort(destination) && (run || routerCli?.loopback || manualConfirm.checked)));
    const sequenceReady = readyToSend && !document.hidden;
    destination.disabled = Boolean(run || checking);
    solid.disabled = !readyToSend || Boolean(run);
    off.disabled = solid.disabled;
    step.disabled = !sequenceReady || Boolean(run?.automatic);
    replay.disabled = !sequenceReady || Boolean(run);
    stop.disabled = !available;
  }

  function clearRun() {
    if (!run) return;
    clearTimeout(run.timer);
    clearTimeout(run.limitTimer);
    run = undefined;
  }

  function finish(message, restart = false) {
    clearRun();
    clearRouteCheck();
    if (restart) position = 0;
    status.textContent = message;
    if (current && !current.closing) {
      current.closing = true;
      current.handle.close();
    }
    renderSteps();
    updateControls();
  }

  provider.addEventListener("change", () => {
    finish("LED Sequence MIDI disconnected.", true);
    osPort.value = "";
    if (directMidi()) refreshPorts();
    renderSteps();
    updateControls();
  });
  access.addEventListener("click", async () => {
    requesting = true;
    accessStatus.textContent = "Requesting Web MIDI permission…";
    updateControls();
    try {
      await adapter.requestMidiAccess();
      accessGranted = true;
      access.textContent = "Refresh Web MIDI ports";
      const outputs = refreshPorts();
      const inputs = adapter.listMidiPorts("receive").length;
      accessStatus.textContent = inputs || outputs ? "Choose a compatible MIDI port." : "No eligible MIDI ports are connected.";
      if (directMidi() && !current) status.textContent = outputs ? "Choose a MIDI output." : "No eligible MIDI outputs are connected.";
    } catch (error) {
      accessStatus.textContent = error.message;
    } finally {
      requesting = false;
      for (const receiver of receivers) receiver.refresh();
      renderSteps();
      updateControls();
    }
  });
  osPort.addEventListener("change", () => { renderSteps(); updateControls(); });

  connect.addEventListener("click", () => {
    const direct = directMidi();
    if (current || !(direct ? osPort.reportValidity() : port.reportValidity())) return;
    const id = direct ? osPort.value : port.valueAsNumber;
    const name = direct ? osPort.selectedOptions[0].text : `MIDI port ${id}`;
    const active = { ready: false, closing: false, name, direct };
    current = active;
    clearRouteCheck();
    try {
      active.handle = adapter.openMidi(id, {
        provider: provider.value,
        direction: "send",
        onState(state, reason) {
          if (current !== active) return;
          if (state === "disconnected") {
            clearRun();
            clearRouteCheck();
            current = undefined;
            if (active.direct) {
              osPort.value = "";
              refreshPorts();
            }
            if (!active.closing) status.textContent = reason || "MIDI disconnected.";
          } else if (state === "connected" && !active.closing) {
            active.ready = true;
            status.textContent = `${name} connected.`;
          } else if (state === "connecting" && !active.closing) {
            status.textContent = `Connecting to ${name}…`;
          }
          updateControls();
        },
      });
    } catch (error) {
      current = undefined;
      status.textContent = error.message;
    }
    updateControls();
  });

  function queue(bytes) {
    if (!current?.ready || current.closing) return false;
    if (current.handle.send(Uint8Array.from(bytes))) {
      status.textContent = `Queued locally on ${current.name}: ${hex(bytes)}. Check the APC pad for delivery.`;
      return true;
    }
    finish("MIDI output could not queue the message. Reconnect MIDI before retrying.");
    return false;
  }

  async function withRouteCheck(send) {
    const binding = current;
    if (!binding?.ready || binding.closing || checking) return;
    if (directMidi()) {
      send();
      return;
    }
    if (!validPort(port) || !validPort(destination)) return;
    if (!routerCli?.loopback) {
      if (manualConfirm.checked) send();
      return;
    }

    const source = port.valueAsNumber;
    const sink = destination.valueAsNumber;
    const attempt = { binding, source, sink };
    checking = attempt;
    routeStatus.textContent = `Checking Raw route ${source} → ${sink} in the local CLI…`;
    updateControls();
    try {
      const state = await routerCli.checkRawRoute(source, sink);
      if (checking !== attempt || current !== binding) return;
      if (state === "ACTIVE") {
        routeStatus.textContent = `Last CLI check: Raw route ${source} → ${sink} Active. Delivery unverified.`;
        send();
      } else {
        const label = state === "OFF" ? "Off" : "Pending";
        status.textContent = `Raw route ${source} → ${sink} is ${label}. Nothing was sent. Connect both ports and use ${leaseOn.textContent}.`;
        routeStatus.textContent = `Last CLI check: Raw route ${source} → ${sink} ${label}.`;
      }
    } catch (error) {
      if (checking !== attempt || current !== binding) return;
      routeStatus.textContent = "Raw route not verified here.";
      status.textContent = `${error.message} Nothing was sent. Check ${leaseRead.textContent} in the router CLI.`;
    } finally {
      if (checking === attempt) {
        checking = undefined;
        updateControls();
      }
    }
  }

  function submit(active) {
    if (run !== active || current !== active.binding || current.closing) return;
    const now = performance.now();
    if (document.hidden || now >= active.deadline) {
      finish("Timed sequence stopped. Reconnect MIDI before another run.");
      return;
    }
    if (active.nextAt > now) {
      active.timer = setTimeout(() => submit(active), active.nextAt - now);
      return;
    }
    const frame = sequence[position];
    if (!queue(frame.bytes)) return;
    position++;
    if (position === sequence.length) {
      finish(`Queued locally: ${hex(frame.bytes)}. Sequence ended. Check the APC pads for delivery. Reconnect MIDI for another run.`);
      return;
    }
    if (active.automatic) {
      active.nextAt = performance.now() + frame.dwell;
      active.timer = setTimeout(() => submit(active), frame.dwell);
    }
    renderSteps();
    updateControls();
  }

  function start(automatic) {
    if (!current?.ready || current.closing || (!directMidi() && !validPort(destination)) || document.hidden) return;
    const active = { binding: current, automatic, deadline: performance.now() + runLimitMs };
    run = active;
    position = 0;
    active.limitTimer = setTimeout(() => {
      if (run === active) finish("Timed sequence reached 20 seconds. Reconnect MIDI before another run.");
    }, runLimitMs);
    submit(active);
  }

  function changedRoute() {
    clearRouteCheck();
    renderSteps();
    updateControls();
  }
  port.addEventListener("input", changedRoute);
  destination.addEventListener("input", changedRoute);
  manualConfirm.addEventListener("change", updateControls);
  window.addEventListener("gridpico-local-cli-binding", () => {
    clearRouteCheck();
    updateControls();
  });
  disconnect.addEventListener("click", () => finish("MIDI disconnected."));
  solid.addEventListener("click", () => { void withRouteCheck(() => queue([0x96, 0, 0x15])); });
  off.addEventListener("click", () => { void withRouteCheck(() => queue([0x96, 0, 0])); });
  step.addEventListener("click", () => {
    if (run && !run.automatic) submit(run);
    else if (!run) void withRouteCheck(() => start(false));
  });
  replay.addEventListener("click", () => { if (!run) void withRouteCheck(() => start(true)); });
  stop.addEventListener("click", () => finish("Sequence stopped. Reconnect MIDI before another run."));
  reset.addEventListener("click", () => finish("Sequence reset to step 1.", true));
  document.addEventListener("visibilitychange", () => {
    if (document.hidden && run) finish("Sequence stopped when Diagnostics was hidden.");
    else if (document.hidden && checking) clearRouteCheck();
    updateControls();
  });
  window.addEventListener("pagehide", () => {
    finish("MIDI disconnected.");
    for (const receiver of receivers) receiver.retire();
    monitor.disable();
  });

  renderSteps();
  updateControls();
  document.querySelector("#midi-signals").hidden = false;
  section.hidden = false;
});
