# gridpico — product brief / business case

**Owner:** Strategist. **Status:** current product direction.
**Related:** [Architecture](ARCHITECTURE.md), [team coordination](AGENT_TEAM_COORDINATION.md).

## Purpose

Make an existing collection of MIDI instruments more useful together without requiring a computer during performance. Gridpico provides routing and transformations first, with Circuit-Tracks-inspired MIDI sequencing available later through the same architecture. The kernel and firmware operate on MIDI.

The intended setup includes a Yamaha P-95, Arturia MicroFreak, Akai APC Key 25 mk2 and Novation Circuit Tracks. A computer is optional for configuration, simulation, additional MIDI applications and loopback routing.

## User experience

The APC is the preferred physical interface. Its browser simulator uses the same APC protocol and production frontend. The browser also provides dedicated Routing and Diagnostics views for inspecting and configuring setups that are difficult to see on the APC surface.

The first browser APC slice shows the 5×8 pad grid, the other APC panel buttons and the 25-key playing keyboard. Prioritize a playable keybed whose presses and releases send MIDI Note On and Note Off through the simulated keybed path into the routing kernel. Basic pad and button LED feedback is sufficient for this slice.

The Web APC follows the physical APC Key 25 mk2 layout and proportions. Its controls send and receive MIDI on the selected controller ports.

The MIDI Log shows the events received on its selected port. Pressing and releasing a browser key on a route with transpose should visibly produce the matching transposed Note On and Note Off. This gives the user a way to check the routing path without USB MIDI devices.

The Monitor receives routed MIDI on its own selected port and generates sound in the browser. It offers an audio enable control, master volume, a listening-channel selector, and a sound or mute choice for each channel.

The web APC owner downloads and uses Akai Professional's official protocol document before mapping the controls. [APC Key 25 mk2 Communications Protocol v1.1](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

The musical kernel is not APC-specific. A CLI and, later, piano-key/pedal controls with an RGB feedback strip operate the same commands and state. Patch and Circuit views share the same musical project.

The Circuit Tracks-style browser surface can ship before sequencing is ready and show which controls are available. Its first musical milestone is one playable 16-step MIDI pattern. The browser surface and APC sequencing mode share the Gridpico sequencer. Playback routes to an existing browser instrument or an external MIDI destination.

The Patch view routes one source to multiple destinations. The Pi and Pico CLIs share commands, usage and help for every available command. The CLI identifies the target, displays recognized devices and directional ports, and shows their Patchbay assignments. Device colors identify instruments. Route feedback shows route state.

The physical APC and Web APC use an editable five-by-eight Patchbay. Users can identify a source by playing it, arrange inputs and outputs, and audition a selected output with a short MIDI phrase. The rig saves a global layout and routes through an explicit Save. Ports use USB product names where available and accept persistent CLI aliases. An APC gesture opens a chooser for Patchbay, Keys passthrough, and the planned Sequencer mode. The chooser shows the current mode. Keys mode hands the panel and LED feedback to the connected music application and keeps the chooser gesture available on the APC.

Routing has its own destination from Home and a distinct layout. Named MIDI inputs are rows, named outputs are columns, and their intersections show and control routes. Port availability is visible separately from route state. Known routes in the selected profile start enabled. Port capacity is configurable within each target's tested limits. The APC viewport can move through matrices larger than its physical grid. When USB devices reconnect, Gridpico reuses logical ports and routes where a device can be matched reliably and asks the user to resolve ambiguous matches.

Diagnostics is the separate page for the serial CLI, MIDI Log, Monitor, and routed protocol replay. The browser terminal uses the same CLI commands as a serial terminal and can select either the local router's loopback CLI or a local serial device through Web Serial. The built-in LED test is ready to run in Diagnostics when its known endpoints are available. One action sets up and runs the sequence through Gridpico to the Web APC. Users can also send LED sequences directly to a physical APC over local Web MIDI. Diagnostics reports submission status so users can check the effect on the receiving APC.

On RP2040, USB-hosted devices connect through a hub. Native USB offers multiple MIDI ports, CDC and a read-only `GRIDPICO` drive carrying the self-contained browser interface and documentation. The user has already demonstrated the HTML-drive approach in a separate prototype.

The bench targets seven simultaneous downstream USB MIDI devices. Native USB presents a dedicated bidirectional APC panel and LED pair and one or more independent assignable bidirectional MIDI pairs for the Patchbay. The Pico's onboard light reports startup, connection readiness, and issues, with a distinct color before a software BOOTSEL reset. Users can configure colors from the web and configure the LED type and pins and the PIO USB pin mapping through CDC on compatible RP2040 boards.

