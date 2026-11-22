# Gridpico APC simulator

Open [index.html](index.html) in a browser. The page is self-contained and works
from disk. To serve it locally, run this from the repository root, then open
[the local preview](http://127.0.0.1:8000/).

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory web/apc
```

Play using a pointer, touch, or the shortcuts printed on the keys. Focused
buttons also respond to Space and Enter. Middle C is the key marked **Q**
at start-up, MIDI note 60 on channel 1. The octave buttons shift the keybed
locally. Cancellation, lost pointer capture, Escape, and leaving the page
release held controls. Panel buttons and Sustain show their local pressed state.

The panel and keybed reference is the official [Akai Communications Protocol
v1.1](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf),
pages 14–15. The display uses 1-based MIDI channels. The initial note range
48–72 and fixed press velocity 96 are simulator defaults.

## Listen to routed MIDI

Start the [connected demo](../../connections/README.md), open **Monitor** and
select **Enable audio**. Hold **Q** to hear destination note 72 (C5, about
523.25 Hz) in the +12 demo fixture. Release **Q** or press Escape to end the
note. The **Capture** tab continues to record routed destination events while
you view Monitor, including when audio is off.

Master volume starts at 35%. Each of the 16 channels can use Sine, Triangle,
Square or Mute. A new sound choice applies to the next received Note On.
Muting a channel releases that channel's voices. Disabling audio or losing
the connection clears all voices. Starting audio or reconnecting waits for
fresh destination events. The monitor uses up to 16 voices, including release
tails, and reclaims the oldest voice if full. Audio settings are local to the
open page. The presets work offline, and the standalone preview opens silent
with an empty capture.

## Local integration

The standalone page opens disconnected. In the connected demo, the Computer
connections adapter calls `window.gridpicoApc.connect(sendKeybed)` after the
`{"type":"ready"}` message on the same-origin `/demo/midi` WebSocket.
[Local browser demo connection contract](../../api/README.md#local-browser-demo-connection)

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

## Demo route pad

In the connected demo, the bottom-left **Route** pad toggles the +12 route.
Green means the route is active. The amber outline shows when the pad is held.
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
