# Configurable router profiles and Routing

**Owner** Architect. **Status** Desktop Routing and the passive APC viewport are independently verified. Physical USB identity replacement retains a separate hardware gate. [Desktop acceptance](#both-axis-acceptance-registration) · [Local router](local-router.md) · [Browser MIDI](browser-midi.md)

## Profile and admission

Application Assembly supplies one startup registry and resource profile. Register named devices and directional logical ports with unique IDs, parent device, role, direction, capability and bounded queue resources. Validate the entire configuration before readiness. Port IDs and order remain stable during a run. Keep the six starter roles and their IDs where configured. The existing default, native and demo profiles retain their respective limits and behavior.

Assembly provides the effective peer admission limit to Connections on their private readiness boundary. The existing private bind reply remains authoritative for each port's direction, queue limit and generation. Graph enforces topology, branch and fan-out budgets. [Current-message dispatch](local-router.md#current-message-managed-routing) defines live delivery and its errors. Session projects the named inventory and route state. Connections bounds pending as well as live peers and queues, with the serial attachment counted separately. On WSS, publication remains an explicit set of directional IDs in the selected profile. Registration alone never publishes a port.

The verified [desktop configuration](../app/profiles/desktop.conf) registers 8 devices and 18 directional ports with 18 socket peer slots, 12 managed branches plus one exclusive Raw branch, four managed destinations per source. Its six managed inputs, nine managed outputs and three dedicated Panel/Raw ports exercise both APC viewport axes. Independent loopback testing admitted all 18 MIDI peers alongside serial and verified scoped saturation handling. The six-port default retains its smaller limits. These are desktop profile values. Each target supplies its own measured limits. Offer each incoming message to its current destinations independently.

### C11 integration preflight

Use the existing `gridpico_local_registry` inventory plus its capabilities vector in port registration order. [Graph types](local_router.h) carry the `gridpico_local_profile` limits alongside it. Counts come from the registry. Keep per-direction queue limits explicit in the bind result and cap Raw at one exclusive branch. Count enabled Pending branches against the configured branch and fan-out limits. The configured initializer copies the registry and limits on success. Existing initializers keep their signatures and prior active counts. Check all counts, per-source fan-out and staging resources against backing before publishing the profile. Assembly owns the startup configuration and validation.

The earlier 8-device, 18-port, 13-branch desktop build staged up to 285 MIDI frames per dispatch with 18 additional fault slots. Its historical bound included 200 release records. Current-message dispatch needs staging for the worst live fan-out, feedback and fault paths. Desktop binding queues are 64 frames except the 128-frame Panel input. Serial retains its independent 8 KiB queue in each direction.

The integrated x86-64 build measures 1,200 bytes for inventory, 1,320 for Patch graph state, 1,400 for the Patch snapshot, 8,440 for graph storage, 208 for Session, 3,636 for output staging and 26,296 for the local runtime. The Routing snapshot is 1,656 bytes and a viewport page is 232 bytes. These are compiled sizes, independent of a fixture's admitted workload. Rebuild consumers of the widened C structs together. Native transpose and native Patch proof traces matched the published baselines.

The private runner sends `ready N` for the configured profile. The canonical positive decimal `N` equals the socket peer limit. The runner validates it against the registered port count and current backing before readiness. The existing unconfigured runner can send bare `ready`, meaning the current six-peer baseline. Connections checks the syntax and directional ID ceiling of 65,535, then uses `N` for pending plus live MIDI reservations. Serial remains independent. Keep the existing `bound PORT GEN in|out LIMIT` reply unchanged.

The [Routing C11 types](routing.h) declare the caller-owned CLI and viewport projections. The module owners expose these concrete entry points in their own headers.

```c
gridpico_result gridpico_patch_graph_init_local_profile(
    gridpico_patch_graph *graph, const gridpico_local_registry *registry,
    const gridpico_local_profile *profile, gridpico_deliver_note_fn deliver_note,
    gridpico_local_deliver_raw_fn deliver_raw, void *delivery_context);
gridpico_result gridpico_patch_graph_read_local_state(
    void *context, gridpico_local_graph_state *out);

void gridpico_patch_session_bind_local_routing(
    gridpico_patch_session *session, gridpico_local_read_graph_fn read_graph,
    gridpico_local_read_availability_fn read_availability, void *availability_context);
gridpico_result gridpico_patch_session_read_local_routing(
    void *context, gridpico_local_routing_snapshot *out);
gridpico_result gridpico_patch_session_read_local_patch_page(
    void *context, const gridpico_local_patch_viewport *request,
    gridpico_local_patch_page *out);
```

Graph's local read copies registry and enabled managed and Raw branches under one graph revision. Its existing Patch graph read keeps the managed-only projection. Session reads the local graph through its existing graph context. Assembly supplies one authoritative availability value per port in registration order through the separate availability context. Session adds the current logical device colors in device order and supplies the nonzero CLI projection revision. No provider retains any borrowed pointer. Failed reads expose no partial snapshot or page.

The CLI frontend adds an optional `gridpico_local_read_routing_fn read_local_routing` beside its existing route callbacks and calls it with `session_context`. The CLI owns paging and text formatting. It encodes a page from one completed projection. The APC frontend uses `gridpico_local_read_patch_page_fn` with the Session context. Offsets are zero-based in eligible port order. Clamp each from zero through the axis count minus the viewport size, or zero when the axis is smaller. Return the effective offsets with both total axis counts. Unused slots contain zero IDs and Off. The page needs no CLI availability read. The frontend retains its viewport and Session supplies the observed graph revision and committed states.

### Both-axis acceptance registration

| Device ID and label | Directional port IDs |
| --- | --- |
| 1 Web APC | 1 Keybed managed in, 2 Panel in, 3 LED feedback Raw out |
| 2 Diagnostics | 4 LED Sequence Raw in |
| 3 Web Monitor | 5 Monitor managed out |
| 4 MIDI Log | 6 Log managed out |
| 5 Keys A | 7–9 managed inputs A1–A3 |
| 6 Keys B | 10–11 managed inputs B1–B2 |
| 7 Instruments A | 12–15 managed outputs A1–A4 |
| 8 Instruments B | 16–18 managed outputs B1–B3 |

Use this registration order. The sixth managed source is 11 and the ninth managed destination is 18. Pan down and right and read back an edit to `route 11 18 managed on`. Separately enable 1 → 5, 6, 12, 13 and 7 → 14, 15, 16, 17 with those sinks attached. Send explicit On and Off messages and verify fan-out, then exercise one branch disable and a stalled receiver without affecting the other receivers. Teardown emits no MIDI. Preserve the exact traces of the legacy proof and demo profiles.

Prove useful concurrent traffic with more than six MIDI attachments and an independent serial reader. Exercise duplicate and wrong-direction rejection, profile-limit rejection, and another binding's continued progress under a slow peer. Verify Routing reads all pages with maximum-length names, discards a stale continuation after a port or route change, and shows a rejected edit without changing the projected route.

Historical desktop tests on the former cleanup implementation verified 200 exact attacks and 200 zero-velocity releases at eight binary receivers during live detach. Orderly runner EOF separately staged and accepted 200 owed releases while the sinks were available. The same gate verified Routing paging, stale continuation rejection, and the Free, In use and pool-full Unavailable transitions. [Routing CLI](#routing-through-the-local-cli)

## Routing through the local CLI

Home links to `/diagnostics/#routing`. The Routing view and Diagnostics transcript share one selected serial session and one serialized command queue. Allow one command in flight. Identify the selected router and serial provider before enabling edits. The view reads the same CLI as a terminal and submits the existing `route SRC DST managed|raw [on|off]` commands. It renders committed readback. A rejected edit preserves authoritative state. Raw LED replay still uses an explicit lease and the designated LED receiver.

### Separate Routing tool

In the first Routing remediation slice, canonical loopback Home and tool navigation link to `/routing/`. Diagnostics retains the terminal, Monitor, Log and LED workflows. The old served `/diagnostics/#routing` entry hands off to the separate tool. The staged direct-file Diagnostics page keeps its in-file Routing view and CDC path. Reuse the existing serialized command client and selected local CLI or Web Serial provider. A navigation releases the previous page's attachment before a fresh attachment. One serial attachment per runtime remains the limit. A second tab encountering an existing owner reports that contention without evicting it. This slice retains the current WSS and trusted-LAN page exposure. No routing HTTP API or second serial wire is added.

Render managed input rows and output columns using registered names and logical IDs from one coherent `routing` snapshot. The existing verified loopback flow submits `route SRC DST managed on|off` and reloads the authoritative projection. The next target Routing increment uses [guarded edits](#target-routing-and-guarded-edits). Keep the last confirmed state while awaiting readback. Refuse changes on a stale, partial or unconfirmed projection. Keep port availability separate from Off, Pending and Active. An unavailable binding on a known compatible pair can still have its desired route edited. The selected provider may leave it Pending or reject a change under its admission rules. Accessible text must distinguish enabled Pending from enabled Active, and distinguish an incompatible crosspoint from a temporarily unavailable port. Preserve the separately scoped Panel and Raw inventory. A page with no authorized CLI snapshot explains that Routing is unavailable and offers the applicable local, SSH or device-provider path. LAN and WSS keep their existing serial admission.

Verify a real named crosspoint edit and restore through the production CLI, with matching readback and feedback. Also exercise a Pending enabled route, a stale continuation or rejected edit, one no-CLI endpoint, and the separate Diagnostics workflow. The visual review covers readable headings, crosspoint labels and keyboard flow in representative desktop and narrow views.

The canonical loopback `--web-ui` runtime enables the Routing reader even with the six-port default registry. An explicit configured profile also enables it. The reader adds no ports, routes or APC modes. Bare unconfigured runners and legacy demo profiles keep their existing CLI exposure. WSS and direct-file hardware access keep their separate gates.

Show named source rows and destination columns with stable logical IDs, direction and capability. The managed matrix shows eligible source and destination pairs. Show dedicated Panel and Raw ports in inventory, and show the configured Raw pair separately. Show binding availability independently from route state. Free means an eligible local binding can currently be claimed, In use means a live attachment or an admitted reservation owns it, and Unavailable means the authoritative adapter has no usable attachment path, including disconnection or ambiguous identity. Availability is scoped to this runtime. A serial snapshot does not reserve a port or establish WSS publication or global OS occupancy. Route state is Off, Pending or Active, with Pending and Active both enabled. Mark incomplete or stale state accordingly. A failed browser WebSocket upgrade alone establishes neither occupancy nor availability.

Add one bounded, read-only CLI operation for the Routing view on a profile that supplies the Routing provider. `routing` starts at the first record. `routing REV OFFSET` continues a nonzero decimal snapshot revision at the supplied unsigned decimal record offset. Keep existing line limits and CRLF reply framing.

| Ordered record | Fields |
| --- | --- |
| Page header | `ROUTING REV NEXT`, with the next offset or `-` at the end |
| Device | Existing `DEVICE ID STATE COLOR "name"` record |
| Port | `BINDING PORT DEVICE in\|out managed\|raw\|panel free\|in-use\|unavailable "name"` |
| Enabled route | Existing `ROUTE SRC DST MANAGED\|RAW PENDING\|ACTIVE` record |
| Terminator | Existing `OK` or a standalone `ERR INVALID\|CAPACITY\|UNAVAILABLE` |

Emit up to eight records per page after the header, with devices and ports in registration order and enabled routes ordered by source ID, destination ID and mode. Use the existing bounded names and JSON string quoting, and at most 256 bytes per reply record including CRLF. Only registered compatible pairs can be shown Off by absence from the complete enabled-route set. Session supplies a coherent projection from Graph and authoritative binding observations, serialized by Assembly. A nonzero, runtime-scoped revision changes when any projected value changes. A changed revision makes continuation return standalone `ERR UNAVAILABLE`. The client stages pages, publishes the matrix only after a complete coherent read, and can restart at the first page. Keep legacy inventory, status, logs and help framing. List `routing` in help only when the provider is available. Routing uses serial CLI bytes only.

### Target Routing and guarded edits

On a paired provider supporting deliberate Managed edits, the browser accepts coherent committed records for eligible Managed directions of the same parent and exposes those pairs through the existing revision-guarded CLI commands. Use the same directional, role and resource rules as the provider. Pending remains enabled and unavailable. Keep Raw validation and lease authority separate. Legacy providers retain their existing compatibility and readback behavior.

The first Pico increment exposes the installed router's registered managed input and output ports and routes over its existing CDC CLI. It uses the same Session and Graph as the physical Patchbay. It adds no device discovery, USB roles, transport or route model. The physical APC layout remains a separate view of these logical routes.

| Owner and paths | Contract |
| --- | --- |
| Application Assembly · `app/rp2040_router.c`, `app/rp2040_router.h`, `platform/rp2040/router/firmware.c` | Bind the existing Session Routing reader to the live Graph reader and a Pico availability provider. Supply the CLI with thin read/route adapters on the existing router instance. Obtain availability from verified current directional attachments, with In use for an attached path and Unavailable for a disconnected or unresolved physical/native path. Use Free only for an actually claimable unowned binding. Compact the typed projection in place to omit the private Audition port, its device and its branches. Keep inventory order, capabilities and availability aligned. Reject private endpoints and Raw pair operations in the Pico CLI adapters. Managed edits delegate to the same Session/Graph setter as the physical surface. Let existing foreground Panel refresh observe the committed revision outside the CLI call. |
| CLI Frontend · `frontends/cli/cli_frontend.c`, `frontends/cli/cli_frontend.h` | Reuse existing `routing`, `routing REV OFFSET` and `route` handlers and record formats. Bind a separate target routing context and borrowed caller-owned projection scratch, as below. Emit only the bounded typed rows on this command. Advertise the guarded form in actual help only where the matching reader and managed setter are present. |
| Session/control · `core/session/patch_session.c` | Supply the coherent revisioned projection. Preserve inactive desired branches as Pending where appropriate. A compact public projection can retain the underlying Routing revision, so a private change may conservatively make a continuation or edit stale. Reuse the existing generation, release accounting and graph validation paths. |
| Application Assembly · `app/local_runtime.c` | Bind the same CLI interface to the Pi router's current providers and owned scratch on routing-enabled profiles. Keep real Pi port availability and existing route semantics. Preserve the recorder reservation, its route and the trusted-LAN listener. |
| Computer Connections · `connections/diagnostics/index.html` | For the Pi Routing view use the selected loopback CLI and its actual help and readback. Stage one complete coherent snapshot and write a crosspoint only through the guarded form below. Preserve the separate opaque Pico console. The first Pico live browser proof uses the already admitted same-origin Diagnostics terminal for explicit commands. A typed Pico browser Routing surface needs its own selected-provider review and gate. |

Add this internal CLI binding in `frontends/cli/cli_frontend.h` before processing commands. `context` is shared only by these routing callbacks. A route-only runtime may leave the projection reader and scratch absent. A runtime advertising `routing` supplies both. Keep the other terminal providers on their existing context.

```c
void gridpico_cli_bind_routing(gridpico_cli_frontend *frontend, void *context,
                              gridpico_local_read_routing_fn read_routing,
                              gridpico_local_read_route_fn read_route,
                              gridpico_local_set_route_fn set_route,
                              gridpico_local_routing_snapshot *scratch);
```

The compact serial projection uses `DEVICE`, `BINDING` and enabled `ROUTE` rows, eight records per page and at most 256 bytes per record, plus the existing header and terminator. Pico Assembly supplies one static projection scratch for the serialized CLI, reused for paging and guarded writes. Keep a full projection out of the CLI stack. Reuse existing CDC staging and pump bounds. Check full 64-bit revision formatting, exact linked foreground and IRQ stack, SRAM and flash extent before a target build is eligible. [Pico terminal and limits](pico-target.md#first-shared-cli-on-pico-and-pi)

For the browser edit use exactly `route SRC DST managed on|off if REV`, with a nonzero decimal `uint64_t` revision from the complete `routing` snapshot. The longest valid command fits the existing 63-byte line limit. The CLI re-reads the selected target's projection in the same serialized command, compares the revision and invokes the existing typed setter without yielding or writing a reply between comparison and mutation. A malformed guard or a guard on Raw is `ERR INVALID`. A stale revision or absent provider is `ERR UNAVAILABLE` with no mutation. Successful edits retain the existing committed `ROUTE` record and `OK`, then the client reloads the projection. Existing unconditional terminal forms retain their semantics. Raw remains on its separate lease. In the browser, scope the revision to the selected serial lease and target identity, and clear edit readiness on target change, disconnect, timeout or invalid paging. A MIDI target change alone leaves this authority unchanged.

For a qualified typed Routing provider, validate each enabled `ROUTE` row against its registered directions and capabilities. Accept authoritative same-device Managed rows. Offer deliberate edits for eligible pairs under the selected target's current typed guard. Shared parent alone cannot hide the cell or establish a loop. An older provider retains its documented limits. Preserve Raw eligibility and the existing serial provider and grants. [Deliberate same-device Managed routes](local-router.md#deliberate-same-device-managed-routes).

Verify first on the real **Pi router**, through strict SSH loopback and its stock browser Routing page. Match inventory, availability and managed route readback against direct terminal commands on that instance. Exercise one independent named route Off/Pending/Active and restore it, plus stale paging and a rejected stale guarded edit. Verify the typed browser uses the returned revision once the guarded provider is installed. Preserve attached recorder authority and the finalized WAV. A host fixture gives supporting tests.

Verify separately on the **physical Pico** with the exact firmware and selected configured rig. Direct USB CDC and the admitted Diagnostics terminal must return the same target's actual `help routing`, coherent paged rows and pair readback. On the named APC-to-MicroFreak branch, observe a physical route change after a captured revision, a stale guarded command rejected without mutation, and a fresh guarded edit reflected in physical Panel feedback and independently observed MIDI at the selected output. Exercise an enabled disconnected route, detach during a deliberate held note with no generated Off or replay, private Audition exclusion and restoration of the starting route. Observe USB transfer and physical visual or acoustic evidence separately. Check CDC under mixed musical traffic and stalled output, same-board BOOTSEL, exact flash readback and return. A fixture or topology enumeration alone does not satisfy either target gate.

Installed Pico `d311cf3` passed direct CDC readback and [same-board recovery](pico-target.md#installed-patchbay-matrix). Independent stock Chrome used one same-origin `/cdc` attachment in `602768b` Diagnostics. On the APC Keybed to native export branch `1 → 3`, guarded Off advanced revision 1 to 2, stale On at revision 1 returned `ERR UNAVAILABLE` and preserved Off at revision 2, and fresh On restored Active at revision 3. The other two routes stayed Active. The socket closed cleanly and the run sent no MIDI. Typed browser Routing, the physical Panel edit, and independent MIDI receipt remain separate checks. [Live gate](../ROADMAP.md#active-backlog)

### Master musical forwarding and Panic

The normal portable `local_runtime` is the sole owner of the master gate for ordinary live musical forwarding. It starts Running with gate revision `1`. The host mirrors the committed gate for queue dispatch. The gate is volatile and survives tool and terminal reconnect within that runtime. Saved route intent, port assignment and transforms have their existing independent lifecycle. Per-edge Off, Pending and Active still describe the configured edge and its bindings. Panel feedback, local controller gestures, Raw control and serial CLI remain usable. Route and transform edits still work through their existing guards while paused.

Use these optional providers in `api/routing.h` and bind them through `frontends/cli/cli_frontend.h` before command processing.

```c
typedef struct {
    gridpico_revision revision;
    bool running;
} gridpico_forwarding_status;

typedef enum {
    GRIDPICO_PANIC_STAGE_NONE,
    GRIDPICO_PANIC_STAGE_RUNNING,
    GRIDPICO_PANIC_STAGE_COMPLETE,
    GRIDPICO_PANIC_STAGE_CANCELLED
} gridpico_panic_stage_state;

typedef enum {
    GRIDPICO_PANIC_OUTPUT_PENDING,
    GRIDPICO_PANIC_OUTPUT_COMPLETE,
    GRIDPICO_PANIC_OUTPUT_REFUSED,
    GRIDPICO_PANIC_OUTPUT_CANCELLED,
    GRIDPICO_PANIC_OUTPUT_UNCONFIRMED
} gridpico_panic_output_state;

typedef struct {
    gridpico_connection output;
    uint16_t requested_count;
    uint16_t accepted_count;
    gridpico_panic_output_state state;
} gridpico_panic_output;

typedef struct {
    gridpico_panic_stage_state state;
    size_t count;
    gridpico_panic_output outputs[GRIDPICO_PATCH_MAX_PORTS];
} gridpico_panic_stage_report;

typedef gridpico_result (*gridpico_read_forwarding_fn)(
    void *context, gridpico_forwarding_status *out);
typedef gridpico_result (*gridpico_apply_forwarding_fn)(
    void *context, bool running, gridpico_revision expected_revision,
    gridpico_forwarding_status *out);
typedef gridpico_result (*gridpico_read_panic_fn)(
    void *context, gridpico_panic_stage_report *out);
typedef gridpico_result (*gridpico_apply_panic_fn)(
    void *context, gridpico_panic_stage_report *out);

void gridpico_cli_bind_musical_control(
    gridpico_cli_frontend *frontend, void *context,
    gridpico_read_forwarding_fn read_forwarding,
    gridpico_apply_forwarding_fn apply_forwarding,
    gridpico_read_panic_fn read_panic,
    gridpico_apply_panic_fn apply_panic, gridpico_panic_stage_report *panic_scratch);
```

The callbacks borrow outputs for the call only and leave them untouched on failure. Panic scratch is caller-owned and reused by the serialized CLI. Each read requires its reader. Each apply requires the reader and applier. Panic forms also require scratch. `forwarding` reads `FORWARDING RUNNING|PAUSED REV` followed by `OK`. `forwarding running|paused if REV` returns the same committed record. The apply callback checks the nonzero expected gate revision and commits in one call. Invalid syntax or revision zero returns `ERR INVALID`. A stale guard returns `ERR UNAVAILABLE` even for a same-value request. A current same-value edit succeeds at the same revision, including at `UINT64_MAX`. An effective edit at `UINT64_MAX` returns `ERR CAPACITY` before changing state or queues. All other effective edits advance the gate revision once. Graph and Routing revisions and ordinary `status` records keep their existing contract.

Validate the ordinary message and its source generation at the existing runtime ingress before applying the gate. While paused, consume valid ordinary input with no delivery and successful dispatch so a healthy input binding stays live. A platform may advertise the gate only when all its ordinary musical producers use the same admission and queue fence. Treat supported Audition output as ordinary music and invalidate its stale scheduled work. Keep local UI gesture pairing. The first portable implementation can gate at `local_runtime_midi` and the owned adapter entry points without extending Graph.

Serialize the gate with ordinary dispatch. Each Running revision is one dispatch interval. Waiting ordinary ingress and egress belong to the accepting Running revision and their exact bindings. Before reporting Paused, close ordinary admission and invalidate and purge waiting work from the retiring interval in the runtime and host. Use the trusted binding class and work provenance to preserve control and explicit Panic. On an effective change, carry `forwarding-state REV running|paused` on the private launcher pipe before the associated public serial reply and before work of the new interval. The host mirrors that state, applies its fence and only then forwards the reply or dispatches the next work. A stale pending callback cannot queue ordinary data in a new interval. Resume admits traffic newly accepted at the target during its new Running revision. Connection generations and public MIDI framing stay intact. An already-issued WebSocket or native write can still arrive. This gate generates no MIDI or release obligations and establishes no receiver silence.

#### M1a · Explicit All Notes Off

The same runtime owns one volatile Panic job and its latest report, independent of forwarding and Routing revisions. `panic` is a deliberate action. It snapshots current eligible musical outputs and starts CC123 immediately through bounded service, including while Paused. `panic status` reads that job without scheduling work. A fresh runtime reports `NONE`. With only M1a bound, a new deliberate action is eligible as soon as the prior job is terminal. There is no timing state or sweep record in an M1a reply.

Use the selected target's existing control authority and private output adapter. Panic bypasses the master gate, routes and transforms. Managed ingress remains Note On/Off-only. Include currently verified connected musical outputs, including unassigned musical outputs. Exclude unavailable or unresolved bindings, Panel, LED, Raw, control-only and reserved capture-only outputs. Use a verified documented channel mask where one exists, otherwise all 16 channels. Display names, routes, transforms, past MIDI and ordinary USB or WebSocket slots supply neither eligibility nor channel scope.

For each applicable channel, send `Bn 7B 00` in ascending channel order. Revalidate the captured logical output ID, generation and eligibility immediately before submission. Keep one active job and a bounded cursor and result per output. Service at most eight triplets per portable foreground pass, rotating through active outputs. The portable activation returns current progress without waiting for drain. Stop one output on its first refusal or unconfirmed submission and continue independent outputs. A full queue leaves its healthy binding available. Output loss cancels its remaining work. Loss of the initiating control binding cancels the active job's remaining work. Already-issued writes can finish. Discard retired waiting work. Never retry or replay, including to a replacement binding. No note history is needed. [MIDI Association message summary](https://midi.org/summary-of-midi-1-0-messages)

Each accepted job belongs to the exact initiating control binding and its generation. This can be the selected target's existing authorized CLI lease or the qualified current APC Panel owner. The runtime checks that owner before servicing its waiting work. A current authorized read can inspect the retained report without acquiring the job or its activation rights.

One output can request 16 triplets, and a portable service pass attempts at most eight. The stage state and accepted prefix distinguish ongoing progress from a terminal partial result. The read-only command retrieves either without scheduling another action. Both portable base commands use the same CRLF response even when M1b is enabled.

```text
PANIC_STAGE CC123 STATE N
PANIC_OUT PORT GENERATION REQUESTED ACCEPTED OUTCOME
... N rows ...
OK
```

`STATE` is `NONE`, `RUNNING`, `COMPLETE` or `CANCELLED`. `NONE` has zero rows. `COMPLETE` means all rows are terminal, including partial or refused outputs. `CANCELLED` records cancellation of the full active job and retains previous counts. Rows are sorted by strictly increasing nonzero logical `PORT` and include the captured nonzero `GENERATION`. The report has at most `GRIDPICO_PATCH_MAX_PORTS` rows. CC123 `REQUESTED` is the applicable channel count, 1–16. `ACCEPTED` is the exact adapter-accepted prefix so far in `0`–`REQUESTED`, or `-` when the exact count is unknown. Store typed count zero for unknown. `OUTCOME` is `PENDING`, `COMPLETE`, `REFUSED`, `CANCELLED` or `UNCONFIRMED`. Only `UNCONFIRMED` uses `-`. `COMPLETE` requires a full count. `REFUSED` and `CANCELLED` can retain a known partial count. `OK` confirms the read or accepted activation. The stage state reports completion.

Bind providers and their scratch before processing commands. A reader and scratch are required to advertise and execute `panic status`. The starter also needs the applier. Advertise only the exact available forms `panic` and `panic status` in `help panic` for an M1a-only provider. Known unbound forms return only `ERR UNAVAILABLE` without calling a provider. Malformed forms return only `ERR INVALID`. A current job still running returns only `ERR UNAVAILABLE` on an activation. Preflight bounded staging and report capacity before work. A capacity refusal emits only `ERR CAPACITY`. Rejections leave committed state and previous reports intact. Use the existing command framing and leave other CLI commands and profiles independent of these bindings.

Use the authorized selected-runtime CLI for typed commands. The [editable APC Patchbay](#m1a-apc-patchbay-control-binding) shares its provider. The portable CLI and Monitor can qualify before the editable surface is available.

For browser readback, use only the selected target's matching committed response. On stale readback or a lost reply keep the last confirmed state, mark the action unconfirmed and refresh on the existing authorized path. Use a new deliberate gesture for any later action. Never send a repeat on reconnect. The report gives local adapter acceptance. MIDI wire receipt and receiver silence require separate evidence.

First qualify M1a with the ordinary portable CLI and Diagnostics Monitor. Check stale and same-value revisions, queued ingress and egress across Pause/Resume, continued control and feedback, refused/partial Panic output alongside healthy outputs, and absent or partial provider bindings.

For the portable receiver witness, attach Monitor and explicitly enable audio before holding a note. If Web APC supplies the note, keep its document in the foreground and drive Pause and Panic through the existing authorized CLI without moving browser focus. Capture the source Note On and verify no source Note Off or focus-loss cleanup before the Panic receipt. Confirm Paused readback and that the same voice is still sounding immediately before Panic. On the same Monitor binding, witness exact `Bn 7B 00` and measured silence after its channel-local release interval. Release the source while paused, then Resume and verify a fresh balanced note. Record the CLI result, source and Monitor MIDI, and PCM separately. [Web APC hold retirement](../web/apc/index.html#L897)

#### Normal Pico CDC Panic

On a normal Pico with the CC123 provider bound, `panic` starts one job through the existing authorized CDC attachment. Acceptance returns exactly `OK\r\n`. Refusal returns the ordinary `ERR` record for its cause. Acceptance describes job admission. USB completion and actual receiver behavior have separate evidence. If the reply is lost, an authorized client can inspect the retained result with the read-only `panic status` command. A new activation requires another deliberate action.

Keep the detailed `PANIC_STAGE CC123` and `PANIC_OUT` rows on `panic status` for diagnosis. Pico counts full matched USB completions on captured output attachments. Interpret the portable activation and diagnostic records according to the portable contract above. Existing portable readers keep their current response shape.

The first Pico stage sends CC123 only. It holds one bounded cursor and result per eligible output. Service ready outputs in bounded foreground turns and retain each unsent cursor while its endpoint is busy. Recheck the initiating and target attachments before issue.

For a later Pico sweep, suspend ordinary musical sends during the explicit action and generate Note Off from each output's cursor in fixed timed batches. The proposed starting point is 32 events per 1 ms tick, aiming to finish in under one second. Keep pending sweep work in the cursor and pace it by the timer. Use the existing USB submission readiness, attachment lifetime and matching completion at the transport boundary. The design uses direct output submission and leaves the ordinary musical queues and application acknowledgements out of sweep pacing.

One full output sweep can contain 2,048 events. At one four-byte event per service opportunity, sustained 1–4 ms service takes approximately 2.048–8.192 s for that output. At 4 ms, completing in under one second requires at least nine events per opportunity. Where the endpoint and driver support a 64-byte USB transfer of 16 events, 128 such opportunities take approximately 512 ms for one full sweep. Logical cables sharing an endpoint share those opportunities. Qualify the selected host and native paths separately and count the actual endpoint workload before claiming the timing target.

#### M1a APC Patchbay control binding

The APC action surface is the physical APC's editable 5×8 LED Patchbay. Any browser qualification uses its editable Web APC mirror on a matching Panel and feedback path. The 40 pads still edit routes and report committed cell state while paused. Preserve the chooser, Keys, Arrange, Assign, Move, Learn, Audition and their existing gesture ownership. Audition musical delivery follows the ordinary master gate. A passive panel or the separate browser Routing table supplies no evidence for this surface.

Bind the editable frontend only on a qualified trusted Panel input with current role and attachment ownership. One dispatcher per gesture uses the same M1a provider and serialized queue fence as CLI. MIDI admission alone supplies no M1 authority. Keep Web APC master actions unavailable until the current Panel dispatch and committed feedback path qualify. The Panel opens no serial lease. Keep the existing CLI, listener and grants. The current normal portable profile binds a passive 5×8 projection and has no editable M1a Panel dispatcher. Wiring that dispatcher and its committed readback is an APC delivery dependency. The portable CLI and Monitor witness can proceed independently. [Portable Panel initialization](../app/local_runtime.c#L204)

In ordinary Patchbay with Arrange closed, consume one unmodified Play/Pause press (candidate Panel note `0x5b`) to request the opposite of the last confirmed Running/Paused indication delivered to the current surface. Use that indication's exact revision. A missing or stale state earns a readback and requires a fresh press. Shift held before the first fresh Record press (candidate `0x5d`) invokes explicit CC123 Panic once. Bare Record retains its existing behavior. Match these candidates after chooser or mode admission and before the frontend's general Shift filter. Keep repeat and matching-release disposition with the initial gesture and owner through cutover. Repeat, release, Shift alone, binding, mode entry and refresh never invoke an action. A stale refusal refreshes only. M1a has no sweep gesture.

Use an optional private binding in `frontends/apc/apc_physical.h`. The callbacks borrow the existing `panel_context`. Place the result indication through the serialized foreground, after the start callback returns.

```c
typedef gridpico_result (*gridpico_apc_start_panic_fn)(void *panel_context);

typedef enum {
    GRIDPICO_APC_PANIC_COMPLETE,
    GRIDPICO_APC_PANIC_ATTENTION
} gridpico_apc_panic_cue;

void gridpico_apc_physical_bind_musical_control(
    gridpico_apc_physical *frontend,
    gridpico_read_forwarding_fn read,
    gridpico_apply_forwarding_fn apply,
    gridpico_apc_start_panic_fn start_panic);
void gridpico_apc_physical_forwarding_indicated(
    gridpico_apc_physical *frontend, const gridpico_forwarding_status *status);
void gridpico_apc_physical_panic_result(
    gridpico_apc_physical *frontend, gridpico_apc_panic_cue cue, uint64_t now_ms);
```

The adapter checks the exact current Panel owner and feedback destination before enabling the binding. A forwarding toggle requires read and apply. Publish only an actual committed provider result after its queue fence, including changes initiated by CLI. Both initiators use the same committed feedback path. In a hosted runtime, place the effective `forwarding-state` barrier before the associated serial reply or Panel indication and before new-interval ordinary work. The host applies its waiting-queue fence before forwarding either indication or dispatching that work. Where no host pipe is involved, apply the local queue fence before offering committed feedback. Refresh on attach or return to Patchbay, committed state change and stale refusal. The forwarding guard uses the last successfully delivered indication, even if a more recent commit is waiting for feedback. An unseen CLI update therefore makes a press stale instead of reversing its intended action. Refresh after refusal and require a fresh press. The layout and Graph revision guards for route cells remain independent.

Carry the committed running value and revision with the bounded lamp flight. Pass a nonnull status to `gridpico_apc_physical_forwarding_indicated` only after successful completion for its exact current Panel input and feedback output epochs. A queued frame, a pending command, an undelivered commit or a route-page completion grants no forwarding guard. A new revision can require a new indication even if its lamp bytes match. Pass null to invalidate on confirmed readback or feedback loss. Invalidate on owner change and mode retirement. Ignore old completions after retirement and qualify fresh feedback on return. A surface without qualified current delivery keeps forwarding control unavailable. Ordinary Patchbay operations and the independently qualified explicit Panic provider remain separate.

The Panic adapter starts the shared M1a job using runtime-owned report scratch. A successful start indicates admission. Post one terminal cue only to the still-current initiating surface after checking its exact input and output epochs and correlating the job. Choose Complete only when a nonempty result reports every output row fully accepted at the adapter. A rejected deliberate press can show Attention without a job. Cancellation, partial, unknown or an empty cohort can show Attention on the terminal job. Emit no completion for admission, for somebody else's job or to a replacement surface. Detailed counts remain available through authorized CLI readback. LED indications supply no evidence of sink silence.

Show confirmed Running steady, Paused blinking and unavailable unlit on the dedicated Play/Pause lamp. Candidate fixed-color messages are `90 5B 01`, `90 5B 02` and `90 5B 00` pending physical optical proof. Current Web APC does not decode this transport lamp. Qualify its rendered state on the same current feedback binding before claiming the mirror. Coalesce one latest lamp frame per surface and use the existing per-output flight fence. Keep it outside the 53-frame route page and its completion. Musical service keeps its existing priority. The optional Panic cue is one brief pulse for Complete or three for Attention on the bottom Scene edge. Enable it only after qualifying delivery of the individual phases on the current owner, including with busy TX and an already-lit edge. Use one bounded temporary override in ordinary Patchbay. Advance each phase only after matching delivery and its qualified visible dwell. Page coalescing alone cannot establish a pulse. On completion or cancellation restore the latest selection and activity on the same current owner. On owner retirement drop the override and let the fresh owner receive its own committed page. Keep all 40 cell bytes and their revision guards independent. In Keys and the chooser leave feedback with their existing owner.

Keep portable provider and Monitor evidence independent of editable Web APC and physical checks. For the editable surface, verify both actions from current gestures, CLI-originated readback on the panel, a press after an unseen CLI commit, rejection of stale and busy requests, no repeat, mode and owner retirement, all 40 cells and Arrange/Keys/Audition controls, and feedback loss with no old result presented as current. The physical gate also witnesses real button bytes and paired releases and lamp optics under row activity, independent of MIDI transfer receipt. These are acceptance checks, with no board action authorized by this contract.

#### M1b · Approved timed individual Note Off sweep

M1b is the approved follow-on. Preserve the M1a types, command framing and report. Bind this optional extension on the same CLI instance, with the same context and runtime owner. Its reader, applier and scratch are all required along with the base Panic reader, applier and scratch. Enable target timing only when the complete extension is bound. In its absence, help omits the sweep forms, both well-formed sweep forms return `ERR UNAVAILABLE`, and M1a runs without sweep timing or activation. APC clients require the selected provider's current `help panic` to advertise both exact sweep forms and a complete current sweep readback before interpreting timing or offering the action.

```c
typedef enum {
    GRIDPICO_PANIC_IDLE,
    GRIDPICO_PANIC_WAIT,
    GRIDPICO_PANIC_OPEN,
    GRIDPICO_PANIC_CLOSED
} gridpico_panic_window;

typedef struct {
    uint64_t cycle;
    gridpico_panic_window window;
    uint32_t wait_us;
    uint32_t remaining_us;
    gridpico_panic_stage_report cc123;
    gridpico_panic_stage_report sweep;
} gridpico_panic_sweep_status;

typedef gridpico_result (*gridpico_read_panic_sweep_fn)(
    void *context, gridpico_panic_sweep_status *out);
typedef gridpico_result (*gridpico_apply_panic_sweep_fn)(
    void *context, uint64_t expected_cycle, gridpico_panic_sweep_status *out);

void gridpico_cli_bind_panic_sweep(
    gridpico_cli_frontend *frontend,
    gridpico_read_panic_sweep_fn read_sweep,
    gridpico_apply_panic_sweep_fn apply_sweep,
    gridpico_panic_sweep_status *sweep_scratch);
```

The extension borrows caller-owned scratch, leaves it untouched on failure and binds before command processing. `help panic` lists the literal forms `panic sweep status` and `panic sweep if <cycle>` when available, as alternatives in its normal `COMMAND` record. `panic sweep status` is read-only. After an accepted base `panic`, the timed client can use this read to obtain target phase and cycle. `panic sweep if CYCLE` is a separate deliberate action with a nonzero guard.

A fresh runtime starts with cycle zero, `IDLE` and no jobs. With M1b bound, acceptance of `panic` allocates one fresh nonzero, nonwrapping cycle and stamps monotonic target time. Empty or partial CC123 work still opens the timing window. Only the same authorized initiating binding can consume the window, once, with CC123 terminal and `1,000,000 < elapsed_us < 10,000,000`. A held or early repeat cannot trigger a sweep. Expiry sends nothing and does not cancel already-admitted work. Loss of the initiating binding closes the window and cancels its remaining work. A bare repeat cannot restart the clock. A fresh first action becomes eligible at original expiry once neither stage is running. Return `ERR UNAVAILABLE` for stale or wrong ownership, busy, early, expired or already-consumed activation. A zero guard or malformed form is `ERR INVALID`. A required allocation or cycle ID exhaustion is `ERR CAPACITY` before any work or state change.

The admitted sweep takes a fresh bounded cohort with the same output and channel rules as CC123. The captured generations and cohorts of the two stages may differ. For each applicable channel send explicit `8n KEY 00` for every key 0–127 in ascending order. Keep the existing eight-triplet foreground budget, per-output generation checks, independent failure and cancellation, and no retries or replay. A request needs at most 2,048 frames per output. Store cursor and result state only. Use the same stage rows and outcome rules, with sweep `REQUESTED` equal to the captured channel count times 128.

Both a timed read and a successful timed activation return these CRLF records.

```text
PANIC_WINDOW CYCLE WINDOW WAIT_US REMAINING_US
PANIC_STAGE CC123 STATE N
PANIC_OUT PORT GENERATION REQUESTED ACCEPTED OUTCOME
... N rows ...
PANIC_STAGE SWEEP STATE N
PANIC_OUT PORT GENERATION REQUESTED ACCEPTED OUTCOME
... N rows ...
OK
```

Read time from the target's monotonic clock. In `IDLE` emit cycle zero and `- -`. Through 1,000,000 µs emit `WAIT`, with time to the first admissible tick and to the exclusive expiry. From 1,000,001 through 9,999,999 µs emit `OPEN`, with zero wait and positive remaining time. At expiry, consumption or initiating-binding loss emit `CLOSED`, with `-` for wait and remaining time to the original expiry, clamped to zero. Store typed zero for any omitted time. The target rechecks all guards at activation. Offer a sweep only on current confirmed `OPEN` with CC123 terminal and `SWEEP NONE`. A lost or stale response permits a read-only refresh. A new deliberate action is required after refresh.

Qualify M1b after M1a through the existing authorized CLI and a deliberate second action on the physical APC Patchbay, including while Paused. Qualify the physical gesture and prompt before enabling them. The browser Routing table can show authorized readback independently. Check exact boundaries at 1,000,000, 1,000,001, 9,999,999 and 10,000,000 µs, one deliberate in-window activation, no emission on early, stale, busy, third or expired requests, and expiry without a second gesture. Verify all requested keys and channels, changed cohorts, continued sibling progress during an output fault, and cancellation on lease loss without work reaching a replacement. Keep bounded readback and actual wire receipts separate.

## APC viewport

The production APC frontend owns a passive five-source by eight-destination viewport over Session's committed managed projection. Pan one row or column per accepted arrow press, clamp at both ends, and keep existing logical port ordering through attach and detach. Render all 40 cells from one coherent stored viewport and snapshot. An empty or incompatible cell is dark. Use committed Off, Pending and Active feedback for compatible cells. Navigation and pad input in this passive view change no routes. An edit through CLI refreshes the current view. A Raw lease still suppresses ordinary feedback, and restore uses the current committed viewport.

Select this mode explicitly in the configured frontend profile. Use APC protocol Port 1, MIDI channel index 0. Shift is note `62` hex. The left four Clip Stop buttons are Up `40`, Down `41`, Left `42` and Right `43`. The first nonzero Note On for an arrow while Shift is held requests one pan. Repeated presses while held and release edges request none. A Note Off or zero-velocity Note On releases that button. Losing the panel binding clears held edges and retains the viewport. Legacy color gestures remain in their own mode.

Chromium Port 1 captures matched the documented addresses using `90 nn 7F` on press and `80 nn 00` on release for Shift and all four arrows. Existing focused controls, pointer and touch paths emit ordinary panel MIDI. Independent desktop testing verified both-axis clamping, stable assignments, a CLI edit to 11→18 beyond the first window, and a complete restore of the committed viewport after Raw replay. Physical hardware confirmation remains separate. [APC Patch feedback baseline](patchbay-controls.md#production-patch-surface) · [Akai Control Mapping](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf) · [Akai user guide](https://cdn.inmusicbrands.com/akai/apc-key-25-mkii/APC%20Key%2025%20mk2%20-%20User%20Guide%20-%20v1.2.pdf)

### Editable physical APC Patchbay

The editable Patchbay uses the same production APC frontend on Linux (M5 and Pi) and RP2040 Pico. Each target router has one live APC interaction state for gestures, held controls, chooser, mode, selection and previews. Physical and browser controllers act like attached keyboards. They send ordinary Panel MIDI through authorized connections, and the core applies each delivered event in receipt order. Connecting or disconnecting a browser leaves that state and committed edits intact. Existing transport and Panel-role authorization apply. Once delivered to the selected target, an edit uses the shared state and the typed Session guards, regardless of the browser that sent it or the progress of any LED view. Existing Raw ownership and feedback restore rules still apply.

Use the shared editable implementation in `frontends/apc/apc_physical.c`. The published portable runner still uses `gridpico_apc_patch_*` for its configured passive viewport and earlier profiles. The Pico runner already uses `gridpico_apc_physical_*` for its editable surfaces. Shared-state editing and mirrored LED feedback need separate qualification on the selected target. Existing passive and demo evidence applies to those profiles. [Shared frontend](../frontends/apc/apc_physical.c) · [Portable runner](../app/local_runtime.c) · [Pico runner](../app/rp2040_router.c)

The editable types live in [`routing.h`](routing.h). The published provider uses one volatile five-source by eight-destination placement shared by the current surface instances. Assembly serializes reads and edits with Graph. Placement uses managed, registered logical port IDs in their correct direction. Zero means empty. Each axis permits one placement per port. Assign changes one slot. Move swaps two same-axis slots and can move into an empty slot. Neither operation rewrites a route. Reject wrong-direction, duplicate and stale assignments before changing placement. Keep the passive and existing one-pad profiles independent.

The Session snapshot gives both revisions, the full layout and ordered eligible ports, a bounded attack record per input, and all 40 cells from one coherent graph observation. Compatibility needs an assigned eligible Managed input and output. Once the selected provider supports deliberate same-device routes, the cell uses the same availability and Off, Pending and Active presentation regardless of shared parent. Apply the same deliberate eligibility predicate to cell display and edits. Startup snapshots keep their existing registered-pair rule, including the declared native exception for same-parent pairs. Graph validates deliberate edits through the [manual Managed route contract](local-router.md#deliberate-managed-route-edits). The published native-pair-only provider retains its existing qualification. Availability describes current connections. [Deliberate same-device Managed routes](local-router.md#deliberate-same-device-managed-routes). For a compatible cell, the typed Graph route remains authoritative for desired Off, Pending or Active and current-message delivery. Permit a compatible disconnected pair to be enabled Pending. Set a cell by explicit desired state with the layout and Graph revisions from the core observation used for the edit. On stale state or Graph rejection, refresh feedback from committed state and wait for a fresh press. The 40 addressed cells are independent of the configured number of simultaneous branches and fan-out.

Assembly identifies the current source attachment and reports each accepted normalized, nonzero Note On to Session before dispatching through Graph. Attack sequence and monotonic timestamps describe input activity even when no route delivers it. They confer no delivery claim. In Learn, the shared frontend arms from the observed sequence, chooses one assigned row and confirms a single uniquely observed eligible input. More than one distinct source requires a fresh arm. Require a still-connected candidate and current layout revision at confirmation. Keep selection, preview, held gestures and transient activity timing in the frontend. The private Audition source and passive device census are outside placement, Learn and activity.

In Patch, the first nonzero pad press edits its mapped cell. Use the existing visual order, with row `r` and column `c` at pad note `8 × (4 − r) + c`. Scene and Track select the respective edge. Stop All Clips opens Arrange or cancels it. In Arrange the upper-left four pads select Assign, Move, Learn and Audition, and the bottom-right pad confirms a previewed edit. Matrix pad edits are suspended there. Select an edge before using left/right to browse eligible Assign ports. Move takes two edges on one axis. Input Learn takes a Scene row and one actual source. Audition takes a Track output and a fresh held Audition action. No action affects transport. Shift arrows retain navigation when there is an approved larger viewport. This fixed page clamps at its edges. Keep Keys and the Shift+Sustain prototype separate from this first build.

Render a coherent picture of 40 RGB pad, eight Track and five Scene states from the target's live APC state. Use solid frames for the current phase. Active is steady green, available Off is dim, empty or unavailable Off is dark, and Pending alternates amber phases on a 500 ms monotonic interval. Use edge lamps for selection. A source attack inverts its Scene edge briefly, initially 100 ms, then restores current selection. Coalesce repeats so they cannot extend an active pulse. Mirror the same incremental MIDI LED orders to every connected LED view on a best-effort basis, with independent bounded output service. At attach, a view can receive a full current picture followed by the same incremental stream. LED delivery, rendering and acknowledgement are independent of edit admission. Keep musical delivery ahead of feedback.

Assembly owns the single Audition operation and clock. Resolve the selected verified output and admit Audition only for the operation. When using a private Managed branch, acquire it on demand from available backing. On exhaustion refuse Audition without evicting routes or reserving a permanent slot for it. The first phrase authors On and Off events for MIDI notes `48, 55, 60, 67, 72` at 150 ms each with 50 ms gaps, channel index `0` and velocity `96`. Start only from deliberate hold, with at most one current step and no catch-up or loop. The same owner's explicit release or Stop cancels future steps. If the current step's On was admitted, that control may send the step's authored Off once, early, to its still-current eligible target. Mode retirement and target or panel loss cancel stale work without creating an Off. A committed effective Managed route or transform edit targeting the exact selected logical output also cancels, including successful automatic admission of a new ordinary branch. An unrelated output, rejected or same-value edit, or Auto pass that commits no branch leaves the phrase intact. The cancellation stops future authored steps and leaves already admitted transport work under its existing rules. Refusal or unavailability drops the event with no debt or retry.

The retained Pico image starts with an empty placement and has no compiled musical placements or routes. See [discovery placement](#discovery-placement-and-verified-restore) for the approved boot behavior. Generic USB MIDI ports appear in Assign after [runtime Graph admission](pico-target.md#circuit-tracks-and-downstream-capacity). Enumeration alone supplies no matrix eligibility. The automatic default works independently of placement, and Assign changes placement only. A fixture can exercise all five input and eight output positions. Physical acceptance covers real assignments, stale edits, placement moves, Learn, Audition, scoped delivery and feedback. For a shared physical and browser Pico witness, use the dedicated WSS path on the isolated bench. For Linux, qualify the existing Panel and feedback transports on the selected runtime. On equivalent Session state and timing, compare the same gesture traces, committed edits and LED orders, including stale edit refusal and losing one surface while another remains bound. Qualify physical lamp behavior separately from returned MIDI bytes. The live Pi router MIDI roles retain their own runtime.

In physical Assign, offer one deliberate Unassigned candidate on each axis. Confirm submits port ID `0` with the captured layout revision and clears just the selected slot. Cancel and stale confirmation leave the placement intact. Clearing or reassigning a slot preserves the desired routes between logical ports.

### Discovery placement and verified restore

Before granting defaults, resolve explicit and verified control reservations, then apply verified saved positions at boot or preserve current known positions on rediscovery. Resolve saved references by verified directional identity. Names are for display. Keep unresolved saved positions visible and reserved until verified or explicitly replaced without holding up unrelated discovery. Then fill free rows and columns in the initial 5×8 window with newly admitted eligible Managed inputs and outputs respectively, using stable registry order and no fixed instrument identities. Preserve named positions across a verified detach and return. An operator's Assign, Move or Unassigned edit survives subsequent reads and a verified return in the running session. Repeated discovery of that identity does not refill a slot the operator cleared. Assign and automatic placement change presentation only.

The 5×8 is a viewport onto the shared ordered placement. Keep the 40 physical crosspoints and the existing 53-frame page shape. Use coherent window reads and layout and Graph revision guards. Report additional admitted directions by name and logical ID in Routing inventory and keep them eligible in Assign even when the initial window is full. Qualify any further editable windows against that inventory. Report unplaced directions, registration refusals and route refusals separately. Placement occupancy grants no branch or fan-out capacity.

### Automatic musical routes

Discovery checks explicit intent and the exact declared loopback before committing through the ordinary route setter in the same serialized turn.

Honor verified explicit saved On and Off choices before new-device defaults when the startup restore provider admits them. Startup restore keeps its format, storage and layout and role checks. A verified enabled choice that cannot currently deliver can remain Pending under directional admission. Keep an explicit Off even when the pair has no enabled branch. Establish the first unambiguous verified APC control binding through role authority before placement or automatic routing. Honor explicit role overrides and leave user-claimed outputs with their owners.

At startup and on new verified musical admission, try to enable every eligible current Managed IN-to-OUT pair incident to a newly discovered device, including when no APC is present. This pass uses the existing deterministic order of ordinary USB pairs followed by verified managed native or external endpoints, then source and destination logical ID within each group. Exclude Panel, LED, Raw and Audition. Keep explicitly identified loopbacks Off by default. The normal Pico uses its existing exact native virtual cable declaration for the Computer return. Eligible deliberate edits remain available regardless of this default decision.

For each proposed default, match the exact registered input and output against the loopback declaration in routing or wiring. Evaluate direction, role and resources independently of shared parents, reciprocal routes and Pending intent. The check is a direct pair match. Auto performs no cycle traversal or external return test. Retain existing enabled intent and report actual allocation exhaustion separately. Route discovery and enablement emit no musical content or held-note replay. Evaluate candidates with bounded foreground work after coherent inventory changes.

The sealed Pico profile configures seven Managed branches, fan-out three and one permanent Audition reservation, leaving six public branches. Its successor lets manual and automatic Managed enables use all available allocated branch storage. Enabled Active and Pending branches occupy actual slots. Audition consumes a slot only while using it. Per-input fan-out can use the available branch slots and eligible outputs. Manual and automatic branches share the same storage.

Report allocated branch capacity and the actual reason for a refusal. Auto reports named pairs omitted for storage exhaustion separately from loops or explicit Off. Under excess traffic, delivery may degrade. Each existing bounded destination queue refuses its own excess messages while sibling destinations progress. [Normal Pico capacity smoke](pico-target.md#normal-managed-capacity-smoke)

Use named `routing` rows for actual branches and bindings. An optional `autoroute` summary may emit `AUTO REV CANDIDATES ACTIVE PENDING USER_OFF FEEDBACK LIMIT UNAVAILABLE` and `OK`. Qualification requires a complete list of excluded pairs as well as the committed routes.

`autoroute explain` starts the bounded exclusion read. `autoroute explain REV OFFSET` continues it. Reply with `AUTO_EXPLAIN REV ROUTING_REV COUNT NEXT`, up to eight `AUTO_SKIP SRC DST REASON` records ordered by source ID then destination ID, and `OK`. `COUNT` is the total excluded pair count and `NEXT` is the next offset or `-`. Use the existing 256-byte record and line framing limits. Read names and direction from both IDs' `BINDING` records using the exact `ROUTING_REV` on the same target. Report `ERR UNAVAILABLE` for a stale continuation or an incomplete current evaluation. Restart both reads if revisions no longer match. Keep the existing Routing rows and revision.

Evaluate managed musical input and output pairs before applying pair-specific exclusions. Panel, LED, Raw and Audition are outside the musical candidate list. Normal generic Pico flat routing emits `computer-return` for the exact declared Computer pair, `user-off` for explicit Off, `branch-limit` for full allocated route storage, and `unavailable` for a candidate the current evaluation cannot apply. Already enabled pairs count as Active or Pending. The existing wire also decodes historical `same-parent`, `known-feedback`, `cycle`, `audition-reserved` and `fanout-limit` values. Retain one actual reason per excluded pair. A pair outside the current new-device grant does not count as a feedback or capacity refusal. The explanation is a read of a coherent evaluation and never attempts to enable a route.

The candidate count is bounded by inputs times outputs, at most 81 pairs with 18 registered directions and 324 with 36. A bounded record of two registration indices and one reason byte per excluded pair fits in target-owned storage. Resolve actual IDs and names from the same revision rather than duplicating names per pair. The order of explanation rows is independent of admission priority.

Treat operator Off as explicit intent even when the pair is already Off. Keep it for the verified logical pair across rediscovery in the running instance and respect explicit role clear or override. Apply the override only when the ordinary Graph edit succeeds. Persist an Off override only through a qualified configuration that represents it. An enabled-pair snapshot alone cannot distinguish it from a missing route.

The configured runtime loans Session a bounded bitmap indexed by stable Graph registration slots. CLI route edits and physical cell edits share the operator path. Successful On clears explicit Off, and a failed Graph operation leaves the bitmap unchanged. Auto enable checks the current bitmap before asking Graph. Audition and startup snapshot restore use a system path that leaves operator intent alone. Plan and summary invalidation include bitmap changes even when the Graph revision stays the same.

A verified return can activate its existing desired routes at the fresh generation. Default grants belong to startup or a genuinely new admission, and survive a plan restart until the coherent grant pass completes. Layout edits, explicit route edits and APC role mark or unmark may invalidate a plan without granting new defaults. Reserved and retiring LED outputs are ineligible. After unmark, the output is available for explicit routes and stays excluded from automatic routing during the current attachment generation. The explicit LED role override remains in force on return.

Verify saved positions and verified On and Off choices across [an explicit Save and physical reboot](pico-target.md#normal-physical-boot-acceptance), with a new device filling free slots and overflow directions visible in inventory. In a bounded fixture, verify Auto enables eligible same-parent and opposing directions without an explicit loopback designation and leaves the exact declared Computer pair Off. Verify automatically routed notes without an APC, preserved explicit intent, deliberate reciprocal and transitive enables, named capacity omissions, scoped queue retirement and zero replay. Automatic routing supplies no additional MIDI message types.

### Managed note transform

For an existing enabled managed branch, apply one optional [note transform](local_router.h) in the production Patch Graph. A zeroed configuration preserves key and channel. Processor owns `gridpico_transform_managed_note` in `core/processors/managed_note.h` and `.c`, with the `gridpico_managed_note_transform_fn` signature. Reuse the existing transpose function for pitch. Apply a semitone offset and optionally replace the output channel with 1–16. Preserve velocity. An out-of-range key yields no output.

Graph binds the optional processor before ingress. Graph reads and edits the configuration of the selected enabled managed branch through these internal operations.

```c
void gridpico_patch_graph_bind_note_transform(
    gridpico_patch_graph *graph, gridpico_managed_note_transform_fn transform);
gridpico_result gridpico_patch_graph_read_managed_note_transform(
    const gridpico_patch_graph *graph, const gridpico_local_route *route,
    gridpico_managed_note_transform *out);
gridpico_result gridpico_patch_graph_set_managed_note_transform(
    gridpico_patch_graph *graph, const gridpico_local_route *route,
    const gridpico_managed_note_transform *transform);
```

The branch transforms each current On or Off independently. A filtered result drops only that branch for that event. An effective edit checks revision capacity, commits once and governs subsequent ingress. A same-value edit leaves the Graph revision unchanged. Explicit controls validate and guard the selected target and branch before calling Graph. Branch creation defaults to identity.

The processor handles managed notes only. It adds no messages to Raw, USB framing, SysEx or transport. Verify identity, current transforms for both On and Off, and per-branch out-of-range silence. An edit while a note is held emits no MIDI. The later ordinary Off uses the new transform. Verify an unchanged sibling path and independent delivery despite a stalled sink. Keep the native proof independent.

#### Normal desktop transform authority

The normal portable runner entry enables the optional transform provider when its validated profile contains the singleton row `note-transform on`. Reject duplicate or malformed rows. The typed value grants this control independently of the profile filename and path, CPU, display names and port IDs. The shipped desktop profile includes the row. The shipped Pi profile and built-in profiles omit it and leave the provider inactive. Other runtime entries, including Pico, leave this provider unbound. Initial release qualification covers Linux x86-64 desktop with the shipped profile.

Use the existing exclusive terminal connection and its admitted same-origin loopback `/serial` WebSocket. `transform SRC DST SEMITONES CHANNEL if REV` selects one existing Active public Managed socket branch by its named logical IDs and current Routing revision. Semitones range from −127 to 127. Channel 0 preserves the input and 1–16 select an output channel. Pass the request once to the typed apply callback. Preflight revision capacity for an effective edit and return the committed setting and actual Routing revision in the same serialized call. A stale guard or inactive branch returns `UNAVAILABLE`. Failure preserves the setting and reply value. Newly enabled branches start at identity. The profile enables the control and supplies no route, transform setting or additional transport access.

### APC LED output reservation

A verified APC Key 25 mk2 attachment can suggest its compatible LED output for that particular controller. Show the named logical output, interface and cable for the suggestion. A suggestion grants no ownership and creates no route. The operator can choose any eligible output that can carry the APC feedback messages. Identify the chosen controller separately from the output. Match each surface and output to its own verified binding and generation. A matching model or label supplies neither identity nor selection.

Mark and unmark are explicit guarded commands on the selected target's CLI. Use the revision of a coherent inventory and role read. Mark an eligible public managed output only after its enabled routes are Off and transport has fenced the old queue and flight with no competing owner. Refuse an occupied, ambiguous or stale request with a reason. A reservation atomically removes the output from musical routing, Assign and Audition, clears its column if placed, and advances the affected revisions. Keep its ID visible as reserved for the selected surface. APC feedback uses the panel feedback path. Suppress any managed compatibility declaration whose endpoint is reserved.

One LED writer owns each reserved output. Each controller has independent input gesture ownership, current binding and feedback delivery state. Feedback reflects the shared committed Session and carries the selected surface and binding generation through completion. On unmark, stop new feedback and discard unsent frames from the retired owner. Keep the output reserved until its in-flight work completes or the transport proves retirement. Restore managed eligibility on the same logical ID after the fence. The port returns unassigned with routes Off. A disconnected reservation stays unresolved until the verified identity returns or the operator explicitly removes it. Rebinding never uses model or name alone. A new generation gets a fresh committed page.

Qualify manual override and two independent controller identities, including equal model names, stale revisions, pending transport, delayed feedback, detach and no cross-surface leakage. Count reserved outputs separately from the target of [32 assignable directions](#32-assignable-directional-ports). Size ownership records, per-surface feedback and transport queues independently of [seven ordinary USB devices](pico-target.md#circuit-tracks-and-downstream-capacity).

### Append-only managed inventory

Enable dynamic admission only in the configured Graph initializer by setting both `dynamic_device_limit` and `dynamic_port_limit` in [the local profile](local_router.h). They bound total registered inventory, including existing and private entries. Both zero preserve fixed inventory. One zero is invalid. At init require nonzero limits to cover the startup counts and fit compiled backing. The first Pico profile starts with four devices and five ports, including Audition, and admits up to five devices and seven ports. Keep `midi_peer_limit = 5` under the startup initializer's independent peer rule.

Graph declares the following in `core/midi/patch_graph.h`. Assembly calls it in its serialized foreground after actual USB class and identity checks.

```c
gridpico_result gridpico_patch_graph_append_managed_device(
    gridpico_patch_graph *graph, const gridpico_patch_device *device,
    const gridpico_patch_port *ports, size_t port_count);
```

Append exactly one new device and one or more of its managed directional ports as one batch. Prevalidate all names, IDs, directions, parent IDs, disconnected starting states and Patch eligibility. Reject IDs already registered or repeated in the batch. The operation copies the descriptors, gives each new slot managed capability and dormant generation `1`, and increments the Graph revision exactly once. First attachment uses the existing port update at generation `2`, then strictly newer generations on replacement. Absence retains the slots and the desired routes. Only the existing update path attaches or detaches a direction.

If admission is disabled, return `UNAVAILABLE`. Invalid input returns `INVALID`. A target ceiling, compiled backing or exhausted revision returns `CAPACITY`. No failure publishes a partial device, consumes a slot, changes any existing route or retires notes. Never reorder or recycle slots within the runtime. The append changes inventory only. The owning Session and Auto planner handle subsequent placement and route changes. A later USB interface on the same device may need a separate admitted batch contract once the measured topology requires it. This first operation accepts the eligible directions together for one complete observed device.

The first physical profile has room for one new single-interface bidirectional device beyond its startup inventory. Allocate the lowest unused positive ID separately for the device and for each directional port, from the current registry. IDs from a separate Keys build impose no reservations in this profile. In that initial manual profile, the startup layout stays unchanged and new ports start unassigned. A second physical discovery witness needs a profile with enough unused slots for that device's full batch. If the target is full, the USB census may still show the mounted device while the Routing and Assign views omit it. Preserve the existing inventory and report capacity at the admission boundary. Logical ID continuity in this slice is within one running instance. Cross-restart mappings require qualified persistence.

The next normal Pico profile admits at least six devices and nine directions, including private Audition, so two generic duplex witnesses can remain attached and registered together. The independent seven-ordinary-address USB allocation does not change that lower bound or the number of directions each qualified device supplies. [Combined target profile](pico-target.md#circuit-tracks-and-downstream-capacity)

The editable Session is already bound when runtime append occurs. Existing Assign reads the live Graph. For activity and Learn, declare this notification in `core/session/patch_session.h`:

```c
gridpico_result gridpico_patch_session_add_apc_attack_source(
    gridpico_patch_session *session, size_t registry_index, gridpico_port_id source);
```

Assembly supplies the actual stable registry index and ID of each admitted managed, Patch-eligible input, excluding Audition. Notify after the Graph append and before accepting notes or serving a refreshed page, in the same serialized foreground. Session validates the slot bound, nonzero public ID, and unused or identical slot. Reject conflicts with an already tracked source. An exact repeat preserves activity. The notification neither assigns a row nor resets prior layout or note activity. Use the existing attack cache, with no per-note registry projection or slot reuse.

## Verified USB replacement

Stage 3 reserves the verified USB selector for Linux input attachment. Assembly resolves the registered input against one unique adapter-qualified match with a verified serial, interface and direction. Missing or ambiguous matches leave enabled routes Pending. Display names, transient USB/OS addresses and topology alone cannot establish continuity.

Retire the old input generation and discard its stale queued traffic. A verified return uses a fresh generation and waits for fresh input. A USB selector on an output reserves it and stays Pending, even with a matching identity. Existing operator-selected numeric outputs remain available through their existing configuration. The physical input and future generation-bound output gates are separate. Cross-restart persistence awaits a separate configuration or Projects contract. [Linux adapter contract and evidence](linux-midi-adapter.md#stage-3-usb-identity-seam)

## 32 assignable directional ports

Provide at least 32 independently assignable managed logical ports per qualified router profile. Count one input direction or one output direction as one port. A duplex cable contributes two ports, so 16 duplex cables are one way to reach 32. Private Audition and dedicated non-assignable Panel and Raw ports do not count toward 32. A name, USB address, MIDI channel, physical device and logical port are separate values. A 5×8 APC page displays a selection from the registry. Assign can choose every eligible port of its direction. Enablement, branch count, fan-out and transport queues have independent limits.

Discover generic supported interfaces and register their validated directional ports at runtime. The generic profile embeds no instrument identity, device-specific port assignment or physical device role. Controller roles come from explicit runtime assignment of compatible discovered ports. A standard native USB descriptor still describes its transport functions. Neither discovery, assignment, role selection nor preferred-number lookup creates an enabled route. Keep current endpoint ownership on removal and fresh-generation return. [Runtime admission](#append-only-managed-inventory) · [Preferred port cache](pico-target.md#preferred-port-cache)

Use 12 devices and 36 directional slots as the first shared backing candidate, with at least 32 available to managed assignments after all registered private, reserved and otherwise non-assignable entries. Count configured LED reservations against this overhead. Keep the existing 13-branch backing. Support actual directed routes for each eligible port regardless of its ID or discovery order. Size simultaneously armed branches and queues separately. A runtime whose declared resources cannot admit the requested port reports capacity. The USB address, class and endpoint pool limits stay separate. [Physical USB limits](pico-target.md#circuit-tracks-and-downstream-capacity)

A Cortex-M0+ `-Os` sizing probe changed only backing from 8 devices and 18 ports to 12 and 36 on the same source. This measures structures and per-function stack, before linkage or hardware qualification.

| Structure or function | Current bytes | Sizing candidate bytes |
| --- | ---: | ---: |
| Graph | 7,208 | 8,232 |
| Session | 488 | 856 |
| Routing snapshot | 1,320 | 2,280 |
| APC snapshot | 608 | 1,040 |
| Pico router, including Graph, Session and APC scratch | 11,160 | 12,984 |
| CLI frontend | 5,288 | 6,152 |
| Session APC projection stack frame | 1,880 | 3,192 |
| Session Routing projection stack frame | 1,392 | 2,352 |

The measured APC frame exceeds the 2,816-byte foreground limit before its caller or IRQs. Before enabling larger Pico backing, move full temporary projections out of the foreground stack. Reuse serialized caller-owned scratch or write a bounded compact projection after validating its source. Retain one coherent revision and preserve outputs on failure. Budget endpoint mapping, identity cache, per-output packet queues, private replies and rollback staging separately. Thirty-two output queues of 128 four-byte packets use 16 KiB before queue metadata. Budget the worst allowed directional mix, and avoid a shared queue that lets one stalled endpoint block others. Check linked SRAM, flash extent, largest foreground plus IRQ path and at least 256 untouched stack bytes under real mixed traffic before promotion.

Keep existing CLI paging at eight records and 256 bytes per record. A 12-device, 36-port, 13-branch projection has at most 61 data records across eight pages. Raise the browser's current 8/18/13 validation bounds to match the approved shared backing and retain strict bounded names, ordered records, paging and revision checks. The same browser supports smaller profiles. A balanced 32-port matrix has 16 × 16 crosspoints even though few can be enabled concurrently. Keep selection and display bounded and avoid a dense matrix in Pico Session storage. Test maximum escaped names. The current 4 KiB synchronous Pico reply buffer cannot be assumed to hold every full `inventory` response at this size. Use the existing paged `routing` for authoritative browser inventory and provide a bounded foreground record pump for a full plain `inventory` response if it exceeds staging. Retain the per-pass CDC budgets and no interleaved replies.

First qualify 32 real logical directions through the production configured runtime, shared CLI and browser, with actual delivery, scoped queue retirement and zero replay at independent peers. Exercise all IDs in Assign, including directions outside the initial page, and full admission plus one over-capacity request. Then qualify the exact widened Pico image with physical generic discovery, mixed routing and CLI, and verified BOOTSEL/readback. Distinguish a 32-port logical profile, the number of physical USB devices exercised and the number of routes enabled. Target reboot continuity requires the separate Save gate.
