# Gridpico architecture

**Owner** Architect. **Product authority** Human, through Strategist.
[Product brief](PRODUCT_BRIEF.md) · [Public contracts](api/README.md) · [Roadmap](ROADMAP.md) · [Diagrams](docs/diagrams/README.md)

## 1. Product and design intent

Gridpico is a portable, controller-independent MIDI workstation kernel. It connects and enhances existing MIDI devices through routing, transformations and, later, Circuit-Tracks-inspired sequencing. The initial instrument set is Yamaha P-95, Arturia MicroFreak, Akai APC Key 25 mk2 and Novation Circuit Tracks, with an optional computer. The piano's physical MIDI connection is supplied through a suitable interface; the kernel sees logical MIDI endpoints.

RP2040 uses native USB to a computer and PIO USB to host instruments through a hub. Linux and Windows use OS MIDI services. Raspberry Pi Zero runs the Linux ARM application.

The portable kernel, platform backends and firmware handle MIDI. Tracks target MIDI destinations. Browser Monitor and standalone instrument peers generate sound at the receiving end.

CodeZero/GridZero, miniMIDI and rpico-bridge are design references. Gridpico defines its own code, configuration, wire and storage contracts. [Sources](docs/SOURCES.md)

## 2. Architectural rules

1. One production kernel runs across supported targets. Simulation substitutes services, not musical logic.
2. Frontends operate a common command/query/state API. The kernel never depends on APC coordinates, piano gestures, CLI syntax, browser widgets or LED layouts.
3. Live playing and sequencer playback share the MIDI graph, processors and output safety.
4. Musical state belongs to its domain module. Session/control coordinates it; frontends hold only presentation and interaction state.
5. Platform capabilities are explicit. An unsupported feature is reported, not silently emulated or ignored.
6. Public contracts live under `api/` and belong to Architect. A module owner owns implementation, not another module's contract.
7. Timing-critical work is bounded. Browser rendering, disk reads, logging and persistent saves must not become musical timing sources.
8. Hardware/runtime claims require evidence for the actual target; a simulated or cross-compiled pass is not a physical-device pass.

## 3. The kernel: six cooperating modules

| Module / implementation path | Owns | Does not own |
| --- | --- | --- |
| **Session/control** `core/session/` | Typed commands, queries, dispatch, coherent state snapshots/deltas and project-level coordination | Copies of every module's state; controller protocols; operating-system services |
| **MIDI graph** `core/midi/` | Logical endpoint registry, identity binding, desired and active routes, per-message fan-out and event origin | USB enumeration; sequencing data; UI pages |
| **Processors** `core/processors/` | Bounded composable MIDI transformations and processor-local state | Global routes, USB/OS I/O or surface behavior |
| **Musical time** `core/time/` | Musical position, tempo, transport, clock-source policy and scheduling/cancellation of timed events | Wall-clock/driver implementation, browser refresh or pattern content |
| **Sequencer** `core/sequencer/` | Tracks, recording, steps, patterns, chains, scenes, automation and playback semantics | Sound generation, direct device output, APC gestures |
| **Projects** `core/projects/` | Versioned project serialization, validation, load/save coordination and recovery | Musical policy or physical filesystem/flash drivers |

The Sequencer boundary is retained from the outset; its implementation can be deferred. This does not require dummy production classes, allocated pattern banks or a speculative complete sequencing API in the initial release.

### Session/control and state ownership

Commands express intent, such as changing a route or processor setting. Queries and state notifications describe musical state. In-process APC and CLI frontends use the same typed command boundary, which also accommodates a future piano frontend. Browser peers reach those frontends through APC MIDI and serial CLI respectively.

Session/control validates cross-domain operations and dispatches to the owning module. Each domain remains authoritative for its own state. A coherent snapshot/revision mechanism lets frontends refresh without maintaining independent musical truth. State notifications must not be reinterpreted as new commands and echoed back.

Application Assembly supplies the execution context. Platform callbacks submit bounded events; they must not mutate musical state concurrently through undocumented side channels. Thread/core allocation is a target decision, not a public-kernel dependency.

### MIDI graph and processors

The basic route is **source selection → processor chain → destination(s)**. Fan-out can have independent branches and processor settings. The patch grid and advanced editing operate on this one model, not separate routing engines.

