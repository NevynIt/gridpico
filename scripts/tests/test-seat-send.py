#!/usr/bin/env python3
"""Exercise seat-send in a disposable tree with fake native services."""

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "seat-send"
STRATEGIST_THREAD = "00000000-0000-0000-0000-000000000001"
ARCHITECT_THREAD = "00000000-0000-0000-0000-000000000002"
PM_THREAD = "00000000-0000-0000-0000-000000000003"

FAKE_HERDR = f"""#!{sys.executable}
import json
from pathlib import Path
import sys

root = Path(__file__).parent
args = sys.argv[1:]
with (root / "herdr.log").open("a") as log:
    print(json.dumps(args), file=log)
if args == ["agent", "list"]:
    print(json.dumps({{"result": {{"agents": [
        {{"agent": "codex", "agent_status": "idle",
          "agent_session": {{"kind": "id", "value": "{ARCHITECT_THREAD}"}}}}
    ]}}}}))
else:
    sys.exit(2)
"""

FAKE_CODEX = f"""#!{sys.executable}
import json
from pathlib import Path
import sys

with (Path(__file__).parent / "codex.log").open("a") as log:
    print(json.dumps(sys.argv[1:]), file=log)
print("Queued message fake for thread {ARCHITECT_THREAD}.")
"""


class SeatSendTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "scripts").mkdir()
        self.script = self.root / "scripts" / SCRIPT.name
        shutil.copy2(SCRIPT, self.script)
        local = self.root / ".team" / "local"
        local.mkdir(parents=True)
        self.inbox = local / "jill-inbox.txt"
        (local / "roster.json").write_text(json.dumps({"agents": [
            {"seat": "Strategist", "alias": "strategist", "pane_id": "w:p1",
             "codex_session_id": STRATEGIST_THREAD},
            {"seat": "Architect", "alias": "architect", "pane_id": "w:p2",
             "codex_session_id": ARCHITECT_THREAD},
            {"seat": "Project Manager", "alias": "project-manager", "pane_id": "w:p3",
             "codex_session_id": PM_THREAD},
        ]}), encoding="utf-8")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        for name, source in (("herdr", FAKE_HERDR), ("codex", FAKE_CODEX)):
            path = self.bin / name
            path.write_text(source, encoding="utf-8")
            path.chmod(0o755)
        self.env = {**os.environ, "PATH": str(self.bin) + os.pathsep + os.environ["PATH"]}
        self.env.pop("HERDR_PANE_ID", None)

    def invoke(self, *args, stdin=None):
        return subprocess.run([sys.executable, str(self.script), *args], cwd=self.root,
                              env=self.env, input=stdin, capture_output=True, timeout=10)

    def jill(self, topic, *args, stdin=None):
        return self.invoke("--from", "strategist@dev", "--topic", topic, *args, stdin=stdin)

    def native_calls(self, tool):
        path = self.bin / f"{tool}.log"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def test_regular_queue_parity(self):
        self.inbox.write_bytes(b"Local sentinel")
        result = self.invoke("--from", "Project Manager", "architect", "line one\nline two")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"Queued message fake for thread {ARCHITECT_THREAD}.\n".encode())
        self.assertEqual(self.native_calls("herdr"), [["agent", "list"]])
        self.assertEqual(self.native_calls("codex"), [
            ["queue", "--thread", ARCHITECT_THREAD, "--message",
             "FROM: project-manager@dev\nline one\nline two"],
        ])
        self.assertEqual(self.inbox.read_bytes(), b"Local sentinel")

    def test_jill_options_are_local_only(self):
        self.inbox.write_bytes(b"Local sentinel")
        for options in (("--topic", "Topic"), ("--reply-needed",)):
            with self.subTest(options=options):
                result = self.invoke("--from", "strategist@dev", *options, "architect", "body")
                self.assertEqual(result.returncode, 2)
        self.assertEqual(self.native_calls("herdr"), [])
        self.assertEqual(self.native_calls("codex"), [])
        self.assertEqual(self.inbox.read_bytes(), b"Local sentinel")

    def test_reject_ineligible_sender_or_unsafe_topic(self):
        self.inbox.write_bytes(b"Local sentinel")
        for arguments in (
            ("--from", "project-manager@dev", "--topic", "Topic", "jill", "body"),
            ("--from", "strategist@dev\nother", "--topic", "Topic", "jill", "body"),
            ("--topic", "Topic", "jill", "body"),
            ("--from", "strategist@dev", "jill", "body"),
            ("--from", "strategist@dev", "--topic", "", "jill", "body"),
            ("--from", "strategist@dev", "--topic", "two\nlines", "jill", "body"),
            ("--from", "strategist@dev", "--topic", "a | b", "jill", "body"),
            ("--from", "strategist@dev", "--topic", "Topic", "Jill", "body"),
        ):
            with self.subTest(arguments=arguments):
                result = self.invoke(*arguments)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")
        self.assertEqual(self.native_calls("herdr"), [])
        self.assertEqual(self.native_calls("codex"), [])
        self.assertEqual(self.inbox.read_bytes(), b"Local sentinel")

    def seed_inbox(self, numbers=(1, 2, 3)):
        records = [
            f"[J{number:03d}] 2026-11-26 08:0{number} CET | Jill | Topic {number} | No reply needed\n"
            f"Original body {number}\n"
            "[J999] 2026-11-26 08:00 CET | Strategist | Quoted header | No reply needed\n"
            "\n------------\n"
            for number in numbers
        ]
        previous = "\n".join(records).encode("utf-8")
        self.inbox.write_bytes(previous)
        return previous

    def test_existing_numbering_utf8_and_receipt(self):
        previous = self.seed_inbox()
        # An explicit local sender needs neither a live session nor Herdr.
        self.env["HERDR_PANE_ID"] = "w:p3"
        body = "Grüße\n\nSecond line\n\n".encode()
        result = self.jill("Überblick", "--reply-needed", "jill", stdin=body)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"Appended J004 to Jill's local inbox.\n")
        entry = self.inbox.read_bytes()[len(previous):].decode()
        self.assertRegex(entry, r"^\n\[J004\] \d{4}-\d\d-\d\d \d\d:\d\d (?:CET|CEST) "
                                r"\| Strategist \| Überblick \| Reply needed\n")
        self.assertEqual(entry.split("\n", 2)[2], "Grüße\n\nSecond line\n\n------------\n")
        self.assertTrue(self.inbox.read_bytes().startswith(previous))
        self.assertEqual(self.native_calls("herdr"), [])
        self.assertEqual(self.native_calls("codex"), [])

    def test_missing_inbox_and_invalid_utf8_fail_without_receipt(self):
        result = self.jill("Topic", "jill", "body")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, b"")
        self.assertFalse(self.inbox.exists())
        self.inbox.write_bytes(b"")
        result = self.jill("Topic", "jill", stdin=b"\xff")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, b"")
        self.assertEqual(self.inbox.read_bytes(), b"")
        self.assertEqual(self.native_calls("herdr"), [])
        self.assertEqual(self.native_calls("codex"), [])

    def test_parallel_multiline_appends(self):
        previous = self.seed_inbox(range(1, 9))
        bodies = {f"Topic {i}": f"First {i}\n\n" + str(i) * 8192 + f"\nLast {i}\n"
                  for i in range(12)}

        def append(item):
            topic, body = item
            return self.jill(topic, "jill", body)

        with ThreadPoolExecutor(max_workers=8) as workers:
            replies = list(workers.map(append, bodies.items()))
        for reply in replies:
            self.assertEqual(reply.returncode, 0, reply.stderr)
        self.assertEqual({reply.stdout for reply in replies}, {
            f"Appended J{i:03d} to Jill's local inbox.\n".encode() for i in range(9, 21)
        })
        result = self.inbox.read_bytes()
        self.assertTrue(result.startswith(previous))
        entries = result[len(previous):].decode().split("\n------------\n")
        self.assertEqual(entries.pop(), "")
        self.assertEqual(len(entries), len(bodies))
        for number, entry in enumerate(entries, start=9):
            self.assertTrue(entry.startswith("\n"))
            header, body = entry[1:].split("\n", 1)
            match = re.fullmatch(
                rf"\[J{number:03d}\] \d{{4}}-\d\d-\d\d \d\d:\d\d (?:CET|CEST) "
                r"\| Strategist \| ([^|]+) \| No reply needed", header,
            )
            self.assertIsNotNone(match, header)
            self.assertEqual(body, bodies.pop(match[1]))
        self.assertEqual(bodies, {})
        self.assertEqual(self.native_calls("herdr"), [])
        self.assertEqual(self.native_calls("codex"), [])

    def test_empty_existing_inbox_starts_at_one(self):
        self.inbox.write_bytes(b"")
        result = self.jill("Initial", "jill", "Body without newline")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"Appended J001 to Jill's local inbox.\n")
        leading, header, body = self.inbox.read_text(encoding="utf-8").split("\n", 2)
        self.assertEqual(leading, "")
        self.assertTrue(header.startswith("[J001] "))
        self.assertTrue(header.endswith(" | Strategist | Initial | No reply needed"))
        self.assertEqual(body, "Body without newline\n\n------------\n")

    def test_berlin_dst_boundaries(self):
        header = runpy.run_path(str(self.script))["jill_header"]
        for when, local in (
            (datetime(2026, 3, 29, 0, 59, tzinfo=timezone.utc), "2026-03-29 01:59 CET"),
            (datetime(2026, 3, 29, 1, 0, tzinfo=timezone.utc), "2026-03-29 03:00 CEST"),
            (datetime(2026, 10, 25, 0, 59, tzinfo=timezone.utc), "2026-10-25 02:59 CEST"),
            (datetime(2026, 10, 25, 1, 0, tzinfo=timezone.utc), "2026-10-25 02:00 CET"),
        ):
            with self.subTest(utc=when):
                self.assertEqual(header(4, "Strategist", "Topic", False, when),
                                 f"[J004] {local} | Strategist | Topic | No reply needed")


if __name__ == "__main__":
    unittest.main()
