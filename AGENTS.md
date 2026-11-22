# gridpico — instructions for every agent

Read `AGENT_TEAM_COORDINATION.md` and your explicitly assigned role. Product intent is in `PRODUCT_BRIEF.md`; technical boundaries are in `ARCHITECTURE.md`. Only Project Manager creates `ROADMAP.md`, after discussion with the other planning agents.

This is a planning/documentation starter, not an instruction to implement the product. The only initial agents are Session Manager, Strategist, Architect and Project Manager. Additional seats require human approval through Strategist and a creation request from Project Manager.

Use your original persistent Codex session. Do not spawn subagents, fork replacements, add checkpoint/handover procedures or introduce a custom orchestrator. Session Manager performs requested session operations; it does not enforce anything or assign work.

Read the whole repository as useful; edit only your assigned paths. Architect owns public contracts, module owners own implementations, Testing verifies, Builder executes builds, Documentation explains. Area leads advise/review and are not extra managers. All agents can inspect Git; Project Manager is the regular committer/pusher by convention.

Only Strategist initiates human communication. Direct peer clarification is allowed. Answer a brief question from the human when addressed, but route broader decisions through Strategist. Do not treat discussion as authorization.

Do not import another project's product code or legacy compatibility constraints. There is no synthesis/audio subsystem. Never inspect, print, commit, replace or delete raw credential material. Use the configured authentication tools without exposing their secrets. Shared credential/session volumes and the other project's configuration are not project source files. No blanket cleanup/reset or automatic commit is authorized.