Bindings use stable logical IDs and user aliases. Platform adapters report device, interface, cable and direction independently of MIDI channel, display name and attachment generation. Session keeps desired routes and placements across detach. Platform adapters discard stale queued traffic on detach and admit fresh messages on a verified generation. Missing or ambiguous identity leaves routes Pending. Aliases change labels only.

The production registry starts from an Assembly-validated configuration. Runtime registration and retirement of discovered ports is the direction for dynamic Patchbay and still needs target qualification. The Linux USB selector supports verified input only. An output selector reserves its logical port and stays Pending. Operators can configure an explicit numeric ALSA output. [Verified USB replacement](api/routing.md#verified-usb-replacement)

Represent cable/port, MIDI channel and device identity separately. USB packet framing belongs in adapters. The core event contract must preserve ordinary MIDI, realtime messages and bounded/streamed SysEx without assuming every message is a four-byte USB packet.

Live Managed routing evaluates each received note against the currently Active branches and current transforms. Note Off follows the same rule as Note On. Route edits and disconnects generate no MIDI. [Current-message routing](api/local-router.md#current-message-managed-routing) defines admission, overload and the implications of edits while notes sound.

Detect or reject graph-local feedback cycles. External applications can create loops outside the graph's visibility; do not claim to detect those universally. PC loopback is explicit routing, never automatic all-to-all echo.

Processors include filters, transpose, key/velocity splits, channel and controller mapping, curves, relative/absolute conversion and bounded stateful operations. Extensibility is a goal; unrestricted scripting or unbounded generated traffic is not a requirement. The precise extension mechanism belongs to Architect after resource discussion.

### Time and future sequencing

The platform supplies monotonic time and wakeups. Musical time owns the musical schedule and maps the chosen internal or external clock to transport and position. The [musical time contract](api/README.md#musical-time) defines transport boundaries when scheduling is present. Timing budgets and clock-loss thresholds belong to the verified target profile.

Reserve these concepts now:

- Stable track/pattern/route identifiers, independent of displayed pads and pages.
- Sequencer playback as a normal graph source, and explicit track recording inputs.
- Event-origin information so playback is not accidentally recorded again.
- Per-target reported capacities rather than UI assumptions about unlimited tracks or events.
- Project sections for sequence data that can be introduced without changing the frontend boundary.

Sequence capacity and playback behavior belong to the supported engine profile. Tracks store MIDI sequencing and performance state. The controller layout places no requirement on engine allocation. [Sequencer workflows](USE_CASES.md#deferred-sequencing-on-two-surfaces)

### Projects and live configuration

Keep draft editing, live Apply and persistent Save distinct. Validate a whole change before activating it; a rejected change leaves the previous valid state in use. Scheduling cancellation and invalidation of stale transport work participate in state transitions.

Persist desired device bindings, routing/processing configuration and, when implemented, musical projects. Do not persist the playhead or every knob movement. Physical storage adapters provide recoverable writes and communicate any required interruption. A frontend must present an interruption honestly; uninterrupted flash saves are not assumed.

The computer-visible read-only filesystem is **not** this writable project store.

## 4. Interfaces: five module owners

| Module / path | Responsibility |
| --- | --- |
| **APC frontend** `frontends/apc/` | Decode APC MIDI, interpret gestures/pages/modifiers, call the kernel API, and render state into incremental APC feedback plus full reconnect refreshes |
| **Piano frontend** `frontends/piano/` | Later: interpret 88-key/pedal control gestures and render feedback through an abstract RGB strip; distinguish control consumption from musical forwarding |
| **CLI frontend** `frontends/cli/` | Translate text into the common commands and present queries/state; no musical logic duplicated in the parser |
| **Web APC simulator** `web/apc/` | Emulate APC controls, keybed and received LED feedback. The separate Route and Patch demos also present Capture and their profile-specific Monitor controls. |
| **Computer connections** `connections/` | Production web home, Diagnostics and Routing presentation, shared browser MIDI adapter, local serial, WebSocket admission, framing and connection lifecycle |

Inside the APC frontend, keep protocol decoding/encoding separate from view/gesture interpretation even though one owner initially owns both. Other frontends neither impersonate the APC nor import its view implementation.

Linux on M5 and Pi and the RP2040 Pico use one shared production frontend for the editable APC Patchbay. Physical and simulated Panel MIDI enters that frontend through platform MIDI or Computer connections. The shared frontend interprets gestures, owns each surface's controller state, submits typed commands and produces APC LED orders from Session readback. Assembly keeps a frontend instance and current Panel/LED owner for each surface. Surfaces on one runtime share its authoritative Session. Keybed MIDI enters the Graph through musical ingress. Web APC emulates the hardware controls and lamps and manages its connections. The existing passive viewport has its own qualification. [APC viewport](api/routing.md#apc-viewport) · [Editable Patchbay](api/routing.md#editable-physical-apc-patchbay)

Independently running components exchange MIDI or serial CLI bytes. The CLI frontend uses one typed command registry for dispatch and help on Pico and Pi, with each runtime advertising only the forms its providers support. Serial authority follows the selected target. The Pi CLI controls the Pi router. The Pico CLI exposes readback and cooperative recovery, with typed Routing and Save unavailable. Registration, route state and accepted-delivery accounting remain inside the production runtime. [Shared terminal contract](api/pico-target.md#first-shared-cli-on-pico-and-pi)

Frontends own the pairing of locally consumed gesture edges. Ordinary musical messages follow current dispatch. RGB-strip drawing describes logical feedback; the physical strip driver is a platform service.

### Web landing page and browser MIDI

Assembly owns `scripts/web-ui.sh` as the production web entry point. Connections serves the tools from one release bundle. The offline home opens self-contained HTML tools, with hardware access conditional on the browser, selected device and target. [Browser entry points](api/browser-midi.md#entry-points-and-packaging)

Connections shares one in-page browser MIDI layer for provider and port selection, send, receive and binding lifecycle. Web MIDI can select ordinary OS ports, including OS-exposed RTP-MIDI endpoints when the browser grants access. The WebSocket provider binds directional ports to the production router over the existing binary MIDI wire. This shared layer is a code boundary inside the page. Diagnostics opens its serial CLI stream independently. [Browser MIDI and landing page contract](api/browser-midi.md)

Routing shares the Diagnostics terminal implementation. It displays named directional ports and occupancy separately from Off, Pending and Active. Route edits use the selected router's typed serial CLI and a fresh authoritative readback. A connection or transcript alone establishes no edit authority. [Routing and serial](api/routing.md#routing-through-the-local-cli)

### Browser Monitor

Diagnostics hosts Monitor, MIDI Log and routed LED replay. Monitor and MIDI Log bind independently to selectable MIDI inputs and consume the bytes delivered to their own port. Monitor owns its local audio, channel settings and bounded voices, which start after explicit enablement. [Browser roles](api/browser-midi.md#production-startup-and-page-roles)

Track voices by destination, MIDI channel and key so two channels using the same key can sound and release independently. Muting a channel releases its voices. Disabling audio or losing the connection releases all browser voices. That voice behavior is local to the audio receiver. The Monitor uses the existing routed MIDI delivery path and runs independently of its timing and acceptance.

### Optional instrument peers

Sound-generating peers open ordinary MIDI inputs and own their synthesis and audio output. The FluidSynth wrapper owns its engine process and SoundFont. Browser FM owns its built-in voice, Web Audio and voice cleanup. Both run independently of the router and firmware. [Instrument peers](api/instrument-peers.md)

An optional hosted Web Synth uses an explicitly published receive-only WSS MIDI output. The external synth owns its interface and engine and requires separately reviewed origin, enrollment, corresponding source and notices for distribution. [Web Synth peer contract](api/instrument-peers.md#optional-hosted-web-synth)

### Patch and Circuit views

Patchbay presents five source rows and eight destination columns with navigation for additional endpoints. The shared inventory maps logical ports to the viewport. A route may fan out to multiple destinations. The frontend renders route state and shares supported device color preferences with the CLI. [Patch controls](api/patchbay-controls.md)

Changing views preserves running routes and transport. Future physical sequencer controls and encoder mappings for external instruments belong to the frontend contract.

The Circuit browser preview presents 16 inspection positions. Step focus is presentation state. Playback, pitch edits, pattern data, timing and MIDI output depend on the shared sequencer. Report unavailable controls when the selected router lacks that backend. [Circuit workflow](USE_CASES.md#deferred-sequencing-on-two-surfaces)

Diagnostics selects a serial CLI target independently of its MIDI ports. Pi loopback, the authorized LAN CDC relay to the exact attached Pico and local Web Serial have separate admission and give access only to the selected target. [Diagnostics providers](api/browser-midi.md#shared-in-page-adapter)

## 5. Platform contracts and five platform-group owners

The [platform contracts](api/README.md#platform-services) cover MIDI endpoints and discovery, monotonic time and wakeups, persistent storage, feedback devices and capability reporting. Interface values use portable types. USB device classes are RP2040 integration details.

| Owner / path | Scope |
| --- | --- |
| **Application Assembly** `app/` | Dependency wiring, entry points, target selection, resource profiles, build/glue sources and UI/documentation packaging |
| **RP2040 HAL** `platform/rp2040/` | Board services, PIO USB host, native USB device classes, driver queues, timers, flash/storage and physical feedback |
| **Linux HAL** `platform/linux/` | Shared OS MIDI/time/storage implementation with explicit x86 and ARM build/test profiles |
| **Windows HAL** `platform/windows/` | Windows MIDI/time/storage services and Windows-specific runtime integration |
| **Simulation backend** `sim/platform/` | Deterministic fake MIDI, clock, storage and feedback behind the same contracts |

Linux uses one HAL implementation with separate x86-64 and arm64 build and test settings. The native adapter selects ALSA sequencer ports. [Linux MIDI adapter](api/linux-midi-adapter.md)

Raspberry Pi Zero runs the Linux application. Linux and Windows use OS services for USB and MIDI and distribute or serve their UI assets locally.

The opt-in Pi boot profile provides a private LAN web listener and an independent loopback CLI reachable through strict SSH. The LAN can expose the authorized Pico CDC relay. The optional recorder receives on a dedicated local managed output and uses a separate FluidSynth process to produce a finalized WAV. [Pi LAN router and recorder](api/pi-recorder.md)

### Network MIDI transports

Gridpico supports two product transports for production routes and selectable MIDI ports. Each has independent acceptance evidence.

- **RTP-MIDI** uses native OS-presented MIDI ports. On Linux, the production router binds ALSA sequencer ports provided by `rtpmidid`. Windows uses its OS-presented endpoints when available. [Linux ALSA adapter](api/linux-midi-adapter.md) · [Windows RTP endpoints](https://microsoft.github.io/MIDI/sdk-reference/Transports/Rtp/)
- **WebSocket MIDI** defaults to IPv4 loopback. The opt-in authenticated cross-host listener binds a configured LAN address and uses the same directional binary MIDI and attachment rules. Its grants cover authorized MIDI roles. Serial has independent admission. [WebSocket MIDI contract](api/websocket-midi.md)

[Network MIDI acceptance](ROADMAP.md#network-midi-and-browser)

The operator selects available ports and routes explicitly. A local adapter's acceptance establishes its own delivery boundary. Actual remote receipt and peer loss require evidence for the selected transport.

### RP2040 computer connection

The installed Pico matrix uses CDC and two native MIDI cable pairs. The Pico owns the physical USB host graph, controller semantics and LED feedback. The Pi can relay selected native MIDI and separately authorized Pico CDC bytes. Its own router CLI controls the Pi graph. [Pico target and relay](api/pico-target.md)

The next native USB allocation uses one composite device.

| Function | Purpose |
| --- | --- |
| MIDI A and MIDI B | Bidirectional general pairs on cables `0` and `2`, with independently assignable directional ports |
| APC Panel/LED | Dedicated bidirectional surface and feedback on cable `1` |
| CDC | Shared CLI transport and target recovery |
| Planned read-only mass storage `GRIDPICO` | Static HTML tools and offline documentation |

The three proposed MIDI pairs share one physical USB endpoint pair. Enumeration is verified. Asymmetric traffic, dispatch cutover, target resources and verified recovery gate promotion from the current two-pair deployment. [Three-pair contract](api/pico-target.md#next-native-keys-and-patchbay-increment)

The planned read-only `GRIDPICO` volume serves an immutable build-time image from flash with offline browser tools, documentation and a version manifest. Reject host writes at the storage protocol boundary and keep the volume separate from writable settings. Verify mounted reads, write rejection and concurrent MIDI service on Gridpico hardware. [Browser packaging](api/browser-midi.md#entry-points-and-packaging)

### Single-product staging

Advance one verified Pico release and one verified Pi release. Keep probes and deterministic fixtures outside normal product startup. Replace fixed bench device roles with bounded runtime inventory and explicit role binding as the target supports them. Qualify Pico updates with exact source and artifact identity, readback on the identified board and verified BOOTSEL recovery. [Pico release and recovery](api/pico-target.md#recovery-and-release) · [Current product scope](ROADMAP.md#current-product-status)

## 6. Dependencies and replaceability

- In-process frontends consume the Session command, query and state contracts. APC frontend owns APC interpretation. The web simulator emulates APC protocol traffic and feedback. Computer connections delivers MIDI to the appropriate ingress and local serial text to the production CLI. The kernel is independent of HTTP, WebSocket and RTP session handling.
- Session/control consumes domain contracts. It coordinates validation, application and coherent state reads. Projects consumes portable configuration values and the storage service. Session/control coordinates capture and restore so Projects has no dependency on live domain implementations.
- MIDI graph consumes the processor and platform MIDI contracts. Processors receive event and instance context from the graph. Musical time consumes the platform clock and wakeup contract.
- When present, Sequencer consumes the musical time and graph contracts. Time dispatch and graph recording feeds reach Sequencer through assembly wiring and public contracts, keeping the graph and clock independent of Sequencer implementation.
- Backends, including the simulation backend, provide the same platform contracts. Application Assembly selects concrete implementations, configures limits and wires event delivery. Shared value definitions live at the contract boundary. Modules use other modules through that boundary.

Arrows in the [architecture diagram](docs/diagrams/README.md) show data or control flow. The rules above define source dependencies. Replaceability comes from explicit service interfaces and bounded events wired by Application Assembly.

### Target capacities

Application Assembly publishes one effective capacity profile for each supported target or deterministic simulation setup, with backend input and domain validation. Optional features report availability separately. Kernel limits apply before a configuration becomes live. Queues and other performance resources have finite bounds and explicit exhaustion behavior. Frontends page endpoints and report limits using the current profile and command results. The [capacity contract](api/README.md#capacity-profiles) lists the required units and admission rules.

Qualify registry, live peer admission, fan-out and queue isolation per target. A larger registry grants no additional WSS publication. [Configured profile contract](api/routing.md#profile-and-admission)

## 7. Agent mapping and ownership

The [team coordination agreement](AGENT_TEAM_COORDINATION.md) defines path ownership, area leads, approvals and session lifecycle.

## 8. Verification requirements, not a roadmap

Acceptance must eventually cover the same interaction through APC simulation, physical APC and non-APC command entry; desired/active routing and reconnection; processor note-lifetime safety; timing/recording boundaries; independent save/load recovery; browser feedback; USB port separation; and USB disk activity alongside MIDI.

Browser Monitor verification covers a transposed note from the live destination feed, voice cleanup and channel isolation. Observe the same normalized MIDI destination trace with monitoring enabled and disabled.

Maintain separate evidence for deterministic simulation, Linux x86, Linux ARM, Windows and RP2040 hardware. Test capacities/overflow, malformed configuration, SysEx concurrency, external clock transitions, device identity ambiguity and power-loss behavior where applicable.

Maintain separate transport results for verified WebSocket loopback, cross-host browser and native WebSocket, Linux RTP-MIDI and Windows RTP-MIDI. Verify actual MIDI receipts, scoped queue retirement and zero replay. Cross-host WebSocket acceptance also exercises TLS client admission, origin checks, port authorization and isolation of the local CLI. [WebSocket MIDI acceptance](api/websocket-midi.md#integration-handoff-and-acceptance)

See [current acceptance](ROADMAP.md#current-product-status) for verified outcomes and remaining target gates.

## 9. Decisions intentionally left to planning

Open design decisions include the capacity and timing thresholds for additional targets and the supported Circuit sequencing behavior.
