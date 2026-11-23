# Gridpico at a glance

Gridpico routes and transforms MIDI between existing instruments. Dashed links mark mounted-drive and physical CDC paths that remain pending. See the [planned hardware topology](HARDWARE.md).

```mermaid
flowchart LR
  subgraph browser[Chromium browser surfaces]
    keys[Keybed / panel controls]
    route[Route view<br/>original Capture + local audio]
    patch[Patch view<br/>destination-tagged Capture]
    inventory[Patch inventory]
    leds[Received APC feedback]
    monitor[Web Monitor<br/>current port / generation + channel filter]
    audio[Chromium audio signal]
    diagnostics[Offline Diagnostics]
    mock[Mock serial adapter]
  end

  staged[Host-staged HTML files]

  subgraph app[Web APC application]
    transport[Route / Patch transports]
    routeRunner[Route fixture<br/>25-key fast-play]
    patchRunner[Default Patch fixture<br/>25 keys × up to 3 outputs]
    apc[Production APC frontend]
  end

  subgraph kernel[Production kernel]
    session[Session/control]
    midiRouter[MIDI graph<br/>separate Route / Patch configurations]
  end

  subgraph cliCheck[Independent native CLI check]
    nativeCli[Native CLI harness]
    cli[Production CLI]
  end

  drive[Mounted GRIDPICO]
  cdc[Physical CDC]

  keys --> transport
  route --> transport
  patch --> transport
  transport -->|keybed / panel input| routeRunner
  transport -->|keybed / panel input| patchRunner
  routeRunner -->|panel input| apc
  patchRunner -->|panel input| apc
  apc -->|typed route / color commands| session
  session -->|route state / runtime colors| apc
  session --> midiRouter
  routeRunner -->|note input| midiRouter
  midiRouter -->|accepted Route delivery| routeRunner
  patchRunner -->|note input| midiRouter
  midiRouter -->|accepted destination deliveries| patchRunner
  session -->|Patch inventory snapshot| patchRunner
  apc -->|Route LED feedback| routeRunner
  apc -->|Patch LED feedback| patchRunner
  routeRunner -->|state-derived LEDs / original feed| transport
  patchRunner -->|state-derived LEDs / inventory| transport
  patchRunner -->|destination-tagged events / attachment state| transport
  transport --> leds
  transport -->|Patch snapshot| inventory
  transport -->|original Route feed| route
  route -->|same feed| audio
  transport -->|destination + generation-tagged events| patch
  transport -->|Monitor attachment + events| monitor
  monitor -->|own attachment + included channel| audio
  nativeCli --> cli
  cli --> session
  session -->|runtime colors| cli
  staged -->|source comparison| diagnostics
  diagnostics --> mock
  drive -. pending mounted-page access .-> diagnostics
  diagnostics -. pending real serial .-> cdc
  cdc -. pending CLI link .-> cli
```

Route keeps its original accepted-event feed for Capture and local audio. Default Patch Capture lists accepted destinations. Web Monitor plays only its current attachment and included channel. Chromium audio signal measurements do not establish physical speaker output.

## Maturity and evidence

| Status | Scope and evidence |
| --- | --- |
| **Verified · fast play** | Published Route gate `7d8889a` and Patch gate `299ec25`. The two-output profile now runs via `--two-branch`, with 25 keys, untagged Capture and silent browser audio. Default Patch with Monitor supports 25 keys × up to 3 outputs. See [gate evidence](../ROADMAP.md#web-apc-rapid-play-regression-and-next-monitor-update) and [connections startup](../connections/README.md). |
| **Verified · Web Monitor** | Independent Chromium measured a 523 Hz held Monitor tone continuing after instrument Note Off. The selector defaults to All channels and offers MIDI 1–16. Patch Capture shows accepted events from all destinations. Stale-generation Note Offs leave current voices sounding. With 25 notes held across three active destinations, the runner drained 75 releases on connection loss. This confirms browser audio signal, not physical speaker output. See [Monitor evidence](../ROADMAP.md#web-apc-rapid-play-regression-and-next-monitor-update). |
| **Verified offline · target pending** | APC↔CLI runtime colors and offline Diagnostics with mocked serial are verified. Host-staged `INDEX.HTM` and `DIAG.HTM` matched their sources byte for byte. Mounted `GRIDPICO` and physical CDC remain pending. Projects, sequencing, OS MIDI and portable Windows/Linux ARM/RP2040 coverage remain planned. See [offline evidence](../ROADMAP.md#next-approved-product-work-after-web-apc-protocol-and-timed-leds). |
