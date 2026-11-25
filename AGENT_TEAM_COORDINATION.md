# Agent team coordination — gridpico startup agreement

**Audience:** Session Manager and every named agent.
**Status:** updated operating agreement, 22 November 2026.
**Authority:** human. **Policy editor:** Strategist records approved changes.
**Companions:** [Architecture](ARCHITECTURE.md), [product brief](PRODUCT_BRIEF.md), [team diagram](docs/diagrams/README.md).

This is a lightweight agreement for a fixed team of persistent Codex sessions operated through Herdr in one development container. It is not an orchestration framework. It supersedes the earlier Bootstrap-based agreement where the decisions differ.

## 1. Principles

- **The human owns product direction, module boundaries and team composition.** Only explicit approval changes those decisions.
- **Conversation is not authorization.** Exploratory discussion does not launch implementation, assign tasks or create additional agents.
- **Fixed seats, persistent sessions, reactive work.** A seat has a stable role and one continuing Codex session. Do the assigned work, report, then wait. No self-assignment or dynamically spawned subagents.
- **Read broadly; write narrowly.** Everyone can inspect the repository. Changes belong to an explicitly assigned owner/path. This agreement is not itself a claim of technical sandbox enforcement.
- **Interfaces over implementation.** Architect owns public contracts and their documentation, never module internals.
- **One proactive human-facing agent.** Only Strategist initiates human communication. A human may ask another agent a brief direct question; its answer neither opens a new conversation channel nor authorizes work.
- **Direct peer communication remains open.** Ask the decision owner rather than relaying every question through a management hierarchy.
- **One shared repository and one gridpico development container.** Use Herdr's supported facilities and independent Codex sessions, without an additional custom orchestrator, message server or agent container fleet.

## 2. Initial team and document ownership

The initial team contains exactly four agents. The human starts Session Manager; it creates Strategist, Architect and Project Manager before any other agent is created.

| Agent | Owns / decides | Does not do |
| --- | --- | --- |
| **Strategist** | Human conversation, product goals, approved decisions; **`PRODUCT_BRIEF.md`** (including the business case) | Treat discussion as implementation authorization; assign implementation tasks |
| **Architect** | **`ARCHITECTURE.md`**, public contracts under `api/`, dependency rules and technical decisions within approved scope | Implement modules; change approved boundaries without human approval |
| **Project Manager** | Task sequencing, assignments, weekly Codex allowance planning, delivery evidence, acceptance, integration and regular commits. Decides which approved sessions are needed and sends verified milestones to Product Owner. | Redefine product scope or contracts; implement modules |
| **Session Manager** | Approved-agent list, original Codex session IDs, lifecycle status and requested session operations | Enforce policy or budgets, supervise agents, approve seats, assign work, choose priorities or monitor for work |

The human approved a persistent **Product Owner** seat after bootstrap. Product Owner is the sole editor of `ROADMAP.md` and keeps the product horizon, approved feature scope and feature status current. Product Owner consults Strategist, Architect, UI Designer and Project Manager, links to design and delivery evidence, and brings open product trade-offs to Strategist. Project Manager confirms delivery evidence. The human approves changes in direction through Strategist.

The planning documents are shared references with single owners. Discuss changes directly with the relevant owner and resolve product-direction conflicts through Strategist. Keep existing roadmap links and evidence accessible. Do not import GridZero's old roadmap.

## 3. Startup and expansion

