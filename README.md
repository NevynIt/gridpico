# gridpico

**A portable MIDI workstation kernel: routing and transformations now, a preserved path to sequencing later. No synthesis or audio engine.**

Gridpico connects existing instruments through interchangeable controllers and platform backends. RP2040 is the compact hardware deployment; Linux x86, Linux ARM (including Raspberry Pi Zero) and Windows run the same musical application through their own services.

This repository starts with design documents, diagrams and development tooling. It does **not** contain product firmware, a working simulator or an implementation roadmap.

## Start here

| Document | Owner | Purpose |
| --- | --- | --- |
| [Product brief](PRODUCT_BRIEF.md) | Strategist | Product intent, scope and business case |
| [Architecture](ARCHITECTURE.md) | Architect | Modules, connections, portability and future sequencer boundary |
| [Agent coordination](AGENT_TEAM_COORDINATION.md) | Human authority; Strategist records approved changes | Initial team, responsibilities, communication and session operations |
| [Container and first-run setup](docs/SETUP.md) | Application Assembly when created | Development environment, shared credentials and planning-team startup |
| [Diagrams](docs/diagrams/README.md) | Respective document owners | Visual module/team maps and editable Mermaid sources |

**`ROADMAP.md` is deliberately absent.** Project Manager creates it after discussing the product and architecture with Strategist and Architect.

## Initial team

The human starts **Session Manager**, which creates **Strategist, Architect and Project Manager**. No other agent is created until the human approves expansion and Project Manager requests it. The proposed full team has 23 seats, created progressively rather than all at once.

Session Manager keeps the approved list and original Codex session IDs and performs requested create/start/pause/resume operations. It is not an enforcer or supervisor. Resumption uses the exact original session, with no checkpoint/reconstruction workflow.

```mermaid
flowchart TB
  human["Human"] -->|"Starts first"| sm["Session Manager"]
  sm -.->|"Bootstrap: creates"| st["Strategist<br/>PRODUCT_BRIEF.md"]
  sm -.->|"Bootstrap: creates"| ar["Architect<br/>ARCHITECTURE.md"]
  sm -.->|"Bootstrap: creates"| pm["Project Manager<br/>ROADMAP.md — later"]
  human <-->|"Discussion / approval"| st
  st <--> ar
  st <--> pm
  ar <--> pm
  pm -->|"Requested lifecycle operations"| sm
  pm -->|"Assignments after approval"| later["Later module owners + independent support<br/>19 proposed seats; created progressively"]
```

[Open the complete team and architecture diagrams](docs/diagrams/README.md).

In the complete team diagram, grouped frames contain multiple agents; each numbered card is one agent. Lead badges are extra duties, not extra seats. The [coordination agreement](AGENT_TEAM_COORDINATION.md) governs operational details and the initial bootstrap exception.

## Development container

Use a **new gridpico container and checkout** on the same Docker daemon as the existing rpico-bridge environment. The provided recipe is adapted from that environment, while reusing these exact external named volumes:

- `pico-dev-codex` → `/home/pico/.codex`
- `pico-dev-gh` → `/home/pico/.config/gh`

No credentials are copied into this repository or image. The mounted Codex directory also contains existing configuration and session history; the setup does not overwrite it. Herdr runtime state uses a separate `gridpico-herdr` volume.

From this directory on the Docker host (for example, the M5):

```sh
./scripts/build.sh   # Build/update the development image
./scripts/start.sh   # Start the container (without rebuilding)
./scripts/herdr.sh   # Open the interactive Herdr TUI inside it
```

On the M5 the nested sandbox is known to fail; [Docker-only Codex execution](docs/CODEX_RUNTIME.md) is explicitly approved. Inside a shell pane in Herdr, run `bash scripts/start-session-manager.sh`. Agent models and effort levels are listed in [MODEL_PLAN.md](.team/MODEL_PLAN.md). The Herdr script only opens the TUI; it does **not** create/start agents. First-run authentication inside the container is available with `docker compose -f .devcontainer/compose.yaml exec dev bash` followed by `bash .devcontainer/sign-in.sh`. All scripts resolve the repository directory from their own location, so they can be run from another working directory.

VS Code's **Reopen in Container** uses the same Compose definition. See [setup](docs/SETUP.md) for authentication checks and the first Session Manager prompt. No setup command automatically launches agents.

## Launch the web UI

With the checkout's Compose `dev` service already running, run this from the Docker host or WSL with Python 3 and Docker Compose:

```sh
./scripts/web-ui.sh
```

The launcher opens [http://127.0.0.1:8767/](http://127.0.0.1:8767/) when a desktop opener is available and always prints the link. The home links to Web APC, Diagnostics, and the FM Instrument. The launcher builds the portable production router and installs the locked browser host dependencies in `dev`. Press Ctrl+C to stop the host and loopback relay. The legacy one-pad demo is still available through `./scripts/web-apc.sh` on port 8765.

Open Web APC to play the keybed. Open Diagnostics to see the MIDI Log and enable Monitor audio. Each destination receives new notes as soon as its port connects.

To stage the offline home and tools on the build host, configure with `cmake -S . -B build/web-ui` and run `cmake --build build/web-ui --target gridpico_offline_assets`. Open `build/web-ui/GRIDPICO/HOME.HTM` directly. The tools include the shared browser MIDI code, and Diagnostics can still use Web Serial. `VERSION.TXT` records SHA-256 checksums for each staged page.

## Get the source

```sh
git clone https://github.com/NevynIt/gridpico.git
cd gridpico
```

Use a separate checkout and the same Docker daemon as rpico-bridge. The container commands above are run from this checkout on the Docker host. No additional repository-creation script is needed.

## Scope and provenance

APC is the preferred interface, not the identity of the kernel. Its web simulator exercises the production APC frontend; CLI and future piano controls use the same musical commands. RP2040 includes a read-only USB drive for static HTML/documentation. Desktop deployments do not emulate a USB disk or CDC when they do not need them.

Earlier projects are design references and experiments only. No product code is imported. The development-tooling attribution is in [Sources](docs/SOURCES.md) and the corresponding [notice](third_party/notices/rpico-bridge-MIT.txt). The repository uses the [MIT license](LICENSE) selected when the human created it.
