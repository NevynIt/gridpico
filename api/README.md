# Gridpico public contracts

**Owner** Architect. **Status** Draft for planning review.

These are behavioral contracts for the module boundaries in [Architecture](../ARCHITECTURE.md). Command types, wire formats, storage encoding and language bindings will follow the work that needs them. Each module owns its state and returns portable values through its contract. Data crossing a queued boundary must remain valid until consumed. Application Assembly serializes musical state changes and defines how callbacks enter that context.

See the [Patchbay, inventory and Diagnostics proposal](patchbay.md) for the next delivery boundary.

The [Web Monitor destination contract](web-monitor-proposal.md) defines the named virtual Patch output, receiver lifecycle and destination-tagged delivery.

## Session/control

- Accept intent to change a binding, route or processor setting. Return an outcome that distinguishes invalid input, unsupported features, resource limits and temporary unavailability. An accepted route may be pending if its binding is unresolved. Queries expose the reason it is pending and which routes are active.
- Validate the whole configuration change before committing it. A rejected change leaves the applied configuration and its revision intact. A successful Apply publishes one coherent revision after reconciling the affected domains. Stage the replacement and arrange note cleanup before new traffic for affected routes can overtake it.
- Read authoritative domain values through a coherent snapshot. Snapshot revisions advance for observable changes such as a device disconnect. The applied configuration can still identify the revision at which it was committed. Change notifications identify the current snapshot revision. Frontends recover from skipped notifications or reconnects by reading a snapshot and submit commands only for new intent.

## MIDI graph

- Accept platform endpoint updates and normalized MIDI events. Keep logical binding, port or cable, channel where applicable, and connection lifetime distinct. Close a disconnected connection to further delivery. Reevaluate desired bindings on endpoint changes. Ambiguity leaves the corresponding routes pending until explicitly resolved.
- Route from a source through a bounded processor chain to each selected destination. Carry event origin across transformations. Preserve per-stream order, including SysEx fragment order and realtime messages. Keep USB framing in adapters. Reject configuration changes that create a feedback cycle visible inside the graph.
- Track each accepted note occurrence by ingress connection, channel and key, matching repeated presses to releases in arrival order. The graph keeps the bounded set of actual output obligations for each occurrence. For overlapping obligations on the same output, channel and key, the draft policy emits one Note On and releases the note when the last obligation ends. A release, stop, route edit or source disconnect retires only the obligations owned by the affected traffic. Sustain held by several contributors on one output channel follows the same last-contributor release rule.
- Admit a new note only when the graph can account for its output and retain capacity for cleanup. Order cleanup with accepted output or cancel unsent note starts before releasing their obligations. A rejected press must still be accounted for if its release could match an earlier press. If accounting cannot continue reliably, quiesce the affected ingress, clean up its obligations and report the fault. Resume at a known input boundary, such as a fresh connection after disconnect.
- If delivery of required cleanup becomes unavailable, block new performance traffic on the affected output and report the delivery fault. Retain bounded recovery obligations for that binding and reconcile them before reactivation on a verified reconnect. A replacement or ambiguous device requires explicit binding, and stale performance traffic is discarded.
- Keep SysEx in bounded fragments. Each destination has one active SysEx stream at a time, with realtime MIDI permitted during that stream. When a stream cannot continue within its budget, abort it explicitly at the affected destination, including terminating an already-emitted partial stream if reachable. Report the failure and free the stream slot.

## Processors

- Accept a normalized event and instance configuration. Return a bounded ordered set of results, including an empty result for a filtered event. Each processor declares its supported event kinds, expansion limit and bounded state requirements before activation. The graph also enforces total expansion across the chain and fan-out.
- Preserve event origin and expose which input occurrence generated each output note. A processor owns only its instance state. The graph keeps delivery and release accounting even when a processor is reset, removed or reconfigured.
- For the proposed transpose slice, shift each input key by the configured semitone offset and emit at most one corresponding note. Suppress a note whose resulting key is out of range. The graph resolves its eventual release from the original occurrence, including an occurrence that produced no output.

