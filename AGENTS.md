# gridpico — instructions for every agent

Read `AGENT_TEAM_COORDINATION.md` and your explicitly assigned role. Product intent is in `PRODUCT_BRIEF.md` and technical boundaries are in `ARCHITECTURE.md`. Product Owner maintains `ROADMAP.md`. Project Manager coordinates delivery and sends verified milestones and dependencies to Product Owner.

Work on planning, documentation, or implementation requires explicit authorization. The initial team consists of Session Manager, Strategist, Architect and Project Manager. Additional seats require human approval through Strategist and a creation request from Project Manager.

Use your original persistent Codex session. Do not spawn subagents, fork replacements, add checkpoint/handover procedures or introduce a custom orchestrator. Session Manager performs requested session operations; it does not enforce anything or assign work.

Read the whole repository as useful; edit only your assigned paths. Architect owns public contracts, module owners own implementations, Testing verifies, Builder executes builds, Documentation explains. Area leads advise/review and are not extra managers. All agents can inspect Git; Project Manager is the regular committer/pusher by convention.

UI Designer owns `USE_CASES.md` and the associated visual designs. Discuss major UI changes and new workflows with a visual concept before asking for approval. A single sheet can show several key states or steps across web and physical controls. Use ImageGen for visual exploration and have UI Designer review UI choices before implementation. Testing independently verifies usability and the intended visual hierarchy. Aim for a good, usable implementation. Ask UI Designer about uncertain decisions first and escalate unresolved product choices to the human through Strategist.

Only Strategist initiates human communication. Direct peer clarification is allowed. Answer a brief question from the human when addressed, but route broader decisions through Strategist. Do not treat discussion as authorization.

Start every message to another agent with `FROM: seat@location` on its own line, followed by the message. Use your own seat name and location label, for example `FROM: strategist@dev`.

Send questions, replies, decisions, blockers, and completion reports directly to the intended seat. Use `./scripts/seat-send <seat> '<message>'` for routine queued messages. If the queue is unavailable, send through Herdr once the recipient is idle. Use immediate steering during an active turn only when delaying would materially affect the work. Include the result or question and links to relevant files or evidence so the recipient can respond without checking another pane.

Do not import another project's product code or legacy compatibility constraints. The kernel and firmware handle MIDI. Approved browser and standalone instrument peers generate audio on their receiving hosts. Never inspect, print, commit, replace or delete raw credential material. Use the configured authentication tools without exposing their secrets. Shared credential/session volumes and the other project's configuration are not project source files. No blanket cleanup/reset or automatic commit is authorized.
