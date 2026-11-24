# Production Patch controls and Diagnostics

**Owner** Architect. **Status** Verified connected Patch and shared control profiles. This document retains the stage-one bounds for comparison. The independent production web tools use the [local router](local-router.md). [Public C11 declarations](patchbay_controls.h) · [Stage-one fixture](patchbay.md#stage-one-native-patch-fixture).

## Production Patch surface

Use the production APC frontend with `gridpico_patch_read_snapshot_fn`, `gridpico_patch_set_route_fn` and `gridpico_apc_surface_feedback_fn`. Session and Graph own route meaning and delivery. APC owns pad gesture state, projection of the snapshot to LEDs and page display. Web APC sends surface bytes and renders returned feedback. Computer connections owns framing, and Assembly supplies the runner and serializes patch, note and connection dispatches.

- Stage one uses one 5×8 page. Source rows run top to bottom and destination columns left to right. For zero-based row `r` and column `c`, the Akai pad note is `8 × (4 − r) + c`. The native fixture assigns its source/destination pairs to the top-left two pads, `20` and `21` in hex. Keep the existing bottom-left Route annotation and behavior in the independent one-pad demo. [Akai pad map](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)
- On surface Port 1, the first `90 nn vv` press with nonzero velocity sets the route enabled when its cell is off and disables it when pending or active. Read the current Session snapshot to resolve the pair of logical port IDs. Empty slots and repeated presses while held have no effect. `80 nn vv` or `90 nn 00` releases the gesture. Stage one consumes mapped pad presses as control. Musical keybed notes remain on Port 0 and enter the graph.
- Send `96 nn 00` for off or unassigned, `96 nn 09` for pending, and `96 nn 15` for active. Render solid amber-orange for pending and green for active. Refresh from a single Session snapshot at surface startup, on explicit refresh and after each attempted toggle of a mapped cell. The in-process fixture also refreshes after an inventory or connection revision change. Send the full 40-pad page in visual row-major order, including dark cells. A release, repeat or ignored gesture emits no feedback. Accepted graph cleanup reaches capture before the corresponding LED refresh. [Akai LED palette](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

### Separate connected Patch path

The [Web Monitor destination contract](web-monitor-proposal.md) extends this path for the default Patch host. The two-branch host mode retains its untagged Capture framing and stays silent. Browser lifecycle requests in the Monitor profile apply only to its own virtual receiver.

Keep the one-pad `/demo/midi` runner and wire contract in place. The Patch runner uses `/patch/midi` and the existing note and `apc-surface` object shapes. In the Patch connection, `{"type":"patch-refresh"}` requests another full live snapshot. The host maps it to `surface-ready`, which also performs the initial refresh. The runner keeps the same LF command framing for `surface`, `on` and `off`. The host sends browser `ready` before initial LEDs. A host can request a refreshed snapshot after an in-process simulated port change. Port lifecycle events are supplied by the simulation or platform adapter through the typed update, with no browser command for changing device identity or connection state.

The original connected Patch profile used at most one request in flight, 16 queued and four accepted note rows per response. The [verified two-branch fast-play profile](#interactive-patch-fast-play-proposal) and [default Monitor profile](web-monitor-proposal.md#profile-limits-and-storage) set their own queue and response limits. Keep one local fixture per socket, a maximum of 256 UTF-8 bytes per browser frame, and at most 40 surface rows per page. A refresh carries the inventory block defined below. Use `ok` on success. A valid command rejected by the core ends with `err INVALID`, `err CAPACITY` or `err UNAVAILABLE`. The Patch host forwards the records in order and reports that rejection as `{"type":"error","code":"UNAVAILABLE"}` using the returned code. An ordinary rejection keeps the connection open while the authoritative LED snapshot remains visible. Malformed framing, unexpected runner termination or transport overflow use the existing close-and-cleanup behavior. Drain runner output while disconnect cleanup completes.

A failed authoritative read during `surface-ready` returns only `err CODE`. Stage inventory and LEDs before writing either. After a valid mapped gesture, send accepted destination rows first. If the command failed, still send a full page when the authoritative post-command read succeeds, then the command's `err CODE`. If that read also fails, retain any accepted destination rows and terminate with the error. When only the post-command read fails, use its error code. Publish complete pages only.

Verify the full dark startup page and independently toggle the two mapped pads. Hold a key while disabling one branch and check that only its destination Off precedes the new page. A simulated port disconnect changes its enabled pad to pending while the other remains active. Verified reattach restores green after any owed release is accepted. Rerun the unchanged native proof and connected one-pad demo.

### Read-only browser inventory

The first connected Patch view shows device names and connection state, then each device's named ports with direction, connection state and current row or column. Show unassigned ports explicitly. Assemble the inventory and the LED page from one serialized `gridpico_patch_snapshot`. The browser uses the IDs, names and placements it receives. The payload has no editing commands. CLI `inventory` remains a separate reader of the same typed state.

Use the existing Patch startup and `patch-refresh`. For each successful `surface-ready`, emit this inventory block ahead of the LED page and `ok`. The browser host sends `ready` before the first block and forwards a complete block after validating the successful response. Ordinary note and pad dispatches keep their existing reply shape.

| Runner line | Browser JSON message |
| --- | --- |
| `inventory <revision> <device-count> <port-count>` | `{"type":"patch-inventory-begin","revision":"1","deviceCount":2,"portCount":5}` |
| `device <id> <state> <name-hex>` | `{"type":"patch-device","id":1,"state":"connected","nameHex":"415043"}` |
| `port <id> <device-id> <in-or-out> <state> <eligible> <slot> <name-hex>` | `{"type":"patch-port","id":1,"deviceId":1,"direction":"in","state":"connected","eligible":true,"slot":1,"nameHex":"4B6579626564"}` |
| `inventory-end` | `{"type":"patch-inventory-end"}` |

The block carries the snapshot revision as a decimal string, then device and port records in stable snapshot order. States are `connected`, `disconnected` or `ambiguous`. Runner eligibility is `0` or `1`. Slot is `0` for unassigned, otherwise the one-based row for an input or the one-based column for an output. Encode the name's UTF-8 bytes without its trailing NUL as two uppercase hex digits per byte. The maximum 31-byte name becomes 62 digits. An empty name uses `-` on the runner line and an empty `nameHex` in JSON. Decode and display labels as text.

The two-branch profile uses at most nine inventory frames including the boundaries. The default Monitor profile allows up to 11. Inventory precedes up to 40 LEDs for a refresh. Every frame fits the existing 256 UTF-8 byte limit. Validate counts, known device references and unique IDs within each record kind before replacing the displayed inventory. Discard partial blocks and clear the connection's inventory on disconnect. An explicit refresh after simulated port changes presents new connection state without moving the assigned slots. Verify the reserved panel and LED feedback ports remain visible and unassigned.

### Native simulation access

Assembly may provide a separate native test entry point with an in-process fixture. Inject lifecycle changes through `gridpico_patch_port_update` on the existing Simulation and Graph adapters. Connect a serial byte adapter or pseudo-terminal to the actual CLI. Simulated Diagnostics uses that CLI byte stream through its mocked Web Serial interface. Cross-client tests for APC and CLI use one Session instance for their scenario, with port events, CLI commands and APC actions serialized. The browser runner retains only its existing commands. Lifecycle injection stays in the native test entry point.

## Interactive Patch fast-play proposal

The two-branch fast-play gate was verified and published at `299ec25` after the separate Route-view gate. Assembly selects the opt-in `gridpico_patch_graph_init_browser` profile for the `--two-branch` host mode and uses its module-owned occurrence and release constants. The stage-one native Patch fixture retains two input occurrences and four release credits through `gridpico_patch_graph_init`. The native transpose proof and Route-view profiles remain independent regressions. Reuse the same Graph, Session, fixture and production APC boundaries.

| App resource | Two-branch bound and purpose |
| --- | --- |
| Input and fan-out | 25 input occurrences, including retired or suppressed holds, cover the physical controls. Two branches are enabled independently. Reserve up to 50 release credits, one for each potential accepted destination note. |
| Runner response | Stage at most 50 accepted notes per dispatch, then at most one page marker and 40 LED rows. A single branch disable can emit 25 releases. Ingress cleanup across both branches can emit 50. Keep the retained history at 16 and stage accepted rows independently of eviction. |
| Input backlog | One request in flight and 160 queued. The connected view forwards 25 keys and 54 panel controls (40 pads, eight Track buttons, five Scene buttons and Shift). A press and cancellation of every forwarded control uses at most 158 requests, leaving room for two refresh requests. A conservative compact browser payload for that burst is 7,542 bytes. |
| Transport | Retain the 256-byte frame, 8 KiB socket buffer and existing reader and runner deadlines. A full 50-Off, 40-LED response with a page marker and error consumes under 5 KiB of compact frames including framing allowance. Preserve capture-before-feedback order. |

### Early capture and terminal keybed overflow

Process genuine keyboard, pointer and cancellation events in order. The runner writes and flushes accepted note rows before any LED page and dispatch terminator. The host forwards each validated accepted note row in order as it arrives on a writable socket. It keeps the per-dispatch row limit and does not wait for the LED page or `ok` or `err`. Inventory and LEDs remain atomic complete blocks. On a stalled or lost transport, drain runner cleanup and clear held controls and browser audio locally.

The terminal discriminator is `err CAPACITY` for a validated, in-flight keybed Note On request. The graph quiesces that ingress and Assembly completes source teardown before the runner writes the reply. Include all accepted zero-velocity cleanup releases without duplicating releases already delivered. Flush them before `err CAPACITY` and dispatch no further requests. The host sends the existing `{"type":"error","code":"CAPACITY"}` after the accepted rows, then retires the socket with close code `1013` and reason `Patch keybed capacity exceeded`. It discards queued controls for that fixture and drains runner output. These operations use the existing stdin/stdout and browser frame shapes.

A valid surface `CAPACITY`, keybed Note Off `CAPACITY`, and other ordinary command rejections retain the normal recoverable reply. Forward their accepted rows and any complete authoritative LED page before the error. Route gestures keep their normal meaning. The fixture remains available for further valid commands after a recoverable reply.

Expose the disconnected state and a manual **Reconnect** action for a genuine overflow or loss. One click starts one fresh connection attempt. A runner still retiring can reject that attempt. Leave the action available for a later explicit attempt. The next accepted connection starts a fresh Patch fixture with empty capture and routes initially off. Call for fresh route and key gestures and explicit audio enablement. An ordinary Route pad gesture continues to change only its mapped route.

### Independent fast-play gate

1. In connected Chromium, exercise fast sequential presses and a genuine chord of at least three notes. Enable both branches and hold all 25 keys. Confirm all 50 accepted attacks. Cancel the held controls, including panel release, and check all accepted releases and continued route control. Use destination-aware native capture to confirm branch attribution.
2. Disable one branch with notes held. Check its 25 accepted zero-velocity releases reach the browser before the LED page and the other branch stays active. Delay the remainder of a fixture reply and verify an accepted release is forwarded before the terminator. Verify both-branch cleanup can produce a full 50-row dispatch, independently of the retained 16-row history.
3. Exercise a deliberate 26th input, a forced valid surface `CAPACITY` reply and sustained transport overflow as separate cases. Verify terminal keybed cleanup and error ordering, recoverable surface rejection, visible loss and one fresh manual reconnect. Verify no held note or route replays. Rerun the small native Patch, native transpose and Route-view proofs.

This gate covers two instrument branches. The [Web Monitor destination contract](web-monitor-proposal.md) adds the third destination in the Monitor app profile.

## Shared runtime colors

Session/control implements `gridpico_session_read_device_color` and `gridpico_session_set_device_color` with the two callback signatures in [patchbay_controls.h](patchbay_controls.h). CLI and APC receive these callbacks through Assembly and use the same logical `DeviceId`. Session owns one nullable RGB preference per known device, including a disconnected device. A known unset device returns `GRIDPICO_OK` with `is_set=false` and zeroed RGB. `#000000` is a set value. A zero or unknown ID returns `GRIDPICO_INVALID`. Setting the same value succeeds without advancing the color revision. A changed value advances the Session-owned color revision once. Stage-one inventory and graph revisions retain their original meanings.

Use the [APC color layer and swatches](patchbay.md#follow-on-apc-color-gesture). Read the preference on entry and on refresh of that layer. After an APC edit, render the committed value, and restore the live route snapshot when leaving the layer. `inventory` reads colors and Patch state within one serialized command. Runtime preferences survive reconnect of the same logical device within the run. Projects persistence is a later increment.

### LED page identity for color-capable Patch

The production APC frontend announces each complete page via the optional `gridpico_patch_led_page_fn` before its 40 existing surface messages. `GRIDPICO_PATCH_LED_LAYER_ROUTE` carries device ID `0`. `GRIDPICO_PATCH_LED_LAYER_DEVICE_COLOR` carries the selected logical device ID. The physical surface output still uses only the LED MIDI triplets.

| Runner line before the 40 `surface` lines | Browser message |
| --- | --- |
| `led-page route` | `{"type":"patch-led-page","layer":"route"}` |
| `led-page device-color <device-id>` | `{"type":"patch-led-page","layer":"device-color","deviceId":2}` |

On a color-capable connection the page header applies only to the immediately following 40 LED rows, in the existing visual row-major order. Forward it after any capture and inventory rows. There is at most one header per dispatch. Commit the presentation after the full page arrives. Render route and color pages separately. A displayed cached route page during the color layer is the last received route page. The color preview is the committed preference's palette projection. Shift state and palette bytes do not select the presentation. In a route-only connection every complete untagged page is a route page. The one-pad connection keeps its current framing.

## Read-only Diagnostics commands

Support `status` and `logs` in the same CLI as `inventory` and `color`. `status` uses the current Session Patch snapshot. `logs` uses one bounded, read-only runtime log supplied by Assembly. The log records committed port-state transitions observed from Graph and failed route or note operations as reported by the authoritative command or graph boundary. No additional producer is required for stage one of Diagnostics.

| Command | Reply records before `OK` or `ERR` |
| --- | --- |
| `status` | `STATUS <revision> <devices> <ports> <active> <pending>` from one Patch snapshot. Counts use decimal and active/pending refer to the current single page. |
| `logs` | Up to 16 `LOG <sequence> PORT <port-id> <state>` or `LOG <sequence> ROUTE_ERROR <destination-port-id> <result>` or `LOG <sequence> NOTE_ERROR <source-port-id> <result>` records, oldest retained first. Sequence starts at 1 for each run. Results use `INVALID`, `CAPACITY` or `UNAVAILABLE`. States use the inventory spellings. |

The Assembly log evicts its oldest entry when full. Log writes and reads run on the serialized context and the read callback copies into caller-owned storage. An empty log yields `OK` alone. An unwired log provider yields `ERR UNAVAILABLE`. The CLI owns flow, echo and formatting. It limits command text to 63 ASCII bytes before the terminator and uses the existing CRLF reply records.

Standalone Diagnostics is a Computer connections Web Serial transcript. Use the user-initiated port chooser with 115200 baud, 8 data bits, no parity, 1 stop bit and no flow control. Send one LF-terminated command at a time through serialized writes. Decode incoming UTF-8 incrementally, render it as text and retain at most 16,384 decoded UTF-16 code units. Diagnostics displays the same terminal stream without interpreting command records. Actual CDC control-line behavior and access from the mounted offline page are target acceptance checks. [Chrome Web Serial](https://developer.chrome.com/docs/capabilities/serial/)

Verify `status` counts and log order and eviction through the CLI. Feed the same command and reply stream through the simulated browser serial adapter and a terminal. Use the mounted-drive hardware check to validate access to the Pico's real CDC port.

Assembly can distribute the [standalone offline Diagnostics file](../connections/diagnostics/index.html) before CDC integration. Direct-file and mocked serial checks cover the browser artifact. Mounted read-only `GRIDPICO` access to the Pico's real CDC CLI, including the port chooser and reconnect, remains its own hardware and browser acceptance gate.
