# gridpico — initial architecture

**Status:** architecture baseline with approved transport contracts.
**Owner:** Architect. **Product authority:** human, through Strategist.
**Baseline:** decisions agreed on 22 November 2026.
**Related:** [Product brief](PRODUCT_BRIEF.md), [public contracts](api/README.md), [team coordination](AGENT_TEAM_COORDINATION.md), [diagrams](docs/diagrams/README.md).

This document describes responsibility boundaries, dependencies and design constraints. Project Manager owns task sequencing and the roadmap.

## 1. Product and design intent

Gridpico is a portable, controller-independent MIDI workstation kernel. It connects and enhances existing MIDI devices through routing, transformations and, later, Circuit-Tracks-inspired sequencing. The initial instrument set is Yamaha P-95, Arturia MicroFreak, Akai APC Key 25 mk2 and Novation Circuit Tracks, with an optional computer. The piano's physical MIDI connection is supplied through a suitable interface; the kernel sees logical MIDI endpoints.

The planned embedded deployment uses RP2040. Native USB connects to the computer as a device, and PIO USB hosts instruments through a hub. The same product must also run as a Linux or Windows application. Raspberry Pi Zero runs the Linux ARM application.

The first proof passed in a Linux x86_64 container. It exercises production Session/control, MIDI graph and transpose against deterministic in-process MIDI services. The [proof contract](api/README.md#linux-x86_64-route-and-transpose-proof) defines the scope of that evidence. The standalone rehearsal and RP2040 acceptance depend on access to hardware.

The portable kernel, platform backends and firmware handle MIDI. Tracks target MIDI destinations. Browser Monitor and standalone instrument peers generate sound at the receiving end.

CodeZero/GridZero, miniMIDI and rpico-bridge are references and experimental evidence only. There is no legacy code, configuration, wire-protocol or storage compatibility requirement. Reusing the development-container recipe is tooling reuse, not adoption of another product's implementation.

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
| **MIDI graph** `core/midi/` | Logical endpoint registry, identity binding, desired/active routes, fan-in/fan-out, delivery queues, event origin and note/sustain lifetime | USB enumeration; sequencing data; UI pages |
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

Device bindings use logical identity and user aliases. Stage 3 supports opt-in Linux USB input selection when the adapter can prove one match. The physical gate covers scoped cleanup on unplug and a verified return on a fresh input generation. Missing or ambiguous input matches leave enabled routes Pending. A USB output selector reserves its port and stays Pending. Operators can still configure an explicit numeric ALSA output. Generation-bound verified output awaits a separate transport and delivery review. [USB replacement contract](api/routing.md#verified-usb-replacement)

Represent cable/port, MIDI channel and device identity separately. USB packet framing belongs in adapters. The core event contract must preserve ordinary MIDI, realtime messages and bounded/streamed SysEx without assuming every message is a four-byte USB packet.

The graph owns note-release correctness. It retains the delivered output obligations for each accepted Note On. Processor results carry enough association for the graph to release the original outputs after parameter changes or route edits. The [MIDI graph contract](api/README.md#midi-graph) defines overlap, cleanup and overload behavior.

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

GridZero supplies interaction ambitions: recording/playback, 16/32-step editing, multiple patterns, quantized selection, chains, scenes and later automation/step parameters. Eight track selectors and eight pattern memories are reference design targets, **not a frozen memory allocation or first-release commitment**. The planning team must choose the supported subset and capacities. A track is MIDI sequencing/performance state, not a synth instance.

### Projects and live configuration

Keep draft editing, live Apply and persistent Save distinct. Validate a whole change before activating it; a rejected change leaves the previous valid state in use. Note cleanup and scheduling cancellation participate in state transitions.

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

Physical and simulated panel messages reach the production APC frontend through Computer connections or platform MIDI. The frontend translates panel gestures into typed commands and renders state-derived LED feedback. Keybed notes enter the production MIDI graph through musical ingress. The desktop passive viewport and the installed Pico editable Patchbay use the same logical route model. Keep their exercised capabilities explicit until the product paths converge. [APC viewport contract](api/routing.md#apc-viewport) · [Installed matrix](api/pico-target.md#installed-patchbay-matrix)

Independently running components exchange MIDI or the serial CLI byte stream. On RP2040, the browser can select computer-visible MIDI ports. The desktop WebSocket adapter carries the same APC messages to the production frontend. Router inventory, status and route control are available through the local serial CLI. Typed routing, port registration and delivery accounting stay within the production runtime.

The piano frontend must remember whether a press/pedal transition was consumed or forwarded until its matching release, even across mode changes. Gesture interpretation is frontend-owned; ordinary MIDI note/sustain cleanup stays graph-owned. RGB-strip drawing describes logical feedback; the physical strip driver is a platform service.

### Web landing page and browser MIDI

Assembly owns `scripts/web-ui.sh` as the computer entry point for the production web home. Connections hosts the landing page with links to Web APC, Diagnostics, Routing and the optional FM instrument. Assembly stages `HOME.HTM` with relative links to self-contained `INDEX.HTM`, `DIAG.HTM` and `FM.HTM` for read-only `GRIDPICO` distribution. Staged-file checks verified direct opening. Routing through local CDC and actual mounted Pico access remain pending. [Web startup and evidence](api/browser-midi.md)

Connections shares one in-page browser MIDI layer for provider and port selection, send, receive and binding lifecycle. Web MIDI can select ordinary OS ports, including OS-exposed RTP-MIDI endpoints when the browser grants access. The WebSocket provider binds directional ports to the production router over the existing binary MIDI wire. This shared layer is a code boundary inside the page. Diagnostics opens its serial CLI stream independently. [Browser MIDI and landing page contract](api/browser-midi.md)

The Home-linked Routing view shares the local loopback CLI session with Diagnostics. The six-port default and 18-port desktop profile both expose it. It displays named directional ports and occupancy separately from desired and active route state. The CLI owns route commands and replies. The browser keeps its own provider, permission and selected-port state. The local Web Serial CDC option for Routing and direct Web MIDI to physical APC LED RX remain Stage 3 gates. [Routing and serial](api/routing.md#routing-through-the-local-cli)

### Browser Monitor

Diagnostics hosts Monitor, MIDI Log and routed LED replay in the production web tools. Web APC owns the controller surface. Monitor and MIDI Log bind independently to selectable MIDI inputs. Monitor starts audio from live Note On and releases voices on the corresponding Note Off for its binding. MIDI Log shows its own received bytes. Monitor audio starts after explicit enablement. Master volume, per-channel sound or mute, lightweight presets and bounded voice state belong to the local Monitor. The earlier Route and Patch demos retain their verified Capture paths. [Browser roles](api/browser-midi.md#production-startup-and-page-roles)

Track voices by destination, MIDI channel and key so two channels using the same key can sound and release independently. Muting a channel releases its voices. Disabling audio or losing the connection releases all browser voices. The graph continues to own MIDI cleanup, including releases that occur after the browser has disconnected. The Monitor uses the existing routed MIDI delivery path and runs independently of its timing and acceptance.

### Optional instrument peers

Sound-generating peers open ordinary MIDI inputs. Stock FluidSynth and the standalone wrapper each received MIDI through the production Pi ALSA route and produced a verified local WAV. The wrapper owns the engine process, SoundFont and local output. The standalone browser FM instrument uses the shared receive adapter and owns its built-in voice, Web Audio and local voice cleanup. Routed Note On and Note Off were verified through loopback WebSocket and selected ALSA Web MIDI ports. Both peers ship independently from the router and firmware. [Instrument-peer contracts and evidence](api/instrument-peers.md)

The optional Web Synth uses an explicitly published receive-only WSS MIDI output and retains its own interface, engine and local audio. The actual engine passed on the private same-LAN bench. Any operator release requires a reviewed, independently hosted and credited synth with complete corresponding source and notices, plus separately approved origin and enrollment. Gridpico retains the staged offline FM instrument. [Web Synth peer contract](api/instrument-peers.md#optional-hosted-web-synth)

### Patch and Circuit views

Preserve GridZero's useful interaction ideas without inheriting its synth requirements:

- Patch view uses all 40 pads as a five-source by eight-destination matrix, with navigation outside the matrix and pages for additional endpoints.
- The shared Patch inventory maps logical input and output ports to source rows and destination columns. One source can route to multiple destinations. Device color preferences are shared by APC and CLI. Route feedback reflects route state.
- Desired and active routes have distinct visible states. GridZero's green/amber/rejected feedback is an initial reference, subject to a documented palette.
- Future Circuit view can use the top eight pads for tracks and the lower 32 for steps/editors, with the keybed supplying musical notes.
- Both views operate the same routes, tracks and transport. Changing views does not stop musical behavior.
- Synth master-filter/volume behavior is not inherited. Any encoder allocation for external-instrument MIDI controls must be explicitly specified.

Published Patch controls pin the implemented gestures, mappings and feedback. The first Circuit browser page can ship independently as an inspection-only 16-step view. Show that the selected router lacks a sequencer. Step focus is presentation state. Playback, pitch edits, pattern data, timing and MIDI output require the shared backend. Do not enable those controls based on page availability. [Patch controls](api/patchbay-controls.md) · [Circuit preview](USE_CASES.md#deferred-sequencing-on-two-surfaces)

The standalone Diagnostics page exposes the local serial CLI and connection status offline. Its MIDI controls send sequences through a separately selected MIDI port. Computer connections owns both transports and the browser client. The CLI frontend owns command text and calls Session/control for inventory, device colors and route commands. Application Assembly packages Diagnostics on the read-only `GRIDPICO` drive. [Diagnostics and local router](api/local-router.md)

## 5. Platform contracts and five platform-group owners

The [platform contracts](api/README.md#platform-services) cover MIDI endpoints and discovery, monotonic time and wakeups, persistent storage, feedback devices and capability reporting. Interface values use portable types. USB device classes are RP2040 integration details.

| Owner / path | Scope |
| --- | --- |
| **Application Assembly** `app/` | Dependency wiring, entry points, target selection, resource profiles, build/glue sources and UI/documentation packaging |
| **RP2040 HAL** `platform/rp2040/` | Board services, PIO USB host, native USB device classes, driver queues, timers, flash/storage and physical feedback |
| **Linux HAL** `platform/linux/` | Shared OS MIDI/time/storage implementation with explicit x86 and ARM build/test profiles |
| **Windows HAL** `platform/windows/` | Windows MIDI/time/storage services and Windows-specific runtime integration |
| **Simulation backend** `sim/platform/` | Deterministic fake MIDI, clock, storage and feedback behind the same contracts |

Linux uses one HAL implementation with separate target build and test settings. The native ALSA adapter sits under `platform/linux/` and builds for both x86-64 and arm64. The Pi arm64 bench has its own verified receipts and cleanup results. Additional Linux targets require their own build and runtime evidence. [Linux MIDI adapter](api/linux-midi-adapter.md)

Raspberry Pi Zero runs the Linux application. It is not required to emulate RP2040 USB device behavior, CDC or a mounted disk. A Windows or Linux application talks to devices through the OS and distributes/serves UI assets locally. Direct USB hosting here means using OS services, not writing a desktop USB host stack.

The opt-in Pi boot profile exposes the APC controller on a single trusted private WLAN address. Its optional recorder receives MIDI on a dedicated local managed output and uses a separate stock FluidSynth process to produce a finalized WAV. The LAN browser can control that recording and download the completed file. Inventory and serial CLI stay on Pi loopback, accessible to the operator through the strict SSH path. Pi deployment, recorder and boot evidence have their own acceptance. [Pi LAN router and recorder](api/pi-recorder.md)

### Network MIDI transports

Gridpico supports two product transports for production routes and selectable MIDI ports. Each has independent acceptance evidence.

- **RTP-MIDI** uses native OS-presented MIDI ports and the selected platform's RTP service. On Linux, the production router binds ALSA sequencer ports provided by `rtpmidid`. The Linux↔Linux run observed exact receipt and scoped release on selected disconnects. Repeatability, controlled packet loss and timing require further evidence. Windows uses native OS-presented RTP ports with acceptance deferred to the nominated Windows target. [Linux ALSA adapter](api/linux-midi-adapter.md) · [Network MIDI status](ROADMAP.md#network-midi-and-browser) · [Windows RTP endpoints](https://microsoft.github.io/MIDI/sdk-reference/Transports/Rtp/)
- **WebSocket MIDI** defaults to the verified IPv4 loopback profile. The opt-in cross-host profile binds a selected LAN address and authenticates standard browser and native clients over TLS. Both listeners use the same directional binary MIDI bindings and production route and cleanup rules. The cross-host listener serves MIDI controls. The local serial listener retains its independent CLI byte stream. Two-host native-client receipt, scoped release and fresh binding were verified with the Pi listener. Isolated headed Linux Chrome verified same-origin receipt and a distinct-origin browser synth over a private LAN after top-level certificate selection. Two-host browser acceptance and public-origin Local Network Access remain pending. [WebSocket MIDI contract](api/websocket-midi.md)

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

This is the next standard native allocation. The three MIDI pairs share one physical USB endpoint pair. The read-only census verified actual enumeration. Promote the descriptor after asymmetric send and receive, mode and release cleanup, and target resource and recovery checks. Keep exact source and verified flash rollback for the installed release until replacement passes. [Three-pair contract and census](api/pico-target.md#next-native-keys-and-patchbay-increment)

The read-only volume contains `HOME.HTM`, the standalone `INDEX.HTM` and `DIAG.HTM` tools, documentation and a version manifest. Generate its immutable filesystem image at build time and serve it directly from flash. Host writes are rejected at the storage protocol boundary. Keep it separate from writable settings and projects, with defined behavior during flash operations.

At revision `36c61af1`, rpico-bridge sets up a native TinyUSB composite device with one MIDI cable pair, CDC and build-optional MSC. Its PIO host task services a separate root port. It links the prebuilt documentation image into read-only data and serves bounded reads while rejecting writes. These source patterns are reference evidence for firmware planning. [Composite descriptor](https://github.com/NevynIt/rpico-bridge/blob/36c61af1e03c799b60c250dd41263a174b5ee5f4/firmware/usb_device.c#L62-L71) · [Host and device servicing](https://github.com/NevynIt/rpico-bridge/blob/36c61af1e03c799b60c250dd41263a174b5ee5f4/firmware/usb_host.c#L70-L108) · [Image build](https://github.com/NevynIt/rpico-bridge/blob/36c61af1e03c799b60c250dd41263a174b5ee5f4/CMakeLists.txt#L64-L89) · [Read-only storage callbacks](https://github.com/NevynIt/rpico-bridge/blob/36c61af1e03c799b60c250dd41263a174b5ee5f4/firmware/usb_docs_msc.c#L46-L88)

The reference records a complete read of its 512 KiB drive and reported write protection. Gridpico must verify mounted-drive behavior and MIDI service during disk activity on its own target. Users open the offline HTML explicitly. [Reference bench results](https://github.com/NevynIt/rpico-bridge/blob/36c61af1e03c799b60c250dd41263a174b5ee5f4/docs/STATE.md)

### Single-product staging

1. Reuse `frontends/cli` on Pico and Pi. Start with `help`, `help <command>`, `status` and `inventory` through one command table for dispatch and help. Use the selected runtime's actual providers for each advertised command and subform. A known form without a provider reports unavailable. Keep the existing bounded line and reply contract and typed Session callbacks. Size the Pico parser, stack and nonblocking CDC reply path on the linked target, including full-width status revisions. Preserve the cooperative BOOTSEL path and its authority from early startup. Verify a real Pico USB CDC terminal and the Pi loopback terminal. Diagnostics sends CLI bytes through its selected authorized provider and consumes that target's replies. Keep the Pico-via-Pi Diagnostics form on its existing read-oriented scope until the new forms are explicitly admitted. A transcript connection does not establish typed Routing support.
2. Register live USB MIDI inventory within target bounds, then bind operator-selected directional ports and controller roles to stable logical IDs. Adapters report interface and cable identity separately from MIDI channel, attachment generation and display name. Match a return to a uniquely verified identity or an explicit operator-bound physical path. Conflicts stay visible and routes stay Pending. Aliases change labels only. Graph currently reads a startup registry, so qualifying runtime registration and retirement is a dependency for dynamic Patchbay. Session preserves desired routes and placements while detach and replacement retire scoped releases. Publish measured device, hub, interface, endpoint, branch and release bounds independently. A volatile binding is available before target Save is qualified.
3. Advance one normal Pico and one normal Pi release from verified source, then retire superseded product toggles and fixed bench routes. Qualify the real rig's selected routing and three-pair native roles, watchdog/BOOTSEL and exact readback before promotion. Keep probes and deterministic fixtures in testing or release history. A retained rollback image does not impose a second product profile or legacy format compatibility. The Circuit browser preview is independent of these steps.

## 6. Dependencies and replaceability

- In-process frontends consume the Session command, query and state contracts. APC frontend owns APC interpretation. The web simulator emulates APC protocol traffic and feedback. Computer connections delivers MIDI to the appropriate ingress and local serial text to the production CLI. The kernel is independent of HTTP, WebSocket and RTP session handling.
- Session/control consumes domain contracts. It coordinates validation, application and coherent state reads. Projects consumes portable configuration values and the storage service. Session/control coordinates capture and restore so Projects has no dependency on live domain implementations.
- MIDI graph consumes the processor and platform MIDI contracts. Processors receive event and instance context from the graph. Musical time consumes the platform clock and wakeup contract.
- When present, Sequencer consumes the musical time and graph contracts. Time dispatch and graph recording feeds reach Sequencer through assembly wiring and public contracts, keeping the graph and clock independent of Sequencer implementation.
- Backends, including the simulation backend, provide the same platform contracts. Application Assembly selects concrete implementations, configures limits and wires event delivery. Shared value definitions live at the contract boundary. Modules use other modules through that boundary.

Arrows in the [architecture diagram](docs/diagrams/README.md) show data or control flow. The rules above define source dependencies. Replaceability comes from explicit service interfaces and bounded events wired by Application Assembly.

### Target capacities

Application Assembly publishes one effective capacity profile for each supported target or deterministic simulation setup, with backend input and domain validation. Optional features report availability separately. Kernel limits apply before a configuration becomes live. Queues and other performance resources have finite bounds and explicit exhaustion behavior. Frontends page endpoints and report limits using the current profile and command results. The [capacity contract](api/README.md#capacity-profiles) lists the required units and admission rules.

The verified desktop profile maps the five-by-eight APC viewport over six managed source rows and nine managed destination columns. Its 18 directional ports and 200 release credits reflect the tested desktop workload. Registry backing, live peer admission, fan-out, held occurrences, release capacity and queue isolation are target-specific. Pico's three-pair census and its mixed-traffic qualification are distinct. WSS publication is independent of registry size. [Configurable profile contract](api/routing.md#profile-and-admission)

## 7. Agent mapping and ownership

One implementation module means one proposed owner seat. The six kernel modules, five interface modules and five platform-group modules yield **16 implementation seats**. Testing, Builder and Documentation add three independent seats. With the four initial planning/session seats, the proposed full roster is **23 agents**, not 23 agents to start now.

APC frontend owner additionally acts as **Interfaces Lead**. Application Assembly owner additionally acts as **Platforms Lead**. These are advisory duties, not extra seats or competing managers. Architect guides the kernel directly, owns all public contracts and resolves cross-area design questions. Project Manager alone assigns work.

Each source file has one owner. `api/` belongs to Architect; build/glue code belongs to Application Assembly; `tests/` belongs to Testing; user documentation belongs to Documentation except the explicitly owned planning documents. Builder runs builds and does not repair source.

See the coordination agreement for approvals, escalation and session lifecycle.

## 8. Verification requirements, not a roadmap

Acceptance must eventually cover the same interaction through APC simulation, physical APC and non-APC command entry; desired/active routing and reconnection; processor note-lifetime safety; timing/recording boundaries; independent save/load recovery; browser feedback; USB port separation; and USB disk activity alongside MIDI.

Browser Monitor verification covers a transposed note from the live destination feed, voice cleanup and channel isolation. Observe the same normalized MIDI destination trace with monitoring enabled and disabled.

The approved one-pad demo separates route enablement from runtime activity. Session/control publishes committed intent, the graph completes held-note release before disabling, and the production APC frontend derives LED feedback from the route snapshot. Surface and keybed messages use distinct paths over the local connection. [One-pad route and surface contract](api/README.md#one-pad-apc-route-toggle)

Maintain separate evidence for deterministic simulation, Linux x86, Linux ARM, Windows and RP2040 hardware. Test capacities/overflow, malformed configuration, SysEx concurrency, external clock transitions, device identity ambiguity and power-loss behavior where applicable.

Maintain separate transport results for verified WebSocket loopback, cross-host browser and native WebSocket, Linux RTP-MIDI and Windows RTP-MIDI. Verify actual MIDI receipts and scoped cleanup. Cross-host WebSocket acceptance also exercises TLS client admission, origin checks, port authorization and isolation of the local CLI. [WebSocket MIDI acceptance](api/websocket-midi.md#integration-handoff-and-acceptance)

The [roadmap](ROADMAP.md) records sequencing, implemented profiles and verification results.

## 9. Decisions intentionally left to planning

The native Linux x86_64 proof uses portable C11 and shared in-process types for route Apply, state queries and note delivery. [Native proof contract](api/README.md#linux-x86_64-route-and-transpose-proof)

The planning team still needs to resolve additional target build profiles, measured capacity and timing thresholds, the selected Circuit behavior baseline and physical deployment wiring.

The initial documents are maintained collaboratively with single-file ownership. Human-approved changes to product scope, module boundaries or agent seats flow through Strategist. References and experimental evidence are recorded in [Sources](docs/SOURCES.md).
