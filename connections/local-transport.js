(() => {
  "use strict";

  function openBytes(path, { direction, onData, onState }, frameLimit, bufferLimit) {
    const url = new URL(path, window.location.href);
    url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    const socket = new WebSocket(url);
    socket.binaryType = "arraybuffer";

    queueMicrotask(() => onState?.("connecting"));
    socket.addEventListener("open", () => onState?.("connected"));
    socket.addEventListener("message", ({ data }) => {
      if (direction === "send" || !(data instanceof ArrayBuffer) ||
          data.byteLength === 0 || !frameLimit(data.byteLength)) {
        socket.close(4000, "Invalid byte frame");
        return;
      }
      onData?.(new Uint8Array(data));
    });
    socket.addEventListener("close", ({ code }) => {
      onState?.("disconnected", code === 1000 ? undefined : "The Gridpico connection closed.");
    });

    return Object.freeze({
      send(bytes) {
        if (direction === "receive" || socket.readyState !== WebSocket.OPEN ||
            !(bytes instanceof Uint8Array) || bytes.byteLength === 0 || !frameLimit(bytes.byteLength)) {
          return false;
        }
        if (socket.bufferedAmount + bytes.byteLength > bufferLimit) {
          socket.close(4000, "Too much queued input");
          return false;
        }
        socket.send(bytes);
        return true;
      },
      close() {
        socket.close(1000, "Binding closed");
      },
    });
  }

  const transport = {
    openMidi(portId, options) {
      if (!Number.isInteger(portId) || portId < 1 || portId > 65535 ||
          (options?.direction !== "send" && options?.direction !== "receive")) {
        throw new TypeError("Choose a valid MIDI port and direction.");
      }
      return openBytes(`/midi/${portId}`, options, (length) => length === 3, 128 * 3);
    },
  };
  if (window.location.protocol === "http:") {
    transport.openSerial = (options) => openBytes("/serial", options, (length) => length <= 256, 8192);
  }
  window.gridpicoLocal = Object.freeze(transport);
})();
