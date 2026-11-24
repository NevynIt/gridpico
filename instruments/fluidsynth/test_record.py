"""Local process tests for the private recorder. Requires no ALSA device or SoundFont."""

import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import unittest
import wave


MODULE_DIR = Path(__file__).resolve().parent
FAKE_FLUID = """
import os
from pathlib import Path
import sys
import time
import wave

args = sys.argv[1:]
path = Path(next(arg.split("=", 1)[1] for arg in args if arg.startswith("audio.file.name=")))
name = next(arg.split("=", 1)[1] for arg in args if arg.startswith("midi.portname="))
assert f"midi.alsa_seq.id={name}" in args
assert "synth.midi-channels=16" in args
# Stock FluidSynth publishes the explicit midi.portname verbatim to ALSA.
marker = Path(os.environ["FIXTURE_ENGINE"])
marker.write_text(f"{os.getpid()}\\n{name}\\n")
if os.environ.get("FIXTURE_WAV") == "oversize":
    deadline = time.monotonic() + 4
    while not marker.with_suffix(".oversize").exists() and time.monotonic() < deadline:
        time.sleep(.01)
    with path.open("r+b") as output:
        output.truncate(12 * 1024 * 1024 + 1)
assert sys.stdin.buffer.readline() == b"quit\\n"
if os.environ.get("FIXTURE_WAV") == "truncated":
    path.write_bytes(b"RIFF" + (100).to_bytes(4, "little") + b"WAVEfmt ")
else:
    with wave.open(str(path), "wb") as wav:
        wav.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
        wav.writeframes(b"\\x01\\x00\\x01\\x00" * 32)
if os.environ.get("FIXTURE_WAV") == "unclean":
    sys.exit(1)
"""
FAKE_SENDER = """
import os
from pathlib import Path
import sys
import time

engine = Path(os.environ["FIXTURE_ENGINE"])
for _ in range(200):
    if engine.exists() and engine.read_text():
        break
    time.sleep(.01)
pid, alsa_port = engine.read_text().splitlines()
assert sys.argv[1:] == [pid, alsa_port]
os.kill(int(sys.argv[1]), 0)
print("ready", flush=True)
with open(os.environ["FIXTURE_MIDI"], "wb", buffering=0) as log:
    for command in sys.stdin.buffer:
        log.write(command)
        if os.environ.get("FIXTURE_ALSA") == "lose":
            print("error UNAVAILABLE", flush=True)
            sys.exit(1)
        print("ok", flush=True)
"""


