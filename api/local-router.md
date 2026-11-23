# Local production router contract

**Owner** Architect. **Status** Approved first local gate. [C11 declarations](local_router.h) · [Roadmap](../ROADMAP.md#proposal-for-approval-midi-and-serial-cli-across-gridpico-hosts).

MIDI and the serial CLI byte stream are the external product interfaces. Session and Graph own the internal registry, generations, route state, accepted-delivery traces and note-release accounting. One Assembly-owned router serves independent Connections peers. Each peer can leave while the runtime and other peers continue. The published Route, Patch and native proof runners retain their separate profiles and entry points.

## Registry and attachment

Assembly passes one bounded `gridpico_local_registry` at startup. Its registered devices and ports stay stable for that runtime. Graph validates nonzero unique IDs within each record kind, valid device references, initial disconnected state, counts, direction and capability. Each capability is indexed in `inventory.ports` order. Session exposes the same inventory via the existing CLI. A newly bound transport selects a registered directional port. A browser can use CLI inventory or a configured set of named bindings to make that selection. It does not submit registry records or generations.

| Default device | Port ID and name | Graph direction | Capability |
| --- | --- | --- | --- |
| `1` Web APC | `1` Keybed | Input | `GRIDPICO_LOCAL_PORT_MANAGED` |
| `1` Web APC | `2` Panel | Input | `GRIDPICO_LOCAL_PORT_PANEL` |
| `1` Web APC | `3` LED feedback | Output | `GRIDPICO_LOCAL_PORT_RAW` |
| `2` Diagnostics | `4` LED Sequence | Input | `GRIDPICO_LOCAL_PORT_RAW` |
| `3` Web Monitor | `5` Monitor | Output | `GRIDPICO_LOCAL_PORT_MANAGED` |
| `4` MIDI Log | `6` Log | Output | `GRIDPICO_LOCAL_PORT_MANAGED` |

The IDs identify the first application's configured bindings. The Graph works from the registry. Port 0 on the virtual APC is keybed TX. Port 1 is panel TX and LED RX. The two Port 1 directions have distinct graph IDs. Only managed endpoints are eligible for the shared Patch page in this profile. Raw and panel bindings appear in inventory without a Patch assignment. The panel ingress goes to the existing production APC frontend.

Graph starts MIDI generations at dormant `1`. Connections supplies `2` at first bind and a strictly newer nonzero `uint32_t` for each replacement. Never wrap. Assembly serializes readiness, detach and replacement through `gridpico_patch_port_update`. Only one live binding is allowed per direction. A detach applies to its exact generation. Reject conflicting, unknown or ambiguous bindings and preserve the current binding. A detached managed route retains desired state and is Pending when unavailable. Only verified replacement generations may reconcile release debt. A stale event leaves the current binding and route unchanged.

## Serial CLI route control

Reuse `inventory`, `status`, `logs` and their existing records. `status` continues to count the managed Patch page. The raw branch has typed readback through `route`.

| Command | Effect |
| --- | --- |
| `route SRC DST managed` or `route SRC DST raw` | Read a compatible directed pair and mode. |
| `route SRC DST managed on` or `route SRC DST raw on` | Enable that route and read back its committed state. |
| `route SRC DST managed off` or `route SRC DST raw off` | Disable that route and read back its committed state. |

Use the existing CLI line limit of 63 ASCII bytes before LF. Use one ASCII space between tokens. IDs are unsigned decimal `1–65535`. Keywords are lower case. Emit `ROUTE SRC DST MANAGED|RAW OFF|PENDING|ACTIVE` followed by `OK`, each with CRLF. An error emits only the existing `ERR INVALID`, `ERR CAPACITY` or `ERR UNAVAILABLE` record and CRLF. A valid, compatible pair with no enabled branch reads Off. An absent disable and identical enabled route are idempotent. Unknown endpoints, wrong directions, incompatible capabilities and cycles are invalid. Capacity rejection leaves the desired setting unchanged. A failed required managed release retains debt and the desired setting under existing Patch semantics. A known disconnected managed pair can be enabled Pending. Raw enable requires both live bindings and an available feedback receiver. CLI obtains the typed set/read callbacks at local runtime wiring. Legacy CLI profiles retain their existing commands.

## Production graph

Graph supplies `gridpico_patch_graph_init_local(graph, registry, deliver_note, deliver_raw, delivery_context)` with managed and raw egress callbacks. Its concrete entry points are `gridpico_patch_graph_set_local_route`, `gridpico_patch_graph_read_local_route` and `gridpico_patch_graph_submit_raw`. The C declarations give the exact callback shapes. The typed reader supplies the graph revision and committed state. Session binds the setter and reader through `gridpico_patch_session_bind_local_routes(session, set, read)` and exposes `gridpico_patch_session_set_local_route` and `gridpico_patch_session_read_local_route`. The callbacks use the same supplied context. Assembly can wrap the graph calls to coordinate feedback. Endpoint lifecycle stays at the graph update boundary.

- **Managed.** Decode `8n` Note Off and `9n` Note On from a managed input. Normalize a zero-velocity On to Off and preserve explicit Off velocity. Feed the existing Note On/Off admission, overlap, fan-out, debt and reconnect path. Egress sends standard three-byte Note On or Note Off. Scope cleanup to the affected contribution. A held note reaches each accepting output once and gets its owed release even when a route is edited. A fresh receiver waits for a new press. Monitor and MIDI Log are independent ordinary sinks. The Log records its own received bytes. Observer evidence stays inside the runtime or test instruments.
- **Raw.** Accept complete three-byte MIDI 1.0 channel messages with high nibble `8`, `9`, `A`, `B` or `E` and both data bytes at most `127`. All other statuses, lengths, running status and SysEx are invalid in this profile. Dispatch from a raw input to a raw output in received order. Preserve all three bytes, including zero velocity. The raw path does not allocate note occurrences or release credits. A valid frame with no active route produces no destination delivery. Panel and managed inputs cannot select Raw to circumvent their own dispatch path. The first sequence exercises solid pad feedback and Off.
- **Topology.** A route key combines source, destination and mode. Each registered directional pair supports at most one mode. Admit at most two managed and one raw branch globally. For an enabled router edge, use the source parent `DeviceId` to the destination parent `DeviceId`. Reject a self-edge or an edge that closes a directed cycle, including enabled Pending branches. Parallel acyclic edges are allowed. Dedicated APC feedback is a new emission after frontend processing and is outside route-cycle detection. The graph admits no loopback exception at this gate.

Legacy `gridpico_patch_graph_set_route` on the local graph delegates to guarded managed routing. Legacy `read_graph` projects enabled managed branches only with the shared revision. The typed reader also sees Raw. Shared device array backing grows to four. Other published counts stay unchanged. The local profile admits 25 input occurrences and 50 managed release credits for two accepting destinations. Raw delivery uses the same serialized dispatch and bounded acceptance boundary, with its own byte values.

## Exclusive APC feedback

Selecting Raw `on` from the Diagnostics source to the configured APC feedback receiver grants exclusive writes to that whole receiver for at most 30 seconds of monotonic time. An identical `on` preserves the deadline. Ordinary APC input and Session updates continue. Assembly suppresses ordinary feedback for that receiver while the lease is active. The Graph and Connections treat the port as a bound set of bytes. Assembly reuses the production APC frontend for feedback.

Terminate the run on explicit `off`, completion by the sequence operator, timeout, affected peer loss or a delivery fault. Commit Raw Off, retire and close the Diagnostics raw TX generation, and discard its undispatched work. Frames already sent on the feedback socket precede the restore. Stage a full current committed APC feedback page for the current receiver before resuming incremental feedback. If the page cannot be admitted or the receiver is gone, leave feedback awaiting a full refresh on a verified attachment and stop incremental delivery until the refresh is accepted. A new run requires a fresh Diagnostics raw binding and explicit `on`. Existing panel, keybed, Monitor, Log and serial bindings retain their independent lifetimes.

## Loopback transport

Use the [standard WebSocket upgrade](https://www.rfc-editor.org/info/rfc6455/) on IPv4 loopback, with the URL path selecting the binding. Served pages and compatible native WebSocket clients use the same wire. The host accepts the exact `Host` and `Origin` of its own served pages. For a host listening at `127.0.0.1:8767`, a native client connects to `ws://127.0.0.1:8767` and supplies `Origin: http://127.0.0.1:8767`. The browser supplies the origin when it opens the socket from a served page. Direct-file HTML uses Web MIDI and Web Serial where supported. A network-facing listener and OS-visible virtual ports are separate follow-on proposals.

| Path | Payload | Binding |
| --- | --- | --- |
| `/midi/<port-id>` | Exactly one complete three-byte MIDI message per frame. | A graph input receives bytes from the client. A graph output sends bytes to the client. The successful upgrade means the direction is bound and ready. |
| `/serial` | A CLI byte stream in binary chunks of 1–256 bytes. Chunks can cross line or UTF-8 boundaries. | One independent serial peer, one active command at a time. Keep the existing byte parser and reply formatting. Reset partial input on retirement. |

Reject wrong-direction writes, malformed frames and duplicate live bindings at their transport boundary. Reject invalid MIDI input without delivery. The upgrade or connection close conveys binding success or failure. The host sends no JSON inventory, route state, accepted-delivery or readiness envelope. Registration changes, routing and receiver restoration happen in the persistent router. Serial is independent of MIDI queues and dispatch results.

For a successful MIDI `bind`, the private runner reply starts with `bound PORT GEN in|out LIMIT`, before any staged MIDI for that binding and before the final `ok`. The direction is from the Graph's viewpoint. The runtime selects it and the positive queue limit from the registered direction and role. Connections matches the exact outstanding port and generation, validates `LIMIT` within `1–128`, and uses it for that binding's input or output queue. It waits for the successful reply before upgrading the client. The browser helper uses a generic ceiling of 128 pending triplets (384 payload bytes) for each MIDI send socket. Host queue limits remain authoritative.

Before binding, the host checks the port ID syntax and bounds pending and live MIDI peers to six in this profile, plus the independent serial peer. The runtime decides which IDs are registered. Record generations from a successful `bound` or an exact `fault midi PORT GEN` retirement reported by the runtime. If a bind consumed the generation and retired it before reporting success, the runtime reports that fault before `err`. A rejected unknown ID consumes neither a generation nor persistent host state.

The default roles use at most 64 queued Keybed, 128 queued Panel and 64 queued LED Sequence input frames, 64 queued frames per output binding, and one runtime dispatch in flight. The Panel bound fits the 108 published Patch edges and a complete press/cancel of 57 button controls at 114 edges. Knob movement is separate traffic. The keybed bound fits 25 presses and 25 releases. A feedback output queue can admit an entire 40-frame page. Keep at most 8 KiB queued serial bytes in each direction and a five-second reader or dispatch stall deadline. Connections owns network admission and stall handling. Assembly owns nonblocking per-binding staging and returns success to Graph only when the matching generation accepted an egress frame. Output exhaustion leaves managed release debt subject to existing reconciliation. Retire a stalled binding and preserve other peers. A failed CLI writer records queue overflow and retires that serial generation, preserving the router and MIDI peers. At whole-runtime shutdown, retire inputs while sinks can still accept owed releases, then retire outputs and drain the private pipe through exit.

## Integration and evidence

Assembly builds `gridpico_local_router` as an additive C11/CMake executable. The loopback host uses the existing Node.js and locked `ws` dependency and starts one persistent child. Assembly and Connections own their private process pipe and can tap it in tests. External product interfaces remain the per-port MIDI frames and CLI bytes. Core Graph and Session plus the CLI compile without Node or a network stack. Use the existing APC frontend and in-process staging. Linux x86_64 is the first build target. RTP, ALSA and Pico transport contracts have their own increment.

Use CLI commands on the running host to enable `route 4 3 raw on`, `route 1 5 managed on` and `route 1 6 managed on` for the default registry. Verify `96 00 15` and `96 00 00` byte for byte at ingress, accepted egress and the actual Port 1 receiver. Check the lamp, wrong port/source, disabled route, stale attachment and restore under both normal completion and disconnect. Record the private runtime trace through a pipe tap or owned callback. Verify an ordinary panel gesture still reaches production APC and returns committed MIDI. Hold a note, disable Log and observe its release while Monitor keeps sounding, then reconnect Log without replay. Release the source and observe one Monitor release. Verify CLI framing and continued MIDI during a stalled serial peer. Preserve the published proofs. Independent browser acceptance verifies the same MIDI bytes and rendering at the actual pages. Use a plain WebSocket client as an external interoperability check. An alternate port ID in a private binding test verifies that Connections takes direction and limits from the runtime.
