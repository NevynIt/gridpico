# gridpico — product brief / business case

**Owner:** Strategist. **Status:** initial seed from human decisions, 22 November 2026.
**Related:** [Architecture](ARCHITECTURE.md), [team coordination](AGENT_TEAM_COORDINATION.md).

## Purpose

Make an existing collection of MIDI instruments more useful together without requiring a computer during performance. Gridpico provides routing and transformations first, with Circuit-Tracks-inspired MIDI sequencing available later through the same architecture. It never produces audio itself.

The intended setup includes a Yamaha P-95, Arturia MicroFreak, Akai APC Key 25 mk2 and Novation Circuit Tracks. A computer is optional for configuration, simulation, additional MIDI applications and loopback routing.

## User experience

The APC is the preferred physical interface. Its browser simulator is the preferred graphical configuration interface, using the same APC protocol and production frontend. Additional browser tools are justified only for operations that cannot reasonably fit the APC surface.

The musical kernel is not APC-specific. A CLI and, later, piano-key/pedal controls with an RGB feedback strip operate the same commands and state. Patch and Circuit views share the same musical project.

On RP2040, USB-hosted devices connect through a hub. Native USB offers multiple MIDI ports, CDC and a read-only `GRIDPICO` drive carrying the self-contained browser interface and documentation. The user has already demonstrated the HTML-drive approach in a separate prototype.

Linux x86, Linux ARM (including Raspberry Pi Zero) and Windows run the same musical application with appropriate platform backends. They do not imitate an embedded USB device where the host OS makes that unnecessary.

## Value

The value is reuse and integration of instruments already owned: deliberate routing, useful transformations, visible control and eventual recording/pattern workflows. A common kernel makes controller experiments and hardware-free tests possible without separate products or inconsistent implementations.

This is a product rationale, not a market-size, revenue, cost-saving or performance claim. The human selected an MIT repository license. Commercial positioning, budgets and any distribution commitments remain human decisions.

## Scope boundaries

Included: a portable MIDI kernel; interchangeable frontends/backends; APC simulation; offline UI/documentation distribution; routing/processing; a preserved sequencing module boundary; explicit projects and reliable state feedback.

Excluded: synthesis, sampling, audio/DSP processing, copying Novation's sound engine, legacy compatibility, and a custom agent orchestration framework. Earlier projects are references, not code foundations. Development tooling can reuse the established container recipe.

## Planning and ownership

Strategist maintains this brief in discussion with the human. Architect maintains `ARCHITECTURE.md`. Project Manager creates and maintains `ROADMAP.md` only after discussion with the other two. All three consult one another; each file has one editor/owner.

The initial team is Session Manager, Strategist, Architect and Project Manager. No implementation agents are created merely because a candidate roster or architecture exists. Expansion follows human approval and Project Manager's lifecycle requests.