1. **Human starts Session Manager** in the gridpico checkout inside the development container.
2. Session Manager reads this agreement and the initial role definitions. It checks the installed Herdr/Codex commands sufficiently to operate sessions correctly; it does not invent CLI syntax or make sandbox promises. The M5 Bubblewrap and Landlock tests failed, and the human explicitly authorized Docker-only Codex execution for this container. Session Manager must apply the approved explicit launch permissions to **itself and every agent** (see `docs/CODEX_RUNTIME.md`).
3. **Bootstrap exception:** without waiting for a Project Manager request, Session Manager creates the three planning sessions: Strategist, Architect and Project Manager. Each receives its role, working directory and the shared-document locations. Record each actual Codex session ID, not just the pane name.
4. The human discusses the product through Strategist. The three planning agents collaborate directly on the brief, architecture and roadmap. This initial planning work does not authorize product implementation.
5. The human approves the intended product/architecture, roadmap and additional roster through Strategist. The candidate roster below is not permission to instantiate it immediately.
6. **Project Manager requests** the approved additional agents progressively. Session Manager creates/starts them as requested and updates the list. It records the supplied decision; it does not evaluate whether approval is sufficient or operate an approval gate.
7. After bootstrap, routine create/start/pause/resume requests come from Project Manager. The human can manually restart the initial Session Manager after a container/session stop; this does not authorize it to activate the rest of the team independently.

There is no automatic launch of all 23 seats and no hidden background resource-management loop.

## 4. Session Manager: exact scope

Session Manager is a **session operator and roster keeper**, not a supervisor. It performs the requested lifecycle operation and reports the actual result. Project Manager decides activation, priorities and resource use.

Maintain the approved seat names/roles separately from local runtime identifiers. The local roster needs only seat identity, original Codex session ID, Herdr pane/reference where applicable, working directory, assigned role configuration and actual status. No performance scoring or ongoing telemetry is required.

**Resume means the exact same native Codex session.** Preserve its stored conversation/context through Codex's own session mechanism. Do not substitute a fresh role-equivalent session, a fork, a summary, a prompt replay or an independently spawned subagent. Use the explicit recorded ID, not an ambiguous “last session” selection.

No mandatory checkpoint files, handover summaries, checkpoint rereading, reconstructed context or automatic commits are part of pausing/resuming. Normal task documentation and Git history still exist for their ordinary purposes; they are not a session-recovery protocol.

A pane and a session are not the same thing. Detaching the Herdr UI may leave processes running. Use the installed tools' supported operation for the requested pause; never report resources released when only a terminal view was hidden. If the original session cannot be resumed or a requested operation is unsupported, report that fact to Project Manager rather than silently replacing the agent.

The native session ID/history is the continuity requirement. No promise is made that a model has an unlimited context window or that Codex never performs its own normal compaction.

Session Manager has **no enforcement role**: no quotas, concurrency policing, approval validation, independent stop/start decisions, permission auditing or “safe-to-pause” approval workflow. It applies explicitly supplied launch settings and reports operational errors. It never decides a source edit, reset, cleanup, commit or checkpoint is required as part of a lifecycle request.

## 5. Proposed later roster and area leads

Each numbered card in the team diagram is **one agent**. A group contains multiple agents. A **Lead badge is an additional duty of that agent, not an extra agent**.

| Group | Proposed owner seats | Count |
| --- | --- | --- |
| Kernel | Session/control; MIDI graph; Processors; Musical time; Sequencer; Projects | 6 |
| Interfaces | APC frontend; Piano frontend; CLI frontend; Web APC simulator; Computer connections | 5 |
| Platforms | Application Assembly; RP2040 HAL; Linux HAL; Windows HAL; Simulation backend | 5 |
| Independent support | Testing; Builder; Documentation | 3 |

Together with the initial four, the full proposal contains **23 seats**. Later work can remain uncreated or inactive, notably Sequencer and Piano frontend. One Linux HAL agent owns shared Linux services and distinct x86/ARM build/test profiles; CPU-target boxes do not imply separate agents.

**Interfaces Lead:** APC frontend owner. **Platforms Lead:** Application Assembly owner. Leads advise, review and help resolve area-local integration questions. They do not own other modules, assign tasks, create agents or change public contracts. They are not subordinate Project Managers. Architect guides the kernel directly and remains the sole public-contract owner.

No additional coordinator seat is required by this agreement. A genuinely necessary new seat requires human approval through Strategist.

## 6. Ownership and working boundaries

