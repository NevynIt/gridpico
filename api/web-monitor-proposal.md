# Web Monitor destination contract

**Owner** Architect. **Status** Named Monitor destination independently verified and published at `71c27b4`, following the two-branch Patch gate at `299ec25`. This contract covers the connected `/patch/midi` fixture. The [production Diagnostics Monitor](browser-midi.md#production-startup-and-page-roles) uses a separate directional MIDI binding. [Connected Patch contract](patchbay-controls.md#separate-connected-patch-path) · [Monitor evidence](../ROADMAP.md#web-apc-rapid-play-regression-and-next-monitor-update)

## Local listening controls

Default the selector to **All channels**, with MIDI 1–16 as the other choices. Keep each channel's existing sound and mute preference, plus the master volume. Filtering is local playback state. Accepted MIDI continues to reach Capture. Switching the selector releases voices that are newly excluded and preserves voices still included. Newly included channels wait for a fresh Note On. Apply the same live-event rule after enable, unmute or reattach. Disabling audio releases browser voices and leaves the endpoint attached.

## One logical destination

In the expanded Patch fixture, reserve device ID `3` named **Web Monitor** and output port ID `6` named **Monitor**. Register that port once as Patch-eligible at destination column 3. These IDs belong to this fixture profile. The logical port and its slot survive a same-runtime detach and reattach. Start its route off. Session and the graph own enablement, pending state, active state and held-note cleanup as for any other destination.

The endpoint attaches when the owning browser's Monitor event receiver is ready. Disabling audio, changing channels or muting can leave a working receiver attached. The virtual adapter accepts and captures received MIDI against a connected generation within its existing delivery budget. Audio playback stays local and optional.

## Attachment and recovery

Use one owning browser and one logical Monitor attachment on the existing Patch connection. The browser adapter subscribes when its event receiver is ready. Computer connections owns the subscription lifetime, and Assembly supplies the virtual sink and serializes the existing typed port update with notes and route commands. For same-runtime verification, the host or native integration fixture can independently detach and reattach that attachment while the Patch fixture and instrument routes continue running. Limit browser lifecycle requests to subscribe and unsubscribe for that socket's own Monitor receiver. The host selects the port and generation. An audio-enable control is a separate action.

On detach, the browser releases all voices for that attachment. The graph marks an enabled Monitor route pending, retains any release debt for the logical destination and keeps other outputs usable. Reattach uses a newer nonzero generation after explicit detach. Start with an empty browser voice set. Reconcile owed Note Offs to the verified receiver before the route becomes active. An Off with no matching voice is harmless. Stale-generation events have no playback effect. Reattach does not replay held notes.

Whole-socket close retains the current Patch runner cleanup and retirement. Reopening the page creates a fresh fixture. Keeping instrument routes alive across loss of the entire browser connection would require a host-owned runtime that outlives the socket. Simultaneous connected browsers would require a separate choice between shared playback subscribers and distinct routable destinations. Keep one connected owner for this increment, consistent with the current host.

## Profile limits and storage

| Profile | Devices / ports / branches | Input occurrences / release credits |
| --- | --- | --- |
| Native Patch | `2 / 5 / 2` | `2 / 4` |
| Two-branch interactive Patch | `2 / 5 / 2` | `25 / 50` |
| Monitor-enabled Patch app | `3 / 6 / 3` | `25 / 75` |

The [shared C declarations](patchbay.h) use `GRIDPICO_PATCH_MAX_DEVICES`, `GRIDPICO_PATCH_MAX_PORTS` and `GRIDPICO_PATCH_MAX_BRANCHES` for backing arrays. Size Session color storage and route scratch space by those same limits. Iterate over registered inventory counts and the selected runtime branch limit. Keep sentinels outside valid backing indices. If the eligible ports cannot fit the five source rows or eight destination columns, Session returns `GRIDPICO_CAPACITY` before publishing a snapshot. Snapshot counts, CLI status and tests use the actual profile.

Assembly selects `gridpico_patch_graph_init_monitor(graph, inventory, deliver, delivery_context)` and `gridpico_patch_fixture_init_monitor(fixture, inventory)`. Reuse the Session initializer. Select `GRIDPICO_PATCH_MONITOR_DEVICES`, `GRIDPICO_PATCH_MONITOR_PORTS` and `GRIDPICO_PATCH_MONITOR_BRANCHES` as the Monitor topology limits and `GRIDPICO_PATCH_MONITOR_NOTE_OCCURRENCES` and `GRIDPICO_PATCH_MONITOR_RELEASE_CREDITS` as its performance limits. The native and two-branch initializers retain their respective limits and actual inventory. The Monitor inventory uses `GRIDPICO_PATCH_MONITOR_DEVICE_ID` and `GRIDPICO_PATCH_MONITOR_PORT_ID` for the virtual device and its output. Its optional device color uses the shared Session preference. Existing CLI commands, Diagnostics serial records and providers stay in use. Keep the native transpose and Route-view profiles independent.

Keep the 5×8 grid, 16 retained capture rows and 256-byte frame limit. The Monitor runner stages up to 75 accepted notes per dispatch, independently of retained history. Inventory can use up to 11 frames. Allow one request in flight and 162 queued requests for this host profile. The queue covers 158 key and panel edges, two refreshes and two subscription edges. That compact JSON burst uses at most 7,683 payload bytes within the 8 KiB browser write bound. Use a 12 KiB host outbound bound. Budget up to 11,065 bytes for 75 tagged notes, a page marker, 40 LEDs and an error. A lifecycle dispatch budgets up to 25 Monitor Offs, its marker, inventory, page and error within 6,807 bytes. Both outbound estimates include four bytes of allowance per frame. Keep existing stall deadlines and early forwarding of accepted notes. The browser's Web Audio voice budget remains separate.

## Launch and regression modes

Run `node connections/patch-host.js <patch-runner> [port]` and open `/patch/` on the local host. The default port is 8766. This starts `gridpico_patch_runner --monitor` and serves the Monitor-enabled `/patch/midi` adapter. Verify Patch audio here using the named Web Monitor destination and its attached receiver.

Use `node connections/patch-host.js --two-branch <patch-runner> [port]` for two-branch routing and transport regression. This starts the runner with no arguments and retains the original untagged accepted MIDI in Capture. Browser audio stays silent in this mode because the fixture has no Web Monitor destination.

A runner tap forwards its command-line arguments to the real executable so the requested profile reaches the runner. The separately hosted `/demo/midi` Route view retains its existing Monitor audio, capture feed and manual Reconnect.

## Framing and readiness order

Reuse the existing `gridpico_note_delivery`, route callbacks and `gridpico_patch_port_update`. Browser requests are `{"type":"monitor-subscribe"}` and `{"type":"monitor-unsubscribe"}` with only the `type` field. On the LF runner link they become `monitor-attach <generation>` and `monitor-detach <generation>`. Computer connections selects port `6` and owns the generation. Initial Monitor inventory is disconnected. Reserve generation `1` as the fixture's dormant value. The first attach uses `2` and later attaches use a strictly larger generation after explicit detach. Never wrap. If no newer generation is available, report ordinary `CAPACITY` and preserve the current state.

| Monitor-profile runner output | Browser message |
| --- | --- |
| `on <port-id> <generation> <channel> <note> <velocity>` or `off` with the same fields | `{"type":"note-on","channel":1,"note":60,"velocity":96,"destinationPort":6,"destinationGeneration":2}` or `note-off` |
| `monitor-state 6 <generation> <attached-0-or-1>` | `{"type":"patch-monitor","portId":6,"generation":2,"attached":true}` or `attached:false` |

Only Monitor-profile Patch output notes use the destination-tagged shape. Input `on` and `off` lines, existing browser keybed messages, the 40-pad feedback page and CLI reply formats stay as specified. The host forwards records in this order:

1. Runner `ready`, then initial `surface-ready` exchange. Validate the full startup inventory and LED page before publishing browser `ready`, the disconnected Monitor inventory and the page. Check that port `6` belongs to device `3` and is a Patch-eligible output assigned to column 3. On lifecycle refresh the port state must agree with the attachment marker. The browser adapter installs the receiver and waits for complete startup data before subscribing.
2. On attach, publish `patch-monitor` with `attached:true` for the current generation before any accepted rows for it. Send the accepted owed Note Offs for that generation, then the coherent inventory and LED refresh, then `ok` or `err CODE`. Attachment state says the receiver is available. An enabled route can remain pending, with `err UNAVAILABLE`, if a required release fails. A repeated subscribe while attached uses the current generation and sends the same marker and complete refresh. It retries only owed releases, preserves current voices, and leaves the revision unchanged when no graph state changes.
3. On detach, publish `attached:false` with the retired generation, then a complete inventory and LED refresh, then the result. The browser stops that attachment's voices immediately. Charge release debt to the logical port until the next verified generation. Repeating unsubscribe when already detached emits zero browser frames. The host may suppress the runner request. At the runner, detaching the already retired current generation returns `ok` only.
4. The runner returns `err INVALID` for malformed lifecycle lines or out-of-range generations. A stale generation or a different generation on an already attached receiver returns `err UNAVAILABLE`. An invalid or stale request returns only the error. If attachment changed or the current generation was verified, preserve the authoritative marker and accepted rows even when a later read fails. Publish inventory and LEDs only as a complete refresh. If the read fails, send the error after the marker and accepted rows. Ordinary `patch-refresh` keeps its complete-success or error-only rule. Malformed browser frames follow the existing transport rejection behavior.
5. Forward lifecycle markers and validated accepted notes as they arrive, before inventory, LEDs and any error. The browser adapter calls `receiveMonitor` before `receiveCapture` for lifecycle events and their accepted notes. Every Monitor-profile output note carries a registered output port ID, a nonzero unsigned 32-bit destination generation, channel 1–16, note 0–127, and velocity 1–127 for On or 0–127 for Off. Missing or malformed tags are framing errors. Capture can display all accepted destinations. Only a note for the live socket's attached Monitor port **and** exact current generation can affect its audio. Key voices by `(socket instance, destinationPort, destinationGeneration, channel, note)`. A stale or unrelated Off leaves a Monitor voice sounding.

## Full cleanup at loss

For whole-socket loss, discard queued requests and close runner stdin. The runner disconnects the keybed before retiring the server-owned Monitor sink. The three registered sinks can accept cleanup into their bounded capture even when browser output is unavailable. With 25 distinct sounding notes on all three live branches, this yields exactly 75 accepted zero-velocity Off rows, 25 per destination. Drain those rows through process exit. No delivery to the closed browser is asserted. Its adapter silences audio and clears held controls on loss. A prior Monitor-only detach leaves no live Monitor receiver. In that case the remaining active outputs clean up, and any debt for the gone virtual receiver ends with fixture retirement.

A true keybed Note On overflow follows the same bounded cleanup before `err CAPACITY`, with accepted rows flushed early, the typed error and close `1013`. A later EOF performs no duplicate release. Keep valid surface `CAPACITY` recoverable as in the fast-play contract. Monitor-only detach and reattach is a separate test. It can owe at most 25 releases for Monitor and keeps the instrument branches usable in the same fixture.

## Acceptance

1. In the connected Patch fixture, route one source to an instrument and Web Monitor. Hold a note. Verify that both destinations accepted it and that Monitor sounds. Disable only the instrument branch. Observe its destination-specific Note Off and verify the Monitor signal continues. Release the source and verify exactly one Monitor Note Off and silence. Separately exercise disabling the Monitor branch while held.
2. Verify All and the chosen channel filter in Chromium, including release on switching off a held channel, retention of per-channel sound and mute, and no playback from historical capture. The current connected keybed uses channel 1. Exercise simultaneous different channels using local accepted samples or the native fixture.
3. Detach and reattach Monitor within one running fixture. Check stable destination column, pending and active transitions, a newer generation, owed release reconciliation without held-note replay, and continued instrument delivery. Verify a stale-generation Off and an instrument Off leave a current Monitor voice alone.
4. Hold 25 distinct notes with all three routes active. Verify 75 accepted release rows on whole-browser loss in the native runner drain, by destination and generation. Independently test terminal overflow and confirm no second cleanup after EOF. Measure browser audio signal separately from physical speaker audibility.
5. Run `--two-branch` separately and verify route cleanup, untagged accepted Capture rows and browser silence. Rerun native Patch, native transpose, Route-view playback, and shared CLI/color/Diagnostics regressions.
