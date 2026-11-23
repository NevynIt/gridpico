(() => {
  "use strict";

  const maxBufferedOutput = 8192;
  const inventoryTypes = new Set([
    "patch-inventory-begin", "patch-device", "patch-port", "patch-inventory-end",
  ]);
  let current;

  function openConnection() {
    const url = new URL("/patch/midi", window.location.href);
    url.protocol = "ws:";
    const socket = new WebSocket(url);
    let surface;
    const attempt = { leave: () => stop(1000, "Page closed") };
    current = attempt;

    function clearSurface() {
      const active = surface;
      surface = undefined;
      active?.close();
    }

    function stop(code, reason) {
      clearSurface();
      socket.close(code, reason);
    }

    function send(event) {
      if (socket.readyState !== WebSocket.OPEN) return;
      const message = JSON.stringify(event);
      // The view sends ASCII note, surface and refresh frames.
      if (socket.bufferedAmount + message.length > maxBufferedOutput) {
        stop(4000, "Patch writer stalled");
        return;
      }
      socket.send(message);
    }

    socket.addEventListener("message", ({ data }) => {
      if (socket.readyState !== WebSocket.OPEN) return;
      try {
        if (typeof data !== "string") throw new TypeError("Expected a Patch text frame");
        const event = JSON.parse(data);

        if (event.type === "ready" && !surface) {
          surface = window.gridpicoApc.connect(
            send,
            (bytes) => send({ type: "apc-surface", bytes }),
            () => send({ type: "patch-refresh" }),
          );
        } else if (surface && inventoryTypes.has(event.type)) {
          surface.receiveInventory(event);
        } else if (surface && event.type === "patch-led-page") {
          surface.receiveLedPage(event);
        } else if (surface && event.type === "apc-surface") {
          surface.receiveSurface(event.bytes);
        } else if (surface && (event.type === "note-on" || event.type === "note-off")) {
          surface.receiveCapture(event);
        } else if (surface && event.type === "error") {
          surface.receiveError(event.code);
        } else {
          throw new Error("Unexpected Patch frame");
        }
      } catch {
        stop(4000, "Invalid Patch frame");
      }
    });
    socket.addEventListener("close", ({ code, reason }) => {
      const wasConnected = Boolean(surface);
      clearSurface();
      if (current !== attempt || code === 1000) return;

      let message = wasConnected ? "The connection to Gridpico was interrupted."
        : "Could not connect to Gridpico.";
      if (code === 1013) {
        message = reason === "Patch keybed capacity exceeded" ? "Gridpico reached its held-note limit."
          : "Gridpico received too much input at once.";
      }
      const reconnect = () => {
        if (current === attempt) openConnection();
      };
      window.gridpicoApc?.showDemoReconnect?.(reconnect, message);
    });
  }

  openConnection();
  window.addEventListener("pagehide", () => {
    const attempt = current;
    current = undefined;
    attempt?.leave();
  });
  window.addEventListener("pageshow", (event) => {
    if (event.persisted && !current) openConnection();
  });
})();
