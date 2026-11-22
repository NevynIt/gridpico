#!/usr/bin/env python3
"""Validate the initial starter, not later product releases. Python 3.11+."""
from __future__ import annotations
import json
from pathlib import Path
import re
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
errors: list[str] = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


required = ["ARCHITECTURE.md", "AGENT_TEAM_COORDINATION.md", "PRODUCT_BRIEF.md",
            "AGENTS.md", "README.md", "LICENSE", ".devcontainer/Dockerfile",
            ".devcontainer/compose.yaml", ".devcontainer/devcontainer.json",
            ".team/APPROVED_AGENTS.md", "docs/SETUP.md", "docs/SOURCES.md"]
for name in required:
    require((ROOT / name).is_file(), f"Missing {name}")
# These assertions describe this initial snapshot. Update after approved expansion.
require(not (ROOT / "ROADMAP.md").exists(), "Initial roadmap is left to Project Manager")
try:
    config = json.loads((ROOT / ".devcontainer/devcontainer.json").read_text())
    require(config["service"] == "dev", "Unexpected development service")
    require(config["workspaceFolder"] == "/workspace/gridpico", "Unexpected workspace")
    herdr = tomllib.loads((ROOT / ".team/herdr.toml").read_text())
    require(herdr["session"]["resume_agents_on_restore"] is False,
            "Do not enable automatic agent restoration")
except (OSError, ValueError, KeyError) as exc:
    errors.append(f"Configuration error: {exc}")
compose = (ROOT / ".devcontainer/compose.yaml").read_text()
for name in ("pico-dev-codex", "pico-dev-gh"):
    require(f"name: {name}" in compose, f"Missing shared named volume {name}")
require(compose.count("external: true") == 2, "Credential volumes must be external")
require("privileged: true" not in compose, "Unexpected privileged container")
roles = sorted(p.stem for p in (ROOT / ".team/roles").glob("*.md"))
require(roles == ["architect", "project-manager", "session-manager", "strategist"],
        "Only four initial role definitions should be present")
for source in ROOT.rglob("*.md"):
    if ".git" in source.parts:
        continue
    text = source.read_text()
    require(text.count("```") % 2 == 0, f"Unclosed code fence: {source.relative_to(ROOT)}")
    for link in re.findall(r"!?\[[^\]]*\]\(([^)]+)\)", text):
        target = link.split("#", 1)[0].split("?", 1)[0]
        if not target or re.match(r"[a-zA-Z][\w+.-]*:", target):
            continue
        require((source.parent / target).exists(),
                f"Broken link in {source.relative_to(ROOT)}: {link}")
for script in (ROOT / "tools").glob("*.sh"):
    result = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True)
    require(result.returncode == 0, f"Shell syntax error: {script.name}: {result.stderr}")
diagram_page = (ROOT / "docs/diagrams/README.md").read_text()
blocks = re.findall(r"```mermaid\n(.*?)```", diagram_page, re.S)
for name, block in zip(("architecture.mmd", "agent-team.mmd"), blocks):
    require((ROOT / "docs/diagrams" / name).read_text().strip() == block.strip(),
            f"Diagram source differs from embedded copy: {name}")
require(len(blocks) == 2, "Expected two editable diagrams")
for image in (ROOT / "docs/diagrams").glob("*.png"):
    require(image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), f"Invalid PNG: {image.name}")
if errors:
    print("Starter validation failed:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    raise SystemExit(1)
print("PASS: startup documents, relative links, role count, JSON/TOML, shared-volume declarations and diagram consistency")
print("NOT TESTED HERE: Docker image build, Herdr/Codex sessions, hardware or product behavior")
