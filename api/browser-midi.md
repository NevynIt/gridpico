# Browser MIDI and web landing page

**Owner** Architect. **Status** Production loopback startup and shared browser MIDI verified in Linux browser tests. [Local router](local-router.md) · [WebSocket MIDI](websocket-midi.md) · [Optional instruments](instrument-peers.md).

## Entry points and packaging

`scripts/web-ui.sh` is the computer entry point for the production web tools. Assembly starts or selects the production local-router host and opens its landing page at `/`. The default URL is `http://127.0.0.1:8767/`. Print the usable URL when a browser cannot be opened. The loopback landing page links to `/apc/`, `/diagnostics/` and, when installed, `/fm/` at the same origin. Demo launchers remain separate entry points for their evidence and regression runs.

For the read-only `GRIDPICO` image, Assembly stages `HOME.HTM` with relative links to `INDEX.HTM` for Web APC, `DIAG.HTM` for Diagnostics and `FM.HTM` for the FM instrument. `VERSION.TXT` records checksums for all four pages. The three tool pages inline the shared MIDI adapter and Diagnostics also inlines its own client script. Each tool opens directly offline. Served pages load shared scripts from the same origin. Browser permissions and selected MIDI or serial devices are verified on the target separately.

The production home needs the local router and its existing browser host. The opt-in authenticated listener serves the individual tool paths under its configured origin. Use the published WSS contract for those connections. Native RTP-MIDI, opt-in WSS and optional instruments each have their own acceptance and availability.

## Production startup and page roles

