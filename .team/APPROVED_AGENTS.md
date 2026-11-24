# Approved initial agents

This roster records the initial planning seats and separately approved later seats. Session Manager keeps it aligned with the supplied approved decisions; it does not approve or police them.

| Seat | Initial creation | Role definition | Owned planning document |
| --- | --- | --- | --- |
| Session Manager | Human starts it | [Role](roles/session-manager.md) | None; roster and requested session operations |
| Strategist | Session Manager bootstrap | [Role](roles/strategist.md) | `PRODUCT_BRIEF.md` |
| Architect | Session Manager bootstrap | [Role](roles/architect.md) | `ARCHITECTURE.md` |
| Project Manager | Session Manager bootstrap | [Role](roles/project-manager.md) | Tactical execution, evidence and integration coordination |
| Product Owner | Human-approved request through Project Manager | [Role](roles/product-owner.md) | `ROADMAP.md` strategic horizon, approved outcome states and linked evidence |
| UI Designer | Human-approved request through Project Manager | [Role](roles/ui-designer.md) | `USE_CASES.md` and bounded user-journey / visual concepts |

The proposed models and reasoning effort for all seats are recorded in [MODEL_PLAN.md](MODEL_PLAN.md), owned by Project Manager and applied by Session Manager. Candidate model IDs must be checked against the installed Codex CLI.

Additional seats shown in the team diagram are proposed, not a launch list. Add approved seats only when their creation is requested through the applicable path in `AGENT_TEAM_COORDINATION.md`.

Actual Codex session IDs, pane identifiers, working directories and lifecycle status belong in an ignored `.team/local/roster.json` (or another small local roster chosen by Session Manager). Do not fabricate IDs in this file or put credentials in any roster. The native sessions themselves remain in the mounted Codex state volume.
