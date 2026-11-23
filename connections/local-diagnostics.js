(() => {
  "use strict";

  const section = document.querySelector("#local-midi");
  const port = document.querySelector("#local-midi-port");
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
  const leaseOff = document.querySelector("#local-midi-lease-off");

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
  let position = 0;

  function hex(bytes) {
    return bytes.map((byte) => byte.toString(16).toUpperCase().padStart(2, "0")).join(" ");
  }

  function validPort(field) {
    return field.validity.valid && Number.isInteger(field.valueAsNumber);
  }

  function renderSteps() {
    const source = validPort(port) ? port.valueAsNumber : "SRC";
    const sink = validPort(destination) ? destination.valueAsNumber : "DST";
    leaseOn.textContent = `route ${source} ${sink} raw on`;
    leaseOff.textContent = `route ${source} ${sink} raw off`;
    steps.replaceChildren();
    for (const [index, frame] of sequence.entries()) {
      const row = steps.insertRow();
      if (index === position) row.setAttribute("aria-current", "step");
      for (const value of [
        index + 1, `TX ${source} → LED RX ${sink}`, hex(frame.bytes),
        frame.dwell ? `${frame.dwell} ms` : "End", frame.effect,
      ]) row.insertCell().textContent = value;
    }
  }

  function updateControls() {
    connect.disabled = Boolean(current);
    port.disabled = Boolean(current);
    disconnect.disabled = !current || current.closing;
    const available = Boolean(current?.ready && !current.closing);
    const sequenceReady = available && validPort(destination) && !document.hidden;
    destination.disabled = Boolean(run);
    solid.disabled = !available || Boolean(run);
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
    if (restart) position = 0;
    status.textContent = message;
    if (current && !current.closing) {
      current.closing = true;
      current.handle.close();
    }
    renderSteps();
    updateControls();
  }

  connect.addEventListener("click", () => {
    if (current || !port.reportValidity()) return;
    const active = { ready: false, closing: false };
    const id = port.valueAsNumber;
    current = active;
    try {
      active.handle = window.gridpicoLocal.openMidi(id, {
        direction: "send",
        onState(state, reason) {
          if (current !== active) return;
          if (state === "disconnected") {
            clearRun();
            current = undefined;
            if (!active.closing) status.textContent = reason || "MIDI disconnected.";
          } else if (state === "connected" && !active.closing) {
            active.ready = true;
            status.textContent = `MIDI port ${id} connected.`;
          } else if (state === "connecting" && !active.closing) {
            status.textContent = `Connecting to MIDI port ${id}…`;
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
      status.textContent = `Queued ${hex(bytes)} on MIDI port ${port.value}.`;
      return true;
    }
    finish("MIDI output could not queue the message. Reconnect MIDI before retrying.");
    return false;
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
      finish(`Queued ${hex(frame.bytes)}. Sequence ended. Reconnect MIDI for another run.`);
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
    if (!current?.ready || current.closing || !validPort(destination) || document.hidden) return;
    const active = { binding: current, automatic, deadline: performance.now() + runLimitMs };
    run = active;
    position = 0;
    active.limitTimer = setTimeout(() => {
      if (run === active) finish("Timed sequence reached 20 seconds. Reconnect MIDI before another run.");
    }, runLimitMs);
    submit(active);
  }

  port.addEventListener("input", renderSteps);
  destination.addEventListener("input", () => { renderSteps(); updateControls(); });
  disconnect.addEventListener("click", () => finish("MIDI disconnected."));
  solid.addEventListener("click", () => queue([0x96, 0, 0x15]));
  off.addEventListener("click", () => queue([0x96, 0, 0]));
  step.addEventListener("click", () => {
    if (run && !run.automatic) submit(run);
    else if (!run) start(false);
  });
  replay.addEventListener("click", () => { if (!run) start(true); });
  stop.addEventListener("click", () => finish("Sequence stopped. Reconnect MIDI before another run."));
  reset.addEventListener("click", () => finish("Sequence reset to step 1.", true));
  document.addEventListener("visibilitychange", () => {
    if (document.hidden && run) finish("Sequence stopped when Diagnostics was hidden.");
    updateControls();
  });
  window.addEventListener("pagehide", () => finish("MIDI disconnected."));

  renderSteps();
  updateControls();
  section.hidden = false;
})();
