#!/usr/bin/env python3
"""Exercise the worker with a fake queue in a disposable repository."""

from contextlib import contextmanager
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "seat-stall-check"
FAKE_SEND = """#!/usr/bin/env python3
import json
from pathlib import Path
import sys
import time

root = Path(__file__).parent
calls = root / "calls"
number = len(calls.read_text().splitlines()) + 1 if calls.exists() else 1
with calls.open("a") as log:
    print(json.dumps({"args": sys.argv[1:], "time": time.monotonic()}), file=log)
mode = (root / "mode").read_text()
if mode == "timeout":
    time.sleep(30)
if mode == "failure":
    sys.exit(3)
print(f"Queued message 00000000-0000-0000-0000-{number:012x} for thread fake.")
"""
EXPECTED = [
    "--from", "stall-check@dev", "project-manager",
    "Stall check reminder. Verify live seat activity and the current allowance. "
    "If approved work is stalled, check blockers and start the next runnable task in order, "
    "then confirm the owner has begun.",
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

    def attempts(self):
        path = self.scripts / "calls"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def calls(self):
        return [attempt["args"] for attempt in self.attempts()]

    def mode(self, mode):
        (self.scripts / "mode").write_text(mode)

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
    def test_single_worker_recurs_and_disable(self):
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
            self.assertEqual(first["last_outcome"]["state"], "queued")
            second = lab.wait_for(lambda state: state["last_send"] != first["last_send"])
            self.assertEqual(second["last_outcome"]["state"], "queued")
            self.assertEqual(lab.calls(), [EXPECTED, EXPECTED])
            first_attempt, second_attempt = lab.attempts()
            self.assertGreaterEqual(second_attempt["time"] - first_attempt["time"], 0.70)

            stopped = lab.run("disable")
            self.assertFalse(stopped["enabled"])
            self.assertFalse(stopped["worker_running"])
            self.assertEqual(stopped["last_outcome"], second["last_outcome"])
            self.assertEqual(stopped["last_send"], second["last_send"])
            time.sleep(1.2)
            self.assertEqual(lab.calls(), [EXPECTED, EXPECTED])

            restarted_at = time.monotonic()
            lab.run("enable")
            self.assertEqual(lab.calls(), [EXPECTED, EXPECTED])
            lab.wait_for(lambda state: state["last_send"] != second["last_send"])
            self.assertEqual(lab.calls(), [EXPECTED, EXPECTED, EXPECTED])
            self.assertGreaterEqual(lab.attempts()[-1]["time"] - restarted_at, 0.95)

    def test_failed_and_timed_out_attempts_recover_on_schedule(self):
        for mode, reason in (("failure", "no queue receipt (exit 3)"), ("timeout", "timed out")):
            with self.subTest(mode=mode), sandbox(mode) as lab:
                lab.run("enable", "--interval", "1")
                state = lab.wait_for(lambda status: status["last_outcome"] is not None)
                self.assertEqual(state["last_outcome"]["state"], "uncertain")
                self.assertIn(reason, state["last_outcome"]["reason"])
                self.assertIsNone(state["last_send"])
                self.assertIsNotNone(state["last_attempt"])
                lab.mode("success")
                self.assertEqual(lab.calls(), [EXPECTED])
                recovered = lab.wait_for(lambda status: status["last_send"] is not None)
                self.assertEqual(recovered["last_outcome"]["state"], "queued")
                self.assertEqual(lab.calls(), [EXPECTED, EXPECTED])
                first_attempt, second_attempt = lab.attempts()
                minimum = 0.95 if mode == "timeout" else 0.70
                self.assertGreaterEqual(second_attempt["time"] - first_attempt["time"], minimum)

    def test_slow_send_keeps_cadence_and_skips_missed_ticks(self):
        serve = runpy.run_path(str(SCRIPT))["serve"]
        now = 0
        second = 1_000_000_000
        attempts = []
        state = {"enabled": True, "schedule": 1, "interval_seconds": 4,
                 "last_outcome": None, "last_attempt": None, "last_send": None}

        @contextmanager
        def fake_lock(_):
            yield None

        def advance(seconds):
            nonlocal now
            now += int(seconds * second)
            self.assertLess(now, 40 * second)

        def queue_once(_):
            attempts.append(now)
            advance((2, 10, 0)[len(attempts) - 1])
            if len(attempts) == 3:
                state["enabled"] = False
            return {"state": "uncertain", "reason": "mocked send"}

        clock = SimpleNamespace(monotonic=lambda: now / second,
                                monotonic_ns=lambda: now, sleep=advance)
        with patch.dict(serve.__globals__, time=clock, lock=fake_lock, read_state=lambda: state,
                        write_state=lambda _: None, queue_once=queue_once):
            serve()
        self.assertEqual(attempts, [4 * second, 8 * second, 20 * second])

    def test_explicit_restart_migrates_legacy_attempt_and_recurs(self):
        with sandbox() as lab:
            lab.run("status")
            lab.state.write_text(json.dumps({
                "enabled": True, "interval_seconds": 1, "schedule": 7,
                "last_attempt": "2026-01-01T00:00:00+00:00", "last_send": None,
                "outstanding": {"state": "attempting", "attempted_at": "2026-01-01T00:00:00+00:00"},
            }))
            self.assertFalse(lab.run("status")["worker_running"])
            restarted_at = time.monotonic()
            lab.run("enable")
            state = lab.wait_for(lambda status: status["last_outcome"]["state"] == "uncertain")
            self.assertIn("worker ended", state["last_outcome"]["reason"])
            self.assertNotIn("outstanding", state)
            self.assertEqual(lab.calls(), [])
            lab.wait_for(lambda status: status["last_send"] is not None)
            self.assertEqual(lab.calls(), [EXPECTED])
            self.assertGreaterEqual(lab.attempts()[0]["time"] - restarted_at, 0.95)

    def test_worker_death_during_send_recovers_at_next_interval(self):
        with sandbox("timeout") as lab:
            lab.run("status")
            lab.state.write_text(json.dumps({
                "enabled": True, "interval_seconds": 1, "schedule": 1,
                "last_attempt": None, "last_send": None, "last_outcome": None,
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
            self.assertEqual(stopped["last_outcome"]["state"], "attempting")
            lab.mode("success")
            restarted_at = time.monotonic()
            lab.run("enable")
            self.assertEqual(lab.calls(), [EXPECTED])
            lab.wait_for(lambda status: status["last_outcome"]["state"] == "uncertain")
            lab.wait_for(lambda status: status["last_send"] is not None)
            self.assertEqual(lab.calls(), [EXPECTED, EXPECTED])
            self.assertGreaterEqual(lab.attempts()[-1]["time"] - restarted_at, 0.95)


if __name__ == "__main__":
    unittest.main(verbosity=2)
