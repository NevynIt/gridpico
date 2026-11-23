# Web Monitor destination proposal

**Owner** Architect. **Status** Planning recommendation for Project Manager. See the [current connected Patch contract](patchbay-controls.md#separate-connected-patch-path) and [verified limit](../ROADMAP.md#next-approved-product-work-after-web-apc-protocol-and-timed-leds).

## Local listening controls

Default the selector to **All channels**, with MIDI 1–16 as the other choices. Keep each channel's existing sound and mute preference, plus the master volume. Filtering is local playback state. Accepted MIDI continues to reach Capture. Switching the selector releases voices that are newly excluded and preserves voices still included. Newly included channels wait for a fresh Note On. Apply the same live-event rule after enable, unmute or reattach. Disabling audio releases browser voices and leaves the endpoint attached.

## One logical destination

In the expanded Patch fixture, reserve device ID `3` named **Web Monitor** and output port ID `6` named **Monitor**. Register that port once as Patch-eligible at destination column 3. These IDs belong to this fixture profile. The logical port and its slot survive a same-runtime detach and reattach. Start its route off. Session and the graph own enablement, pending state, active state and held-note cleanup as for any other destination.

The endpoint attaches when the owning browser's Monitor event receiver is ready. Disabling audio, changing channels or muting can leave a working receiver attached. The virtual adapter accepts and captures received MIDI against a connected generation within its existing delivery budget. Audio playback stays local and optional.

## Attachment and recovery

Use one owning browser and one logical Monitor attachment on the existing Patch connection. The browser adapter subscribes when its event receiver is ready. Computer connections owns the subscription lifetime, and Assembly supplies the virtual sink and serializes the existing typed port update with notes and route commands. For same-runtime verification, the host or native integration fixture can independently detach and reattach that attachment while the Patch fixture and instrument routes continue running. Limit browser lifecycle requests to subscribe and unsubscribe for that socket's own Monitor receiver. The host selects the port and generation. An audio-enable control is a separate action.

On detach, the browser releases all voices for that attachment. The graph marks an enabled Monitor route pending, retains any release debt for the logical destination and keeps other outputs usable. Reattach uses a newer nonzero generation after explicit detach. Start with an empty browser voice set. Reconcile owed Note Offs to the verified receiver before the route becomes active. An Off with no matching voice is harmless. Stale-generation events have no playback effect. Reattach does not replay held notes.

Whole-socket close retains the current Patch runner cleanup and retirement. Reopening the page creates a fresh fixture. Keeping instrument routes alive across loss of the entire browser connection would require a host-owned runtime that outlives the socket. Simultaneous connected browsers would require a separate choice between shared playback subscribers and distinct routable destinations. Keep one connected owner for this increment, consistent with the current host.

## Minimal fixture and wire changes

Start after independent acceptance of the [two-branch interactive Patch profile](patchbay-controls.md#interactive-patch-fast-play-proposal). Keep native and Route-view regressions independent. Extend the 25-occurrence app profile to three devices, six ports and three branches. This leaves both instrument outputs usable alongside Monitor. Reserve 75 release credits and stage up to 75 accepted note rows for a full cleanup. Keep the 5×8 grid, 16 retained capture rows and 256-byte frame limit. An inventory refresh can use up to 11 frames including the boundary records. Reuse the existing typed delivery, port update and route callbacks. The Patch runner must retain the destination fields already present in `gridpico_note_delivery` when staging accepted rows.

Keep the fast-play input backlog. A conservative compact response with 75 destination-tagged notes, 40 LED frames, a marker and an error uses about 11 KiB with framing allowance. Plan a 12 KiB outbound bound for this profile if responses are still batched. An ordered drain with the existing 8 KiB bound would require testing that it can carry the full response without starving cleanup. Monitor's local Web Audio voice budget remains separate.

Extend only accepted **Patch output** note frames with `destinationPort` and `destinationGeneration`. For example, `{"type":"note-on","channel":1,"note":60,"velocity":96,"destinationPort":6,"destinationGeneration":2}`. Carry the same IDs on the runner's accepted `on` and `off` rows. The host publishes attachment state such as `{"type":"patch-monitor","portId":6,"generation":2,"attached":true}` before forwarding events for that generation. Capture can identify all destinations. Monitor consumes only its own destination and current generation, keyed by destination, generation, channel and note. The one-pad `/demo/midi` retains its existing single-destination Monitor feed and framing.

## Acceptance

1. In the connected Patch fixture, route one source to an instrument and Web Monitor. Hold a note. Verify that both destinations accepted it and that Monitor sounds. Disable only the instrument branch. Observe its destination-specific Note Off and verify the Monitor signal continues. Release the source and verify exactly one Monitor Note Off and silence. Separately exercise disabling the Monitor branch while held.
2. Verify All and the chosen channel filter in Chromium, including release on switching off a held channel, retention of per-channel sound and mute, and no playback from historical capture. The current connected keybed uses channel 1. Exercise simultaneous different channels using local accepted samples or the native fixture.
3. Detach and reattach Monitor within one running fixture. Check stable destination column, pending and active transitions, a newer generation, owed release reconciliation without held-note replay, and continued instrument delivery. Then verify whole-browser disconnect separately. Measure browser audio signal separately from physical speaker audibility. Rerun the current native and one-pad baselines.