## Musical time

- Read monotonic time and request wakeups through platform services. Where transport is available, expose transport state, position and clock status through Session/control. Schedule work within the target profile and dispatch on the serialized musical context.
- Stop, clock loss and clock-source switches cancel pending work from the affected transport run. Retire its graph obligations before accepting work from a new run. After loss or a source switch, starting follows the new source's start policy. Live routing continues independently of transport.

## Future Sequencer boundary

Keep stable track and pattern identifiers independent of pad position when Sequencer is introduced. Playback will enter the graph as an ordinary source, with origin information preserved through routing. Recording will use explicitly selected graph feeds and exclude the track's own playback. Musical time will supply transport and scheduling. Sequencer can add a versioned project section through Projects. These dependencies reserve the boundary for the eventual implementation.

## Projects

- Decode saved configuration into a bounded draft and validate its version and contents. Loading a draft leaves the active configuration in place. Session/control applies it through normal domain validation and cleanup. Persist desired bindings and configuration. Live connection handles, held notes and playhead position belong to runtime state.
- Save a coherent applied revision and report which revision became durable. Serialize writes for a given store. Retain the previous committed version, when present, until the replacement is durable. On restart, recover the most recent complete committed version, or report that no usable version exists. Surface any backend-required interruption through Session/control before writing. The writable store is independent of the read-only `GRIDPICO` distribution image.

## Frontend and connection boundaries

- Physical and simulated APC surface messages enter the same production APC frontend and its feedback follows the corresponding surface connection. Keybed messages enter the graph as musical input. The browser simulator interprets the feedback it receives and sends APC MIDI gestures. APC page and gesture state belongs to the frontend.
- CLI and any later piano frontend use the same session contracts. Piano gesture handling retains the consume-or-forward choice through the matching release. The platform drives the physical feedback device from logical updates.
- Computer connections owns framing and connection lifecycle. It delivers APC traffic to the production frontend and authorized supplementary commands to Session/control. The receiving module validates and interprets the payload.

## Local browser demo connection

The connected demo serves the staged Web APC page and its connection adapter from the same local origin. Computer connections owns the host, browser adapter and WebSocket at `/demo/midi`. Use one keybed connection and one fresh fixture instance per accepted socket. The local host accepts another player after the previous fixture has finished cleanup. Application Assembly supplies the connected fixture runner and wires the production graph, session and transpose to the same simulator used by the native proof. The runner starts with `+12` applied through Session/control and empty destination capture. The native four-case command runs independently.

### Browser framing

- Use one JSON object per WebSocket text message, at most 256 UTF-8 bytes. The host sends `{"type":"ready"}` after fixture startup. The adapter then calls `window.gridpicoApc.connect(sendKeybed)`.
- In either direction a note is `{"type":"note-on","channel":1,"note":60,"velocity":96}` or the same shape with `"type":"note-off"`. Channel is 1-based and this demo uses channel `1`. Note is an integer `0–127`. Note On velocity is `1–127` and Note Off velocity is `0–127`. Preserve the staged surface's press velocity `96` and release velocity `0`. The host passes input in message order. The runner maps channel `1` to native index `0`.
- Each server note message represents one actual record accepted by the simulated destination. Deliver records once, in capture order, to `receiveCapture(event)`. Preserve their velocities, including `0`. A filtered event can yield no record. Reading, formatting or transmitting capture must leave routing and release delivery independent of browser read speed.
- On malformed input, a runner failure or transport overload, close the socket and retire the fixture. Bound pending transport work and use at most one runner request in flight. Close a stalled reader if necessary while continuing to drain the runner and complete cleanup.

### Host to fixture runner

Run one connected fixture process per accepted socket. Use ASCII lines ending in LF, with `on <channel> <note> <velocity>` and `off <channel> <note> <velocity>` on stdin. The runner writes `ready` once it can accept notes. After each request it writes any newly accepted destination records using the same `on`/`off` line format, then `ok` or `error` for that dispatch. Flush each response. Keep diagnostic text on stderr. Publish newly accepted records even after the simulator's bounded history wraps. Collect them at the destination acceptance boundary and write them after the graph call returns.

