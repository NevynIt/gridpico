# Codex launch modes and sandbox diagnostics

## Why this exists

The gridpico development container has a *Docker* isolation boundary, while Codex normally tries to create another Linux sandbox for tool commands. When that inner sandbox cannot start (for example, a Bubblewrap/user-namespace/AppArmor or container restriction), Codex retries harmless commands without the sandbox and asks for authorization repeatedly. The issue is not fixed by saying "do not ask again" to a series of `cat` or `git status` requests.

The original startup instructions did not test the inner sandbox before launching the planning agents. Correct that **before new seats are created**; keep all existing native Codex sessions.

## 1. Check the installed runtime

Run on the **Docker host**, in the gridpico checkout:

```sh
./scripts/check-codex-sandbox.sh
```

The script runs a harmless read-only command using `codex sandbox linux` inside the existing Compose container. It does not read credentials, change permissions, rebuild the container or start any agent. A failure needs investigation; the precise cause cannot be determined solely from Codex approval prompts.

If the check fails, inspect the *first actual sandbox error*, the host/container user-namespace availability, and AppArmor/Docker restrictions. Ubuntu hosts may require the distribution's Bubblewrap AppArmor profile. Prefer a targeted host fix, following the OpenAI sandbox documentation, rather than indiscriminately disabling AppArmor or adding Docker `SYS_ADMIN` capability. Do not automate host security changes in this repository.

Relevant documentation:

- https://learn.chatgpt.com/docs/agent-approvals-security
- https://learn.chatgpt.com/docs/sandboxing
- https://github.com/herdrdev/herdr/blob/master/skills/herdr/SKILL.md

## 2. Verified, preferred agent launch

If the sandbox test succeeds, Session Manager should supply **explicit native Codex arguments** after the Herdr `--` separator. For example, in a newly created shell pane:

```sh
herdr agent start strategist --kind codex --pane <PANE_ID> -- --sandbox workspace-write --ask-for-approval on-request
```

Then prompt that named agent with its documented role instructions. Use the same launch permission policy for Architect and Project Manager unless the human explicitly approves a narrower or different mode. Do not rely on the global `~/.codex/config.toml` (it is shared with another project), and do not create new sessions as a workaround for sandbox failures.

This launches with workspace writes and approvals for accesses requiring escalation. The Session Manager must separately verify whether its own workspace sandbox permits Herdr control; Herdr uses local sockets/process control, which might not be available in a restricted sandbox even when ordinary file reads work. If blocked, report the limitation rather than repeatedly prompting.

## 3. Optional container-trust fallback: human decision required

OpenAI documents an alternative for an **already isolated and trusted development container** whose nested Linux sandbox cannot operate:

```sh
codex --sandbox danger-full-access --ask-for-approval on-request
```

That uses the Docker boundary without an additional Codex filesystem/network sandbox. It should eliminate the sandbox-failure retries; remaining policy prompts may still occur. A fully non-interactive `--ask-for-approval never` option would suppress those prompts but raises the risk further.

**Do not enable either full-access mode silently.** In this gridpico container, the Codex and GitHub credential volumes are mounted into every agent's filesystem, and the container may have network access. No inner Codex sandbox means agent commands can read those credentials and other container-accessible data. This applies even if the repository ownership agreement says agents must stay within assigned paths. Only the human can approve the trade-off, ideally after removing secrets from the agents' execution environment. Do not change the shared global Codex config.

Once explicitly approved, Session Manager passes the chosen arguments through Herdr on every new agent start/resume, documenting the decision in project-local team settings; it is still an operator, not an enforcer.

## 4. Retain the existing planning team's sessions

If the four initial Codex agents are already open, **record their exact session IDs** (check Codex `/status` and Herdr's identity facilities). Do not delete them, fork them, clear history or use `--last`. After diagnosing and correcting the environment or receiving human approval for another launch mode:

1. Exit the affected running Codex process only after obtaining its session ID. Do not delete the Herdr workspace or Docker named credential/session volumes.
2. Resume the **same session** by ID using `codex resume <SESSION_ID>` with the chosen supported sandbox/approval overrides. In Herdr use `agent start ... -- resume <SESSION_ID> [flags]` only after verifying the installed version's `--help` and available pane/agent state.
3. Check an ordinary file read, a permitted workspace write, and the relevant Herdr operation. Once verified, continue the current discussion without recreating a seat.

Native Codex resume preserves the existing conversation; it does not provide an infinite context window. No checkpoint/handover files are required. **A change to Docker's runtime configuration may require recreating the container, which terminates running processes, so ensure you have the native session IDs first.**

## 5. The coordination distinction

Session Manager is responsible for applying tested and human-approved **launch configuration**, not defining security policy or enforcing permissions. Project Manager schedules agents and manages usage. No source ownership rule is treated as technical sandbox isolation merely because it appears in a prompt.
