(() => {
  "use strict";

  const maxBufferedOutput = 8192;
  let stopConnection;

  function openConnection() {
    const url = new URL("/demo/midi", window.location.href);
    url.protocol = "ws:";
    const socket = new WebSocket(url);
    let surface;

    function clearSurface() {
      const connectedSurface = surface;
      surface = undefined;
      connectedSurface?.close();
    }

    function stop(code, reason) {
      clearSurface();
      socket.close(code, reason);
    }

    function sendKeybed(event) {
      if (socket.readyState !== WebSocket.OPEN) return;
      const message = JSON.stringify(event);
      // The staged surface emits only ASCII note objects.
      if (socket.bufferedAmount + message.length > maxBufferedOutput) {
        stop(4000, "Keybed writer stalled");
        return;
      }
      socket.send(message);
    }

    socket.addEventListener("message", ({ data }) => {
      if (socket.readyState !== WebSocket.OPEN) return;
      try {
        const event = JSON.parse(data);
        if (event.type === "ready" && !surface) {
          surface = window.gridpicoApc.connect(sendKeybed);
        } else if (surface && (event.type === "note-on" || event.type === "note-off")) {
          surface.receiveCapture(event);
        } else {
          stop(4000, "Invalid demo frame");
        }
      } catch {
        stop(4000, "Invalid demo frame");
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
