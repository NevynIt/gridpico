# Gridpico product brief

**Owner:** Strategist

## Purpose

Make the instruments already in the rig work together without a computer during performance. Gridpico routes and transforms MIDI and will add pattern sequencing through the same musical engine. The reference rig is a Yamaha P-95, Arturia MicroFreak, Akai APC Key 25 mk2 and Novation Circuit Tracks.

## User experience

- **Play and patch.** Use the physical or web APC to work with the same editable 5×8 Patchbay. Discover supported USB MIDI inputs and outputs generically, with descriptive USB names and persistent aliases. On fresh setup, activate compatible routes between discovered musical ports even without an APC, with same-channel feedback loops off. Forward each incoming MIDI message over active, connected routes with their current transforms. Drop messages when their route is unavailable. On reconnect, forward only new messages. Arrange ports, identify an input by playing it and audition one selected output. Save the global layout and routes explicitly and reuse cached port numbers and user routing choices on unambiguous reconnects. The capacity target is at least 32 assignable logical MIDI ports. A user-enabled computer return route can connect separate applications on the same host.
- **Choose APC feedback.** Reserve the first recognized APC Key 25 LED output for control by default. Use the CLI to mark any eligible output as APC Keys LED or clear that role. The role reserves the output for feedback until cleared, when it becomes available for ordinary routing again. Identify each controller separately when several are connected.
- **Choose a mode.** An APC gesture opens the chooser for Patchbay, Keys passthrough and the planned Sequencer. Keys hands panel traffic and LED feedback to the connected music application. The Circuit browser surface can ship ahead of the engine and show which controls are available. The first sequencer milestone is a playable 16-step MIDI pattern shared by the Circuit and APC frontends.
- **Set up, play and stop.** Configure routes and transforms, then play with musical routing enabled. Pause or resume all musical routing with one switch while retaining the setup. Panic independently sends All Notes Off to connected musical outputs, including while routing is paused.
- **Configure and diagnose.** Routing shows named inputs and outputs on the selected router. Diagnostics passes serial bytes directly to the selected CLI and shows its replies. The Pi and Pico CLIs share command names, usage and help for each available command. MIDI Log, Monitor and LED checks use separate MIDI connections.
- **Use one web interface.** The standard `scripts/web-ui.sh` launch opens Home for the controller, routing, diagnostics and instrument tools. Local and Pi serve the same assets at the same release. Choose the server URL and open compatible pages in any order, with as many instances as capacity allows. The server registers each instance and its MIDI endpoints automatically, keeping their advertised names. Instances with the same name can be routed independently. Local Web MIDI and Web Serial remain available on supported setups. The Pico's read-only `GRIDPICO` drive carries offline tools, and the Pi can relay Pico CDC over WebSocket on a trusted LAN.
- **Listen and test.** Route MIDI to a physical instrument, browser Monitor, browser FM synth or Linux FluidSynth. The Pi can record and download a WAV. The MIDI File tool provides a repeatable test source. The optional, credited Web Synth integration is a separate instrument project.

## Platforms

The production router targets RP2040, Linux on x86 and ARM, and Windows. RTP-MIDI and WS/WSS connect network MIDI peers. Web MIDI can select RTP-MIDI ports exposed by the OS. Independently running components exchange MIDI or serial CLI bytes.

The Pico hosts USB instruments through a hub, with seven simultaneous MIDI devices as a qualification target. Native USB provides CDC and a bidirectional APC Panel/LED pair alongside assignable MIDI pairs. Board settings cover supported LED and PIO USB pin mappings. The onboard light reports readiness and issues. CDC BOOTSEL and early watchdog recovery support repeatable firmware updates.

## Value and boundaries

The value is a playable, reusable rig with predictable routing, clear feedback and a path to pattern-based performance. One production kernel supports live use and hardware-free testing. The router and firmware handle MIDI. Instrument peers generate audio at their receiving host. Gridpico code is MIT-licensed. The pre-alpha release advances as one product with platform-specific capabilities and a verified recovery path.

## Related documents

[Operator workflows](USE_CASES.md) · [Architecture](ARCHITECTURE.md) · [Roadmap and verified status](ROADMAP.md) · [Team ownership](AGENT_TEAM_COORDINATION.md)