Computer connections closes runner stdin when the socket closes or fails. On EOF, Assembly's runner disconnects the current simulated input and lets the graph release its obligations before exit. The host drains the final release records even when the socket has closed. A fresh runner has a fresh graph and capture. The surface still sends Note Off on cancellation while the link is available, and the adapter calls its `close()` hook to clear local held state at disconnect.

Verify that playing browser key `60` on channel `1` produces an actual captured `note-on` for `72` with velocity `96`, followed by `note-off` for `72` with velocity `0`. Exercise cancellation with the link up and disconnect while a note is held. APC panel and LED traffic retain their separate path.

### Browser performance profile

The Web APC Route view uses the production graph's opt-in `gridpico_midi_graph_init_browser`, with fixed capacity for 25 outstanding input occurrences and 25 release credits. The keybed can hold one occurrence per playable control, including notes whose output was retired by a route change. A dedicated credit per possible sounding note bounds cleanup storage and protects Note Off delivery. The independent native proof keeps its two-occurrence, two-credit profile. A single Route disable or ingress cleanup can emit 25 releases, so the runner and host accept up to 25 destination rows per dispatch before LED feedback and the terminator. The 16-row history bounds retained diagnostic data. Accepted rows are forwarded in order even when history wraps.

One runner request in flight preserves ordered note, cleanup and LED replies. The host permits up to 64 queued requests so 25 presses, 25 cancellation releases and Route pad edges can fit even while dispatch is busy. The 8 KiB socket buffer bounds unread output and the existing deadlines bound retirement. The browser sends actual press and release events for its held controls. On a true 26th outstanding occurrence, quiesce the ingress, deliver owed zero-velocity releases and reject the new press through the existing cleanup and disconnect path. Transport overload and loss also retain their bounded retirement behavior.

