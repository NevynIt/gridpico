# gridpico — product brief / business case

**Owner:** Strategist. **Status:** initial seed from human decisions, 22 November 2026.
**Related:** [Architecture](ARCHITECTURE.md), [team coordination](AGENT_TEAM_COORDINATION.md).

## Purpose

Make an existing collection of MIDI instruments more useful together without requiring a computer during performance. Gridpico provides routing and transformations first, with Circuit-Tracks-inspired MIDI sequencing available later through the same architecture. The kernel and firmware operate on MIDI.

The intended setup includes a Yamaha P-95, Arturia MicroFreak, Akai APC Key 25 mk2 and Novation Circuit Tracks. A computer is optional for configuration, simulation, additional MIDI applications and loopback routing.

## User experience

The APC is the preferred physical interface. Its browser simulator is the preferred graphical configuration interface, using the same APC protocol and production frontend. Additional browser tools are justified only for operations that cannot reasonably fit the APC surface.

The first browser APC slice shows the 5×8 pad grid, the other APC panel buttons and the 25-key playing keyboard. Prioritize a playable keybed whose presses and releases send MIDI Note On and Note Off through the simulated keybed path into the routing kernel. Basic pad and button LED feedback is sufficient for this slice.

The Web APC follows the physical APC Key 25 mk2 layout and proportions. In the demo, a visible summary beside the pad grid identifies the Route pad's source, destination, transformation, and current state.

Include a browser MIDI log that shows events received at the simulated destination. Pressing and releasing a browser key on a route with transpose should visibly produce the matching transposed Note On and Note Off. This gives the user a way to check the routing path with no USB MIDI devices connected.

The web interface can also offer optional audible monitoring of routed MIDI. The Monitor view uses the same destination events as the log and generates sound on the host in the browser. It offers an audio enable control, master volume, and a sound or mute choice for each MIDI channel of the monitored destination. A small set of lightweight sounds supports the first version.

The web APC owner downloads and uses Akai Professional's official protocol document before mapping the controls. [APC Key 25 mk2 Communications Protocol v1.1](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

The musical kernel is not APC-specific. A CLI and, later, piano-key/pedal controls with an RGB feedback strip operate the same commands and state. Patch and Circuit views share the same musical project.

The first usable Patch view routes one source to multiple destinations. The initial serial CLI shows the device names and MIDI ports the core recognizes, their connection state, and which ports occupy each source row and destination column. Users can assign a color to a recognized device through either the CLI or APC. Both views use the same device preference. Colors identify devices and route feedback shows route state.

Diagnostics has a separate web page for the serial CLI, logs, and debug information. The browser terminal uses the same CLI commands as a serial terminal.

On RP2040, USB-hosted devices connect through a hub. Native USB offers multiple MIDI ports, CDC and a read-only `GRIDPICO` drive carrying the self-contained browser interface and documentation. The user has already demonstrated the HTML-drive approach in a separate prototype.

The Web APC and Diagnostics pages must work offline when opened directly from the mounted read-only drive. Diagnostics must be able to connect to the Pico's serial CLI from that page on supported browsers.

Linux x86, Linux ARM (including Raspberry Pi Zero) and Windows run the same musical application with appropriate platform backends. They do not imitate an embedded USB device where the host OS makes that unnecessary.

Gridpico supports two network MIDI transports. RTP-MIDI is a product path for native Linux and Windows routers and compatible MIDI applications. WebSocket MIDI is a product path for the Web APC, Diagnostics, Monitor, and other compatible browser or native clients. Both connect to the production routing system through selectable MIDI ports. Separately running components exchange MIDI or the serial CLI byte stream.

A web landing page links to the available tools, starting with Web APC and Diagnostics. On a computer, `scripts/web-ui.sh` opens that landing page. The Pico's read-only drive carries the landing page and self-contained offline tools. Browser tools share a MIDI connection layer for port selection and sending or receiving messages over Web MIDI or WebSocket MIDI. Web MIDI can select RTP-MIDI ports exposed by the operating system.

Optional instruments are separate MIDI destinations. A FluidSynth wrapper plays routed MIDI on a Linux host, and a standalone browser FM instrument plays it through Web Audio. The initial browser acceptance case is a routed note that sounds and releases using a built-in FM voice. These instruments generate sound at the receiving end, alongside the existing browser Monitor.

## Value

The value is reuse and integration of instruments already owned: deliberate routing, useful transformations, visible control and eventual recording/pattern workflows. A common kernel makes controller experiments and hardware-free tests possible without separate products or inconsistent implementations.

This is a product rationale, not a market-size, revenue, cost-saving or performance claim. The human selected an MIT repository license. Commercial positioning, budgets and any distribution commitments remain human decisions.

## Scope boundaries

Included are a portable MIDI kernel, interchangeable frontends and backends, APC simulation with optional browser audio monitoring, offline UI and documentation distribution, MIDI routing and processing, a preserved sequencing module boundary, explicit projects, and reliable state feedback.

Synthesis, sampling, and audio processing in the kernel or firmware are outside scope. The project also excludes copying Novation's sound engine, legacy compatibility, and a custom agent orchestration framework. Earlier projects provide reference material. Development tooling can reuse the established container recipe.

## Planning and ownership

Strategist maintains this brief in discussion with the human. Architect maintains `ARCHITECTURE.md`. Project Manager creates and maintains `ROADMAP.md` only after discussion with the other two. All three consult one another; each file has one editor/owner.

The initial team is Session Manager, Strategist, Architect and Project Manager. No implementation agents are created merely because a candidate roster or architecture exists. Expansion follows human approval and Project Manager's lifecycle requests.
