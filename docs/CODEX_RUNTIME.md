# Codex runtime for the M5 development container

## Approved startup decision

**2026-11-22: The human explicitly approved Docker-only isolation for gridpico.** On the M5, Codex's nested Bubblewrap sandbox failed with `bwrap: Failed to make / slave: Permission denied`; trying `use_legacy_landlock` produced the same error. Do not require either nested sandbox for this deployment.

The approved Codex launch flags for **Session Manager itself and every agent it operates** are:

```sh
--sandbox danger-full-access --ask-for-approval never
```

This avoids Codex's inner Linux sandbox and routine approval prompts. **It does not remove Docker's restrictions** (unprivileged user `pico`, dropped Linux capabilities, `no-new-privileges`, no Docker socket mount). But agents have unrestricted access **inside the container** to the writable checkout, internet (if Docker provides it), and the mounted `pico-dev-codex` and `pico-dev-gh` authentication/session volumes. Agent prompts and ownership agreements are not filesystem security boundaries. Do not run untrusted instructions or expose additional host mounts.

This choice is deliberate and limited to this environment. It is **not** an instruction to run unrestricted Codex on the M5 host or to change `~/.codex/config.toml`, which is shared with another project.

## How to restart the experimental bootstrap

The human permitted discarding the just-created experimental sessions. Stop those running Codex processes through Herdr/their terminals first so duplicate named agents do not run; do not delete any credential or session volumes.

From the M5 host checkout:

```sh
git pull
./scripts/herdr.sh
```

Within a shell pane in Herdr:

```sh
bash scripts/start-session-manager.sh
```

This script starts only Session Manager with the approved flags and the proposed `gpt-6-luna`/low-effort setting. Its initial task is to create Strategist, Architect and Project Manager using the **same Docker-only flags**, and their entries in [MODEL_PLAN.md](../.team/MODEL_PLAN.md). Verify the installed Codex model IDs; if unavailable, seek a replacement through Project Manager/the human rather than silently changing the model.

If you prefer a one-off manual command in the shell pane:

```sh
codex --sandbox danger-full-access --ask-for-approval never \
  -m gpt-6-luna -c 'model_reasoning_effort="low"' \
  "Read AGENTS.md and .team/roles/session-manager.md. You are Session Manager. Bootstrap only the three planning agents using docs/CODEX_RUNTIME.md and .team/MODEL_PLAN.md."
```

The Session Manager should determine the installed Herdr launch syntax using `herdr --help` / `herdr agent start --help`. Where supported, pass the agent flags **after** Herdr's argument delimiter, for example:

```sh
herdr agent start strategist --kind codex --pane <PANE_ID> -- \
  --sandbox danger-full-access --ask-for-approval never \
  -m gpt-6.2-sol -c 'model_reasoning_effort="high"'
```

Check availability of that model; apply the exact role-specific settings for Architect and Project Manager. This is an example, not permission to make extra seats. Verify that each agent actually runs with the intended settings.

## Ongoing sessions and model choices

Normal later resume always targets the exact recorded Codex session ID, including if the model or effort changes. Do not fork, replace, replay prompts or add checkpoint requirements. This one-time discard of failed bootstrap sessions is an exception.

[MODEL_PLAN.md](../.team/MODEL_PLAN.md) is owned by Project Manager and records initial candidates for every proposed seat. Session Manager is an operator, not a resource-budget enforcer or a model-policy designer.

## Ask about a seat's saved history

From the shared development container, pass a seat alias and one quoted question.

```sh
./scripts/seat-ask architect 'What is the approved onboard status LED pin?'
```

The helper resolves the recorded native session through Session Manager's roster and queries its saved history with one read-only, ephemeral Codex invocation. The original seat receives no message and can continue working. The answer may omit work still in progress. If the session cannot be loaded or the read-only mode cannot be verified, the helper fails without printing an answer. It accepts no override flags.

## If the Docker-only arrangement needs revisiting

See `scripts/check-codex-sandbox.sh` for a Bubblewrap diagnostic. Nested sandbox mode can be reconsidered if the host environment changes, but this has **not** been fixed or tested on the M5. The Docker-only mode has been **configured in scripts/documentation, not independently executed on the server** by the assistant.
