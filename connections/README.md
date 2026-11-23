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

1. Wait for **Connected** and check that the bottom-left **Route** pad is green.
   Hold **Q** (key 60, channel 1) and check **Captured MIDI output** for
   **Note On · 1 · 72 · 96**.
2. With **Q** held, press the **Route** pad. Check that **Note Off · 1 · 72 · 0**
   arrives before the pad goes dark. Release the pad and **Q**. There should be
   one release row. Press and release **Q** while the pad is dark. The capture
   should stay at two rows.
3. Hold **Q** again while the pad is dark. Press and release **Route** to turn
   it green, then release **Q**. The capture should still have two rows. Press
   and release **Q** again and check for a new **72 · 96** On and **72 · 0** Off.
   To hear accepted notes, enable audio in **Monitor**.
4. Hold **Q**, press Escape while still holding **Q**, then release **Q**. Check
   for one additional On/Off pair with the same values.
5. Hold **Q** and disconnect the WebSocket, for example by stopping the host.
   Check that the pad's received LED state clears.
   Start the host again if needed, then reload and wait for **Connected**.
   Confirm the new capture starts empty and the pad is green.
   To see the fixture's EOF release directly, submit a press and let the pipe
   close while the note is held:

   ```sh
   printf 'on 1 60 96\n' | build/browser-demo/gridpico_browser_fixture
   ```

   The expected lines are `ready`, `on 1 72 96`, `ok`, and `off 1 72 0`.

The host requests startup feedback from the fixture and sends it after the
browser is ready. Keybed and surface requests share a queue of up to 16, with
one runner dispatch in flight. The host forwards accepted capture records in
order and then sends LED feedback for that dispatch on success. Monitor uses
the same accepted note records as the capture table. On socket loss or
transport saturation, the host closes runner stdin and drains stdout through
fixture exit, including disconnect releases. Browser output buffering has a
byte limit and a drain timeout. The WebSocket dependency is pinned in the npm
lockfile.

## Connected Patch view

From the repository root:

```sh
cmake -S . -B build/browser-demo
cmake --build build/browser-demo --target gridpico_patch_runner
npm --prefix connections ci --ignore-scripts
node connections/patch-host.js build/browser-demo/gridpico_patch_runner
```

Open [the local Patch view](http://127.0.0.1:8766/patch/). It uses the
same-origin `/patch/midi` socket. An optional port argument overrides 8766.

At startup check the named devices and ports, including the unassigned
**Panel** and **LED feedback** ports, and the dark 5×8 route page. Turn on
the top-left two routes, hold **Q**, then turn off one of those routes.
Check for two accepted Note On records and one accepted Note Off before
the route page updates. Release **Q** and check for the remaining Note
Off. Refresh and verify the inventory and current route LEDs.

A successful refresh delivers a validated inventory block before its
40-pad page. Keybed, pad and refresh requests run in order with one
runner dispatch in flight and up to 16 queued. An ordinary command
rejection reports its code after any accepted capture and complete LED
page. Closing the socket or stopping the host closes runner stdin and
drains output through cleanup.
