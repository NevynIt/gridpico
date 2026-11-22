# gridpico roadmap

**Owner** — Project Manager

**Status** — Native proof, connected Web APC log and browser Monitor independently verified for the 23 November delivery. The human approved both follow-ons below on the existing roster. Architect validated the one-pad contract.

[Product brief](PRODUCT_BRIEF.md) · [Architecture](ARCHITECTURE.md) · [Team coordination](AGENT_TEAM_COORDINATION.md) · [Model and effort plan](.team/MODEL_PLAN.md)

## Demo sequence for 23 November at 09:00 CET (08:00 UTC)

1. Start the independent native proof and the browser surface in parallel when their approved seats are available. Confirm the minimal fixture contracts first for core integration. Keep the browser surface to the 5×8 pad grid, panel buttons and 25-key keyboard. Verify each deliverable as soon as it is runnable.
2. Connect the keybed and captured destination log once the fixture and local connection are ready. Give this integration priority over LED work. Leave LED expansion and panel parity for after the demo.
3. At 07:30 CET (06:30 UTC), choose the demo path from what has passed verification. Include connected playback if pressing and releasing a key reliably shows the actual transposed pair and the cleanup checks pass. Otherwise, use the native proof and present the browser as a surface preview. Show the native command and real captured trace separately. If either baseline deliverable is still missing, show the verified portion and state the specific gap.
4. Reserve the final 90 minutes for verification, fixes to the selected demo path, and rehearsal. Before batching and at the demo-path decision, check `/status` and use any observed usage rate to preserve capacity for this window. If allowance or time tightens earlier, drop LED work and then connected playback before cutting baseline verification.

## Verified demo path

From the repository root, run `./scripts/demo-route-transpose.sh`. Builder and Testing independently verified all four cases and eight captured deliveries. Fresh native runs produced identical traces.

Follow the connected browser startup instructions, open the loopback URL and press and release **Q**. Independent Chromium acceptance confirmed one captured channel-1 Note On for note 72 at velocity 96 followed by Note Off at velocity 0, cancellation without duplicate release, held-note cleanup on disconnect and a fresh capture on reconnect. The standalone surface remains the fallback. Sustained stalled-reader saturation was outside this run's verification. [Connected Web APC startup](connections/README.md) · [Standalone browser surface](web/apc/index.html)

## Approved follow-ons for the rest of tonight

The human approved implementation of browser monitoring and the scoped one-pad route toggle. The verified native command and connected Web APC log are the demo fallback.

### Priority 1. Browser-only Monitor

