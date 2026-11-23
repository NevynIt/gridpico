# Patchbay, inventory and Diagnostics

**Owner** Architect. **Status** Stage-one contract and separate follow-on boundaries. The [architecture](../ARCHITECTURE.md) defines the module boundaries. The [one-pad demo](README.md#one-pad-apc-route-toggle) remains the baseline regression.

## Shared state and commands

| Value | Initial contract |
| --- | --- |
| Device | A logical `DeviceId`, display name, connection state and optional RGB color preference. Several ports can belong to one device. Names can repeat. The ID remains stable across an unambiguous reconnect within the running application. |
| MIDI port | A unique logical `PortId`, parent `DeviceId`, display name, direction, connection state and Patch eligibility. Input and output are from the graph's point of view. Eligible inputs occupy source rows and eligible outputs occupy destination columns. Dedicated APC panel ports can appear in inventory with no Patch assignment. A bidirectional device exposes separate input and output entries. Current adapter addresses and connection generations stay in the delivery boundary. |
| Patch snapshot | A coherent revision with device and port inventory, source-row and destination-column assignments, and route states. Assignments contain logical port IDs or an empty slot. Pages have five source rows and eight destination columns. Page selection belongs to the frontend. The shared projection assigns stable slots by direction, leaves existing slots in place across disconnects, and reports the same mapping to CLI and APC. |
| Route | A source port and destination port identify one desired branch. The visible states are off, pending and active. Pending means the branch is enabled and currently unavailable. Active means the branch can deliver. One source may have several enabled branches within the target profile. |

- **Read inventory and Patch state** through Session/control. Each port appears under its parent device, even if disconnected. Report its row or column and page if assigned. A missing or ambiguous binding stays unavailable until identified. Routes and colors keep their logical IDs across an unambiguous reconnect.
- **Set one route enabled or disabled** by source and destination port ID. Session/control validates endpoint directions and profile capacity. The graph owns desired and active route state. A new Patch branch can pass notes through unchanged. On disable, the graph retires that branch's sounding contributions with the existing cleanup rules before committing. Other branches keep their notes and settings. Re-enable routes fresh presses. A rejected change preserves desired configuration.
- **Read or set device color** by `DeviceId` through Session/control. An unset preference uses the frontend's default. Store one RGB value per recognized device. A successful update publishes the same value to APC and CLI. APC presents the color separately from the route's active, pending or off indication. APC may translate RGB to a supported display color. The first slice keeps preferences for the current run, including an unambiguous reconnect. Projects can add persistence later through the shared device state.

The MIDI graph owns inventory, stable port ordering and routing. Session/control owns the shared Patch projection and runtime device color preference. APC owns pages, gestures, palette presentation and feedback. Neither port order nor route state is derived from a display name.

## Stage one. Native Patch fixture

[patchbay.h](patchbay.h) defines the C11 callback and value types for one serial execution context. Reuse the existing note, ingress and delivery value types. Calls borrow input values for their duration and write caller-owned snapshots. The fixture registers up to two devices and five directional ports. Each name occupies up to 31 UTF-8 bytes plus a NUL terminator. Nonzero device IDs are unique among devices. Nonzero port IDs are unique among ports. Use registration order for the list and page. Each eligible input takes the next source slot and each eligible output the next destination slot. Empty slots use ID `0`. No slot moves when its port disconnects.

| Device ID and name | Port ID, name and direction | Position |
| --- | --- | --- |
| `1` APC Key 25 mk2 | `1` Keybed, input | Source row 1 |
| `1` APC Key 25 mk2 | `4` Panel, input and `5` LED feedback, output | Inventory only |
| `2` Simulated dual-port instrument | `2` MIDI A and `3` MIDI B, outputs | Destination columns 1 and 2 |

The fixture has one five-by-eight page with zero-based C array indices and one-based display positions. Start each port at connection generation `1`. Both routes start off. The graph admits at most two enabled branches and two outstanding source note occurrences. Reserve four output release credits so each occurrence can sound at both destinations. An ordinary dispatch may produce two deliveries, and cleanup may produce up to four. The simulator keeps the last 16 accepted deliveries in a single ordered capture. Eviction never consumes release capacity. These constants describe this fixture.

| Owner | Public boundary |
| --- | --- |
| MIDI graph | Initialize from the bounded inventory supplied by Assembly. Own connection state, stable inventory order, enabled branches, note accounting and snapshot revision. Expose `gridpico_patch_update_port_fn`, `gridpico_patch_set_route_fn`, `gridpico_patch_read_graph_fn` and the existing note submission callback shape. |
| Session/control | Expose `gridpico_patch_read_snapshot_fn` and `gridpico_patch_set_route_fn` to callers. Validate the requested logical pair against inventory, delegate the mutation to the graph, and project rows, columns and cell states from one graph read. Copy the graph revision into the stage-one Patch snapshot. |
| Simulation backend | Accept deliveries for known connected destination generations using the existing delivery callback shape, retain the ordered capture and copy its oldest retained rows first via `gridpico_patch_read_capture_fn`. Expose connection events with `gridpico_patch_port_update`. |
| Application Assembly | Own the named fixture, C11 build target and demo wiring. Serialize endpoint changes, commands, reads, note submission and capture. Keep the independent native proof and browser one-pad demo on their existing entry points and profiles. |

The graph snapshot lists only enabled branches, each pending or active. Session projects absent branches as off. Reject unknown, wrong-direction or ineligible port IDs with `GRIDPICO_INVALID`. Reject capacity overflow before mutation. Enabling an already enabled branch or disabling an absent branch succeeds without changing state. Publish one new graph revision for each observable change to inventory, connection or route state. A rejected configuration change preserves the desired setting. A delivery fault can still change observable availability.

An aggregate device is connected if any of its ports is connected, ambiguous if none is connected and any is ambiguous, and disconnected otherwise. A port disconnect names the current generation. A stale update returns `GRIDPICO_UNAVAILABLE`. For fixture output connection changes, Assembly updates the simulated destination before notifying the graph, so the new generation can accept cleanup during reattach. The snapshot carries connection states and logical IDs. Delivery carries the generation.

Stage one routes Note On and Note Off unchanged. Reserve accounting before fan-out. Record each destination only after it accepts delivery. Disabling a connected branch releases only that branch's accepted output notes at their original keys, with velocity `0`, then removes the branch. Retain suppressed or retired source occurrences until their releases arrive. On disconnect, an enabled branch becomes pending and retains any cleanup due on that logical endpoint. An unambiguous reconnect uses a newer nonzero generation and reconciles its owed releases before the branch becomes active. An event for an old generation cannot drive the new connection. An ambiguous port remains unavailable. If required cleanup fails, preserve the desired setting, block affected output and retain the credit. Other destination branches remain usable.

Match source occurrences FIFO by connection, channel and key. Coalesce overlapping sounding output obligations by destination connection, channel and key. Retire only the affected contributors on branch cleanup. The last contributor releases the sounding output, using the source identity from the accepted Note On. At a verified reconnect only the delivery's destination generation changes.

After aggregate preflight, try active destinations independently. Return `GRIDPICO_UNAVAILABLE` if an attempted delivery fails and retain accepted effects and release credits. Skipping a branch already pending is normal. Admission overflow returns `GRIDPICO_CAPACITY` before emitting new Note Ons and quiesces the offending input, cleaning up only its contributions. Resume on a fresh attachment. Disabling a disconnected destination with retained debt returns `GRIDPICO_UNAVAILABLE` and preserves the pending branch. With no debt, a disconnected branch can be disabled. A `CONNECTED` update for the same still-connected generation retries pending cleanup for that port and otherwise leaves state intact. Require explicit disconnect before a newer attachment replaces a live generation.

## Follow-on. APC color gesture

Shift alone arms device selection and suppresses route clicks while held. Press a Scene Launch button to select the device assigned to that source row, or a Clip Stop button to select the device assigned to that destination column. Use top-to-bottom scene order and left-to-right Clip Stop order. An empty slot keeps the prior selection and produces no feedback. A valid selection enters the color layer. While Shift remains held, pads `00–03` select the four colors below and pad `08` reads the selected device's current preference. Use one full 40-pad page with the four swatches, preview at `08` and all other pads dark. Selecting a color submits the shared device command and rereads the committed preference, even after a rejected setter. Shift release restores the current route page when leaving the color layer. [Akai panel guide](https://cdn.inmusicbrands.com/akai/apc-key-25-mkii/APC%20Key%2025%20mk2%20-%20User%20Guide%20-%20v1.2.pdf) · [Akai palette](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

| Swatch | Stored RGB | APC palette byte |
| --- | --- | --- |
| `00` APC red | `#FF0000` | `05` |
| `01` MicroFreak silver | `#7F7F7F` | `02` |
| `02` P-95 brown | `#591D00` | `0A` |
| `03` Circuit dark gray | `#1E1E1E` | `01` |

APC displays an unset or black preference as dark. For another CLI RGB value outside the preset list, choose the nearest swatch by squared RGB distance, breaking ties by swatch order. Keep the exact stored RGB in shared state. The swatch names describe fixture colors. All recognized devices can use any of them. Color updates never set route LEDs or change routing.

## Serial CLI and standalone Diagnostics

Start with `inventory`, `color get <device-id>` and `color set <device-id> #RRGGBB`. The inventory response includes device/port names, connection state and shared row/column assignments. Use the identifiers printed by inventory to address devices. The CLI frontend owns command text, human-readable output and translation to typed Session commands. The shared state remains authoritative for both frontends.

Computer connections owns the serial byte stream and the standalone Diagnostics Web Serial client. Diagnostics displays the same CLI terminal transcript and connection status. Both an ordinary serial terminal and Diagnostics use the same command service. Keep request lines and output bounded and serialize command processing. The CLI library may edit and tokenize terminal input. Diagnostics can submit complete command lines and display their replies. Initial Diagnostics uses terminal text and its scrollback for logs.

### Follow-on serial formatting

Send one command line at a time. Accept CR, LF or CRLF as one terminator. Diagnostics sends LF. IDs in command text and reply records are decimal. Accept six hexadecimal digits after `#` and render the stored value in uppercase. Command records are UTF-8 and end in CRLF. Quote names as JSON strings so a name cannot introduce a line break or terminal control. End each reply with `OK` or `ERR INVALID`, `ERR CAPACITY` or `ERR UNAVAILABLE`. Terminal prompts and line editing are presentation. Keep unrelated log records outside the reply.

| Command | Ordered result records before the terminator |
| --- | --- |
| `inventory` | `DEVICE <id> <state> <color> "<name>"`, followed by that device's `PORT <id> <device-id> <in\|out> <state> <assignment> "<name>"` records. Use stable inventory order within each group. |
| `color get <device-id>` | `COLOR <device-id> <color>` |
| `color set <device-id> #RRGGBB` | `COLOR <device-id> <color>` with the committed value |

State is `connected`, `disconnected` or `ambiguous`. Color is `#RRGGBB` or `unset`. Assignment is `row:1:<position>`, `col:1:<position>` or `-` for no slot. The middle field is the one-based Patch page. Diagnostics displays the same records as the terminal.

Application Assembly packages Web APC and Diagnostics as separate offline entry points on the read-only `GRIDPICO` image. Diagnostics uses a user-initiated serial port chooser and reports access or availability errors within the page. Direct opening of the mounted file and access to the Pico's CDC CLI are a target browser/OS acceptance check. [Chrome Web Serial](https://developer.chrome.com/docs/capabilities/serial/) · [Secure Contexts](https://w3c.github.io/webappsec-secure-contexts/#is-origin-trustworthy)

Stage one has no dependency on the color gesture, serial client, Projects storage or mounted-drive verification.

See [Production Patch controls and Diagnostics](patchbay-controls.md) for the typed color and log callbacks, APC feedback wire bounds and `status` and `logs` commands.

### CLI library choice

Use upstream **funbiscuit/embedded-cli v0.1.4** as the terminal adapter, pinned at `ffa8014c6230ba809c6a655ccc611a6c4689113f`. The released single C header has SHA-256 `7b6db7fdd691790816f279f850be977433b01903f52f6dd53c0b0569718e6fe2`. It includes the MIT notice, supports caller-provided storage and takes a byte stream. It adds cursor handling and a Linux example over the older snapshot. Configure bounded buffers and keep the library inside the CLI frontend. The released header passed a local strict C11 syntax check. Confirm the configured RAM budget and terminal behavior in the native fixture before target acceptance. [Upstream release](https://github.com/funbiscuit/embedded-cli/releases/tag/v0.1.4) · [Pinned upstream usage](https://github.com/funbiscuit/embedded-cli/blob/ffa8014c6230ba809c6a655ccc611a6c4689113f/README.md)

The rpico-bridge reference vendors the TinyUSB 0.21.0 snapshot. Its header matched that snapshot byte for byte, with SHA-256 `e89c8b219f78d7d6b2a55558c1004316f56ded76690f7a68285b5f5160b4a2e3`. Compared with the upstream v0.1.2 release header, that snapshot comments out dynamic allocation and free. Use caller-provided storage with v0.1.4 and retain its license notice. Gridpico owns its own commands, serial integration and domain logic. [rpico-bridge provenance](https://github.com/NevynIt/rpico-bridge/blob/36c61af1e03c799b60c250dd41263a174b5ee5f4/firmware/third_party/embedded-cli/README.md) · [TinyUSB snapshot](https://github.com/hathach/tinyusb/blob/0.21.0/lib/embedded-cli/embedded_cli.h)

## Runnable gates

1. **Production core on simulation.** Register a named source and a named two-output device. Verify port IDs, directions, connection status and distinct matrix assignments. Apply two branches through Session/control. Each destination captures its own Note On and Note Off. Disable one branch while a note is held and verify cleanup on that branch. A separate disconnect case leaves the affected enabled branch pending and the other branch active. Restore an unambiguous connection, preserve IDs and mapping, and confirm any release owed to that endpoint is reconciled before the branch is active again. Rerun the independent four-case native proof and one-pad demo.
2. **Shared control on simulation.** Have a serial terminal and Diagnostics read the same fixture inventory through the CLI service. Set a device color from the CLI and read it in APC. Change it in APC and read it via CLI, with both ports showing the common preference and route feedback retaining its meaning. Exercise offline Diagnostics against a simulated serial byte stream. A reconnect within the run retains the preference.
3. **Mounted drive on hardware.** Open the offline Diagnostics page directly from read-only `GRIDPICO` in a supported browser on the intended OS. Select Pico CDC through the browser chooser, run the same commands and confirm close/reopen behavior. Record the browser, OS and device results separately from simulated evidence.
