# Approved initial agents

This initial roster authorizes planning/session setup only. Session Manager keeps it aligned with the supplied approved decisions; it does not approve or police them.

| Seat | Initial creation | Role definition | Owned planning document |
| --- | --- | --- | --- |
| Session Manager | Human starts it | [Role](roles/session-manager.md) | None; roster and requested session operations |
| Strategist | Session Manager bootstrap | [Role](roles/strategist.md) | `PRODUCT_BRIEF.md` |
| Architect | Session Manager bootstrap | [Role](roles/architect.md) | `ARCHITECTURE.md` |
| Project Manager | Session Manager bootstrap | [Role](roles/project-manager.md) | `ROADMAP.md` (created after the planning discussion) |

The proposed models and reasoning effort for all seats are recorded in [MODEL_PLAN.md](MODEL_PLAN.md), owned by Project Manager and applied by Session Manager. Candidate model IDs must be checked against the installed Codex CLI.

Additional seats shown in the team diagram are proposed, not a launch list. Add approved seats only when their creation is requested through the applicable path in `AGENT_TEAM_COORDINATION.md`.

Actual Codex session IDs, pane identifiers, working directories and lifecycle status belong in an ignored `.team/local/roster.json` (or another small local roster chosen by Session Manager). Do not fabricate IDs in this file or put credentials in any roster. The native sessions themselves remain in the mounted Codex state volume.
