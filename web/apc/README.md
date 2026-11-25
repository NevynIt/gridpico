# Gridpico Web APC

Run `./scripts/web-ui.sh` from the repository root on the Docker host or in
WSL with Python 3 and the Compose `dev` service running. Choose **Web APC**
on [Gridpico Home](http://127.0.0.1:8767/) to open `/apc/`. For the offline
`GRIDPICO` package, open `HOME.HTM` and choose **Web APC** in a browser that
supports Web MIDI. [Launcher and local router setup](../../connections/README.md#local-router) · [Offline tools](../../connections/README.md#offline-tools)

## Local router

The canonical loopback launcher tries each published starter binding once.
The page shows the selected server and the state of each role. Browser TX
binds a router input. Browser RX binds a router output.

| Web APC role | Default router port | APC function |
| --- | --- | --- |
| Keybed TX | `1` Keybed input | Sends Port 0 notes |
| Panel TX | `2` Panel input | Sends Port 1 button presses and releases |
| LED RX | `3` LED feedback output | Displays received Port 1 LED feedback |

Use **Retry** after a failed binding or choose a configured port for that
role. The local Diagnostics CLI's `inventory` command lists the router port
IDs. For system MIDI, select **Web MIDI** on the role, choose **Allow Web
MIDI**, then choose a system output for TX or a system input for LED RX and
click **Connect**. The offline page starts with Web MIDI where supported.
Replacing a binding releases held input on the old link when it is still
available. Replacing LED RX clears its display until fresh feedback arrives.
[Browser role contract](../../api/browser-midi.md#production-startup-and-page-roles)

On the canonical loopback page, use **Switch router** for the configured Pi
endpoint. If the switch fails, the page offers to open Web APC at the requested
address. On pages with **Open server**, use it to open Web APC at a supported
origin. Authenticated HTTPS pages with a published Keybed input also offer
the optional **Laptop keybed bridge**, which shares the exclusive Keybed
binding. Disconnect the surface's Keybed TX before starting that bridge.
[Endpoint switching](../../api/browser-midi.md#isolated-web-apc-in-place-gate) · [WSS server selection](../../api/browser-midi.md#server-selection-and-status) · [Laptop keybed bridge](../../api/pico-target.md#laptop-keybed-bridge)

Open **Diagnostics** for Monitor, MIDI Log and LED tests. The canonical
launcher enables the pitch-preserving Keybed routes to Monitor (`1 → 5`)
and MIDI Log (`1 → 6`). Choose **Enable audio** in Diagnostics and play a
fresh note after the receivers connect. The **Run LED test** action displays
received feedback inside Diagnostics and needs the exclusive LED RX port
`3`. Release Web APC's LED RX before running it if that binding is active.
For timed replay to the Web APC surface, use the separate Raw route from
Diagnostics TX `4` to Web APC LED RX `3`.
[LED test lifecycle](../../api/local-router.md#one-action-local-diagnostics-led-test) · [Timed replay setup](../../connections/README.md#timed-rgb-pads)

## Play the controller

Play with a pointer, touch or the shortcuts printed on the keys. Tab skips
the musical keys and reaches the panel controls, which support Space and
Enter. At startup, **Q** plays middle C (note 60 on channel 1), the keybed
spans notes 48–72, and the fixed press velocity is 96. Octave buttons shift
the keybed locally. Cancellation, lost pointer capture, Escape and leaving
the page release held controls.

The surface omits the **Knob Ctrl** legend. All eight knobs retain individual
accessible names. Drag a knob or use its arrow keys to turn it. Knob detents
and Sustain remain local in the production surface. The pad matrix and MIDI
mapping follow the APC protocol, and the printed labels follow the user guide.
[Communications Protocol v1.1 diagram](https://raw.githubusercontent.com/NevynIt/miniMIDI/main/ref/Keyboards/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1%20%281%29.pdf) · [Akai User Guide v1.2](https://cdn.inmusicbrands.com/akai/apc-key-25-mkii/APC%20Key%2025%20mk2%20-%20User%20Guide%20-%20v1.2.pdf)

## Legacy preview and Route

Open [index.html](index.html) directly or serve the standalone preview from
the repository root and open [the preview](http://127.0.0.1:8000/).

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory web/apc
```

The connected one-pad demo has its own Route pad, Capture and Monitor. Its
default route transposes the simulated keybed by +12. With green Route
feedback, hold **Q** to capture note 72 (C5). To hear it, choose **Enable
audio** in the demo's Monitor tab. Monitor has local volume, channel
filtering, and per-channel sound or mute. After reconnecting, wait for green
feedback, enable audio if needed and play a fresh note.
[One-pad launch and smoke check](../../connections/README.md#one-pad-web-apc-demo) · [Legacy browser framing](../../api/README.md#local-browser-demo-connection) · [Route pad and LED contract](../../api/README.md#one-pad-apc-route-toggle)

## Connected Patch

The separate Patch demo at `/patch/` shows received inventory and committed
route LEDs. Its Capture has destination-tagged records, and its Monitor plays
only the attached Monitor receiver's deliveries. A fresh Patch connection
starts with routes Off.
[Patch host and operator steps](../../connections/README.md#connected-patch-view) · [Patch inventory and color layers](../../api/patchbay-controls.md#read-only-browser-inventory) · [Patch Monitor contract](../../api/web-monitor-proposal.md#framing-and-readiness-order)

## Legacy Protocol Lab

**Protocol** shows direction, logical port, and exact hex for manual input.
In the connected Route view, the keybed and Route pad use the live adapter.
Other panel controls and knobs emit local protocol frames. The manual trace
keeps the latest 128 frames.

- **Port 1 buttons** use `90 nn 7F` on press and `80 nn 00` on release or
  cancellation. `nn` is `00–27` for clip pads starting at the bottom left,
  `40–47` for tracks, `52–56` for scenes, `51` for Stop All Clips, `5B` for
  Play/Pause, `5D` for Record, and `62` for Shift. The press velocity is a
  fixture convention.
- **Port 0** contains keybed Note On and Note Off. Sustain shows local CC
  `B0 40 7F` and `B0 40 00`. Octave changes emit no bytes.
- Drag knobs up or down or use the arrow keys. The simulator uses Port 1
  `B0 30–37 01` and `B0 30–37 7F` for opposite detents. Akai documents the
  CC numbers and relative operation. Direction, step encoding, and
  mode-specific hardware behavior remain unverified. The knobs have no
  Gridpico parameter bindings.

**Step** advances one row. **Replay** starts the selected sequence at the
beginning and advances automatically. The table identifies expected frames
and records the frames actually applied, submitted, or received.

- **Direct Solid LEDs** applies these RX Port 1 frames to the isolated
  surface in order: `96 00 05`, `96 00 15`, `96 01 2D`, `90 40 01`,
  `90 52 01`, `96 00 00`, `90 40 00`, `90 52 00`, `96 01 00`.
  Enter three hex bytes below the isolated surface to inspect the full RGB
  palette at any documented solid brightness (`90–96`). Track LEDs use
  fixed red and scene LEDs fixed green. Velocity `00` is off, `01` and
  `03–7F` are on.
- **Timed LEDs** applies the following RX Port 1 frames. Step leaves each
  effect running until a later frame, reset, or exit. Replay waits 1,200 ms
  after starting the last timed LED before sending any cleanup frame.

| Frame | Isolated preview |
| --- | --- |
| `9A 00 15` | Pad 00 green pulse, half-note cycle of 1,000 ms |
| `9E 01 2D` | Pad 01 blue blink, quarter-note cycle of 500 ms |
| `90 40 02` | Track 1 fixed red blink, 1,000 ms fixture cycle |
| `90 52 02` | Scene 1 fixed green blink, 1,000 ms fixture cycle |
| `96 00 05` | Replace the pulse with solid red |
| `96 01 00` | Clear pad 01 |
| `90 40 01` | Replace the track blink with solid red |
| `90 52 00` | Clear scene 1 |
| `96 00 00` | Clear pad 00 |
| `90 40 00` | Clear track 1 |

The **local preview** runs at 120 BPM, with 500 ms per quarter note. The
period is one full bright–dim–bright pulse or one lit/dark blink cycle.
Each new effect starts bright. Blink phases are equal, and the pulse
smoothly dims to 15% of the palette color over the dark tile and returns
to full brightness. A replacement frame cancels the old animation.
An RGB palette value of `00` clears the LED at any supported status.

| RGB statuses | Note divisions in status order | Local periods in ms |
| --- | --- | --- |
| `97–9A` pulse | 1/16, 1/8, 1/4, 1/2 | 125, 250, 500, 1,000 |
| `9B–9F` blink | 1/24, 1/16, 1/8, 1/4, 1/2 | 83⅓, 125, 250, 500, 1,000 |

Fixed blink velocity `02` applies to tracks `40–47` and scenes `52–56` with
status `90`. Its local fixture timing is 500 ms per phase. The Replay
dwell is also local preview timing. Timed effects use browser rendering,
with external clock and physical LED timing outside this preview.
Reset LEDs or leave the Protocol preview to clear the timed effects.
[Akai Communications Protocol v1.1, pp. 2–3 and 6](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

- **Encoder Detents** emits local TX Port 1 pairs `B0 nn 01`, `B0 nn 7F`
  in order for each encoder from `nn=30` through `37`.
- **Route Off While Held** requires a fresh Route connection with a green
  Route LED. Reload Web APC to start another run. The sequence
  blocks manual performance input while it owns the keybed and Route pad.

| Step | Submitted | Expected reply in order |
| --- | --- | --- |
| Hold Q | Port 0 `90 3C 60` | Output 2 `90 48 60` |
| Disable route | Port 1 `90 00 7F` | Output 2 `80 48 00`, then Port 1 `96 00 00` |
| Release pad | Port 1 `80 00 00` | None expected |
| Release retired Q | Port 0 `80 3C 00` | None expected |
| Hold Q while off | Port 0 `90 3C 60` | None expected |
| Re-enable route | Port 1 `90 00 7F` | Port 1 `96 00 15` |
| Release pad | Port 1 `80 00 00` | None expected |
| Release suppressed Q | Port 0 `80 3C 00` | None expected |
| Fresh Q press | Port 0 `90 3C 60` | Output 2 `90 48 60` |
| Release Q | Port 0 `80 3C 00` | Output 2 `80 48 00` |

The live sequence waits up to 2.5 seconds for each expected reply group and
allows 20 seconds between manual Steps. The connection has no per-command
acknowledgement. For steps with no expected reply, the table reports local
submission only. Stop, timeout, or loss of focus releases held sequence
inputs through the available adapter. The route LED continues to follow
committed feedback, so stopping while Off can leave the route Off.