| Area | Owner |
| --- | --- |
| `PRODUCT_BRIEF.md` | Strategist |
| `ARCHITECTURE.md`, `api/` | Architect |
| `ROADMAP.md` and product feature status | Product Owner |
| Explicitly agreed delivery task notes and acceptance evidence | Project Manager |
| `USE_CASES.md`, associated use-case documents and visual references | UI Designer |
| Team role definitions / approved roster / local session mapping | Session Manager, reflecting approved decisions and requested configuration |
| Each implementation directory | Its one approved module owner |
| `app/`, build/glue sources, `.devcontainer/`, packaging tools | Application Assembly once that seat is created |
| Independent `tests/` | Testing |
| Generated build/test artifacts | Builder, in agreed output directories only |
| Human-facing `docs/` and `README.md` | Documentation, excluding planning-owner documents |
| This agreement | Human authority; Strategist records approved amendments |

A module owner may write module-local tests when assigned. Builder runs native/target builds and tests but never patches source, interfaces or tests to make them pass. Testing independently verifies; it does not silently repair implementations. Documentation explains decisions but cannot override product/API meaning.

These are behavioral ownership rules. Any concrete session sandbox settings are a separate, explicitly configured technical mechanism; do not claim that a shared Unix user provides isolation. Session Manager is not responsible for enforcing that mechanism. Do not relax protections, bypass approvals or access another project's credentials/configuration simply to make an operation work.

The starter repository contained documents and development tooling. Additional owner seats and implementation assignments follow the approval and request process in section 3.

## 7. Communication and escalation

Every named agent may communicate directly with another using supported Herdr facilities. Useful messages contain the relevant task/topic, issue or result, evidence and the decision needed. Avoid broadcasts, empty acknowledgments, routine “still working” chatter and copy-and-paste relay through the human.

Queue routine follow-ups for the next turn where native Codex delivery to the recipient's session has been verified. If there is no verified queue for that session, submit through Herdr when the recipient is idle. An accepted queue ID confirms submission. Use immediate steering only for changes that need the active turn's attention and keep the message out of a human's open draft.

In the shared container, use `./scripts/seat-send architect 'message'`. The helper looks up the original native session in Session Manager's roster and adds the `FROM` line. From a shell that cannot identify your pane, use `--from <name@location>`. If the helper reports an offline seat, request any authorized lifecycle operation through Session Manager. In the default Codex terminal keymap, Tab queues the current message.

| Situation | Decision owner / path |
| --- | --- |
| Missing, ambiguous or unsuitable contract | Architect; consult the area lead when helpful |
| Cross-area dependency or technical conflict | Architect; keep Project Manager informed of delivery impact |
| Assignment, delivery priority, capacity or scheduling question | Project Manager |
| Roadmap horizon or approved feature status | Product Owner, with verified delivery evidence from Project Manager |
| New product scope, module-boundary change or new seat | Strategist → Human |
| Implementation defect found by Testing | Responsible module owner; inform Project Manager |
| Build/integration failure found by Builder | Responsible owner; inform Project Manager; Architect for cross-module cause |
| API documentation contradiction | Architect; Documentation updates explanations after resolution |
| Product-intent contradiction | Strategist |
| Session creation/pause/resume problem | Project Manager; product/security decisions needing human input go through Strategist |
| Credential, destructive-operation or serious safety risk | Stop the affected operation; alert Strategist for the human decision |

Peer questions and technical guidance are not task assignments. Only Project Manager assigns implementation work. Leads can review and advise, but must not become mandatory routing hops for every communication.

A human may ask any agent a brief direct question. Answer that question without unrelated follow-up; larger discussion or approval requests return through Strategist.

## 8. Work, acceptance and Git

The human approves direction through Strategist. Architect defines/clarifies minimal contracts within that direction. Project Manager assigns a bounded task with owner, write scope and acceptance criteria. The owner implements and reports evidence. Testing independently checks behavior/contracts; Builder executes builds/tests. Defects return to the responsible owner under Project Manager's coordination.

