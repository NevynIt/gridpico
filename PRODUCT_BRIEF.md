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

The first usable Patch view routes one source to multiple destinations. The initial serial CLI shows the device names and MIDI ports the core recognizes, their connection state, and which ports occupy each source row and destination column. Users can assign a color to a recognized device through either the CLI or APC. Both views use the same device preference. Colors identify devices and route feedback shows route state.

The dedicated Routing view shows named MIDI inputs and outputs in a matrix. It distinguishes port availability from route state and lets users enable or disable routes through the existing serial CLI. Port capacity is configurable within each target's tested limits. The APC viewport can move through routing matrices larger than its physical grid. When USB devices reconnect, Gridpico reuses logical ports and routes where a device can be matched reliably and asks the user to resolve ambiguous matches.

Diagnostics is the separate page for the serial CLI, MIDI Log, Monitor, and routed protocol replay. The browser terminal uses the same CLI commands as a serial terminal and can select either the local router's loopback CLI or a local serial device through Web Serial. Users can send Diagnostics LED sequences through Gridpico to the Web APC or directly to a physical APC over local Web MIDI. Diagnostics reports submission status so users can check the effect on the receiving APC.

On RP2040, USB-hosted devices connect through a hub. Native USB offers multiple MIDI ports, CDC and a read-only `GRIDPICO` drive carrying the self-contained browser interface and documentation. The user has already demonstrated the HTML-drive approach in a separate prototype.

The Web APC and Diagnostics pages must work offline when opened directly from the mounted read-only drive. Diagnostics must be able to connect to the Pico's serial CLI from that page on supported browsers.

The desktop targets are Linux x86, Linux ARM (including Raspberry Pi Zero), and Windows. They use the same musical application with platform-specific backends. The [roadmap](ROADMAP.md) records the verification status for each target.

Gridpico supports two network MIDI transports. RTP-MIDI is a product path for native Linux and Windows routers and compatible MIDI applications. WebSocket MIDI is a product path for the Web APC, Diagnostics, Monitor, and other compatible browser or native clients. Both connect to the production routing system through selectable MIDI ports. Separately running components exchange MIDI or the serial CLI byte stream.

A web landing page links to Web APC, Diagnostics, Routing, and instrument tools. On a computer, `scripts/web-ui.sh` opens that landing page. The Pico's read-only drive carries the landing page and self-contained offline tools. Each MIDI tool independently selects its ports through the shared browser connection layer, using local Web MIDI or the chosen WS/WSS router. Web MIDI can select RTP-MIDI ports exposed by the operating system. A WSS router's operator chooses which directional ports to publish.

Optional instruments are separate MIDI destinations and generate audio at the receiving end. A FluidSynth wrapper plays routed MIDI on a Linux host, and a lightweight browser FM instrument plays it through Web Audio offline. Gridpico links to a separately distributed and credited Web Synth fork as an optional instrument. The fork retains the upstream FM interface and licensing and adds a receive-only WSS MIDI input.

## Value

The value is reuse and integration of instruments already owned: deliberate routing, useful transformations, visible control and eventual recording/pattern workflows. A common kernel makes controller experiments and hardware-free tests possible without separate products or inconsistent implementations.

This is a product rationale, not a market-size, revenue, cost-saving or performance claim. The human selected an MIT repository license. Commercial positioning, budgets and any distribution commitments remain human decisions.

## Scope boundaries

Included are a portable MIDI kernel, interchangeable frontends and backends, APC simulation with optional browser audio monitoring, offline UI and documentation distribution, MIDI routing and processing, a preserved sequencing module boundary, explicit projects, and reliable state feedback.

Synthesis, sampling, and audio processing in the kernel or firmware are outside scope. The project also excludes copying Novation's sound engine, legacy compatibility, and a custom agent orchestration framework. Earlier projects provide reference material. Development tooling can reuse the established container recipe.

## Planning and ownership

Strategist maintains this brief in discussion with the human. Architect maintains `ARCHITECTURE.md`. Project Manager creates and maintains `ROADMAP.md` only after discussion with the other two. All three consult one another; each file has one editor/owner.

The initial team is Session Manager, Strategist, Architect and Project Manager. No implementation agents are created merely because a candidate roster or architecture exists. Expansion follows human approval and Project Manager's lifecycle requests.
