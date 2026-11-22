# Approved initial agents

This initial roster authorizes planning/session setup only. Session Manager keeps it aligned with the supplied approved decisions; it does not approve or police them.

| Seat | Initial creation | Role definition | Owned planning document |
| --- | --- | --- | --- |
| Session Manager | Human starts it | [Role](roles/session-manager.md) | None; roster and requested session operations |
| Strategist | Session Manager bootstrap | [Role](roles/strategist.md) | `PRODUCT_BRIEF.md` |
| Architect | Session Manager bootstrap | [Role](roles/architect.md) | `ARCHITECTURE.md` |
| Project Manager | Session Manager bootstrap | [Role](roles/project-manager.md) | `ROADMAP.md`, to be created after discussion |

The 19 additional seats in the diagram are the proposed later roster, not a launch list. Add approved seats only when their creation is requested by Project Manager.

Actual Codex session IDs, pane identifiers, working directories and lifecycle status belong in an ignored `.team/local/roster.json` (or another small local roster chosen by Session Manager). Do not fabricate IDs in this file or put credentials in any roster. The native sessions themselves remain in the mounted Codex state volume.
