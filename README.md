# gridpico

**A portable MIDI workstation for routing and transforming existing instruments, with sequencing planned for later. The production kernel and Pico firmware remain MIDI-only; optional instruments run as independent peers.**

Gridpico connects existing instruments through interchangeable controllers and platform backends. Linux x86_64 and Raspberry Pi arm64 paths have verified evidence. Windows, broader Linux ARM coverage and the RP2040 USB/CDC hardware path remain open.

The repository includes the production MIDI router, Web APC and Diagnostics tools, CLI, offline browser assets and separate instrument peers. See the [roadmap](ROADMAP.md) for verified scope and remaining gates.

## Start here

| Document | Owner | Purpose |
| --- | --- | --- |
| [Product brief](PRODUCT_BRIEF.md) | Strategist | Product intent, scope and business case |
| [Architecture](ARCHITECTURE.md) | Architect | Modules, connections, portability and future sequencer boundary |
| [Roadmap](ROADMAP.md) | Project Manager | Current delivery, evidence and pending work |
| [Agent coordination](AGENT_TEAM_COORDINATION.md) and [agent policy](AGENTS.md) | Human authority; Strategist records approved changes | Team boundaries, ownership and operating policy |
| [Container and first-run setup](docs/SETUP.md) | Documentation | Development environment and startup guidance |
| [At a glance](docs/AT_A_GLANCE.md) and [other documentation](docs/) | Documentation | User-facing overview, setup, validation and hardware references |
| [Diagrams](docs/diagrams/README.md) | Documentation, with Architect and Session Manager review | Visual architecture/team maps and editable Mermaid sources |

## Documentation owners

| Paths | Owner |
| --- | --- |
| `README.md`, `docs/**` | Documentation |
| `PRODUCT_BRIEF.md`, approved policy in `AGENTS.md` and `AGENT_TEAM_COORDINATION.md` | Strategist |
| `ARCHITECTURE.md`, `api/**` | Architect |
| `ROADMAP.md`, `.team/MODEL_PLAN.md` | Project Manager |
| `.team/APPROVED_AGENTS.md`, `.team/roles/**` | Session Manager, reflecting approved decisions |
| `connections/README.md` | Computer Connections |
| `web/apc/README.md` | Web APC |
| `frontends/cli/README.md` | CLI Frontend |
| `instruments/fluidsynth/README.md` | Application Assembly |

`LICENSE` and repository configuration/data have their own purposes and are outside the documentation owner map.

## Team operations

Session Manager operates the approved persistent sessions. Additional seats require human approval through Strategist and a Project Manager request, except for the approved Research workflow.

Session Manager keeps the approved list and original Codex session IDs and performs requested create/start/pause/resume operations. It is not an enforcer or supervisor. Resumption uses the exact original session, with no checkpoint/reconstruction workflow.

See the [complete team and architecture diagrams](docs/diagrams/README.md). Grouped frames contain multiple agents; each numbered card is one agent. Lead badges are extra duties, not extra seats. The [coordination agreement](AGENT_TEAM_COORDINATION.md) governs startup and operating details.

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

Open Web APC to play the keybed. Open Diagnostics separately for the MIDI Log and Monitor. Each managed route becomes active when the Web APC keybed and that route’s receiving port are connected. Enable Monitor audio with a user gesture. The launcher also links to the separate FM Instrument. See the [connection startup guide](connections/README.md#local-router).

To stage the offline home and tools on the build host, configure with `cmake -S . -B build/web-ui` and run `cmake --build build/web-ui --target gridpico_offline_assets`. Open `build/web-ui/GRIDPICO/HOME.HTM` directly. Its relative links open `INDEX.HTM`, `DIAG.HTM`, and `FM.HTM`. The manifest checksums all four HTML files. The three tool pages inline the shared MIDI provider. Diagnostics can use Web Serial when available. A mounted Pico drive and real CDC are still pending.

## Get the source

```sh
git clone https://github.com/NevynIt/gridpico.git
cd gridpico
```

Use a separate checkout and the same Docker daemon as rpico-bridge. The container commands above are run from this checkout on the Docker host. No additional repository-creation script is needed.

## Scope and provenance

APC is the preferred interface, not the identity of the kernel. Its Web APC controller exercises the production APC frontend; CLI and future piano controls use the same musical commands. The RP2040 target design includes a read-only USB drive for static HTML/documentation, but Gridpico's mounted-drive, USB and CDC paths remain pending hardware validation. Desktop deployments do not emulate a USB disk or CDC when they do not need them.

Earlier projects are design references and experiments only. No product code is imported. The development-tooling attribution is in [Sources](docs/SOURCES.md) and the corresponding [notice](third_party/notices/rpico-bridge-MIT.txt). The repository uses the [MIT license](LICENSE) selected when the human created it.
