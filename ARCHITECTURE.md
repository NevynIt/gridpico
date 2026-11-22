# gridpico — initial architecture

**Status:** planning baseline, not an implemented system.
**Owner:** Architect. **Product authority:** human, through Strategist.
**Baseline:** decisions agreed on 22 November 2026.
**Related:** [Product brief](PRODUCT_BRIEF.md), [team coordination](AGENT_TEAM_COORDINATION.md), [diagrams](docs/diagrams/README.md).

This document describes responsibility boundaries, dependencies and design constraints. It is not a task plan. **Project Manager will create `ROADMAP.md` after discussing the product and architecture with Strategist and Architect.**

## 1. Product and design intent

Gridpico is a portable, controller-independent MIDI workstation kernel. It connects and enhances existing MIDI devices through routing, transformations and, later, Circuit-Tracks-inspired sequencing. The initial instrument set is Yamaha P-95, Arturia MicroFreak, Akai APC Key 25 mk2 and Novation Circuit Tracks, with an optional computer. The piano's physical MIDI connection is supplied through a suitable interface; the kernel sees logical MIDI endpoints.

The initial embedded deployment uses an RP2040: native USB is a device connection to the computer, while PIO USB hosts instruments through a hub. The same product must also run as a Linux or Windows application. Raspberry Pi Zero is a Linux ARM deployment, not a separate appliance architecture.

**There is no synthesizer, sampler, audio engine or DSP subsystem, now or later within this scope.** Tracks target external MIDI destinations. MIDI controls for external instruments are legitimate; internal sound generation is not.

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

Commands express intent, such as connecting a route, changing a processor parameter, editing a step or starting transport. Queries and state notifications describe musical state rather than a rendered surface. The same commands apply whether invoked by APC, piano, CLI or an authorized browser helper.

Session/control validates cross-domain operations and dispatches to the owning module. Each domain remains authoritative for its own state. A coherent snapshot/revision mechanism lets frontends refresh without maintaining independent musical truth. State notifications must not be reinterpreted as new commands and echoed back.

Application Assembly supplies the execution context. Platform callbacks submit bounded events; they must not mutate musical state concurrently through undocumented side channels. Thread/core allocation is a target decision, not a public-kernel dependency.

### MIDI graph and processors

The basic route is **source selection → processor chain → destination(s)**. Fan-out can have independent branches and processor settings. The patch grid and advanced editing operate on this one model, not separate routing engines.

Device bindings use logical identity and user aliases, not transient USB addresses or OS port indices. A missing device leaves its desired connections pending. Ambiguous identities require an explicit binding decision rather than silently substituting an instrument.

Represent cable/port, MIDI channel and device identity separately. USB packet framing belongs in adapters. The core event contract must preserve ordinary MIDI, realtime messages and bounded/streamed SysEx without assuming every message is a four-byte USB packet.

The graph owns note-release correctness. It records what destinations/notes a Note On produced, including transformation results, so later Note Off, sustain release, route edits, transport stops and disconnects can resolve the original output. Processors must expose enough association information for that accounting. Define collision/overlap and overflow behavior rather than assuming repeated identical notes are interchangeable.

Detect or reject graph-local feedback cycles. External applications can create loops outside the graph's visibility; do not claim to detect those universally. PC loopback is explicit routing, never automatic all-to-all echo.

Processors include filters, transpose, key/velocity splits, channel and controller mapping, curves, relative/absolute conversion and bounded stateful operations. Extensibility is a goal; unrestricted scripting or unbounded generated traffic is not a requirement. The precise extension mechanism belongs to Architect after resource discussion.

### Time and future sequencing

The platform supplies monotonic time and wakeups. Musical time owns the musical schedule and maps the chosen internal/external clock to transport and position. Define clock loss, source switching, event cancellation, timing budgets and stop behavior independently of any frontend.

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
| **Web APC simulator** `web/apc/` | Primary graphical interface; emulate APC controls, keybed and received LED feedback using the production APC MIDI protocol |
| **Computer connections** `connections/` | Optional CDC/serial and local browser transports, framing and connection lifecycle; no musical state or controller semantics |

