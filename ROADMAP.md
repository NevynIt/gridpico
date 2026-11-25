# gridpico roadmap

**Owner** — Product Owner

**Status** — Pre-alpha. One Gridpico product across Pico, Pi and browser, with runtime device and role binding. Deploy normal verified firmware with demonstrated recovery. The operative weekly Codex allowance floor is **5%**.

[Product brief](PRODUCT_BRIEF.md) · [Architecture](ARCHITECTURE.md) · [Operator workflows](USE_CASES.md) · [Editorial policy](docs/EDITORIAL_POLICY.md)

## Product horizon

- **Now · Make the active rig usable.** Finish the physical editable 5×8 APC Patchbay and generic Keys passthrough. Keep the live Pico and Pi CLI paths usable, including shared command and subcommand help. Circuit is visible in the browser as an inspection surface.
- **Next · Qualify persistence and board configuration.** Finish quiet-point Save and recovery, onboard status and compatible RP2040 configuration. Keep later physical USB and Ableton acceptance independent of Patchbay progress.
- **Later · Expand hardware and sequencing coverage.** Qualify seven downstream USB devices, direct browser USB and mounted Pico tools, and wider network and desktop targets. Scope one MIDI track and one 16-step pattern on the shared sequencer after the inspection UI. [Sequencer workflow](USE_CASES.md)

## Current product status

| Area | Verified scope | Open acceptance |
| --- | --- | --- |
| Pico firmware and CLI | Normal `3ae97ba` firmware passed startup, USB CDC, cooperative BOOTSEL and same-board return without another flash write. [Installed firmware](api/pico-target.md#installed-patchbay-matrix) | Mixed musical traffic with a stalled CLI reader and broader board qualification. |
| Pi browser and recording | Canonical `3ae97ba` serves Circuit and Web APC. Home ↔ Circuit shows 16 neutral inspection positions. Web APC retains eight controls with “KNOB CTL” removed. The CLI rollout preserved the existing recorder WAV. [Pi deployment](platform/linux/PI_ROUTER.md) | Circuit editing, transport and the shared engine. |
| Diagnostics | Live Chrome reached Pico CDC help, inventory, status and TRACE. A separate browser run reached Pi loopback help, inventory and status via strict SSH. [Live CLI evidence](platform/linux/PI_ROUTER.md#cli-and-pico-diagnostics) | Pico typed Routing, Diagnostics outbound MIDI, direct browser USB permissions and deployed browser-to-Pico MIDI. |
| Physical Patchbay and Keys | Jill played MicroFreak from the APC. Native Panel Move and revert passed with volatile placement. Temporary `0105` enumerated bidirectional A, Panel/LED and B. Keys cutover passed host fixtures. [Physical Pico evidence](api/pico-target.md) | Full computer-free 5×8 editing, live Audition, optical feedback and genuine physical Keys traffic and chooser. |
| Save and onboard status | Corrected Save transactions passed host fixtures. Optional fixed GP16 status passed offline checks. [Pico persistence and status contracts](api/pico-target.md) | Target writes remain disabled pending bounded USB/PIO pause and resume. Physical power-cut recovery, persisted settings, visible LED behavior, cadence and board configuration remain open. |

The curated documentation PDF and repository snapshot were published from `e689cdd`. [Download the documentation PDF](https://github.com/NevynIt/gridpico/releases/download/docs-2026-11-25-curated/gridpico-documentation.pdf)

## Approved physical Pico and CDC delivery

- **Routing and census.** The physical APC → MicroFreak route has operator evidence and separate native transfer evidence. The live census found APC, MicroFreak and Circuit Tracks behind two hubs. Passive Circuit Tracks Note On and Off arrived on channels 1 and 10 in an SRAM run. Circuit egress and use as an editable Patchbay source remain open. A temporary fourth address did not reveal MiniFuse 4. Seven-device capacity, MiniFuse enumeration and attributable Yamaha P-95 DIN MIDI remain open. [Pico topology and route evidence](api/pico-target.md#standalone-firmware-route)
- **Browser paths.** Stock Chrome C4 reached MicroFreak over a temporary SSH/WSS fixture on flashed firmware. Matching physical APC LED transfers have separate SRAM evidence. Deployed browser-to-Pico MIDI, physical APC → browser synth, MIDI File playback to Pico/MicroFreak, laptop-attached APC and acoustic output retain separate gates. The live private-LAN Pico CDC path uses plain WS. Direct local Web MIDI and Web Serial, as well as mounted `GRIDPICO` tools, need target evidence. [Pico browser and transport contracts](api/pico-target.md)
- **MicroFreak Program Change.** A flash trial recorded a USB completion for `C3 07`. Preset change and audio were unobserved. The normal image has Program Change Off. Omni receive and channel 4 transmit remain open. [Program Change bench](api/pico-target.md#native-program-change-bench)

## Proposed standalone APC Patchbay and USB qualification

Jill approved delivery from `92b2e85`. Project Manager sequences implementation and independent acceptance. The design legend governs the concept image. [APC concept](design/visuals/apc-patchbay-journey.png) · [Design legend](USE_CASES.md#proposed-editable-apc-patchbay--pre-go-concept)

- **Patchbay.** Boot into Patchbay with the last valid global layout. Prove all 5×8 positions, stable device and route identities, Assign, Arrange, Learn, input activity and one-output Audition with matching release. Verify shared edits and feedback on the physical APC and another control surface. Use a 1×1 route for internal bring-up.
- **Keys and native USB.** Verify generic passthrough and the proposed ordered Shift then Sustain chooser with the bound physical APC. Keep the mode cue inside the chooser, preserve release obligations and pass DAW LED traffic through. Standardize on bidirectional A, Panel/LED and B after traffic and recovery evidence. Keys defaults to A, its return starts unassigned, and B is general-purpose. Additional pairs depend on measured capacity. Jill’s actual Ableton Live and MPE witness remains separate, as does seven-device USB qualification.
- **Save and board status.** Require explicit Save at a safe quiet point, with Busy/Unsaved and immediate failure feedback. Verify disjoint storage, durable readback, previous-or-next recovery under interruption and safe service resumption. Persist layout, routes, per-device settings, CLI aliases and LED presets. Verify visible status on supported boards, with a best-effort violet indication before software BOOTSEL reset. Validate LED and PIO mappings over CDC while retaining rollback and native CDC rescue. [Snapshot contract](api/pico-target.md#explicit-session-snapshot)

## Network MIDI and browser

Linux RTP-MIDI has actual two-host receipts, scoped cleanup and rebind evidence. Repeatability, packet-loss recovery and timing remain open. Native cross-host WSS received MIDI and exercised cleanup. Browser mutual TLS has controlled Linux evidence, with operator laptop acceptance separate. Broader Linux ARM and Windows/WSL qualification and Windows RTP remain open. [Linux RTP evidence](api/linux-midi-adapter.md) · [WSS evidence](api/websocket-midi.md) · [Browser MIDI](api/browser-midi.md)

Earlier delivery narratives and their original anchors are available in the [pre-curation Git revision](https://github.com/NevynIt/gridpico/blob/f8b15cd0f43c519f3b27296d7c24677e49fbffe4/ROADMAP.md).
