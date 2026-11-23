# Browser MIDI and web landing page

**Owner** Architect. **Status** Approved contract. [Local router](local-router.md) · [WebSocket MIDI](websocket-midi.md) · [Optional instruments](instrument-peers.md).

## Entry points and packaging

`scripts/web-ui.sh` is the computer entry point for the production web tools. Assembly starts or selects the production local-router host and opens its landing page at `/`. The default URL is `http://127.0.0.1:8767/`. Print the usable URL when a browser cannot be opened. Connections serves the landing page and links to `/apc/` and `/diagnostics/` at the same origin. List optional tools when they are available. Demo launchers remain separate entry points for their evidence and regression runs.

For the read-only `GRIDPICO` image, stage a self-contained `HOME.HTM` with relative links to `INDEX.HTM` for Web APC and `DIAG.HTM` for Diagnostics. Include the HTML files in the checksummed asset manifest. Both tools retain direct offline entry. Assembly packages the shared client source into those pages where direct-file execution needs an inline copy. Served pages can load one shared script. The files and their local code are sufficient for offline use. Browser permissions and selected MIDI or serial devices are verified on the target separately.

The production home needs the local router and its existing browser host. Cross-host hosting can serve the same links under the opt-in authenticated origin. It uses the published WSS contract. Native RTP-MIDI, opt-in WSS and optional instruments each have their own acceptance and availability.

## Shared in-page adapter

Connections owns one browser MIDI layer. Web APC, Diagnostics, Monitor, MIDI Log and the optional FM page choose a provider and a port explicitly. Pages own their controls and presentation. The layer uses the existing `openMidi(portId, { direction, onData, onState })` shape for either provider. This JavaScript boundary is within the page. A WebSocket peer still sends or receives binary MIDI only.

| Boundary | Behavior |
| --- | --- |
| Select | Choose Web MIDI or WebSocket and a direction from the page's point of view. `send` requires an output the page can write. `receive` requires an input the page can read. Treat an ID as local to its provider and direction. |
| Web MIDI ports | Request MIDI access from the user's Connect action. List compatible inputs or outputs from the granted `MIDIAccess` using the OS names and opaque `MIDIPort.id` strings. This includes OS-exposed RTP ports when the selected browser can see them. Use fresh port availability when opening a selected port. |
| WebSocket ports | Select a configured or operator-entered directional router ID `1–65535` from the local CLI inventory or deployment information. Use `/midi/<port-id>` and derive `ws:` or `wss:` from the served origin. The runtime admits the binding and owns its queue limits. |
| Open | Return the existing binding handle with `send(bytes)` and `close()`. Report `connecting`, `connected` or `disconnected` via `onState(state, reason?)`. Report connected after the Web MIDI port opens or the WebSocket upgrades. Surface unavailable access or connection failure. |
| Receive | `onData(bytes)` delivers one complete MIDI message as a `Uint8Array` from the selected input, in receipt order. Preserve status and data bytes. |
| Send | `send(bytes)` accepts a complete `Uint8Array` on a live send binding. `true` means local submission was accepted. `false` means that binding could not queue it. Apply the selected provider's supported message sizes and queue bounds. WSS uses the published three-byte profile. |
| Close and replace | Close the selected directional binding and retire its callbacks, other than its final disconnect notification. The page releases its own held controls or voices. A new selection opens a fresh binding and waits for fresh input. |

Report failure on the selected provider. The user chooses any replacement. Browser Web MIDI opens OS-presented MIDI ports. Direct WebSocket MIDI connects to a selected Gridpico listener. RTP session setup stays with the OS adapter. Serial remains a separately opened Diagnostics byte stream under the local CLI or Web Serial contracts. Route state, attachment generations and accepted-delivery accounting stay inside the router. [Web MIDI](https://www.w3.org/TR/webmidi/) · [WebSocket wire](websocket-midi.md).

## Bounded acceptance

Use the canonical launcher on the nominated user setup. Open both links, then use Diagnostics to send the solid/off pair and one Timed RGB Replay through the production router to the live Web APC receiver. Check exact bytes, green/dark and timed behavior, and committed restore. Exercise the staged home and its relative links with Internet access off. Keep direct mounted-drive, OS-presented RTP and authenticated cross-host WebSocket results separate. For shared binding behavior, verify a real selected send and receive port, direction rejection, and disconnect cleanup on each provider being accepted. [Timed replay](local-router.md#timed-diagnostics-replay).
