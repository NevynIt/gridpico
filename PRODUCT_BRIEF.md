# Gridpico product brief

**Owner:** Strategist

## Purpose

Make the instruments already in the rig work together without a computer during performance. Gridpico routes and transforms MIDI and will add pattern sequencing through the same musical engine. The reference rig is a Yamaha P-95, Arturia MicroFreak, Akai APC Key 25 mk2 and Novation Circuit Tracks.

## User experience

- **Play and patch.** Use the physical or web APC to work with the same editable 5×8 Patchbay. Linux and Pico share one APC frontend for control actions, Patchbay state and LED commands. The Web APC relays control events and draws the returned LEDs. Discover supported USB MIDI inputs and outputs generically, with descriptive USB names and persistent aliases. When SD settings are present, restore verified placements and routes, including routes saved Off. On a boot without saved settings, discover the connected USB MIDI rig, assign eligible ports to free rows and columns and activate the expected routes between its devices without manual setup. Keep one routing table of inputs, outputs, enabled states and transforms. Forward each incoming MIDI message over matching enabled routes to connected outputs using their current transforms. Allow bounded queueing for connected outputs and drop packets when the queue is full. Activate eligible routes for new devices even without an APC, skipping only explicitly identified loopbacks. Compatible directions can be patched manually without a cycle veto, including directions of the same device. Drop messages when their route is unavailable. On reconnect, forward only new messages. Arrange ports, identify an input by playing it and audition one selected output. Use the optional SD card for persistent layout and routes. Retire the Pico's internal-flash layout Save function. Reuse cached port numbers and user routing choices on unambiguous reconnects. The capacity target is at least 32 assignable logical MIDI ports. A user-enabled computer return route can connect separate applications on the same host.
- **Choose APC feedback.** Reserve the first recognized APC Key 25 LED output for control by default. Use the CLI to mark any eligible output as APC Keys LED or clear that role. The role reserves the output for feedback until cleared, when it becomes available for ordinary routing again. Identify each controller separately when several are connected.
- **Choose a mode.** An APC gesture opens the chooser for Patchbay, Keys passthrough and the planned Sequencer. Keys hands panel traffic and LED feedback to the connected music application. The Circuit browser surface can ship ahead of the engine and show which controls are available. The first sequencer milestone is a playable 16-step MIDI pattern shared by the Circuit and APC frontends.
- **Set up, play and stop.** Configure routes and transforms, then play. Use Pause, Resume, and Panic from the CLI or the physical APC Patchbay. Its 5×8 LED grid toggles routes, and actions can use any suitable button or key on the APC Keys surface. Pause retains the route and transform setup. Panic sends MIDI All Notes Off to connected musical outputs, including while paused. Activate Panic again after one second and before ten seconds to send an individual Note Off for every note on the applicable channels.
- **Configure and diagnose.** Routing shows named inputs and outputs on the selected router. Diagnostics passes serial bytes directly to the selected CLI and shows its replies. Pi and Pico share understandable CLI commands, named results and practical help by default. Automation explicitly opts into machine-readable output. MIDI Log, Monitor and LED checks use separate MIDI connections.
- **Use one web interface.** The standard `scripts/web-ui.sh` launch opens Home for the controller, routing, diagnostics and instrument tools. Local and Pi serve the same assets at the same release. Choose the server URL and open compatible pages in any order, with as many instances as capacity allows. The server registers each instance and its MIDI endpoints automatically, keeping their advertised names. Instances with the same name can be routed independently. Local Web MIDI and Web Serial remain available on supported setups. Plug the Pico into a computer to mount its read-only `GRIDPICO` drive and open the HTML interface directly. The Pi can relay Pico CDC over WebSocket on a trusted LAN.
- **Extend the Pico rig from the browser.** Open the Pi-hosted Web UI and join browser MIDI devices to the Pico over WebSocket. Use the physical APC and a Pico-targeted Web APC together on the same Patchbay with shared route and LED feedback. A separate Web APC tab can target the Pi router. Show the target on each page and join automatically without asking for port numbers or repeated connection setup. Play browser synths from a physical keybed and physical synths from Web APC through the Pico.
- **Listen and test.** Route MIDI to a physical instrument, browser Monitor, browser FM synth or Linux FluidSynth. The Pi can record and download a WAV. The MIDI File tool provides a repeatable test source.

## Platforms

The production router targets RP2040, Linux on x86 and ARM, and Windows. RTP-MIDI and WS/WSS connect network MIDI peers. Web MIDI can select RTP-MIDI ports exposed by the OS. Independently running components exchange MIDI or serial CLI bytes. Provide a user-testable portable Windows ZIP with the native Gridpico executable and static Web UI, including the bundled FM instrument and a supported way to connect to a Pico. Allow standalone pages to open directly from disk. For live browser control of the native router, use a minimal native HTTP and WebSocket server library to serve the packaged assets and the router's MIDI and CLI connections. The Windows package runs without Node.js.

The Pico hosts USB instruments through a hub. Full seven-device and 32-direction hardware qualification is deferred. Routing uses available target storage without policy caps on total routes or destinations per input. Native USB provides CDC and a bidirectional APC Panel/LED pair alongside assignable MIDI pairs. Board settings cover supported LED and PIO USB pin mappings. The onboard light reports readiness and issues. CDC BOOTSEL and early watchdog recovery support repeatable firmware updates.

## Pico, PC router, and Web UI concept

This is the target design for hardware work and a later architecture review. The Pico and PC builds share the routing, controller, MIDI transform and sequencer code, with platform-specific I/O. The Web UI can run without either Gridpico build.