Add a Monitor tab, an explicit **Enable audio** action, a master volume control, and sound or mute for each of MIDI channels 1–16. Keep the APC keys playable while viewing the tab. Start silent on page load. Use a few small procedural Web Audio sounds that work offline. The monitor consumes the same newly received destination Note On and Note Off events as the log. Scope the implementation to the Web APC simulator and independent browser verification. Firmware, the kernel and the existing live transport stay outside the Monitor work. [Web Audio API](https://www.w3.org/TR/webaudio/)

Acceptance in real Chromium uses the connected demo without USB MIDI. Enable audio, press and release **Q**, and check that the captured channel-1 note 72 produces its expected pitch in the browser audio signal and returns to silence on Note Off or cancellation. Verify master volume, sound choice and channel mute, including mute while a note is held. Disabling audio and disconnecting release all voices. Enabling or reconnecting plays only fresh destination events. Keep voices bounded and audio failures local to the monitor. Show identical routed MIDI traces with monitoring disabled and enabled, and rerun the native four-case proof. The live fixture carries channel 1. Exercise all 16 channel controls and simultaneous same-pitch notes on different channels in local browser tests at the destination-event boundary.

Report whether verification measured a browser audio signal, confirmed physical audibility, or both. Use the verified log-only demo if browser audio is unavailable.

**Verified.** Independent Chromium measured live note 72 at about 523 Hz. Release, cancel, disable and disconnect returned to silence. Tests passed for master volume, presets, all 16 channel controls, same-pitch channel isolation and no replay on enable or reconnect. The connected MIDI trace matched with audio disabled and enabled, and the four-case native trace matched the prior build byte for byte. Speaker output was outside the headless measurement.

### Stretch. One real Patch route toggle

Start this batch after the verified Monitor is published and allowance covers integration, verification and fixes. Use Akai-mapped pad traffic over a separate logical surface path on the existing local socket through the production APC frontend to Session/control, with Application Assembly wiring the path. Render the active/off LED from committed route state. Verify that toggling off mid-note produces exactly one actual destination release, and that the later source Note Off adds no duplicate. New notes while off produce no destination output. Toggling on replays no held notes, and a fresh press routes through the original +12 setting.

Architect validated the explicit Session enable intent, independent of runtime active state, and the surface and LED framing. The graph retires held outputs on disable and consumes later releases. The runner publishes accepted release rows before the LED update.

Use ordinary `/status` readings before each batch and after verification. The pre-Monitor reading showed 89% weekly remaining with 1,652 credits. Fit the Monitor and its regression checks first and leave room for fixes. If the production surface path, route-off contract, time or capacity is uncertain, keep the verified Monitor and report the remaining stretch work through Strategist.

## Independent native proof

Prove one MIDI route with transpose in the existing Linux x86_64 container. The planned command, `./scripts/demo-route-transpose.sh`, builds and runs the native demo from the checkout once delivered.

- Wire the production Session/control, MIDI graph and transpose processor to one serialized in-process simulation fixture. Use one logical source, one captured destination, one route and one transpose instance. Allow two outstanding note occurrences and reserve two dedicated release credits. Capture the last 16 accepted deliveries synchronously. The fixture has no queue or clock dependency.
- Apply the route with a +12-semitone transpose through the common session command boundary and query that the route is active. The fixture verifies four cases in a fixed order:
  1. Channel 1 Note On and Note Off for input note 60 produce exactly one Note On and one matching Note Off for output note 72, in order.
  2. Input note 120 and its release produce no output because the transposed key would be out of range.
  3. Change the transpose offset from +12 to +7 while input note 60 is held. The accepted edit releases the original output note 72 at Apply. The subsequent source Note Off produces no extra release. A fresh press and release use the new setting and produce the matching pair for note 67.
  4. Submit an otherwise valid change that adds a second processor instance and exceeds the profile limit. The command boundary reports the capacity rejection. The applied configuration and its revision remain unchanged, and the existing route continues to behave as before the rejected change.
- Compare the route state and exact normalized MIDI output. Repeated runs produce the same trace, and the command exits successfully only when every case matches its expected result.
- Build portable C11 natively with the container's installed GCC and CMake. The proof needs no connected MIDI devices, firmware toolchain, browser, system MIDI services or external transport. Save the command and its output as reproducible simulation evidence.

Architect validated the shared declaration header and the native proof contract. The fixture uses route 1→2, source Note On velocity 64, explicit Note Off velocity 32 and Apply-generated release velocity 0. Verify applied revision 0 before configuration, 1 at +12 and 2 at +7. The over-limit request `[+7,+12]` leaves revision 2 and offset +7. Application Assembly owns the demo script and build wiring. Builder runs the command. Broader graph behavior, project recovery, clock scheduling and target capacity measurements are outside tonight's acceptance criteria. [Shared declarations](api/native_proof.h) · [Native proof contract](api/README.md#linux-x86_64-route-and-transpose-proof)

## Browser APC slice alongside the core

The browser surface can proceed alongside the core proof. The Web APC simulator owner must download and use Akai Professional's official Communications Protocol v1.1 before mapping controls. The miniMIDI reference directory can supplement the official document if needed. [Akai protocol v1.1](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf) · [miniMIDI references](https://github.com/NevynIt/miniMIDI/tree/main/ref)

The surface can start before the fixture runs. Browser acceptance requires playable keys and a visible **Captured MIDI output** panel connected to one runnable instance of the deterministic fixture. The instance starts with +12-semitone transpose applied through Session/control. Computer connections delivers browser key presses and releases to simulated MIDI ingress, where the production graph routes them. The local keybed path is separate from APC panel and LED traffic.

The Simulation backend owns bounded capture of normalized output after the simulated destination accepts it. Computer connections exposes an ordered, read-only view of the capture. Web APC renders the event type, channel and note number. Viewing the capture or falling behind as a reader must leave routing and release delivery unaffected.

With no USB MIDI connected, play and release the browser key for channel 1 note 60. The panel must show exactly one Note On for note 72 followed by its matching Note Off. Compare those rows with the fixture's expected output. Also verify that canceling a browser key interaction releases the held note and that disconnecting the keybed triggers ingress cleanup. The four-case native proof can be verified independently of the browser.

Add basic pad and button LED feedback when the production APC frontend and local surface connection can return feedback. Surface messages go through that frontend, and the simulator renders the feedback it receives. Verify a pad and an LED-capable button can light and clear. More extensive panel behavior and parity can follow.

## Other follow-on while hardware is unavailable

Use the CLI to apply the same route and read back state through the common session API when that API is runnable. CLI parity can proceed independently of browser work. Consider project recovery after parity, subject to approval and available capacity.

The native demo is simulation evidence from an x86_64 container. Linux MIDI integration and target acceptance require their own runtime evidence.

## Hardware work when access returns

Schedule the RP2040 bench check once the board, hub, power supply, P-95 interface, instruments and physical APC are available. Check enumeration, panel and keybed separation, feedback and stable bindings across reconnect. Measure latency, queue headroom and note cleanup on the board. The proposed standalone rehearsal is the P-95 → MicroFreak route with transpose, controlled by the physical APC with the computer disconnected. Confirm the rehearsal scope and first-release target profiles through Strategist.

Use NevynIt/rpico-bridge as the human-reported working USB reference for later firmware planning. Architect's review of `36c61af1` identifies native TinyUSB with one MIDI cable, CDC and read-only mass storage alongside PIO USB hosting. Gridpico still needs target-hardware evidence for the proposed five-pair composite layout, actual hub and power setup, reconnect behavior and MIDI while the read-only `GRIDPICO` drive is active. Keep simulation, Linux x86, Linux ARM, Windows and RP2040 build and runtime evidence distinct. Sequencer and piano control remain deferred. [Reference notes](docs/SOURCES.md)

## Approved roster and allowance

- **Five source owners** for the first proof under the current module boundaries. Session/control, MIDI graph, Processors, Application Assembly and Simulation backend.
- **Two independent verification seats** for the same proof. Bring in Testing and Builder when there is a runnable candidate to verify. Stagger these seven seats across the proof. The first proof does not require Linux HAL, RP2040 HAL, Musical time or frontend seats.
- **Parallel browser slice.** Activate Web APC simulator for the initial surface. Computer connections joins for the connected keybed and output panel once the fixture is runnable. APC frontend joins when panel and LED integration fits the remaining time and allowance. Schedule independent verification when browser integration is runnable.
- **Account usage.** Project Manager takes ordinary Codex `/status` readings around active team work, including before batching and after substantial work. Note the weekly percentage, reset and approximate active time covered by the readings. Exclude idle gaps and count overlapping work once. Use comparable readings within the same reset period and skip intervals whose active time cannot be estimated. Once there is enough movement, multiply the total percentage-point drop by 15 and divide by the total active minutes covered. Use the result as a planning estimate in account-wide weekly allowance percentage points per 15 active wall-clock minutes.
- **Pacing.** The weekly allowance moved from 90% to 89% over roughly 12 active wall-clock minutes of this delivery, with one rounded percentage-point change. The latest reading showed 1,652 credits and a 29 November 10:04 UTC reset. There are too few comparable observations to estimate a reliable rate. Keep the existing [model and effort defaults](.team/MODEL_PLAN.md) and leave room for verification and fixes. Project Manager uses the observed allowance and rate when available and brings material pacing trade-offs to Strategist. Normal interactive readings are sufficient.
