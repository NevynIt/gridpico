# Connected Web APC demo

From the repository root, with CMake, a C compiler, Node.js 18 or later and npm:

```sh
cmake -S . -B build/browser-demo
cmake --build build/browser-demo --target gridpico_browser_fixture
npm --prefix connections ci --ignore-scripts
node connections/demo-host.js build/browser-demo/gridpico_browser_fixture
```

Open [the local APC demo](http://127.0.0.1:8765/). The host binds IPv4 loopback
and accepts `/demo/midi` upgrades from that exact origin. To change the local
port, append it to the host command and use the printed URL.

The host serves the staged APC page with the connection adapter attached. One
player owns the fixture at a time. A new page can connect after the previous
fixture finishes cleanup. Reload the page to reconnect after link loss. Stop
the host with Ctrl+C.

## Smoke check

1. Wait for **Connected**. Press and release **Q** (key 60, channel 1). Check
   **Captured MIDI output** for **Note On · 1 · 72 · 96** followed by
   **Note Off · 1 · 72 · 0**.
2. Hold **Q**, press Escape while still holding **Q**, then release **Q**. Check
   for one additional On/Off pair with the same values.
3. Hold **Q** and disconnect the WebSocket, for example by stopping the host.
   Start the host again if needed, then reload and wait for **Connected**.
   Confirm the new capture starts empty and ordinary playback still works.
   To see the fixture's EOF release directly, submit a press and let the pipe
   close while the note is held:

   ```sh
   printf 'on 1 60 96\n' | build/browser-demo/gridpico_browser_fixture
   ```

   The expected lines are `ready`, `on 1 72 96`, `ok`, and `off 1 72 0`.

The host sends only records reported by the fixture and preserves their order
and velocity. It allows one runner dispatch in flight and queues up to 16
incoming notes. On socket loss or transport saturation it closes runner stdin
and keeps draining stdout through fixture exit, including disconnect releases.
Browser output buffering has a byte limit and a drain timeout. The WebSocket
framing dependency is pinned in the npm lockfile.