Inside the APC frontend, keep protocol decoding/encoding separate from view/gesture interpretation even though one owner initially owns both. Other frontends neither impersonate the APC nor import its view implementation.

The preferred browser configuration experience is the APC simulator. Its MIDI goes through **Computer connections/platform MIDI → APC frontend → Session/control**, not directly to a second browser routing engine. Hardware APC panel traffic follows the same APC frontend. Keybed traffic remains ordinary musical input. Panel traffic is consumed as control while LED output remains possible; it is not merely globally dropped.

On RP2040, the browser can use the computer-visible MIDI ports. A desktop build can carry equivalent APC MIDI messages over a local connection into the same frontend. Supplementary browser tools, when a task truly does not fit the APC surface, use the common kernel API through Computer connections. Examples include file import/export and long text entry. These are additions, not an independently authoritative configurator.

The piano frontend must remember whether a press/pedal transition was consumed or forwarded until its matching release, even across mode changes. Gesture interpretation is frontend-owned; ordinary MIDI note/sustain cleanup stays graph-owned. RGB-strip drawing describes logical feedback; the physical strip driver is a platform service.

### Patch and Circuit views

Preserve GridZero's useful interaction ideas without inheriting its synth requirements:

- Patch view uses all 40 pads as a five-source by eight-destination matrix, with navigation outside the matrix and pages for additional endpoints.
- Desired and active routes have distinct visible states. GridZero's green/amber/rejected feedback is an initial reference, subject to a documented palette.
- Future Circuit view can use the top eight pads for tracks and the lower 32 for steps/editors, with the keybed supplying musical notes.
- Both views operate the same routes, tracks and transport. Changing views does not stop musical behavior.
- Synth master-filter/volume behavior is not inherited. Any encoder allocation for external-instrument MIDI controls must be explicitly specified.

Final gestures, mappings and behavior-baseline versions belong in later approved specifications, not guessed implementations.

## 5. Platform contracts and five platform-group owners

`api/platform/` provides only services the portable modules need: MIDI endpoints/discovery, monotonic time/wakeup, persistent storage, feedback devices and capability/resource reporting. No TinyUSB, ALSA, Windows or browser types cross that boundary. USB device class details are target services rather than requirements imposed on every backend.

| Owner / path | Scope |
| --- | --- |
| **Application Assembly** `app/` | Dependency wiring, entry points, target selection, resource profiles, build/glue sources and UI/documentation packaging |
| **RP2040 HAL** `platform/rp2040/` | Board services, PIO USB host, native USB device classes, driver queues, timers, flash/storage and physical feedback |
| **Linux HAL** `platform/linux/` | Shared OS MIDI/time/storage implementation with explicit x86 and ARM build/test profiles |
| **Windows HAL** `platform/windows/` | Windows MIDI/time/storage services and Windows-specific runtime integration |
| **Simulation backend** `sim/platform/` | Deterministic fake MIDI, clock, storage and feedback behind the same contracts |

Linux must distinguish architectures from the start, but **does not need duplicate Linux implementations or separate agents**. Suggested structure is `platform/linux/common/`, `platform/linux/x86/` and `platform/linux/arm/`, with target-specific build settings and genuinely necessary differences isolated. The exact x86/ARM ABI profiles and Raspberry Pi generation remain explicit planning decisions. A Linux ARM build/test result must not be inferred from an x86 pass.

Raspberry Pi Zero runs the Linux application. It is not required to emulate RP2040 USB device behavior, CDC or a mounted disk. A Windows or Linux application talks to devices through the OS and distributes/serves UI assets locally. Direct USB hosting here means using OS services, not writing a desktop USB host stack.

### RP2040 computer connection

Native USB provides one composite device with:

| Function | Purpose |
| --- | --- |
| Three general MIDI input/output pairs | Computer applications, recording/injection and explicit PC-to-PC routing |
| Two APC simulator MIDI pairs | Separate simulated keybed and surface/LED traffic |
| CDC | Optional text/control/diagnostic transport, not a dependency of the kernel |
| Read-only mass storage: `GRIDPICO` | Static self-contained HTML interface and offline documentation |