The canonical launcher selects `--web-ui` in the Connections host and persistent local runner. The loopback host marks the served pages for that profile with `data-gridpico-profile="web-ui"` on the shared transport script. The marked profile identifies the default bindings below. Keep the role defaults in one shared browser definition. The host still obtains the actual direction and limit from each accepted private runtime binding. [Starter routes](local-router.md#production-web-ui-startup).

| Production tool | Role | Page direction | Default port | Starter bind |
| --- | --- | --- | --- | --- |
| Web APC | Keybed TX | Send | `1` | Once |
| Web APC | Panel TX | Send | `2` | Once |
| Web APC | LED RX | Receive | `3` | Once |
| Diagnostics | LED Sequence TX | Send | `4` | Operator connects and enables Raw through CLI |
| Diagnostics | Monitor RX | Receive | `5` | Once |
| Diagnostics | MIDI Log RX | Receive | `6` | Once |

The marker enables a single auto-bind attempt for each listed starter role when the page opens. Status and manual Connect/Retry stay available for each binding. A configured alternate is eligible only when the same role, direction and existing route intent are established by an explicit mapping. Try at most one such alternate when the match is unambiguous. The default profile supplies one candidate per role. Numeric adjacency, name similarity and direction alone do not establish role compatibility. Pages without the marker use explicit port selection. Direct-file launch offers Web MIDI by default and can navigate to a chosen served origin. Web MIDI permission starts with a user gesture and an explicit OS port choice for each role.

Web APC owns the production controller and LED rendering. Diagnostics owns routed LED replay, Monitor playback and MIDI Log. Monitor connects as its own sink and starts audio only after explicit user enablement. MIDI Log records its own received bytes. Keybed readiness depends on its own binding. Link to Diagnostics for sound and log; the router begins delivery to each destination once that destination is attached and active. A later attachment waits for a fresh press. The separate FM page and the published demo surfaces retain their own roles.

## Server selection and status

The server field accepts an origin with an optional trailing slash. Accept `http://` or `ws://` on `127.0.0.1`, and `https://` or `wss://` for the configured authenticated listener. Convert `ws` to `http` and `wss` to `https` for navigation, preserving the explicit host and port. Reject credentials, non-root paths, queries, fragments and plaintext remote addresses. Applying the field navigates to the current tool's path on the chosen origin (`/apc/`, `/diagnostics/` or `/fm/`). A file launch uses its corresponding tool path. The page then opens its own same-origin sockets. The established Host, Origin, TLS and publication rules apply. Serial stays on its local binding or the direct-file Web Serial path. [WSS admission](websocket-midi.md#cross-host-deployment-and-admission).

Reserve the exact provider, port and direction while a page binding is opening or active. Report **Busy** when that page already holds the reservation. Report **Unavailable** for a known missing or disconnected Web MIDI port. Distinguish a denied Web MIDI grant from an unsupported browser or context. If a WebSocket upgrade fails before `open`, show the selected port and say it may be busy or unavailable. The browser cannot read the HTTP rejection status. Use the existing binding lifecycle and per-role retry controls. A separate preflight would report a snapshot of occupancy, while the runtime alone grants the binding. This profile uses the binding result directly. [Browser WebSocket failure behavior](https://websockets.spec.whatwg.org/#feedback-from-the-protocol).

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

## Local device access

The Home-linked [Routing view](routing.md#routing-through-the-local-cli) opens at `/diagnostics/#routing` and uses the same selected serial session as the Diagnostics terminal. On a supported local page or direct-file launch, offer explicit **Loopback CLI** when the launcher supplies it and **Web Serial CDC** when the browser supports it. Open Web Serial from a user gesture and chooser. Use the existing bounded terminal byte contract on either provider, with one attachment and one serialized command queue. Changing providers retires pending writes and read callbacks for the old session and clears its inventory and route readback. Display the selected provider and endpoint. The operator confirms which router the terminal controls before issuing route edits. WSS publishes no serial endpoint. [Serial transcript contract](patchbay-controls.md#read-only-diagnostics-commands)

For Stage 3 CDC, start in plain transcript at 115200 baud, 8N1 with no flow control. Keep opening free of automatic CLI writes and deliberate DTR/RTS changes. On explicit **Check Routing**, send `routing` plus LF as the first structured request, even when the initial prompt was missed. Confirm capability only from the selected session's matching command echo, complete valid bounded CRLF replies and next prompt, and one coherent Routing revision through the last page. A leading initial prompt may be present. A failed check leaves the transcript usable. Before allowing edits, require explicit confirmation of the selected CDC endpoint and which router it controls. The reply establishes CLI capability. Operator selection establishes the intended router.

If the first read-only capability probe times out, keep the live session in plain transcript and require a fresh attachment before another structured probe. A confirmed CLI request timeout, session cancellation or provider loss retires outstanding work and invalidates partial or displayed Routing state. Close the selected Web Serial port after canceling reads and releasing both stream locks. Ignore late callbacks from the retired request or session. If an edit already reached the CLI, recover its outcome by readback on an explicit fresh connection. A CDC session never supplies same-router readiness for a WS/WSS Raw lease. Real CDC behavior, including any driver-controlled signal changes at open, requires its own hardware evidence.

For each MIDI role, retain independent explicit direction and port selection for Web MIDI or WS/WSS. Web APC has Keybed TX, Panel TX and LED RX. Diagnostics has LED Sequence TX, Monitor RX and Log RX. The FM app has its selected receive binding. Preserve the explicit starter mapping in the marked loopback profile. A permission grant or matching display name alone cannot select an ambiguous port. On Web MIDI device loss, retire that binding, clear held local controls or voices and require a verified fresh selection. Browser availability reports describe only the selected provider and port.

Direct Diagnostics-to-APC uses the documented solid and Off triplets on the manually selected OS output that feeds the actual APC Port 1 LED receiver. Report submitted bytes and independently check the physical lamp. This mode has no router lease or committed-page restore. Separately verify a real CDC port chooser, CLI help and inventory, loss and explicit reconnect while MIDI remains usable. Verify physical APC and actual Pico-mounted browser access on their nominated equipment. OS-presented RTP port selection and actual RTP receipt are separate evidence. [Browser Web MIDI](https://www.w3.org/TR/webmidi/) · [Web Serial](https://wicg.github.io/serial/)

## Bounded acceptance

Use the canonical launcher on the nominated user setup. Open Web APC and Diagnostics from the home, then send the solid/off pair and one Timed RGB Replay through the production router to the live Web APC receiver. Check exact bytes, green/dark and timed behavior, and committed restore. Exercise the staged home and its three relative links with Internet access off. Keep direct mounted-drive, OS-presented RTP and authenticated cross-host WebSocket results separate. For shared binding behavior, verify a real selected send and receive port, direction rejection, and disconnect cleanup on each provider being accepted. [Timed replay](local-router.md#timed-diagnostics-replay).

For starter behavior, open both production tools on a fresh canonical launch. Confirm the named directional bindings, enable Monitor audio in Diagnostics and press/release one APC key. Verify actual receipt by both Diagnostics sinks and silence on release. Exercise one occupied default with clear per-role feedback, one Web MIDI permission denial and an explicit retry or corrected selection. Check that entering a remote origin loads its tool page before binding. Confirm independent serial access and real Web MIDI port selection. Keep browser certificate and remote-receipt evidence scoped to the tested origin.

Independent testing of `b7d4fc6` exercised the exact launchers in Linux with a Compose stand-in. It verified starter delivery to both Diagnostics sinks, gesture-controlled audio, real CLI help, routed LEDs and restore, a second live loopback origin, and Web MIDI selection and device loss with real ALSA ports. The FM release `130c9ed` also verified the staged home and all three direct-file tool links. Windows/WSL startup, desktop browser cross-host mTLS, Web MIDI through an OS-presented RTP endpoint, and access from a mounted Pico drive remain separate checks. [Startup evidence](../ROADMAP.md#approved-follow-up--controller-and-diagnostics-startup) · [Instrument evidence](../ROADMAP.md#approved-tonight-delivery)
