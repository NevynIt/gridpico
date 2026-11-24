# Configurable router profiles and Routing

**Owner** Architect. **Status** Desktop profile, local Routing and passive APC viewport independently verified. Physical device access and USB identity verification remain Stage 3 gates. [Roadmap and evidence](../ROADMAP.md#approved-next-delivery--routing-and-local-device-access) · [Local router baseline](local-router.md) · [Browser MIDI](browser-midi.md)

## Profile and admission

Application Assembly supplies one startup registry and resource profile. Register named devices and directional logical ports with unique IDs, parent device, role, direction, capability and bounded queue resources. Validate the entire configuration before readiness. Port IDs and order remain stable during a run. Keep the six starter roles and their IDs where configured. The existing default, native and demo profiles retain their respective limits and behavior.

Assembly provides the effective peer admission limit to Connections on their private readiness boundary. The existing private bind reply remains authoritative for each port's direction, queue limit and generation. Graph enforces topology, fan-out, occurrence and release budgets. Session projects the named inventory and route state. Connections bounds pending as well as live peers and queues, with the serial attachment counted separately. On WSS, publication remains an explicit set of directional IDs in the selected profile. Registration alone never publishes a port.

The verified [desktop configuration](../app/profiles/desktop.conf) registers 8 devices and 18 directional ports with 18 socket peer slots, 12 managed branches plus one exclusive Raw branch, four managed destinations per source, 50 aggregate held occurrences and 200 release credits. Its six managed inputs, nine managed outputs and three dedicated Panel/Raw ports exercise both APC viewport axes. Independent loopback testing admitted all 18 MIDI peers alongside serial and verified scoped saturation handling. The six-port default retains its smaller limits. These are desktop profile values. Each target supplies its own measured limits. A press gets no new output until all required accounting can be reserved.

### C11 integration preflight

Use the existing `gridpico_local_registry` inventory plus its capabilities vector in port registration order. [Graph types](local_router.h) carry the `gridpico_local_profile` limits alongside it. Counts come from the registry. Keep per-direction queue limits explicit in the bind result and cap Raw at one exclusive branch. Count enabled Pending branches against the configured branch and fan-out limits. The configured initializer copies the registry and limits on success. Existing initializers keep their signatures and prior active counts. Check all counts, per-source fan-out and staging resources against backing before publishing the profile. Assembly owns the startup configuration and validation.

The desktop build has shared backing for 8 devices, 18 ports, 13 branches, 50 occurrences and 200 credits. Per-occurrence membership is bounded by configured fan-out. Assembly stages up to 285 MIDI frames per dispatch with 18 additional fault slots. The MIDI budget covers 200 owed releases, two 40-frame page allowances, four fan-out frames and one Raw frame. Desktop binding queues are 64 frames except the 128-frame Panel input. Serial retains its independent 8 KiB queue in each direction.

A strict C11 `sizeof` probe of the pre-extension x86-64 headers measured 464 bytes for inventory, 504 for Patch graph state, 664 for the Patch snapshot, 4,672 for graph storage, 64 for Session, 4,680 for output staging and 22,792 for the entire local runtime.

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

Use this registration order. The sixth managed source is 11 and the ninth managed destination is 18. Pan down and right and read back an edit to `route 11 18 managed on`. For full cleanup separately enable 1 → 5, 6, 12, 13 and 7 → 14, 15, 16, 17 with those sinks attached. Hold 25 distinct notes on each source and verify 200 accepted attacks and the 200 owed releases across the distinct outputs on orderly runtime teardown. Verify earlier accepted release, branch-only disable, and a stalled binding as separate cases. Preserve the exact traces of the legacy proof and demo profiles.

Prove useful concurrent traffic with more than six MIDI attachments and an independent serial reader. Exercise duplicate and wrong-direction rejection, profile-limit rejection, and another binding's continued progress under a slow peer. Verify Routing reads all pages with maximum-length names, discards a stale continuation after a port or route change, and shows a rejected edit without changing the projected route.

Independent desktop tests verified 200 exact attacks and 200 zero-velocity releases at eight binary receivers during live detach. Orderly runner EOF separately staged and accepted 200 owed releases while the sinks were available. The same gate verified Routing paging, stale continuation rejection, and the Free, In use and pool-full Unavailable transitions. [Roadmap and evidence](../ROADMAP.md#2-expand-registered-ports-and-make-routing-visible)

## Routing through the local CLI

Home links to `/diagnostics/#routing`. The Routing view and Diagnostics transcript share one selected serial session and one serialized command queue. Allow one command in flight. Identify the selected router and serial provider before enabling edits. The view reads the same CLI as a terminal and submits the existing `route SRC DST managed|raw [on|off]` commands. It renders committed readback. A rejected edit preserves authoritative state. Raw LED replay still uses an explicit lease and the designated LED receiver.

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

## APC viewport

The production APC frontend owns a passive five-source by eight-destination viewport over Session's committed managed projection. Pan one row or column per accepted arrow press, clamp at both ends, and keep existing logical port ordering through attach and detach. Render all 40 cells from one coherent stored viewport and snapshot. An empty or incompatible cell is dark. Use committed Off, Pending and Active feedback for compatible cells. Navigation and pad input in this passive view change no routes. An edit through CLI refreshes the current view. A Raw lease still suppresses ordinary feedback, and restore uses the current committed viewport.

Select this mode explicitly in the configured frontend profile. Use APC protocol Port 1, MIDI channel index 0. Shift is note `62` hex. The left four Clip Stop buttons are Up `40`, Down `41`, Left `42` and Right `43`. The first nonzero Note On for an arrow while Shift is held requests one pan. Repeated presses while held and release edges request none. A Note Off or zero-velocity Note On releases that button. Losing the panel binding clears held edges and retains the viewport. Legacy color gestures remain in their own mode.

Chromium Port 1 captures matched the documented addresses using `90 nn 7F` on press and `80 nn 00` on release for Shift and all four arrows. Existing focused controls, pointer and touch paths emit ordinary panel MIDI. Independent desktop testing verified both-axis clamping, stable assignments, a CLI edit to 11→18 beyond the first window, and a complete restore of the committed viewport after Raw replay. Physical hardware confirmation remains separate. [APC Patch feedback baseline](patchbay-controls.md#production-patch-surface) · [Akai Control Mapping](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf) · [Akai user guide](https://cdn.inmusicbrands.com/akai/apc-key-25-mkii/APC%20Key%2025%20mk2%20-%20User%20Guide%20-%20v1.2.pdf)

## Verified USB replacement

Stage 3 reserves the verified USB selector for Linux input attachment. Assembly resolves the registered input against one unique adapter-qualified match with a verified serial, interface and direction. Missing or ambiguous matches leave enabled routes Pending. Display names, transient USB/OS addresses and topology alone cannot establish continuity.

Retire the old input generation and clean up its contributions at reachable outputs. A verified return uses a fresh generation and waits for fresh input. A USB selector on an output reserves it and stays Pending, even with a matching identity. Existing operator-selected numeric outputs remain available through their existing configuration. The physical input and future generation-bound output gates are separate. Cross-restart persistence awaits a separate configuration or Projects contract. [Linux adapter contract and evidence](linux-midi-adapter.md#stage-3-usb-identity-seam)
