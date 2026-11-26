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

Render managed input rows and output columns using registered names and logical IDs from one coherent `routing` snapshot. The existing verified loopback flow submits `route SRC DST managed on|off` and reloads the authoritative projection. The next target Routing increment uses [guarded edits](#target-routing-and-guarded-edits). Keep the last confirmed state while awaiting readback. Refuse changes on a stale, partial or unconfirmed projection. Keep port availability separate from Off, Pending and Active. An unavailable binding on a known compatible pair can still have its desired route edited. Graph may leave it Pending or reject a change under the existing topology and admission rules. Accessible text must distinguish enabled Pending from enabled Active, and distinguish an incompatible crosspoint from a temporarily unavailable port. Preserve the separately scoped Panel and Raw inventory. A page with no authorized CLI snapshot explains that Routing is unavailable and offers the applicable local, SSH or device-provider path. LAN and WSS keep their existing serial admission.

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

For the later typed Pico browser view, validate the direction and managed capability of every enabled `ROUTE` row in the authoritative snapshot. Accept and show a committed [native virtual cable](local-router.md#managed-native-virtual-cable) with a shared parent. An Off result or absent enabled row supplies no same-parent edit eligibility. Before offering to enable that cell, require the selected target's explicit typed eligibility for the exact directed pair. Keep other same-parent cells unavailable for edit and retain the existing Raw checks. This adds no browser serial provider or grant to the current firmware slice.

Verify first on the real **Pi router**, through strict SSH loopback and its stock browser Routing page. Match inventory, availability and managed route readback against direct terminal commands on that instance. Exercise one independent named route Off/Pending/Active and restore it, plus stale paging and a rejected stale guarded edit. Verify the typed browser uses the returned revision once the guarded provider is installed. Preserve attached recorder authority and the finalized WAV. A host fixture gives supporting tests.

Verify separately on the **physical Pico** with the exact firmware and selected configured rig. Direct USB CDC and the admitted Diagnostics terminal must return the same target's actual `help routing`, coherent paged rows and pair readback. On the named APC-to-MicroFreak branch, observe a physical route change after a captured revision, a stale guarded command rejected without mutation, and a fresh guarded edit reflected in physical Panel feedback and independently observed MIDI at the selected output. Exercise an enabled disconnected route, detach during a deliberate held note with no generated Off or replay, private Audition exclusion and restoration of the starting route. Observe USB transfer and physical visual or acoustic evidence separately. Check CDC under mixed musical traffic and stalled output, same-board BOOTSEL, exact flash readback and return. A fixture or topology enumeration alone does not satisfy either target gate.

Installed Pico `d311cf3` passed direct CDC readback and [same-board recovery](pico-target.md#installed-patchbay-matrix). Independent stock Chrome used one same-origin `/cdc` attachment in `602768b` Diagnostics. On the APC Keybed to native export branch `1 → 3`, guarded Off advanced revision 1 to 2, stale On at revision 1 returned `ERR UNAVAILABLE` and preserved Off at revision 2, and fresh On restored Active at revision 3. The other two routes stayed Active. The socket closed cleanly and the run sent no MIDI. Typed browser Routing, the physical Panel edit, and independent MIDI receipt remain separate checks. [Live gate](../ROADMAP.md#active-backlog)

### Master musical forwarding and Panic

The normal portable runtime owns one master gate for ordinary live musical forwarding. It starts Running with gate revision `1`. The gate is volatile and survives tool and terminal reconnect within that runtime. Saved route intent, port assignment and transforms have their existing independent lifecycle. Per-edge Off, Pending and Active still describe the configured edge and its bindings. Panel feedback, local controller gestures, Raw control and serial CLI remain usable. Route and transform edits still work through their existing guards while paused.

Use an optional provider in the existing target CLI. The read callback takes its context and a caller-owned result containing `bool running` and `gridpico_revision revision`. The apply callback also takes the desired `running` and nonzero expected gate revision. It guards and commits in one serialized dispatch, returning the same committed result shape. On refusal the output is untouched. `forwarding` reads `FORWARDING RUNNING|PAUSED REV` and `OK`. `forwarding running|paused if REV` writes and returns the same record. A stale guard returns `ERR UNAVAILABLE`. Invalid syntax returns `ERR INVALID`. Revision exhaustion returns `ERR CAPACITY` before an effective change. An effective change advances the gate revision once. A current same-value edit is idempotent. Graph and Routing revisions do not advance due to the gate. Advertise read, apply and `panic` only for their bound, qualified providers. The existing Routing and status records retain their formats.

Validate the ordinary message and its source generation at the existing runtime ingress before applying the gate. While paused, consume valid ordinary input with no delivery and successful dispatch so a healthy input binding stays live. A platform may advertise the gate only when all its ordinary musical producers use the same admission and queue fence. Treat supported Audition output as ordinary music and invalidate its stale scheduled work. Keep local UI gesture pairing. The first portable implementation can gate at `local_runtime_midi` and the owned adapter entry points without extending Graph.

Serialize Pause with ordinary dispatch. Before reporting Paused, purge waiting ordinary ingress and egress owned by runtime and host. Distinguish each pipe binding's trusted musical/control class and the provenance of ordinary and explicit output work. Carry the committed gate result in the existing private launcher pipe before exposing the associated serial reply, so the host applies the same fence before its next dispatch. Connection generations and public MIDI framing stay intact. Resume admits events newly accepted at the target after the gate transition. A WebSocket or native write already issued is outside the purge boundary and can still arrive. The gate generates no MIDI or release obligations and establishes no receiver silence.

The first Panic slice implements the optional explicit `panic` command for All Notes Off. The product's timed second action is a separate qualification. On acceptance, snapshot the eligible connected musical output IDs and their current generations. Send MIDI All Notes Off, CC 123 value `0`, once on each of the 16 channels for each selected output through its own actual adapter. This action bypasses the gate and individual route and transform settings. Exclude disconnected, unresolved, control-only, Panel, LED, Raw and reserved capture-only outputs. The command uses no remembered keys and leaves gate, routes and transforms unchanged. Keep generations and existing per-output order. Pause leaves explicit Panic work eligible. Disconnect still retires queued Panic for that binding. Bound work by the active output pool, attempt each eligible output independently and report refusal or uncertainty without retrying or replaying.

Eligibility includes an available musical output with no assigned cell or enabled edge. The runtime sends the fixed three-byte message `Bn 7B 00` through a private, exact-generation output adapter. The Managed Graph API remains Note On/Off-only. A full Panic queue is a refusal for that output and leaves its healthy binding available for subsequent ordinary traffic. Preflight the bounded result and staging capacity before any emission. At most `16 × eligible outputs` triplets can be attempted. Budget this independently of ordinary per-message fan-out.

The independent Panic callback takes its context and caller-owned bounded result storage. On a completed evaluation emit `PANIC N` followed by exactly `N` records `PANIC_OUT PORT GENERATION ACCEPTED` in logical port order, then `OK`. `ACCEPTED` is the exact count `0`–`16` accepted by the router's output adapter, or `-` if the count is unconfirmed. `N` can be zero. The report includes refusals and partial results. A command error before evaluation emits only `ERR`. Name the acceptance boundary in the UI. Receiver receipt and actual silence require separate observations.

Use the selected router's authorized CLI for browser controls. Show the last confirmed state with a pending indication during an operation. Adopt only the matching committed reply. Re-read on stale guard or reconnect. Never retry an ambiguous Panic automatically. All authorized views read the same gate. A second view needs an authorized read path under the existing exclusive serial admission. First qualify the ordinary portable terminal, then the same-origin Routing UI. Test queued ordinary traffic across Pause and Resume, continued CLI and feedback, and a failing Panic output alongside healthy outputs while paused. Pico and other platforms qualify the same command contract only when their actual providers and queue fences are ready.

## APC viewport

The production APC frontend owns a passive five-source by eight-destination viewport over Session's committed managed projection. Pan one row or column per accepted arrow press, clamp at both ends, and keep existing logical port ordering through attach and detach. Render all 40 cells from one coherent stored viewport and snapshot. An empty or incompatible cell is dark. Use committed Off, Pending and Active feedback for compatible cells. Navigation and pad input in this passive view change no routes. An edit through CLI refreshes the current view. A Raw lease still suppresses ordinary feedback, and restore uses the current committed viewport.

Select this mode explicitly in the configured frontend profile. Use APC protocol Port 1, MIDI channel index 0. Shift is note `62` hex. The left four Clip Stop buttons are Up `40`, Down `41`, Left `42` and Right `43`. The first nonzero Note On for an arrow while Shift is held requests one pan. Repeated presses while held and release edges request none. A Note Off or zero-velocity Note On releases that button. Losing the panel binding clears held edges and retains the viewport. Legacy color gestures remain in their own mode.

Chromium Port 1 captures matched the documented addresses using `90 nn 7F` on press and `80 nn 00` on release for Shift and all four arrows. Existing focused controls, pointer and touch paths emit ordinary panel MIDI. Independent desktop testing verified both-axis clamping, stable assignments, a CLI edit to 11→18 beyond the first window, and a complete restore of the committed viewport after Raw replay. Physical hardware confirmation remains separate. [APC Patch feedback baseline](patchbay-controls.md#production-patch-surface) · [Akai Control Mapping](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf) · [Akai user guide](https://cdn.inmusicbrands.com/akai/apc-key-25-mkii/APC%20Key%2025%20mk2%20-%20User%20Guide%20-%20v1.2.pdf)

### Editable physical APC Patchbay

The approved Pico increment uses the additive editable types in [`routing.h`](routing.h). Session owns one volatile five-source by eight-destination placement shared by both APC frontends. Assembly serializes reads and edits with Graph. Placement uses managed, registered logical port IDs in their correct direction. Zero means empty. Each axis permits one placement per port. Assign changes one slot. Move swaps two same-axis slots and can move into an empty slot. Neither operation rewrites a route. Reject wrong-direction, duplicate and stale assignments before changing placement. Keep the passive and existing one-pad profiles independent.

The Session snapshot gives both revisions, the full layout and ordered eligible ports, a bounded attack record per input, and all 40 cells from one coherent graph observation. Compatibility needs two assigned eligible ports with distinct parents or the exact Assembly-declared [independent native pair](local-router.md#managed-native-virtual-cable). Apply the same predicate to display, edits and startup snapshot validation. Availability describes current connections. For a compatible cell, the typed Graph route remains authoritative for desired Off, Pending or Active and current-message delivery. Permit a compatible disconnected pair to be enabled Pending. Set a cell by explicit desired state with the rendered layout and Graph revisions. On stale state or Graph rejection, repaint current state and wait for a fresh press. The 40 addressed cells are independent of the configured number of simultaneous branches and fan-out.

Assembly identifies the current source attachment and reports each accepted normalized, nonzero Note On to Session before dispatching through Graph. Attack sequence and monotonic timestamps describe input activity even when no route delivers it. They confer no delivery claim. In Learn, the initiating panel arms from the observed sequence, chooses one assigned row and confirms a single uniquely observed eligible input. More than one distinct source requires a fresh arm. Require a still-connected candidate and current layout revision at confirmation. Keep selection, preview, held gestures and transient activity timing in the frontend. The private Audition source and passive device census are outside placement, Learn and activity.

In Patch, the first nonzero pad press edits its mapped cell. Use the existing visual order, with row `r` and column `c` at pad note `8 × (4 − r) + c`. Scene and Track select the respective edge. Stop All Clips opens Arrange or cancels it. In Arrange the upper-left four pads select Assign, Move, Learn and Audition, and the bottom-right pad confirms a previewed edit. Matrix pad edits are suspended there. Select an edge before using left/right to browse eligible Assign ports. Move takes two edges on one axis. Input Learn takes a Scene row and one actual source. Audition takes a Track output and a fresh held Audition action. No action affects transport. Shift arrows retain navigation when there is an approved larger viewport. This fixed page clamps at its edges. Keep Keys and the Shift+Sustain prototype separate from this first build.

Use a coherent page of 40 RGB pad, eight Track and five Scene states per panel. Use solid frames for the current phase. Active is steady green, available Off is dim, empty or unavailable Off is dark, and Pending alternates amber phases on a 500 ms monotonic interval. Use edge lamps for selection. A source attack inverts its Scene edge briefly, initially 100 ms, then restores current selection. Coalesce repeats so they cannot extend an active pulse. Assembly owns independent bounded TX for both panels. At most the latest committed page and one started page cursor are pending for each. Keep musical delivery ahead of page refresh.

Assembly owns the single Audition operation and clock. Resolve the selected verified output and reserve one private Managed branch. The first phrase authors On and Off events for MIDI notes `48, 55, 60, 67, 72` at 150 ms each with 50 ms gaps, channel index `0` and velocity `96`. Start only from deliberate hold, with at most one current step and no catch-up or loop. The same owner's explicit release or Stop cancels future steps. If the current step's On was admitted, that control may send the step's authored Off once, early, to its still-current eligible target. Mode retirement, route edits and target or panel loss cancel stale work without creating an Off. Refusal or unavailability drops the event with no debt or retry.

A fresh normal Pico has no compiled musical placements or routes. Generic USB MIDI ports appear in Assign after [runtime Graph admission](pico-target.md#circuit-tracks-and-downstream-capacity). Enumeration alone supplies no matrix eligibility. The automatic default works independently of placement, and Assign changes placement only. A fixture can exercise all five input and eight output positions. Physical acceptance covers real assignments, stale edits, placement moves, Learn, Audition, scoped delivery and feedback. For a shared physical and browser witness, use the dedicated Pico WSS path on the isolated bench. The live Pi router MIDI roles retain their own runtime.

In physical Assign, offer one deliberate Unassigned candidate on each axis. Confirm submits port ID `0` with the captured layout revision and clears just the selected slot. Cancel and stale confirmation leave the placement intact. Clearing or reassigning a slot preserves the desired routes between logical ports.

### Automatic musical routes

The next normal Pico release applies explicit verified configuration first. Establish the first unambiguous verified APC control binding through the role authority before considering its output for automatic routing. Honor explicit role overrides and leave user-claimed outputs with their owners. Then consider currently attached, eligible managed musical input and output directions on different parents. Start with ordinary USB pairs, followed by verified managed native or external endpoints. Use source and destination logical ID order within each group. Skip Panel and LED ports, Raw, Audition, known feedback paths and all same-parent pairs. The Computer virtual cable remains explicit-only.

Use the Graph's existing directed device-cycle check, including enabled Pending edges. The first default validates the whole device graph because the managed path carries accepted notes on every channel. USB descriptors establish port direction and transport shape. They supply no instrument channel setting or external echo information. Refuse reverse and transitive cycles, keep already enabled intent, and leave excess pairs Off when branch or fan-out runs out. Route discovery and enablement emit no musical content or held-note replay. Evaluate candidates with bounded foreground work after coherent inventory changes.

The normal budget of seven managed branches and fan-out three leaves at most six automatic branches with one reserved for Audition and space reduced by explicit enabled routes. Four independent single-pair duplex devices supply 12 directed candidates and at most six acyclic edges. Additional native endpoints, cables or physical devices can exceed the budget. Report effective routes and resource limits separately from physical USB and logical port capacity.

Use named `routing` rows for actual branches and bindings. An optional `autoroute` summary may emit `AUTO REV CANDIDATES ACTIVE PENDING USER_OFF FEEDBACK LIMIT UNAVAILABLE` and `OK`. Qualification requires a complete list of excluded pairs as well as the committed routes.

`autoroute explain` starts the bounded exclusion read. `autoroute explain REV OFFSET` continues it. Reply with `AUTO_EXPLAIN REV ROUTING_REV COUNT NEXT`, up to eight `AUTO_SKIP SRC DST REASON` records ordered by source ID then destination ID, and `OK`. `COUNT` is the total excluded pair count and `NEXT` is the next offset or `-`. Use the existing 256-byte record and line framing limits. Read names and direction from both IDs' `BINDING` records using the exact `ROUTING_REV` on the same target. Report `ERR UNAVAILABLE` for a stale continuation or an incomplete current evaluation. Restart both reads if revisions no longer match. Keep the existing Routing rows and revision.

Evaluate managed musical input and output pairs before applying pair-specific exclusions. Record the Computer return and other same-parent managed exclusions. Panel, LED, Raw and Audition are outside the musical candidate list. Reasons come from the actual evaluated condition, including `user-off`, `computer-return`, `same-parent`, `known-feedback`, `cycle`, `audition-reserved`, `branch-limit`, `fanout-limit`, or `unavailable`. Use the Graph admission decision for reverse and transitive cycles, including Pending edges. A generic error does not establish a specific reason. Retain one reason per excluded pair. The explanation is a read of a coherent evaluation and never attempts to enable a route.

The candidate count is bounded by inputs times outputs, at most 81 pairs with 18 registered directions and 324 with 36. A bounded record of two registration indices and one reason byte per excluded pair fits in target-owned storage. Resolve actual IDs and names from the same revision rather than duplicating names per pair. The order of explanation rows is independent of admission priority.

Treat operator Off as explicit intent even when the pair is already Off. Keep it for the verified logical pair across rediscovery in the running instance and respect explicit role clear or override. Apply the override only when the ordinary Graph edit succeeds. Persist an Off override only through a qualified configuration that represents it. An enabled-pair snapshot alone cannot distinguish it from a missing route.

The configured runtime loans Session a bounded bitmap indexed by stable Graph registration slots. CLI route edits and physical cell edits share the operator path. Successful On clears explicit Off, and a failed Graph operation leaves the bitmap unchanged. Auto enable checks the current bitmap before asking Graph. Audition and startup snapshot restore use a system path that leaves operator intent alone. Plan and summary invalidation include bitmap changes even when the Graph revision stays the same.

Startup and verified musical attachment or return trigger default enablement. Layout edits, explicit route edits, and APC role mark or unmark update actual state and may invalidate a running plan. They do not trigger new automatic routes on their own. Reserved and retiring LED outputs are ineligible. After unmark, the output is available for explicit routes and stays excluded from automatic routing during the current attachment generation. A verified return makes it eligible for automatic routing again, subject to existing pair-specific Off overrides. The explicit LED role override remains in force on return.

Verify automatically routed notes with APC absent, cycle rejection with both directions present, exhaustion, manually disabled pairs staying Off after verified reconnect, scoped queue retirement and zero replay. Automatic routing supplies no additional MIDI message types.

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

If admission is disabled, return `UNAVAILABLE`. Invalid input returns `INVALID`. A target ceiling, compiled backing or exhausted revision returns `CAPACITY`. No failure publishes a partial device, consumes a slot, changes any existing route or retires notes. Never reorder or recycle slots within the runtime. The append changes inventory only. Assign and route enablement are subsequent operator actions. A later USB interface on the same device may need a separate admitted batch contract once the measured topology requires it. This first operation accepts the eligible directions together for one complete observed device.

The first physical profile has room for one new single-interface bidirectional device beyond its startup inventory. Allocate the lowest unused positive ID separately for the device and for each directional port, from the current registry. IDs from a separate Keys build impose no reservations in this profile. The startup layout stays unchanged. New ports start unassigned. A second physical discovery witness needs a profile with enough unused slots for that device's full batch. If the target is full, the USB census may still show the mounted device while the Routing and Assign views omit it. Preserve the existing inventory and report capacity at the admission boundary. Logical ID continuity in this slice is within one running instance. Cross-restart mappings require qualified persistence.

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