The Web APC Monitor uses a local budget of 25 voices to match the keybed. Each sounding or releasing voice holds an oscillator and an envelope gain node. Release tails count toward the same budget. At capacity, Monitor reclaims a releasing voice first, then the oldest sounding voice if no release tail is available. This bounds connected Web Audio nodes during overlap. Accepted MIDI and release accounting use the separate browser graph profile. Independent Chromium testing measured three-tone and 25-tone signals and verified their cleanup. [Route-view verification](../ROADMAP.md#web-apc-rapid-play-regression-and-next-monitor-update)

The Web APC Route view offers **Reconnect** after an unexpected disconnect. The adapter closes the old attachment and calls `gridpicoApc.showDemoReconnect(reconnect, closeReason)`. Web APC displays the reason as text and starts one fresh socket for each explicit attempt, with at most one attempt in progress. The host can reject an attempt while the old runner is retiring. A later click can proceed once cleanup finishes. The new fixture starts with its usual route and empty capture and held-control state. Audible playback starts with a fresh key press after the user explicitly enables audio. Route gestures continue to set the route state.

The Route-view acceptance checks cover overlapping keyboard presses, the full 25-note cancellation burst and continued route control. They also cover cleanup beyond the 16-row retained history, a true 26th press and successful manual recovery after both overload and a real host stop. The four-case native proof remains a separate acceptance check.

## One-pad APC route toggle

This slice controls the existing demo route `1 → 2` with `+12` transpose. Use the shared [route types](native_proof.h) and [APC surface declarations](demo_apc_surface.h). Calls are serialized with note ingress. The native four-case command and the existing browser keybed, capture and Monitor note feed retain their entry points and formats.

### Route enable and release

- `gridpico_set_route_enabled_fn` sets the desired enablement of the one configured route. Session/control owns command validation and revision publication. Apply configures the slot and enables it on first success. Later Apply calls preserve the slot's desired enablement. Setting enablement on an empty slot returns `GRIDPICO_INVALID`.
- Snapshots distinguish `configured`, `enabled` and `active`. An empty slot has all three false. A configured route is active only when enabled, connected and available for delivery. Each successful enable command publishes one applied revision, including a request for the current setting. A same-setting request preserves sounding notes.
- On disable, the graph retires the route's sounding outputs before committing the disabled state. Emit one Note Off with velocity `0` per sounding output on its original destination, channel and key, using the existing release credits. Retain retired occurrences and track presses while disabled within the existing occurrence bound. Release of a retired or suppressed occurrence adds no destination row. Enablement preserves endpoints and transpose, with new Note Ons routed from that point forward.
- Retry pending releases on the current destination before a valid Apply or enable command can commit. If required release delivery fails, return `GRIDPICO_UNAVAILABLE`, keep the desired setting and applied revision, and retain the pending credit. The route reports inactive while output is blocked. A release already accepted remains effective.

### Surface and LED

Pad `0x00` is the bottom-left Clip Launch pad. The simulator emits channel 1 press `90 00 7F` and release `80 00 00`, in hex. The production APC frontend accepts nonzero channel 1 Note On for this pad as a press. A channel 1 Note Off or zero-velocity Note On for the pad clears the held gesture. The first press after release reads the Session snapshot and requests the opposite of `enabled`. Further presses while held and other well-formed surface messages have no action. The frontend owns this gesture state and the LED mapping. [Akai APC Key 25 mk2 Communications Protocol v1.1](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

The frontend reads a snapshot at surface startup and after each successful toggle. For an active route it emits solid green `96 00 15`. For an inactive route it emits dark `96 00 00`. The browser renders these as pad state, independently of its momentary pressed appearance. A failed toggle uses the existing dispatch `error` and connection cleanup path. The browser clears its received LED state on disconnect.

### Connected framing and order

- Keep one `/demo/midi` WebSocket and distinguish surface traffic by frame type. Surface frames in both directions are `{"type":"apc-surface","bytes":[144,0,127]}` with exactly three integers. The first byte is a channel-voice status `128–239` and the two data bytes are `0–127`. Existing note frames remain the musical and capture path. Computer connections maps a surface frame to or from the decimal runner line `surface <status> <data1> <data2>` using the existing ASCII LF and single-space framing.
- After the runner's `ready`, the pad-capable host dispatches one internal `surface-ready` line. The runner invokes the production APC frontend's startup refresh and responds `surface 150 0 21` followed by `ok` for an active startup route. The host then sends the existing browser `ready` message followed by that LED frame. A note-only client of the runner can continue to use the original `ready`, `on` and `off` dialogue.
- The browser adapter calls `gridpicoApc.connect(sendKeybed, sendSurface)`. The optional `sendSurface(bytes)` carries the three raw pad bytes. The returned `receiveSurface(bytes)` accepts only surface feedback. `receiveCapture(event)` continues to receive accepted destination note records. The surface remains playable as a standalone preview when no adapter is attached.
- Assembly wires surface requests to the APC frontend and its enable/query callbacks to Session/control. Stage accepted destination releases and the frontend's feedback during each serialized dispatch. Write capture `off` lines first, then the new `surface` LED line, then `ok`. Computer connections forwards those records in that order. A pad release or ignored message responds `ok` alone.

Acceptance starts with the green pad and a captured transposed Note On when `Q` is held. Press the pad. Capture one Note Off for the original note at velocity `0` before the dark LED. Then release `Q` and verify no second release. Press and release a key while off and verify no destination rows. Release and press the pad again. Check the green LED and a fresh `Q` producing the original `+12` pair. A key held through re-enable remains silent until a new press.

## Platform services

| Service | Minimum contract |
| --- | --- |
| MIDI | Report endpoint lifecycle and delivery availability. Receive and submit normalized messages and stream fragments with bounded buffering. Report queue rejection and connection loss to the graph. |
| Monotonic clock and wakeup | Supply elapsed time on one monotonic timebase per running instance. Wakeups hand control back to the assembly context. Report timing limits in the target profile. |
| Writable storage | Read the committed version and replace it recoverably. Surface unsupported storage, temporary unavailability, capacity limits and required interruptions. |
| Feedback | Accept bounded logical feedback updates for a supported device. Report unavailability so a frontend can refresh when the device returns. |
| Capabilities | Supply effective support and capacity values for the running target. The simulation backend provides the same observable outcomes and supports deterministic time and failure injection. |

## Capacity profiles

Application Assembly publishes effective bounds in agreed units using backend limits and domain costs. Each kernel configuration is validated against the selected profile, including aggregate resource use. Frontends use that profile for navigation and to explain capacity rejections. Numeric values depend on target measurements and deployment choices.

| Bound | Units and exhaustion behavior |
| --- | --- |
| Logical endpoints and graph topology | Count input endpoints, output endpoints, configured route branches, branches per source, processor instances, and processors per branch. Reject a configuration that exceeds any bound, including configuration for pending bindings. |
| Processor and note expansion | Bound emitted events per invocation and per routed input, tracked note occurrences, and output obligations including sustain. Reject an over-budget configuration. Apply the graph's admission and cleanup rules when runtime state fills. |
| Traffic and SysEx | Bound queue entries and bytes per queue, work per dispatch, SysEx streams and queued bytes. Reserve delivery capacity for releases and surface saturation. Abort a stream that exhausts its budget and report any loss of ingress synchronization. |
| Time | Bound pending scheduled events and publish clock and wakeup resolution plus the measured routing latency and jitter under a stated workload. Reject scheduling beyond capacity. When external transport is supported, report the clock-loss timeout or its derivation. |
| Configuration and projects | Bound bytes per stored project and decoded configuration resource counts. Reject oversize saves and loads before replacing committed data or applying a configuration. |

The five-source by eight-destination Patch page and the proposed RP2040 port allocation are sizing cases. Per-target profiles account for routed traffic, including transposition, fan-out, reconnection and simultaneous disk activity where supported. Tracks, patterns and recording-event counts are capacity dimensions for the future Sequencer feature.

## Linux x86_64 route and transpose proof

The native proof uses portable C11, native GCC and CMake in the existing Linux x86_64 container. [native_proof.h](native_proof.h) defines the shared values and call signatures. They are compiled together in one process with ordinary C calling conventions. Assembly owns the entry point, native CMake target and demo command. Assembly wires Session/control, the graph and transpose to the in-process simulator. Calls run serially to completion. The fixture uses standard C facilities and synchronous MIDI delivery.

### Shared values and lifetimes

- A normalized note has kind (`GRIDPICO_NOTE_ON` or `GRIDPICO_NOTE_OFF`), channel index `0–15`, key `0–127` and velocity. Display channels as `1–16`, so the fixture's channel 1 is index `0`. Note On velocity is `1–127`. Note Off velocity is `0–127`. An adapter receiving a zero-velocity Note On normalizes it to Note Off with velocity `0` before submission. Unknown kinds and out-of-range normalized fields return `GRIDPICO_INVALID` without changing routing or note state.
- The fixture registers logical input `1` and output `2`, each with connection generation `1` at startup. Logical endpoint IDs and connection generations are nonzero. A generation identifies one attachment of an endpoint. Apply addresses stable logical endpoints. Ingress and delivery carry the current connection identity, and delivery also retains the source identity. A stale or unavailable connection returns `GRIDPICO_UNAVAILABLE`. Keep borrow lifetimes to the call. A receiving module copies values it needs afterward.
- The graph invokes transpose on a valid Note On with an offset in `−127` to `+127`. The processor adds the offset using signed arithmetic wide enough for the sum. It returns one result with the changed key, preserving kind, channel and velocity, or returns false if the result is outside `0–127`. The graph records output obligations and resolves Note Off against the output previously delivered. Preserve the received velocity on a matched natural release. Use release velocity `0` for graph-generated cleanup.

### Apply, query and delivery

- Apply submits the full proposed route for the one route slot. Session/control checks for a missing request, a null or empty offset span, zero endpoint IDs or identical endpoints first. These return `GRIDPICO_INVALID`. A span larger than one instance then returns `GRIDPICO_CAPACITY` before any offsets are read. For a one-instance request, `−128` is invalid. The graph resolves the endpoint IDs and returns `GRIDPICO_INVALID` for any unregistered ID or wrong direction. The native fixture registers only input `1` and output `2`. A route to this registered pair can remain configured and inactive while the input is disconnected or quiesced.
- The graph owns route and note state. Session/control publishes configured and active status, endpoints, offset and the applied revision. The initial slot is empty at revision `0`. Every successful Apply republishes the snapshot and advances the applied revision once, including an identical Apply. An identical Apply preserves held notes and can retry previously failed cleanup. A rejected Apply preserves the configured route and its applied revision.
- Finish cleanup of prior output obligations before an accepted edit returns. Keep retired or filtered input occurrences until the corresponding Note Off arrives, so a late release cannot be matched to another press. A delivered Note On creates an output obligation only when the simulated destination returns `GRIDPICO_OK`. Successful delivery stores the event in the destination capture before returning. An unavailable destination returns `GRIDPICO_UNAVAILABLE` and adds no row. The capture is observed in delivery order.
- Match overlapping input occurrences in arrival order. One output connection, channel and key has one sounding note and one reserved release credit. The last contributor triggers its release. A failed required release retains its credit and blocks new performance on that output. The route stays configured and reports inactive while cleanup is pending. A subsequent valid Apply retries cleanup on the current destination before it can succeed. A failed retry returns `GRIDPICO_UNAVAILABLE`. Releases already accepted at the destination remain effective. Destination reconnection and identity recovery are outside the native proof.
- Explicit input disconnect retires that connection's input occurrences and attempts cleanup. Attaching the registered input with a fresh generation restores input availability, subject to any pending output cleanup. Events from older generations return `GRIDPICO_UNAVAILABLE`.

### Fixture capacities

| Resource | Bound and behavior |
| --- | --- |
| Topology | One input, one output, one route branch and one transpose instance. A well-formed proposal requiring a second instance returns `GRIDPICO_CAPACITY` before any mutation. |
| Dispatch | One in-flight ingress or control call at a time. An ordinary note produces at most one delivery. An Apply or ingress cleanup can release up to two held outputs. |
| Note accounting and release | Two outstanding input occurrences, including silent or retired occurrences still awaiting Note Off. Hold one dedicated release credit per sounding output before accepting its Note On, with two credits total. Credits stay available to release existing notes even when ordinary admission is full. If a new press exceeds the occurrence bound, reject it with `GRIDPICO_CAPACITY`, quiesce the ingress and release its sounding outputs using those credits. Resume on a fresh connection. |
| Destination capture | Keep the latest 16 accepted deliveries. A full history evicts the oldest row and continues accepting deliveries. Reading history leaves admission and release credit unchanged. The four-case native trace fits in eight rows. |

### Deterministic trace

The fixture submits all normal presses at velocity `64` and their releases at velocity `32`, on channel index `0`. Start with an empty capture. Verify the selected destination, exact normalized fields and exact delivery order in each case.

The four cases below are the native demo acceptance trace. Overlap, input recovery and cleanup retry follow the rules above and can be checked independently of that trace.

| Case | Expected result |
| --- | --- |
| Apply `+12`, then press and release key `60` | Active at revision `1`. Capture On `72/64`, Off `72/32`, where each pair is `key/velocity`. |
| Press and release key `120` | Capture no rows. |
| Hold key `60`, apply `+7`, then release and play key `60` again | Capture On `72/64`, Off `72/0` during Apply, then On `67/64`, Off `67/32` from the new press and release. The late source release after Apply adds no row. The applied revision becomes `2`. |
| Propose offsets `[+7, +12]`, then press and release key `60` | Apply returns `GRIDPICO_CAPACITY`. The snapshot stays active at revision `2` with `+7`. Capture On `67/64`, Off `67/32`. |

OS MIDI, USB, browser interaction, project recovery, transport and broader timing evidence are later checks. A later browser instance can reuse this route setup and destination capture with its own input connection.