The first physical routing check sends MIDI from the real APC through the Pico's USB host hub and Gridpico router to the MicroFreak. The Pi can configure and observe the Pico while the Pico handles the MIDI route. Gridpico firmware on this bench retains a CDC command to enter BOOTSEL and starts a recovery watchdog early. If USB and CDC startup fail, the watchdog recovery enters BOOTSEL so the connected Pi can reflash the Pico.

The physical and web APC surfaces use the same router state and show feedback from that state. A physical APC connected to the Pico can play a browser instrument through the Pi's WSS MIDI connection. Web APC can play the MicroFreak through the Pi and Pico. With the physical APC attached to the laptop instead, a browser MIDI connection can forward its input from local Web MIDI over WSS to the same Pico route.

The Web APC and Diagnostics pages must work offline when opened directly from the mounted read-only drive. Diagnostics must be able to connect to the Pico's serial CLI from that page on supported browsers.

Browser users can select local USB MIDI through Web MIDI and local USB CDC through Web Serial on supported setups. When the Pico is attached to the Pi, Diagnostics can select the Pico's CDC through an opt-in WebSocket endpoint on the trusted LAN without requiring certificates. Authenticated WSS remains an option for a future deployment. The interface identifies the selected connection and the device receiving commands.

The desktop targets are Linux x86, Linux ARM (including Raspberry Pi Zero), and Windows. They use the same musical application with platform-specific backends. The [roadmap](ROADMAP.md) records the verification status for each target.

Gridpico supports two network MIDI transports. RTP-MIDI is a product path for native Linux and Windows routers and compatible MIDI applications. WebSocket MIDI is a product path for the Web APC, Diagnostics, Monitor, and other compatible browser or native clients. Both connect to the production routing system through selectable MIDI ports. Separately running components exchange MIDI or the serial CLI byte stream.

A web landing page links to Web APC, Diagnostics, Routing, and instrument tools. The parameterized `scripts/web-ui.sh` is the single launcher. Local and Pi hosts serve the same browser assets at the same release. The interface changes the features it offers according to the selected endpoint's capabilities. Users can switch between reachable Gridpico instances by changing the server address in the interface. The Pico's read-only drive carries the landing page and self-contained offline tools. Each MIDI tool independently selects its ports through the shared browser connection layer, using local Web MIDI or the chosen WS/WSS router. Web MIDI can select RTP-MIDI ports exposed by the operating system. A WSS router's operator chooses which directional ports to publish.

A dedicated MIDI File tool plays a file selected on the laptop into a chosen WSS MIDI input. It gives users a repeatable source for testing remote routes and releases notes when playback stops.

Optional instruments are separate MIDI destinations and generate audio at the receiving end. A FluidSynth wrapper plays routed MIDI on a Linux host, and a lightweight browser FM instrument plays it through Web Audio offline. Gridpico links to a separately distributed and credited Web Synth fork as an optional instrument. The fork retains the upstream FM interface and licensing and adds a receive-only WSS MIDI input.

## Value

The value is reuse and integration of instruments already owned: deliberate routing, useful transformations, visible control and eventual recording/pattern workflows. A common kernel makes controller experiments and hardware-free tests possible without separate products or inconsistent implementations.

This is a product rationale, not a market-size, revenue, cost-saving or performance claim. The human selected an MIT repository license. Commercial positioning, budgets and any distribution commitments remain human decisions.

## Scope boundaries

Included are a portable MIDI kernel, interchangeable frontends and backends, APC simulation with optional browser audio monitoring, offline UI and documentation distribution, MIDI routing and processing, a preserved sequencing module boundary, explicit projects, and reliable state feedback.

Synthesis, sampling, and audio processing in the kernel or firmware are outside scope. The project also excludes copying Novation's sound engine, legacy compatibility, and a custom agent orchestration framework. Earlier projects provide reference material. Development tooling can reuse the established container recipe.

## Planning and ownership

Strategist maintains this brief in discussion with the human. Architect maintains `ARCHITECTURE.md`. Product Owner maintains `ROADMAP.md` and tracks approved features and milestones. Project Manager coordinates delivery and confirms implementation and testing evidence. UI Designer maintains `USE_CASES.md` alongside this brief and reviews user workflows and visual choices before UI implementation. Each file has one editor.

The initial team is Session Manager, Strategist, Architect and Project Manager. No implementation agents are created merely because a candidate roster or architecture exists. Expansion follows human approval and Project Manager's lifecycle requests.
