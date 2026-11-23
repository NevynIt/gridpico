(() => {
  "use strict";

  const maxBufferedOutput = 8192;
  let current;

  function isSurfaceBytes(bytes) {
    return Array.isArray(bytes) && bytes.length === 3 &&
      Number.isInteger(bytes[0]) && bytes[0] >= 128 && bytes[0] <= 239 &&
      [bytes[1], bytes[2]].every((byte) => Number.isInteger(byte) && byte >= 0 && byte <= 127);
  }

  function isCapture(event) {
    return (event.type === "note-on" || event.type === "note-off") && event.channel === 1 &&
      Number.isInteger(event.note) && event.note >= 0 && event.note <= 127 &&
      Number.isInteger(event.velocity) && event.velocity <= 127 &&
      event.velocity >= (event.type === "note-on" ? 1 : 0) &&
      Object.keys(event).length === 4;
  }

  function openConnection() {
    const url = new URL("/demo/midi", window.location.href);
    url.protocol = "ws:";
    const socket = new WebSocket(url);
    let surface;
    const attempt = {
      leave: () => stop(1000, "Page closed"),
    };
    current = attempt;

    function clearSurface() {
      const connectedSurface = surface;
      surface = undefined;
      connectedSurface?.close();
    }

    function stop(code, reason) {
      clearSurface();
      socket.close(code, reason);
    }

    function send(event) {
      if (socket.readyState !== WebSocket.OPEN) return;
      const message = JSON.stringify(event);
      // Keybed and surface frames both serialize as ASCII.
      if (socket.bufferedAmount + message.length > maxBufferedOutput) {
        stop(4000, "Demo writer stalled");
        return;
      }
      socket.send(message);
    }

    function sendSurface(bytes) {
      if (!isSurfaceBytes(bytes)) {
        stop(4000, "Invalid surface bytes");
        return;
      }
      send({ type: "apc-surface", bytes });
    }

    socket.addEventListener("message", ({ data }) => {
      if (socket.readyState !== WebSocket.OPEN) return;
      try {
        if (typeof data !== "string") throw new TypeError("Expected a text frame");
        const event = JSON.parse(data);
        if (event.type === "ready" && !surface && Object.keys(event).length === 1) {
          surface = window.gridpicoApc.connect(send, sendSurface);
        } else if (surface && isCapture(event)) {
          surface.receiveCapture(event);
        } else if (surface && event.type === "apc-surface" && Object.keys(event).length === 2 &&
                   isSurfaceBytes(event.bytes)) {
          surface.receiveSurface(event.bytes);
        } else {
          stop(4000, "Invalid demo frame");
        }
      } catch {
        stop(4000, "Invalid demo frame");
      }
    });
    socket.addEventListener("close", ({ code }) => {
      const wasConnected = Boolean(surface);
      clearSurface();
      if (current !== attempt || code === 1000) return;
      const reconnect = () => {
        if (current === attempt) openConnection();
      };
      const message = code === 1013 ? "Gridpico received too much input at once."
        : wasConnected ? "The connection to Gridpico was interrupted."
          : "Could not connect to Gridpico.";
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
