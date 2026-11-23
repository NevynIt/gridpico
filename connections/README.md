# Connected Web APC

From the repository root, with CMake, a C compiler, Node.js 18 or later and npm:

```sh
cmake -S . -B build/browser-demo
cmake --build build/browser-demo --target gridpico_browser_fixture
npm --prefix connections ci --ignore-scripts
node connections/demo-host.js build/browser-demo/gridpico_browser_fixture
```

Open [the local Web APC application](http://127.0.0.1:8765/). The host binds
IPv4 loopback and accepts `/demo/midi` upgrades from that exact origin. To
change the local port, append it to the host command and use the printed URL.

The host serves Web APC with the connection adapter attached. One
player owns the fixture at a time. After a disconnect, click
**Reconnect** to start a fresh fixture once cleanup finishes. Start playback
with a fresh key press and enable Monitor audio again if needed. Stop the
host with Ctrl+C.

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
   Start the host again if needed, click **Reconnect**, and wait for **Connected**.
   Confirm the new capture starts empty and the pad is green.
   To see the fixture's EOF release directly, submit a press and let the pipe
   close while the note is held:

   ```sh
   printf 'on 1 60 96\n' | build/browser-demo/gridpico_browser_fixture
   ```

   The expected lines are `ready`, `on 1 72 96`, `ok`, and `off 1 72 0`.

The host requests startup feedback from the fixture and sends it after the
browser is ready. The browser fixture supports 25 held notes. Keybed and surface
requests share a queue of up to 64, with one runner dispatch in flight. The host
forwards up to 25 accepted capture records per dispatch in order and then
sends LED feedback for that dispatch on success. Monitor uses
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
**Panel** and **LED feedback** ports and **Monitor** in destination column 3.
Turn on the top-left instrument route and the Monitor route in the first
row. Enable audio in **Monitor** and hold **Q**. Check that Capture shows
both destinations. Turn off the instrument route and verify its Note Off
arrives before the LED update while Monitor keeps sounding. Release **Q**
and verify the Monitor Note Off and silence.

The default Patch profile supports 25 held keys across three routes and
up to 75 accepted note records per dispatch. Keybed, pad, refresh and
Monitor requests run in order with one dispatch in flight and up to 162
queued. The host caps outgoing buffering at 12 KiB, and the browser
retains its 8 KiB write limit. A successful refresh delivers a validated
inventory block before the 40-pad LED page. The Monitor receiver
attaches when the Patch view is ready, and audio remains a separate
local control.

An ordinary command rejection reports its code after any accepted
capture and complete LED page. If a new key press exceeds the held-note
limit, the connection delivers the accepted releases, reports the
capacity error and closes.
After a disconnect, click **Reconnect** for a fresh Patch connection
with routes off and an empty capture. Use fresh route and key gestures
and enable audio again if needed.

### Two-branch regression

```sh
node connections/patch-host.js --two-branch build/browser-demo/gridpico_patch_runner
```

This profile retains two routes, up to 50 accepted note records per
dispatch, 160 queued requests and the original untagged capture format.
Browser playback is silent in this profile. Default Patch plays through
the attached **Web Monitor** destination. The one-pad view keeps its own
browser audio on the `/demo/midi` connection.

## Local router

From the repository root:

```sh
cmake -S . -B build/local-router
cmake --build build/local-router --target gridpico_local_router
npm --prefix connections ci --ignore-scripts
node connections/local-host.js build/local-router/gridpico_local_router
```

Open [Web APC](http://127.0.0.1:8767/apc/) and
[Diagnostics](http://127.0.0.1:8767/diagnostics/). An optional port argument
overrides 8767. Diagnostics connects to the local CLI and MIDI independently.
Open the standalone Diagnostics file directly for Web Serial.

Use the CLI's `inventory` command to identify the configured MIDI bindings.
For the default fixture, Web APC can bind Keybed TX to port 1, Panel TX to
port 2 and LED RX to port 3. Monitor and MIDI Log can receive on ports 5 and
6. To enable their independent keybed routes:

```text
route 1 5 managed on
route 1 6 managed on
```

For the solid/off check, connect Diagnostics MIDI to port 4 and Web APC LED
RX to port 3. Run `route 4 3 raw on` in the local CLI, then click
**Solid green** and **Off** and check the bottom-left APC pad. Run
`route 4 3 raw off` to end the run and restore committed APC feedback.
A new run requires a fresh Diagnostics MIDI connection and an explicit
`on`. The Raw lease also ends after 30 seconds or if an affected MIDI
peer disconnects.

The local host keeps one router running for all bindings. It accepts MIDI
as three-byte binary frames at `/midi/<port-id>` and serial CLI bytes at
`/serial`. The serial connection has its own input and output limits.

### Timed RGB pads

In hosted Diagnostics, use the same selected MIDI TX and APC LED RX
bindings. Enter the shown `route SRC DST raw on` command and wait for the CLI
to report Active. **Step** queues one row at your pace. **Replay** starts
from row 1 and queues each row once with these minimum waits.

| Hex sent and expected at LED RX | Minimum wait after send | Expected at 120 BPM fixture tempo |
| --- | --- | --- |
| `9A 00 15` | 100 ms | Pad 00 green pulse, 1,000 ms cycle |
| `9E 01 2D` | 1,200 ms | Pad 01 blue blink, 500 ms cycle |
| `96 00 05` | 100 ms | Pad 00 solid red replaces the pulse |
| `96 01 00` | 100 ms | Pad 01 off |
| `96 00 00` | End | Pad 00 off |

The sequence uses the APC RGB pad messages. The 120 BPM label describes the
browser renderer's fixture tempo. [Akai APC Key 25 mk2 Communication Protocol](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

Completion, **Stop**, **Reset**, and the 20-second client limit close the
Diagnostics TX binding. The router ends the Raw lease and restores current
committed APC feedback. **Reset** also returns the cursor to row 1.
The typed `route SRC DST raw off` command, peer loss, or the router's
30-second lease expiry also ends the run. Connect a fresh Diagnostics TX
binding and issue Raw on again to start another run.
