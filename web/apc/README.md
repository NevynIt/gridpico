# Gridpico Web APC

Open [index.html](index.html) in a browser. The page is self-contained and works
from disk. To serve it locally, run this from the repository root, then open
[the local preview](http://127.0.0.1:8000/).

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory web/apc
```

Play using a pointer, touch, or the shortcuts printed on the keys. Tab
skips the keybed and reaches the panel controls, which respond to Space
and Enter. Middle C is the key marked **Q**
at start-up, MIDI note 60 on channel 1. The octave buttons shift the keybed
locally. Cancellation, lost pointer capture, Escape, and leaving the page
release held controls. Panel buttons and Sustain show their local pressed state.

The pad matrix and MIDI mapping follow the
[Akai Communications Protocol v1.1 diagram in miniMIDI](https://raw.githubusercontent.com/NevynIt/miniMIDI/main/ref/Keyboards/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1%20%281%29.pdf).
The printed labels follow the [Akai User Guide v1.2](https://cdn.inmusicbrands.com/akai/apc-key-25-mkii/APC%20Key%2025%20mk2%20-%20User%20Guide%20-%20v1.2.pdf).
Gridpico Route is annotated separately below the controller face. The octave
buttons adjust the keybed locally. The display uses 1-based MIDI channels.
The initial note range 48–72 and fixed press velocity 96 are simulator defaults.

## Listen to routed MIDI

Open the [connected Web APC application](../../connections/README.md), select
**Monitor**, and choose **Enable audio**. With the default +12 Route active,
hold **Q** to hear destination note 72 (C5, about 523.25 Hz). Release **Q** or
press Escape to end the note. The **Capture** tab continues to record routed
destination events while you view Monitor, including when audio is off.

Master volume starts at 35%. Each of the 16 channels can use Sine, Triangle,
Square or Mute. A new sound choice applies to the next received Note On.
Muting a channel releases that channel's voices. Disabling audio or losing
the connection clears all voices. Starting audio or reconnecting waits for
fresh destination events. The monitor uses up to 25 voices, including release
tails. At capacity it reclaims a releasing voice before a sounding voice.
Audio settings are local to the open page. The presets work offline, and
the standalone preview opens silent with an empty capture.

## Local integration

The standalone page opens disconnected. In the connected Route view, the
Computer connections adapter calls `window.gridpicoApc.connect(sendKeybed)` after the
`{"type":"ready"}` message on the same-origin `/demo/midi` WebSocket.
[Local Web APC connection contract](../../api/README.md#local-browser-demo-connection)

- `sendKeybed(event)` receives keybed notes as
  `{ type: "note-on" | "note-off", channel, note, velocity }`.
  The surface emits channel 1 with velocity 96 for a press and 0 for a release,
  including cancellation. Send each object in order to simulated keybed ingress.
- The returned `receiveCapture(event)` renders one destination note event of
  the same shape. Feed it each newly accepted destination record once, in capture
  order, preserving velocity. The panel shows the most recent 100 note records
  for the attachment.
- Call the returned `close()` on disconnect. For graceful shutdown call it
  while ingress can still accept releases. Transport-side ingress cleanup
  is required on link loss.

## Route and connection

After an unexpected connection loss, **Reconnect** opens one fresh Web APC
connection. Wait for green Route feedback before playing. A fresh connection
clears the prior capture, starts with the default Route, and has
audio off until explicitly enabled. Held keys from the old connection are
released locally and do not play on the fresh connection.

In connected Web APC, the bottom-left **Route** pad toggles routing
from the simulated APC keybed (input 1), through +12 semitones, to simulated
MIDI capture (output 2). The route status starts at **Disconnected**, shows
**Waiting for feedback** on attach, and changes to **Active** or **Off** as
LED messages arrive. Green means the route is active. The amber outline
shows when the pad is held.
To check the toggle, hold **Q** and press the pad. Capture shows one release
for note 72 as the pad goes dark. Release **Q**, press the pad again, and play
a fresh **Q** to hear or capture the transposed note again.

The pad-capable adapter passes an optional second argument to
`gridpicoApc.connect(sendKeybed, sendSurface)`. The page sends `90 00 7F`
on press and `80 00 00` on release or cancellation. Call the returned
`receiveSurface(bytes)` with `96 00 15` for green or `96 00 00` for dark.
The pad clears its LED state on disconnect. For browser smoke tests, use
`#route-pad`, with `data-led` for received state and `data-held` for the
gesture. [One-pad APC route toggle contract](../../api/README.md#one-pad-apc-route-toggle)

## Connected Patch

Start the separate [Patch host](../../connections/README.md#connected-patch-view)
and open its `/patch/` page. The simulator shows the actual device and port
inventory, the occupied source rows and destination columns, and unassigned
ports such as **Panel** and **LED feedback**. Use **Refresh inventory** to
request a new snapshot. A disconnect clears the inventory and LED page.

Press a pad at the intersection of a source row and destination column.
An outlined pad has both ports assigned. The LED shows the last authoritative
state in the complete 40-pad route page. Dark is Off, orange is Pending,
and green is Active. The same keybed still sends Port 0 notes.

When the connection supports the color layer, hold physical Shift or use
**Hold Shift**. Press the scene for a source row or the track for a destination
column to select its device. Choose pad `00`–`03` as shown in **Device colors**.
The separate color panel displays the selected device and its received APC
palette preview. While the color layer is open, the grid shows the last route
page. Release Shift for fresh route feedback.

Patch Capture shows all accepted note records. The current note frame
carries no destination ID. Matching notes from two outputs can therefore
appear identical in Capture and share a browser Monitor voice. The one-pad
Route view and the offline Protocol Lab remain separate.
[Patch browser contract](../../api/patchbay-controls.md#read-only-browser-inventory)

## Protocol lab

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
