# Starter validation

These checks apply to the documentation/tooling starter, not product acceptance.

## Preparation checks

- Read the supplied reference documents and subsequent product/team decisions.
- Inspected rpico-bridge tooling and license at the pinned commit recorded in Sources.
- Checked relative links, four initial role definitions, absence of `ROADMAP.md`, JSON/TOML parsing, shell syntax and exact external credential-volume names.
- Checked that both Mermaid sources match the embedded diagrams.
- Parsed Compose YAML; checked the selected service, bind mount and runtime volume names.
- Preserved the human-created repository's MIT license; no credentials or session data are included.

## Not tested here

- Docker image build, Compose startup or actual mounting of the user's volumes.
- Real Codex/Herdr session creation, pause or exact-session resumption.
- Firmware, MIDI/USB mass storage, desktop runtime, browser UI or product timing.

Repository publication is separate from local checks; Git history records the published snapshot. This repository contains no product implementation yet. The user-reported mass-storage success belongs to rpico-bridge, not a new gridpico test.

The starter checker's four-seat/no-roadmap assertions describe this initial snapshot, not a permanent constraint. Update or retire them as approved planning progresses.
