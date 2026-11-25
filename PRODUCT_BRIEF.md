# Gridpico product brief

**Owner:** Strategist

## Purpose

Make the instruments already in the rig work together without a computer during performance. Gridpico routes and transforms MIDI and will add pattern sequencing through the same musical engine. The reference rig is a Yamaha P-95, Arturia MicroFreak, Akai APC Key 25 mk2 and Novation Circuit Tracks.

## User experience

- **Play and patch.** Use the physical or web APC to work with the same editable 5×8 Patchbay. Arrange source rows and destination columns, identify an input by playing it and audition one selected output. Save the global layout and routes explicitly. Use descriptive USB names and persistent aliases. Restore routes on an unambiguous reconnect.
- **Choose a mode.** An APC gesture opens the chooser for Patchbay, Keys passthrough and the planned Sequencer. Keys hands panel traffic and LED feedback to the connected music application. The Circuit browser surface can ship ahead of the engine and show which controls are available. The first sequencer milestone is a playable 16-step MIDI pattern shared by the Circuit and APC frontends.
- **Configure and diagnose.** Routing shows named inputs and outputs in an editable matrix. Diagnostics offers the serial CLI, MIDI Log, Monitor and a one-action LED check when the target supports it. The Pi and Pico CLIs share command names, usage and help for each available command. The browser identifies the selected target and explains unavailable actions.
- **Use one web interface.** The parameterized `scripts/web-ui.sh` opens Home, with links to controller, routing, diagnostics and instrument tools. Local and Pi serve the same assets at the same release. Select a server, local Web MIDI port or local Web Serial device as appropriate. The Pico's read-only `GRIDPICO` drive carries offline tools. On a trusted LAN, the Pi can relay the Pico's CDC console over WebSocket.
- **Listen and test.** Route MIDI to a physical instrument, browser Monitor, browser FM synth or Linux FluidSynth. The Pi can record and download a WAV. The MIDI File tool provides a repeatable test source. The optional, credited Web Synth integration is a separate instrument project.

## Platforms

The production router targets RP2040, Linux on x86 and ARM, and Windows. RTP-MIDI and WS/WSS connect network MIDI peers. Web MIDI can select RTP-MIDI ports exposed by the OS. Independently running components exchange MIDI or serial CLI bytes.

The Pico hosts USB instruments through a hub, with seven simultaneous MIDI devices as a qualification target. Native USB provides CDC and a bidirectional APC Panel/LED pair alongside assignable MIDI pairs. Board settings cover supported LED and PIO USB pin mappings. The onboard light reports readiness and issues. CDC BOOTSEL and early watchdog recovery support repeatable firmware updates.

## Value and boundaries

The value is a playable, reusable rig with predictable routing, clear feedback and a path to pattern-based performance. One production kernel supports live use and hardware-free testing. The router and firmware handle MIDI. Instrument peers generate audio at their receiving host. Gridpico code is MIT-licensed. The pre-alpha release advances as one product with platform-specific capabilities and a verified recovery path.

## Related documents

[Operator workflows](USE_CASES.md) · [Architecture](ARCHITECTURE.md) · [Roadmap and verified status](ROADMAP.md) · [Team ownership](AGENT_TEAM_COORDINATION.md)
