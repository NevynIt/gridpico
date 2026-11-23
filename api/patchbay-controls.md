# Production Patch controls and Diagnostics

**Owner** Architect. [Public C11 declarations](patchbay_controls.h) · [Stage-one fixture](patchbay.md#stage-one-native-patch-fixture).

## Production Patch surface

Use the production APC frontend with `gridpico_patch_read_snapshot_fn`, `gridpico_patch_set_route_fn` and `gridpico_apc_surface_feedback_fn`. Session and Graph own route meaning and delivery. APC owns pad gesture state, projection of the snapshot to LEDs and page display. Web APC sends surface bytes and renders returned feedback. Computer connections owns framing, and Assembly supplies the runner and serializes patch, note and connection dispatches.

- Stage one uses one 5×8 page. Source rows run top to bottom and destination columns left to right. For zero-based row `r` and column `c`, the Akai pad note is `8 × (4 − r) + c`. The native fixture assigns its source/destination pairs to the top-left two pads, `20` and `21` in hex. Keep the existing bottom-left Route annotation and behavior in the independent one-pad demo. [Akai pad map](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)
- On surface Port 1, the first `90 nn vv` press with nonzero velocity sets the route enabled when its cell is off and disables it when pending or active. Read the current Session snapshot to resolve the pair of logical port IDs. Empty slots and repeated presses while held have no effect. `80 nn vv` or `90 nn 00` releases the gesture. Stage one consumes mapped pad presses as control. Musical keybed notes remain on Port 0 and enter the graph.
- Send `96 nn 00` for off or unassigned, `96 nn 09` for pending, and `96 nn 15` for active. Render solid amber-orange for pending and green for active. Refresh from a single Session snapshot at surface startup, on explicit refresh and after each attempted toggle of a mapped cell. The in-process fixture also refreshes after an inventory or connection revision change. Send the full 40-pad page in visual row-major order, including dark cells. A release, repeat or ignored gesture emits no feedback. Accepted graph cleanup reaches capture before the corresponding LED refresh. [Akai LED palette](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

### Separate connected Patch path

Keep the one-pad `/demo/midi` runner and wire contract in place. The Patch runner uses `/patch/midi` and the existing note and `apc-surface` object shapes. In the Patch connection, `{"type":"patch-refresh"}` requests another full live snapshot. The host maps it to `surface-ready`, which also performs the initial refresh. The runner keeps the same LF command framing for `surface`, `on` and `off`. The host sends browser `ready` before initial LEDs. A host can request a refreshed snapshot after an in-process simulated port change. Port lifecycle events are supplied by the simulation or platform adapter through the typed update, with no browser command for changing device identity or connection state.

Keep at most one request in flight and 16 queued per connection, one local fixture per socket, and a maximum of 256 UTF-8 bytes per browser frame. One runner response can contain at most four accepted note rows, then 40 surface rows, then its terminator. Use `ok` on success. A valid command rejected by the core ends with `err INVALID`, `err CAPACITY` or `err UNAVAILABLE`. The Patch host forwards the records in order and reports that rejection as `{"type":"error","code":"UNAVAILABLE"}` using the returned code. This keeps the Patch connection open while the authoritative LED snapshot remains visible. Malformed framing, unexpected runner termination or transport overflow use the existing close-and-cleanup behavior. Drain runner output while disconnect cleanup completes.

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

There are at most nine inventory frames including the boundaries, ahead of at most 40 LEDs for a refresh. Every frame fits the existing 256 UTF-8 byte limit. Validate counts, known device references and unique IDs within each record kind before replacing the displayed inventory. Discard partial blocks and clear the connection's inventory on disconnect. An explicit refresh after simulated port changes presents new connection state without moving the assigned slots. Verify the reserved panel and LED feedback ports remain visible and unassigned.

## Shared runtime colors

Session/control implements `gridpico_session_read_device_color` and `gridpico_session_set_device_color` with the two callback signatures in [patchbay_controls.h](patchbay_controls.h). CLI and APC receive these callbacks through Assembly and use the same logical `DeviceId`. Session owns one nullable RGB preference per known device, including a disconnected device. A known unset device returns `GRIDPICO_OK` with `is_set=false` and zeroed RGB. `#000000` is a set value. A zero or unknown ID returns `GRIDPICO_INVALID`. Setting the same value succeeds without advancing the color revision. A changed value advances the Session-owned color revision once. Stage-one inventory and graph revisions retain their original meanings.

Use the [APC color layer and swatches](patchbay.md#follow-on-apc-color-gesture). Read the preference on entry and on refresh of that layer. After an APC edit, render the committed value, and restore the live route snapshot when leaving the layer. `inventory` reads colors and Patch state within one serialized command. Runtime preferences survive reconnect of the same logical device within the run. Projects persistence is a later increment.

## Read-only Diagnostics commands

Support `status` and `logs` in the same CLI as `inventory` and `color`. `status` uses the current Session Patch snapshot. `logs` uses one bounded, read-only runtime log supplied by Assembly. The log records committed port-state transitions observed from Graph and failed route or note operations as reported by the authoritative command or graph boundary. No additional producer is required for stage one of Diagnostics.

| Command | Reply records before `OK` or `ERR` |
| --- | --- |
| `status` | `STATUS <revision> <devices> <ports> <active> <pending>` from one Patch snapshot. Counts use decimal and active/pending refer to the current single page. |
| `logs` | Up to 16 `LOG <sequence> PORT <port-id> <state>` or `LOG <sequence> ROUTE_ERROR <destination-port-id> <result>` or `LOG <sequence> NOTE_ERROR <source-port-id> <result>` records, oldest retained first. Sequence starts at 1 for each run. Results use `INVALID`, `CAPACITY` or `UNAVAILABLE`. States use the inventory spellings. |

The Assembly log evicts its oldest entry when full. Log writes and reads run on the serialized context and the read callback copies into caller-owned storage. An empty log yields `OK` alone. An unwired log provider yields `ERR UNAVAILABLE`. The CLI owns flow, echo and formatting. It limits command text to 63 ASCII bytes before the terminator and uses the existing CRLF reply records.

Standalone Diagnostics is a Computer connections Web Serial transcript. Use the user-initiated port chooser with 115200 baud, 8 data bits, no parity, 1 stop bit and no flow control. Send one LF-terminated command at a time through serialized writes. Decode incoming UTF-8 incrementally, render it as text and retain at most 16,384 decoded UTF-16 code units. Diagnostics displays the same terminal stream without interpreting command records. Actual CDC control-line behavior and access from the mounted offline page are target acceptance checks. [Chrome Web Serial](https://developer.chrome.com/docs/capabilities/serial/)

Verify `status` counts and log order and eviction through the CLI. Feed the same command and reply stream through the simulated browser serial adapter and a terminal. Use the mounted-drive hardware check to validate access to the Pico's real CDC port.

Assembly can distribute the standalone offline Diagnostics file before CDC integration. Direct-file and mocked serial checks cover the browser artifact. Mounted read-only `GRIDPICO` access to the Pico's real CDC CLI, including the port chooser and reconnect, remains its own hardware and browser acceptance gate.