### A. Pico router

- **USB Device.** Expose one CDC serial port for CLI, two bidirectional MIDI cables and one mass-storage device for configuration, logs, recordings and a copy of the Web UI. Cable 1 carries general MIDI. Switch Cable 2 between general MIDI and APC/Ableton using the CLI or APC UI. In APC/Ableton mode, it carries APC Keys panel and LED messages in both directions so APC-aware applications can talk to a physical APC through the Pico. Designer will propose the controls and feedback.
- **USB Host.** The PIO USB host discovers USB MIDI devices and other Gridpicos, directly or through a hub. An exposed 3.3 V UART with TX, RX and ground can later carry a bidirectional link to another Gridpico.
- **Local peripherals.** Use hardware SPI for microSD, I²C for a 64 × 128 monochrome OLED, and an addressable RGB data output. The onboard RP2040-Zero pixel is the initial RGB device. The same protocol can drive an external strip.
- **SD operation.** Use human-editable scenes usable without an APC, with CLI, MIDI or optional GPIO selection. Provide a read-only UI view and an explicit PC edit handoff for the SD data volume. Pico unmounts before giving the PC write access, and retakes ownership after clean eject, sync and validation. Pico may pause routing and stops card writes during PC ownership. Keep the append-only boot/event log (firmware version, boot-relative time, attachment and other events) separate from optional detailed per-port MIDI ingress and egress capture. Use SD as the sole settings store. A no-card boot discovers compatible MIDI devices and uses eligible default routes.

### RP2040-Zero board sketch

| Connector | Requested function |
| --- | --- |
| U1, USB-C | 5 V power input only. No data. |
| U2, USB-C | Native RP2040 USB Device data to the computer. May supply U4 through protection. |
| U3, USB-A | 5 V power output only, fed from U1 with reverse-current protection. |
| U4, USB-A | PIO USB Host data, with protected 5 V power from U1 or U2. |
| RGB | Expose addressable RGB data for a device using the onboard pixel's protocol. |
| SD | SPI microSD socket. |
| OLED | I²C monochrome 64 × 128 display. |
| UART | Expose 3.3 V TX, RX and ground, with no power pin. Defer 5 V level shifting until needed. |

Build a minimal carrier for ordinary DEV use. Power SD and OLED from the RP2040-Zero 3.3 V rail within its usable current budget. Use four-signal USB connectors or breakouts (ground, D+, D− and 5 V) and confirm any required USB-C CC parts on the actual connector. Prioritize protection of the laptop and attached MIDI devices, including prevention of backfeed and unsafe downstream current. Accept local Pico risk. Keep all four USB 2 connector roles and the listed power paths. U3 uses only U1. U4 can draw protected 5 V from U1 or U2. Select the module's SYS5 supply automatically from the protected U1 or U2 feeds. Keep only the control and protection circuitry needed for those power paths, the laptop and the attached devices.

Draw the actual pinout and schematic against the RP2040-Zero module. Evaluate the suggested low-drop paths from U1 to U3, U1 to U4 and U2 to U4 against voltage drop and current for a chain of up to three Picos. Check source isolation, USB power roles and downstream protection on the actual wiring before choosing the power circuit.

### B. PC router

- Run on x86 or ARM Linux and Windows. Connect to A using USB MIDI and CDC. Bring the Pico's two bidirectional cables into the PC router as two inputs and two outputs and route between the nodes.
- Serve MIDI and serial WebSockets for other PC routers and browser tools. For an attached Pico, expose additional WebSocket targets that proxy its MIDI and CLI through USB. The PC instance and direct target for the first, PC-attached Pico can be used in parallel, including Web APC access to its physical APC. Chained second and third Picos can carry routed MIDI. Direct PC control of those units is deferred.
- Allow multiple concurrent MIDI and CLI connections up to actual resource capacity. MIDI connections become routable endpoints on B or use the selected direct Pico USB path. Browser endpoints can keep their names on B and appear under a shared USB cable name on the Pico. Define how concurrent clients share physical MIDI and CDC streams during architecture review. Users choose their routes, including loops. The router must stay responsive enough to stop a mistaken loop.
- Offer RTP-MIDI if feasible as a lower priority.

### C. Web UI

- Ship one UI through the Pico USB mass-storage view, a website, or a PC Gridpico server. Connect to B or the Pico proxy via WebSocket. Also connect directly to any MIDI or serial device available to the browser through Web MIDI or Web Serial, including without Gridpico.
- **Web APC and Web Circuit.** Send the controller's MIDI for buttons and pads and CC for knobs, and display LEDs from received MIDI. Gridpico owns interactions and state interpretation.
- **Serial terminal.** Send and receive bytes over Web Serial or WebSocket without command filtering.
- **Synths.** Offer General MIDI palettes where available and suitable reusable web synth projects.
- **MIDI monitor.** Show channel activity or exact MIDI bytes.
- **MIDI recorder and player.** Record a Standard MIDI File from a selected channel and play a file to a selected channel.

## Value and boundaries

The value is a playable, reusable rig with predictable routing, clear feedback and a path to pattern-based performance. One production kernel supports live use and hardware-free testing. The router and firmware handle MIDI. Instrument peers generate audio at their receiving host. Gridpico code is MIT-licensed. The pre-alpha release advances as one product with platform-specific capabilities and a verified recovery path. Remove obsolete product variants.

## Related documents

[Operator workflows](USE_CASES.md) · [Architecture](ARCHITECTURE.md) · [Roadmap and verified status](ROADMAP.md) · [Team ownership](AGENT_TEAM_COORDINATION.md)
