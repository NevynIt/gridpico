# Web Monitor destination proposal

**Owner** Architect. **Status** Implementation handoff plan. Start only after independent Patch fast-play verification and publication. See the [current connected Patch contract](patchbay-controls.md#separate-connected-patch-path) and [verified limit](../ROADMAP.md#next-approved-product-work-after-web-apc-protocol-and-timed-leds).

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

Use distinct maximum storage extents for C arrays, Session color storage and route scratch space. Three devices, six ports and three branches are backing extents. Iterate over registered inventory counts and the selected runtime branch limit. Keep sentinels outside the valid backing indices. Keep existing native and two-branch fixture declarations and expected counts at `2 / 5 / 2`. The native and two-branch graph initializers keep their respective limits. Only an explicit Monitor app initializer accepts the expanded inventory and admits the third branch. The Monitor inventory declares its six actual ports. Snapshot counts, CLI status and tests use the actual profile. Store the virtual device's optional color in the shared Session state. Existing CLI commands and Diagnostics serial records and providers stay in use. Keep the native transpose and Route-view profiles independent.

Keep the 5×8 grid, 16 retained capture rows and 256-byte frame limit. The Monitor runner stages up to 75 accepted notes per dispatch, independently of retained history. Inventory can use up to 11 frames. Keep one request in flight. Allow 162 queued requests for this host profile, covering the 158 key and panel edges, two refreshes and two subscription edges. Keep the 8 KiB browser write limit. Use a 12 KiB host outbound bound for this profile. A conservative budget for 75 destination-tagged notes plus 40 LEDs, page and error frames is 11,001 bytes including framing allowance. Keep the existing stall deadlines and early forwarding of accepted notes. The browser's Web Audio voice budget remains separate.

## Framing and readiness order

Reuse the existing `gridpico_note_delivery`, route callbacks and `gridpico_patch_port_update`. The host sends the runner only scoped receiver lifecycle requests. Browser requests are `{"type":"monitor-subscribe"}` and `{"type":"monitor-unsubscribe"}`. On the LF runner link they become `monitor-attach <generation>` and `monitor-detach <generation>`. Browser clients do not choose a port or generation. Initial Monitor inventory is disconnected. Reserve generation `1` as the fixture's dormant value to match the current Graph initialization convention. The first attach uses `2` and later attaches use a strictly larger generation after explicit detach. An already attached subscribe retries owed releases on the current generation and is idempotent when none are owed. An already detached unsubscribe leaves state intact. Do not wrap generations within a fixture.

| Monitor-profile runner output | Browser message |
| --- | --- |
| `on <port-id> <generation> <channel> <note> <velocity>` or `off` with the same fields | `{"type":"note-on","channel":1,"note":60,"velocity":96,"destinationPort":6,"destinationGeneration":2}` or `note-off` |
| `monitor-state 6 <generation> <attached-0-or-1>` | `{"type":"patch-monitor","portId":6,"generation":2,"attached":true}` or `attached:false` |

Only Monitor-profile Patch output notes use the destination-tagged shape. Input `on` and `off` lines, existing browser keybed messages, the 40-pad feedback page and CLI reply formats stay as specified. The host forwards records in this order:

1. Runner `ready`, then initial `surface-ready` exchange. Browser receives `ready`, disconnected Monitor inventory and the initial LED page. The adapter installs its receiver before subscribing.
2. On attach, publish `patch-monitor` with `attached:true` for the current generation before any accepted rows for it. Send owed releases, then the coherent inventory and LED refresh, then the dispatch result. Attachment state says the receiver is available. Graph state can still report an enabled route pending if a required release fails. An `err UNAVAILABLE` can accompany the attached marker and pending page.
3. On detach, publish `attached:false` with the retired generation and refresh inventory and LEDs. The browser stops that attachment's voices immediately. Charge release debt to the logical port until the next verified generation. A duplicate attached marker for the same generation preserves current voices.
4. For note and pad dispatches, forward validated accepted notes as they arrive, before LEDs and the terminator. Require a registered output port ID and a nonzero unsigned 32-bit generation on every Monitor-profile output note. Missing or malformed tags are framing errors. Capture can display all accepted destinations. Only a note for the live socket's attached Monitor port **and** exact current generation can affect its audio. Key voices by `(socket instance, destinationPort, destinationGeneration, channel, note)`. A stale or unrelated Off cannot release a Monitor voice. `/demo/midi` retains its existing feed and frame shapes.

## Full cleanup at loss

For whole-socket loss, discard queued requests and close runner stdin. The runner disconnects the keybed before retiring the server-owned Monitor sink. The three registered sinks can accept cleanup into their bounded capture even when browser output is unavailable. With 25 distinct sounding notes on all three live branches, this yields exactly 75 accepted zero-velocity Off rows, 25 per destination. Drain those rows through process exit. No delivery to the closed browser is asserted. Its adapter silences audio and clears held controls on loss. A prior Monitor-only detach leaves no live Monitor receiver. In that case the remaining active outputs clean up, and any debt for the gone virtual receiver ends with fixture retirement.

A true keybed Note On overflow follows the same bounded cleanup before `err CAPACITY`, with accepted rows flushed early, the typed error and close `1013`. A later EOF performs no duplicate release. Keep valid surface `CAPACITY` recoverable as in the fast-play contract. Monitor-only detach and reattach is a separate test. It can owe at most 25 releases for Monitor and keeps the instrument branches usable in the same fixture.

## Acceptance

1. In the connected Patch fixture, route one source to an instrument and Web Monitor. Hold a note. Verify that both destinations accepted it and that Monitor sounds. Disable only the instrument branch. Observe its destination-specific Note Off and verify the Monitor signal continues. Release the source and verify exactly one Monitor Note Off and silence. Separately exercise disabling the Monitor branch while held.
2. Verify All and the chosen channel filter in Chromium, including release on switching off a held channel, retention of per-channel sound and mute, and no playback from historical capture. The current connected keybed uses channel 1. Exercise simultaneous different channels using local accepted samples or the native fixture.
3. Detach and reattach Monitor within one running fixture. Check stable destination column, pending and active transitions, a newer generation, owed release reconciliation without held-note replay, and continued instrument delivery. Verify a stale-generation Off and an instrument Off leave a current Monitor voice alone.
4. Hold 25 distinct notes with all three routes active. Verify 75 accepted release rows on whole-browser loss in the native runner drain, by destination and generation. Independently test terminal overflow and confirm no second cleanup after EOF. Measure browser audio signal separately from physical speaker audibility. Rerun native Patch, native transpose, Route-view, and shared CLI/color/Diagnostics regressions.