class RecorderTest(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="gridpico-record-test-")
        self.addCleanup(folder.cleanup)
        self.directory = Path(folder.name)
        self.take = self.directory / "take"
        self.take.mkdir(mode=0o700)
        self.font = self.directory / "test.sf2"
        self.font.write_bytes(b"RIFF\x00\x00\x00\x00sfbk")
        self.fluid = self.executable("fluidsynth", FAKE_FLUID)
        self.sender = self.executable("sender", FAKE_SENDER)
        self.wav = self.take / "recording.wav"
        self.midi = self.directory / "midi.log"
        self.engine = self.directory / "engine.txt"

    def executable(self, name, body):
        path = self.directory / name
        path.write_text(f"#!{sys.executable}\n{body}")
        path.chmod(0o700)
        return path

    def start(self, *, short_take=False, **extra_env):
        env = {**os.environ, **extra_env, "PATH": f"{self.directory}:{os.environ['PATH']}",
               "PYTHONPATH": str(MODULE_DIR), "FIXTURE_ENGINE": str(self.engine),
               "FIXTURE_MIDI": str(self.midi)}
        entry = "import record, sys; record.TAIL_SECONDS = .05; sys.exit(record.main())"
        if short_take:
            entry = entry.replace("sys.exit", "record.TAKE_SECONDS = .3; sys.exit")
        self.process = subprocess.Popen(
            [sys.executable, "-B", "-c", entry, "--soundfont", self.font,
             "--alsa-sender", self.sender, "--work-dir", self.take],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            bufsize=0, env=env, start_new_session=True,
        )
        self.addCleanup(self.close_process)

    def close_process(self):
        if self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGKILL)
            self.process.wait(timeout=2)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()

    def expect(self, expected):
        self.assertTrue(select.select([self.process.stdout], [], [], 4)[0], expected)
        self.assertEqual(self.process.stdout.readline(), expected + b"\n")

    def write(self, *lines):
        self.process.stdin.write(b"\n".join(lines) + b"\n")

    def attach(self):
        self.expect(b"ready")
        self.assertEqual(self.wav.stat().st_size, 0)
        self.write(b"attached")
        self.expect(b"recording")

    def stop(self):
        self.write(b"stop")
        self.expect(b"closing")
        self.assertEqual(self.wav.stat().st_size, 0)
        self.write(b"closed")

    def assert_reaped(self, status):
        self.assertEqual(self.process.wait(timeout=4), status)
        pid = int(self.engine.read_text().split()[0])
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)

    def test_scoped_releases_and_completed_wav(self):
        self.assertEqual(list(self.take.iterdir()), [])
        self.start()
        self.attach()
        self.write(b"midi 903C64", b"midi 903C60", b"midi 913C64",
                   b"midi 823C00", b"midi 803C2A", b"stop", b"midi 903D64")
        self.expect(b"closing")
        self.write(b"stop", b"closed", b"midi 813C00")
        self.expect(b"complete")
        self.assert_reaped(0)
        self.assertEqual(self.midi.read_bytes().splitlines(), [
            b"midi 903c64", b"midi 903c60", b"midi 913c64", b"midi 803c2a",
            b"midi 803c00", b"midi 813c00",
        ])
        with wave.open(str(self.wav), "rb") as wav:
            self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()),
                             (2, 2, 44100))
            self.assertEqual(len(wav.readframes(32)), 128)

    def test_timeout_closes_with_held_note(self):
        self.start(short_take=True)
        self.attach()
        self.write(b"midi 903C64")
        self.expect(b"closing")
        self.write(b"closed")
        self.expect(b"complete")
        self.assert_reaped(0)
        self.assertEqual(self.midi.read_bytes(), b"midi 903c64\nmidi 803c00\n")

    def test_truncated_wav_fails(self):
        self.start(FIXTURE_WAV="truncated")
        self.attach()
        self.stop()
        self.expect(b"error WAV")
        self.assert_reaped(1)
        self.assertFalse(self.wav.exists())

    def test_unclean_engine_exit_discards_valid_wav(self):
        self.start(FIXTURE_WAV="unclean")
        self.attach()
        self.stop()
        self.expect(b"error ENGINE_EXIT")
        self.assert_reaped(1)
        self.assertFalse(self.wav.exists())

    def test_oversize_idle_take_discards_only_partial(self):
        latest = self.directory / "latest.wav"
        with wave.open(str(latest), "wb") as wav:
            wav.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
            wav.writeframes(b"\x01\x00\x01\x00" * 32)
        previous = latest.read_bytes()
        self.start(FIXTURE_WAV="oversize")
        self.attach()
        self.engine.with_suffix(".oversize").touch()
        self.expect(b"error WAV_LIMIT")
        self.assert_reaped(1)
        self.assertFalse(self.wav.exists())
        self.assertEqual(latest.read_bytes(), previous)

    def test_release_drain_at_occurrence_limit(self):
        self.start()
        self.attach()
        self.write(*(f"midi 90{key:02x}64".encode() for key in range(50)))
        self.stop()
        self.expect(b"complete")
        self.assert_reaped(0)
        self.assertEqual(self.midi.read_bytes().splitlines()[50:],
                         [f"midi 80{key:02x}00".encode() for key in range(50)])

    def test_alsa_loss_discards_candidate(self):
        self.start(FIXTURE_ALSA="lose")
        self.attach()
        self.write(b"midi 903C64")
        self.expect(b"error ALSA_LOST")
        self.assert_reaped(1)
        self.assertFalse(self.wav.exists())

    def test_socket_loss_before_attachment(self):
        self.start()
        self.expect(b"ready")
        self.write(b"fault")
        self.expect(b"error SOCKET_LOST")
        self.assert_reaped(1)
        self.assertFalse(self.wav.exists())

    def test_missing_close_confirmation_discards_candidate(self):
        self.start()
        self.attach()
        self.write(b"stop")
        self.expect(b"closing")
        self.expect(b"error CLOSE_FAILED")
        self.assert_reaped(1)
        self.assertFalse(self.wav.exists())

    def test_host_signal_discards_candidate(self):
        self.start()
        self.attach()
        self.process.send_signal(signal.SIGTERM)
        self.expect(b"error CANCELLED")
        self.assert_reaped(1)
        self.assertFalse(self.wav.exists())


if __name__ == "__main__":
    unittest.main()
