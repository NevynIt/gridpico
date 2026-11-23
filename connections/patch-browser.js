(() => {
  "use strict";

  const maxBufferedOutput = 8192;
  const inventoryTypes = new Set([
    "patch-inventory-begin", "patch-device", "patch-port", "patch-inventory-end",
  ]);
  let stopConnection;

  function openConnection() {
    const url = new URL("/patch/midi", window.location.href);
    url.protocol = "ws:";
    const socket = new WebSocket(url);
    let surface;

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
    socket.addEventListener("close", clearSurface);
    stopConnection = () => stop(1000, "Page closed");
  }

  openConnection();
  window.addEventListener("pagehide", () => stopConnection());
  window.addEventListener("pageshow", (event) => {
    if (event.persisted) openConnection();
  });
})();