The five MIDI pairs are a proposed port allocation satisfying the minimum of three general-purpose pairs. They are ports/cables, not MIDI channels. Application Assembly and RP2040 HAL must budget descriptors, endpoints, buffers, flash and CPU together. Public names are product configuration, not legacy compatibility commitments.

The read-only volume contains `INDEX.HTM`, documentation and a version manifest. Generate its immutable filesystem image at build time; serve it without copying the entire image into RAM. Host writes are rejected at the storage protocol boundary. Keep it separate from writable settings/projects, with defined behavior during any flash operation.

The user reports successful mass-storage/HTML operation in rpico-bridge. Treat that as experimental evidence that the approach works in that setup, not as an unresolved basic feasibility question or proof of the new implementation. Opening/copying/ejecting the volume must not disrupt MIDI. Do not require autorun; users open the HTML explicitly. Package assets without internet dependencies and verify supported browsers/permissions on actual target systems.

## 6. Dependencies and replaceability

- Frontends depend on the command/query/state contracts; only APC frontend knows APC behavior.
- Session/control coordinates domain contracts; it does not depend on concrete frontends.
- MIDI graph invokes processors through their contract and handles common delivery safety.
- Sequencer uses musical time and the graph's recording/playback contracts, never direct USB/OS output.
- Projects obtains/restores domain state through contracts and uses the storage service.
- Backends implement platform contracts. Application Assembly selects implementations and owns integration glue.
- Computer connections adapts transports; it cannot bypass kernel validation or become a musical engine.
- Simulation implements services consumed by the **same production modules**. Independent tests belong to Testing, not hidden behavior inside a fake kernel.

Prefer explicit service interfaces and bounded events over a generic plugin/service framework. The architecture requires replaceability, not runtime loading, microservices or one process per module.

## 7. Agent mapping and ownership

One implementation module means one proposed owner seat. The six kernel modules, five interface modules and five platform-group modules yield **16 implementation seats**. Testing, Builder and Documentation add three independent seats. With the four initial planning/session seats, the proposed full roster is **23 agents**, not 23 agents to start now.

APC frontend owner additionally acts as **Interfaces Lead**. Application Assembly owner additionally acts as **Platforms Lead**. These are advisory duties, not extra seats or competing managers. Architect guides the kernel directly, owns all public contracts and resolves cross-area design questions. Project Manager alone assigns work.

Each source file has one owner. `api/` belongs to Architect; build/glue code belongs to Application Assembly; `tests/` belongs to Testing; user documentation belongs to Documentation except the explicitly owned planning documents. Builder runs builds and does not repair source.

See the coordination agreement for approvals, escalation and session lifecycle.

## 8. Verification requirements, not a roadmap

Acceptance must eventually cover the same interaction through APC simulation, physical APC and non-APC command entry; desired/active routing and reconnection; processor note-lifetime safety; timing/recording boundaries; independent save/load recovery; browser feedback; USB port separation; and USB disk activity alongside MIDI.

Maintain separate evidence for deterministic simulation, Linux x86, Linux ARM, Windows and RP2040 hardware. Test capacities/overflow, malformed configuration, SysEx concurrency, external clock transitions, device identity ambiguity and power-loss behavior where applicable.

This list does not assign tasks, dates, release stages or a delivery order. The planning team determines those in the future roadmap.

## 9. Decisions intentionally left to planning

Architect, Strategist and Project Manager must resolve the kernel language/subset and ABI, exact target CPU/OS profiles, budgets/capacities, formal command/event contracts, the selected Circuit behavior baseline, platform transport choices, physical deployment wiring and concrete acceptance thresholds. Do not silently choose them by copying a reference project.

The initial documents are maintained collaboratively with single-file ownership. Human-approved changes to product scope, module boundaries or agent seats flow through Strategist. References and experimental evidence are recorded in [Sources](docs/SOURCES.md).
