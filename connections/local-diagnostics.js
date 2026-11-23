(() => {
  "use strict";

  const section = document.querySelector("#local-midi");
  const port = document.querySelector("#local-midi-port");
  const connect = document.querySelector("#local-midi-connect");
  const disconnect = document.querySelector("#local-midi-disconnect");
  const status = document.querySelector("#local-midi-status");
  const solid = document.querySelector("#local-midi-solid");
  const off = document.querySelector("#local-midi-off");
  let current;

  function updateControls() {
    connect.disabled = Boolean(current);
    port.disabled = Boolean(current);
    disconnect.disabled = !current || current.closing;
    solid.disabled = !current?.ready || current.closing;
    off.disabled = solid.disabled;
  }

  function close() {
    if (!current || current.closing) return;
    current.closing = true;
    current.handle.close();
    updateControls();
  }

  connect.addEventListener("click", () => {
    if (current) return;
    const active = { ready: false, closing: false };
    const id = port.valueAsNumber;
    current = active;
    try {
      active.handle = window.gridpicoLocal.openMidi(id, {
        direction: "send",
        onState(state, reason) {
          if (current !== active) return;
          if (state === "disconnected") {
            current = undefined;
            status.textContent = reason || "MIDI disconnected.";
          } else if (state === "connected" && !active.closing) {
            active.ready = true;
            status.textContent = `MIDI port ${id} connected.`;
          } else if (state === "connecting") {
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

  function send(velocity) {
    if (!current?.ready || current.closing) return;
    const bytes = Uint8Array.of(0x96, 0, velocity);
    status.textContent = current.handle.send(bytes)
      ? `Queued ${velocity ? "96 00 15" : "96 00 00"} on MIDI port ${port.value}.`
      : "MIDI output could not queue the message.";
  }

  disconnect.addEventListener("click", close);
  solid.addEventListener("click", () => send(0x15));
  off.addEventListener("click", () => send(0));
  window.addEventListener("pagehide", close);
  section.hidden = false;
})();
