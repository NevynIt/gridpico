# Codex model and reasoning-effort plan

**Owner:** Project Manager. **Session operator:** Session Manager. These are suggested starting assignments and can be revised as budget and tasks change. This file is the canonical team model plan; no additional agents are implied by listing future seats.

The model IDs below are candidates previously seen in the `rpico-bridge` development configuration, **not a guarantee** that the installed Codex CLI/account accepts them. Session Manager verifies support before launching and asks Project Manager for another model rather than silently falling back.

| Seat | Codex model ID | Effort | Stage |
| --- | --- | --- | --- |
| Session Manager | `gpt-6-luna` | low | Bootstrap |
| Strategist | `gpt-6.2-sol` | high | Bootstrap |
| Architect | `gpt-6.2-sol` | high | Bootstrap |
| Project Manager | `gpt-6.2-sol` | medium | Bootstrap |
| Kernel: Session/control | `gpt-6.2-sol` | high | Later |
| Kernel: MIDI graph | `gpt-6.2-sol` | high | Later |
| Kernel: Processors | `gpt-6.2-sol` | medium | Later |
| Kernel: Musical time | `gpt-6.2-sol` | high | Later |
| Kernel: Sequencer | `gpt-6.2-sol` | high | Deferred |
| Kernel: Projects | `gpt-6.2-sol` | medium | Later |
| Interfaces: APC frontend | `gpt-6.2-sol` | medium | Later |
| Interfaces: Piano frontend | `gpt-6.2-sol` | medium | Deferred |
| Interfaces: CLI frontend | `gpt-6-luna` | medium | Later |
| Interfaces: Web APC simulator | `gpt-6.2-sol` | medium | Later |
| Interfaces: Computer connections | `gpt-6.2-sol` | medium | Later |
| Platforms: Application Assembly | `gpt-6.2-sol` | high | Later |
| Platforms: RP2040 HAL | `gpt-6.2-sol` | high | Later |
| Platforms: Linux HAL (x86 and ARM) | `gpt-6.2-sol` | medium | Later |
| Platforms: Windows HAL | `gpt-6.2-sol` | medium | Later |
| Platforms: Simulation backend | `gpt-6.2-sol` | medium | Later |
| Testing | `gpt-6.2-sol` | medium | Later |
| Builder | `gpt-6-luna` | low | Later |
| Documentation | `gpt-6-luna` | medium | Later |

## How the model plan is used

- Project Manager chooses model, effort, and which approved seats should run, considering current weekly Codex usage and the complexity of assigned work.
- Session Manager only applies those choices. During the initial three-seat bootstrap, the entries above are its pre-approved defaults. If a model is unavailable, it reports back rather than inventing a substitute.
- Launch flags: `-m MODEL_ID -c 'model_reasoning_effort="medium"'`, changing the effort as specified for that seat.
- In **this Docker deployment** every Codex seat uses the approved `--sandbox danger-full-access --ask-for-approval never` flags described in [Codex runtime](../docs/CODEX_RUNTIME.md), unless the human explicitly revises the policy.
- Normal pause/resume keeps the exact original native Codex session ID; changing the model or effort does not license replacing a session.
- Budget control is planning, not enforcement: Session Manager never sets quotas or monitors consumption.
