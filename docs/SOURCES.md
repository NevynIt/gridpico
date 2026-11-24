# Sources, provenance and decision precedence

## Product and team decisions

**S1 — Human decisions in the gridpico project discussion, 22 November 2026.** These record product and team intent: no synthesis in the kernel; future sequencing boundary; controller-independent kernel; APC simulation as the main configuration experience; a planned read-only HTML/documentation drive; RP2040/Linux x86/Linux ARM/Windows backends; Raspberry Pi as ordinary Linux deployment; four initial agents; single-owner planning documents; Session Manager as operator without enforcement; exact Codex session resumption without checkpoints; roadmap owned by Project Manager after discussion.

**S2 — Supplied reference documents:** `AGENT_TEAM_COORDINATION.md` and `CODEZERO_MUSICAL_PROJECT_OVERVIEW.md`. They were read in full. GridZero's musical interaction ideas and the original coordination agreement inform the new documents, but later explicit decisions take precedence. Original Bootstrap-only startup, enforcement duties, synthesis, Python-specific implementation and old task plans are not inherited.

Original reference documents are not copied into the active tree, to avoid presenting superseded instructions or a historical roadmap as current policy. Their names identify supplied sources, not additional reviewed repositories. Detailed Circuit/APC protocol baselines remain to be selected and pinned by the team.

The human reported successful read-only mass-storage/HTML operation in rpico-bridge. This is attributed experimental evidence, not a claim that gridpico firmware or all browser/OS combinations have been tested.

## Development tooling provenance

The inspected reference is **NevynIt/rpico-bridge at `bbd43d0fb610dd13ce2f5c42d620b42f8d79ff55`**, with commit timestamp 22 November 2026, 21:29:55 UTC.

- [Source Dockerfile](https://github.com/NevynIt/rpico-bridge/blob/bbd43d0fb610dd13ce2f5c42d620b42f8d79ff55/.devcontainer/Dockerfile), blob `379953b8b07e15363204dc0601bc82629a9a1549`.
- [Source devcontainer definition](https://github.com/NevynIt/rpico-bridge/blob/bbd43d0fb610dd13ce2f5c42d620b42f8d79ff55/.devcontainer/devcontainer.json), blob `3be93447616db458a83fd9777865f3b839cbdec9`.
- [Source license](https://github.com/NevynIt/rpico-bridge/blob/bbd43d0fb610dd13ce2f5c42d620b42f8d79ff55/LICENSE), retained in [the tooling notice](../third_party/notices/rpico-bridge-MIT.txt).

Gridpico's Dockerfile adapts that recipe, preserving dependency pins/checksums and user paths. It adds Linux MIDI headers and terminal utilities, removes the global Codex-config seed, and uses new Compose/devcontainer definitions to share the exact existing credential/session volumes while separating the checkout and Herdr state. No product code, model assignment policy or credentials were imported.

The repository contains editable Mermaid diagrams rendered in [the diagram page](diagrams/README.md). The backup ZIP additionally preserves the original architecture and latest team discussion posters as PNGs; these are visual references, not independent policy documents.

## Tooling references

**S3 — Docker named/external volumes:** [Compose volumes reference](https://docs.docker.com/reference/compose-file/volumes/). External volumes are resolved outside this Compose application's lifecycle; an explicit name avoids project-prefix substitution.

**S4 — GitHub access:** the human created `NevynIt/gridpico` with an MIT license. Publication uses the connected GitHub API; subsequent local development can use [GitHub CLI credential integration](https://cli.github.com/manual/gh_auth_git-credential). No repository-creation helper or access token is included.

**S5 — Codex state/authentication/resume:** [CLI reference](https://developers.openai.com/codex/cli/reference/), [environment variables](https://learn.chatgpt.com/docs/config-file/environment-variables), and [authentication](https://learn.chatgpt.com/docs/auth). Resume-by-ID continues the stored session; it is distinct from forking. Verify actual installed commands before lifecycle operations.

**S6 — Herdr lifecycle:** [Session state and restore](https://herdr.dev/tui/docs/session-state/) and [configuration](https://herdr.dev/tui/docs/configuration/). Detaching is distinct from stopping processes; native conversation restoration depends on supported session references/integration. The project uses `HERDR_CONFIG_PATH` and disables `resume_agents_on_restore`. Check these against the pinned installed release.

These references describe tooling, not product approval. No live Docker daemon or user hardware was available during the original starter preparation. Later product evidence and open checks are tracked in the [roadmap](../ROADMAP.md); the [validation notes](VALIDATION.md) describe the starter snapshot only.
