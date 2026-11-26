#!/usr/bin/env python3
"""Exercise the worker with a fake queue in a disposable repository."""

from contextlib import contextmanager
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "seat-stall-check"
FAKE_SEND = """#!/usr/bin/env python3
import json
from pathlib import Path
import sys
import time

root = Path(__file__).parent
with (root / "calls").open("a") as log:
    print(json.dumps(sys.argv[1:]), file=log)
mode = (root / "mode").read_text()
if mode == "timeout":
    time.sleep(30)
if mode == "failure":
    sys.exit(3)
print("Queued message 00000000-0000-0000-0000-000000000001 for thread fake.")
"""
EXPECTED = [
    "--from", "stall-check@dev", "project-manager",
    "Stall check reminder. Review the project backlog when available. "
    "After receiving this reminder, run ./scripts/seat-stall-check ack to allow the next one.",
]


class Sandbox:
    def __init__(self, root, mode):
        self.scripts = root / "scripts"
        self.scripts.mkdir()
        self.script = self.scripts / SCRIPT.name
        # Shorten only the copied supervisor deadline for this host test.
        self.script.write_text(SCRIPT.read_text().replace("SEND_TIMEOUT = 15", "SEND_TIMEOUT = 1"))
        fake = self.scripts / "seat-send"
        fake.write_text(FAKE_SEND)
        fake.chmod(0o755)
        (self.scripts / "mode").write_text(mode)
        self.state = root / ".team/local/stall-check/state.json"

    def command(self, *args):
        return [sys.executable, str(self.script), *args]

    def run(self, *args):
        output = subprocess.check_output(self.command(*args), text=True, timeout=8)
        return json.loads(output)

    def calls(self):
        path = self.scripts / "calls"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def wait_for(self, predicate):
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            status = self.run("status")
            if predicate(status):
                return status
            time.sleep(0.05)
        raise AssertionError(f"condition not met, last status {status!r}, calls {self.calls()!r}")


@contextmanager
def sandbox(mode="success"):
    with tempfile.TemporaryDirectory() as path:
        lab = Sandbox(Path(path), mode)
        try:
            yield lab
        finally:
            lab.run("disable")


class StallCheckTests(unittest.TestCase):
    def test_single_worker_ack_and_disable(self):
        with sandbox() as lab:
            status = lab.run("status")
            self.assertEqual(status["interval_seconds"], 1800)
            self.assertFalse(status["worker_running"])
            self.assertFalse(status["enabled"])
            self.assertEqual(lab.calls(), [])
            enables = [subprocess.Popen(lab.command("enable", "--interval", "1"),
                                        stdout=subprocess.PIPE, text=True) for _ in range(2)]
            for command in enables:
                output, _ = command.communicate(timeout=8)
                self.assertEqual(command.returncode, 0)
                self.assertTrue(json.loads(output)["worker_running"])
            self.assertEqual(lab.calls(), [])
            first = lab.wait_for(lambda state: state["last_send"] is not None)
            self.assertEqual(first["outstanding"]["state"], "queued")
            time.sleep(1.3)
            self.assertEqual(lab.calls(), [EXPECTED])

            stopped = lab.run("disable")
            self.assertFalse(stopped["enabled"])
            self.assertFalse(stopped["worker_running"])
            self.assertEqual(stopped["outstanding"], first["outstanding"])
            self.assertEqual(stopped["last_send"], first["last_send"])
            time.sleep(1.2)
            self.assertEqual(lab.calls(), [EXPECTED])

            lab.run("enable")
            time.sleep(1.2)
            self.assertEqual(lab.calls(), [EXPECTED])
            self.assertIsNone(lab.run("ack")["outstanding"])
            self.assertEqual(lab.calls(), [EXPECTED])
            lab.wait_for(lambda state: len(lab.calls()) == 2 and state["outstanding"] is not None)
            self.assertEqual(lab.calls(), [EXPECTED, EXPECTED])

    def test_failed_and_timed_out_attempts_hold_slot(self):
        for mode, reason in (("failure", "no queue receipt (exit 3)"), ("timeout", "timed out")):
            with self.subTest(mode=mode), sandbox(mode) as lab:
                lab.run("enable", "--interval", "1")
                state = lab.wait_for(lambda status: status["outstanding"] is not None)
                self.assertEqual(state["outstanding"]["state"], "uncertain")
                self.assertIn(reason, state["outstanding"]["reason"])
                self.assertIsNone(state["last_send"])
                self.assertIsNotNone(state["last_attempt"])
                time.sleep(1.2)
                self.assertEqual(lab.calls(), [EXPECTED])

    def test_explicit_restart_keeps_uncertain_reservation(self):
        with sandbox() as lab:
            lab.run("status")
            lab.state.write_text(json.dumps({
                "enabled": True, "interval_seconds": 1, "schedule": 7,
                "last_attempt": "2026-01-01T00:00:00+00:00", "last_send": None,
                "outstanding": {"state": "attempting", "attempted_at": "2026-01-01T00:00:00+00:00"},
            }))
            self.assertFalse(lab.run("status")["worker_running"])
            lab.run("enable")
            state = lab.wait_for(lambda status: status["outstanding"]["state"] == "uncertain")
            self.assertIn("worker ended", state["outstanding"]["reason"])
            time.sleep(1.2)
            self.assertEqual(lab.calls(), [])

    def test_worker_death_during_send_does_not_repeat(self):
        with sandbox("timeout") as lab:
            lab.run("status")
            lab.state.write_text(json.dumps({
                "enabled": True, "interval_seconds": 1, "schedule": 1,
                "last_attempt": None, "last_send": None, "outstanding": None,
            }))
            with subprocess.Popen(lab.command("_worker")) as worker:
                deadline = time.monotonic() + 5
                while not lab.calls() and time.monotonic() < deadline:
                    time.sleep(0.02)
                self.assertEqual(lab.calls(), [EXPECTED])
                worker.kill()
                worker.wait(timeout=3)
            stopped = lab.run("disable")
            self.assertFalse(stopped["worker_running"])
            self.assertEqual(stopped["outstanding"]["state"], "attempting")
            lab.run("enable")
            lab.wait_for(lambda status: status["outstanding"]["state"] == "uncertain")
            time.sleep(1.2)
            self.assertEqual(lab.calls(), [EXPECTED])


if __name__ == "__main__":
    unittest.main(verbosity=2)