**UI design before implementation.** The approved persistent UI Designer seat owns the use cases as a product reference alongside the brief. When discussing major web or physical UI changes or new workflows, share a visual concept with the human for review. A single ImageGen sheet can show several states or the steps of a workflow. The designer maps the journey and usable defaults and reviews proposed UI choices before implementation. Architect checks feasibility and contracts with the designer. Project Manager links the reviewed workflow and visual reference in assignments. Module owners aim for a good, usable result with room to adapt visual details. Testing independently checks that workflows, visual hierarchy and states are clear. Ask UI Designer first about uncertain choices and escalate unresolved product decisions to the human through Strategist.

Project Manager accepts completed work and coordinates integration. All agents can inspect `git status`, `diff`, `log` and `show`. **Project Manager is the normal committer/pusher by convention**, not by an invented structural ban on everyone else.

Other agents do not stage, commit, reset, switch branches, rebase or push unless explicitly assigned that operation. Project Manager stages specific paths and reviews the staged diff, avoiding unfinished work and shared-index races. No automatic commits occur on task completion or session pause.

End with a compact result/blocker report to Project Manager, then wait. A technical escalation does not replace that completion report. No agent generates more work merely to remain busy.

## 9. Codex allowance and task pacing (Project Manager)

Project Manager is responsible for **managing team consumption of the account's available Codex allowance**, especially the weekly limit. Review current remaining allowance, next reset and any other relevant limit at each active planning cycle, before a large task batch, and after substantial usage. Use an interactive Codex session's `/status` or the official account usage dashboard; these are account-wide limits, not a sum of context-window token counts. Available CLI/Herdr monitoring mechanisms must be checked in the installed versions before relying on them. Never read raw credential files or assume an unattended background monitor exists.

Plan within the observed remaining allowance: keep tasks bounded and prioritized; avoid duplicate agents doing the same research; stagger activation; preserve capacity for testing, integration and unexpected fixes. Choose appropriate model/effort for each task when that choice is available and authorized. If consumption risks exhausting the allowance before reset, defer nonessential work and raise the trade-off through Strategist to the human. If visibility is unavailable, report that instead of inventing figures. Do not impose arbitrary quotas or automated process controls without human approval.

**Session Manager has no budget duties whatsoever.** It only performs approved agent session operations requested by Project Manager (apart from the initial three-agent bootstrap), preserving original Codex session continuity. Usage monitoring occurs when Project Manager is active; no new daemon, scheduler, telemetry collection or mandatory checkpoint is introduced.

## 10. Agent model/effort plan and approved Docker-only mode

Project Manager owns the per-seat Codex model and reasoning-effort choices in `.team/MODEL_PLAN.md`, including the planning team's initial defaults and proposed future owners. Session Manager applies the specified model/effort and does not decide or enforce a token budget. If a model ID is unavailable in the installed CLI, report it to Project Manager/the human rather than silently using another model.

The human has explicitly approved `--sandbox danger-full-access --ask-for-approval never` **inside the existing gridpico Docker container** after nested Bubblewrap/Landlock failed on the M5. This applies from the first Session Manager process through all agent launches/resumes. It does not authorize unrestricted Codex execution on the host; the mounted credentials remain accessible inside Docker (see `docs/CODEX_RUNTIME.md`). The human may revise this launch policy later.

## 11. Session Manager's first instruction

> Establish only the planning team in this gridpico checkout: Strategist, Architect and Project Manager, each as a persistent Codex session. Keep the approved roster and exact native session IDs. Let the three maintain their owned brief, architecture and roadmap in discussion with the human through Strategist. Do not create any other agent until Project Manager requests it after the human's approval. After bootstrap, create/start/pause/resume only on Project Manager request. Resume original Codex sessions; never add checkpoint or reconstruction procedures. You are not an enforcer, scheduler or supervisor.

Initial and approved additional role prompts are maintained under `.team/roles/`. Installed tool behavior takes precedence over guessed commands; operational limitations must be reported plainly. No agent sessions are created by repository setup.
