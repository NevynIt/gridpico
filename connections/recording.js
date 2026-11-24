"use strict";

const fs = require("node:fs");
const path = require("node:path");
const net = require("node:net");
const { randomBytes } = require("node:crypto");
const { spawn } = require("node:child_process");
const { WebSocket } = require("ws");

const recorderPort = 12;
const maxWavBytes = 12 * 1024 * 1024;
const startupMs = 10000;
const recordingMs = 60000;
const stopMs = 8000;
const maxControlBytes = 8192;
const helper = path.join(__dirname, "../instruments/fluidsynth/record.py");

function readConfig(filename) {
  const info = fs.statSync(filename);
  if (!info.isFile() || info.size > 4096) throw new Error("Expected a small recorder configuration file");
  const config = JSON.parse(fs.readFileSync(filename, "utf8"));
  const { soundfont, alsaSender, spool } = config;
  if (![soundfont, alsaSender, spool].every(value => typeof value === "string" && path.isAbsolute(value))) {
    throw new Error("Recorder soundfont, alsaSender and spool must be absolute paths");
  }
  if (![soundfont, alsaSender, helper].every(file => fs.statSync(file).isFile())) {
    throw new Error("Expected a recorder helper, SoundFont and ALSA sender");
  }
  fs.accessSync(alsaSender, fs.constants.X_OK);
  const directory = fs.lstatSync(spool);
  if (!directory.isDirectory() || (directory.mode & 0o077) || directory.uid !== process.getuid()) {
    throw new Error("Recorder spool must be an existing private directory owned by the router user");
  }
  return { soundfont, alsaSender, spool };
}

function openWav(file) {
  const fd = fs.openSync(file, fs.constants.O_RDONLY | fs.constants.O_NOFOLLOW | fs.constants.O_NONBLOCK);
  try {
    const stat = fs.fstatSync(fd);
    const header = Buffer.alloc(12);
    if (!stat.isFile() || stat.size <= 44 || stat.size > maxWavBytes ||
        fs.readSync(fd, header, 0, header.length, 0) !== header.length ||
        header.toString("ascii", 0, 4) !== "RIFF" || header.toString("ascii", 8) !== "WAVE" ||
        header.readUInt32LE(4) !== stat.size - 8) throw new Error("Invalid finalized WAV");
    const chunk = Buffer.alloc(8);
    const format = Buffer.alloc(16);
    let pcm = false;
    let data = false;
    let offset = 12;
    for (let count = 0; offset < stat.size && count < 128; count++) {
      if (fs.readSync(fd, chunk, 0, chunk.length, offset) !== chunk.length) break;
      const name = chunk.toString("ascii", 0, 4);
      const size = chunk.readUInt32LE(4);
      offset += 8;
      if (offset + size > stat.size) throw new Error("Invalid WAV chunk size");
      if (name === "fmt ") {
        if (pcm || size < 16 || fs.readSync(fd, format, 0, 16, offset) !== 16 ||
            format.readUInt16LE(0) !== 1 || format.readUInt16LE(2) !== 2 ||
            format.readUInt32LE(4) !== 44100 || format.readUInt32LE(8) !== 176400 ||
            format.readUInt16LE(12) !== 4 || format.readUInt16LE(14) !== 16) throw new Error("Invalid WAV PCM format");
        pcm = true;
      } else if (name === "data") {
        if (data || size === 0 || size % 4 !== 0) throw new Error("Invalid WAV data");
        data = true;
      }
      offset += size + (size % 2);
    }
    if (!pcm || !data || offset !== stat.size) throw new Error("Incomplete WAV");
    return { fd, size: stat.size };
  } catch (error) {
    fs.closeSync(fd);
    throw error;
  }
}

function createRecorder(filename) {
  const { soundfont, alsaSender, spool } = readConfig(filename);
  const latest = path.join(spool, "latest.wav");
  const directory = path.join(spool, "in-progress");
  const ownerFile = path.join(directory, ".owner");
  const bootId = fs.readFileSync("/proc/sys/kernel/random/boot_id", "utf8").trim();
  let state = "idle";
  let revision = 0;
  let job;
  let completedId = null;
  let shuttingDown = false;

  function processStamp(pid) {
    const stat = fs.readFileSync(`/proc/${pid}/stat`, "utf8");
    return `${bootId}:${pid}:${stat.slice(stat.lastIndexOf(")") + 2).split(" ")[19]}`;
  }

  function cleanStaleTake() {
    const slot = fs.lstatSync(directory, { throwIfNoEntry: false });
    if (!slot) return;
    if (!slot.isDirectory() || (slot.mode & 0o077) || slot.uid !== process.getuid()) {
      throw new Error("Recorder staging slot is not a private owned directory");
    }
    const marker = fs.lstatSync(ownerFile, { throwIfNoEntry: false });
    if (marker) {
      if (!marker.isFile() || marker.size > 128) throw new Error("Invalid recorder staging owner file");
      const stamp = fs.readFileSync(ownerFile, "utf8");
      const previous = /^[0-9a-f-]+:([1-9][0-9]*):[0-9]+$/.exec(stamp);
      if (!previous) throw new Error("Invalid recorder staging owner");
      let live;
      try { live = processStamp(previous[1]); } catch (error) {
        if (error.code !== "ENOENT") throw error;
      }
      if (live === stamp) throw new Error("Another process owns the recorder staging slot");
    }
    fs.rmSync(directory, { recursive: true });
  }

  cleanStaleTake();
  try {
    const { fd } = openWav(latest);
    fs.closeSync(fd);
    completedId = randomBytes(16).toString("hex");
  } catch (error) {
    if (error.code !== "ENOENT") console.error("Previous recorder WAV failed validation.");
  }

  function update(next) {
    state = next;
    revision++;
  }

  function cancelTimers(take) {
    clearTimeout(take.timer);
    clearTimeout(take.killTimer);
  }

  function kill(take, signal, group = false) {
    if (!take.child.pid) return;
    try { process.kill(group ? -take.child.pid : take.child.pid, signal); } catch (error) {
      if (error.code !== "ESRCH") console.error("Recorder process could not be signaled.");
    }
  }

  function write(take, line) {
    const writer = take.child.stdin;
    if (writer.destroyed || writer.writableEnded || writer.writableLength + line.length > maxControlBytes) {
      return false;
    }
    writer.write(line);
    return true;
  }

  function stopTimer(take) {
    clearTimeout(take.timer);
    take.timer = setTimeout(() => {
      take.failed = true;
      kill(take, "SIGTERM");
      take.killTimer = setTimeout(() => kill(take, "SIGKILL", true), 1000);
    }, stopMs);
  }

  function fail(take) {
    if (job !== take || take.failed) return;
    take.failed = true;
    update("stopping");
    if (!write(take, "fault\n")) kill(take, "SIGTERM");
    stopTimer(take);
  }

  function stop(take) {
    update("stopping");
    stopTimer(take);
    if (!write(take, "stop\n")) fail(take);
  }

  function connectMidi(take, port) {
    const origin = `http://127.0.0.1:${port}`;
    take.ws = new WebSocket(`ws://127.0.0.1:${port}/midi/${recorderPort}`, {
      origin,
      handshakeTimeout: 5000,
      createConnection() {
        const socket = net.connect({ host: "127.0.0.1", port, family: 4 });
        socket.once("connect", () => {
          if (job === take && !take.failed) take.endpoint = socket.localPort;
        });
        return socket;
      },
    });
    take.ws.on("open", () => {
      if (job !== take || take.failed) return take.ws.terminate();
      if (!write(take, "attached\n")) fail(take);
      else take.forwarding = true;
    });
    take.ws.on("message", (bytes, binary) => {
      if (job !== take || !take.forwarding || state === "stopping") return;
      if (!binary || bytes.length !== 3 || !write(take, `midi ${bytes.toString("hex").toUpperCase()}\n`)) fail(take);
    });
    take.ws.on("error", () => fail(take));
    take.ws.on("close", () => {
      take.endpoint = undefined;
      if (job !== take) return;
      if (take.phase === "closing") {
        if (!write(take, "closed\n")) fail(take);
      } else {
        fail(take);
      }
    });
  }

  function lineFrom(take, line, port) {
    if (take.canceled) return;
    if (line === "ready" && take.phase === "spawned" && !take.failed) {
      take.phase = "ready";
      connectMidi(take, port);
    } else if (line === "recording" && take.phase === "ready" && !take.failed) {
      take.phase = "recording";
      update("recording");
      clearTimeout(take.timer);
      take.timer = setTimeout(() => stop(take), recordingMs);
    } else if (line === "closing" && take.phase === "recording") {
      take.phase = "closing";
      if (state !== "stopping") {
        update("stopping");
        stopTimer(take);
      }
      if (take.ws.readyState === WebSocket.OPEN) take.ws.close(1000);
      else if (take.ws.readyState === WebSocket.CLOSED && !write(take, "closed\n")) fail(take);
    } else if (line === "complete" && take.phase === "closing") {
      take.phase = "complete";
    } else {
      fail(take);
    }
  }

  function finish(take, code) {
    if (job !== take) return;
    cancelTimers(take);
    let success = false;
    if (code === 0 && !take.failed && take.phase === "complete") {
      try {
        const candidate = path.join(take.work, "recording.wav");
        const { fd } = openWav(candidate);
        fs.closeSync(fd);
        fs.renameSync(candidate, latest);
        completedId = take.id;
        success = true;
      } catch {
        console.error("Recorder did not produce an eligible WAV file.");
      }
    }
    if (!success) kill(take, "SIGKILL", true);
    job = undefined;
    take.ws?.terminate();
    try { fs.rmSync(directory, { recursive: true, force: true }); } catch {
      console.error("Could not remove the private take directory.");
    }
    update(success || take.canceled ? "idle" : "failed");
    take.finalized();
  }

  function start(port) {
    if (job || shuttingDown) return false;
    let created = false;
    try {
      const id = randomBytes(16).toString("hex");
      fs.mkdirSync(directory, { mode: 0o700 });
      created = true;
      fs.writeFileSync(ownerFile, processStamp(process.pid), { flag: "wx", mode: 0o600 });
      // A previous helper can only refer to its own take, even after an abrupt host restart.
      const work = path.join(directory, id);
      fs.mkdirSync(work, { mode: 0o700 });
      const child = spawn("python3", [helper, "--soundfont", soundfont, "--alsa-sender", alsaSender,
        "--work-dir", work], { stdio: ["pipe", "pipe", "inherit"], detached: true });
      const take = { id, work, child, phase: "spawned", tail: "" };
      take.done = new Promise(resolve => { take.finalized = resolve; });
      job = take;
      update("starting");
      take.timer = setTimeout(() => fail(take), startupMs);
      child.stdin.on("error", () => fail(take));
      child.stdout.on("error", () => fail(take));
      child.on("error", () => fail(take));
      child.on("close", code => finish(take, code));
      child.stdout.on("data", chunk => {
        const lines = (take.tail + chunk.toString("latin1")).split("\n");
        take.tail = lines.pop();
        if (take.tail.length > 64 || lines.length > 8 || lines.some(line => line.length > 64)) {
          take.tail = "";
          fail(take);
          return;
        }
        for (const line of lines) lineFrom(take, line, port);
      });
      return true;
    } catch {
      if (created) {
        try { fs.rmSync(directory, { recursive: true, force: true }); } catch { /* Local startup error below. */ }
      }
      console.error("Recorder could not start the private take.");
      update("failed");
      return false;
    }
  }

  return {
    start,
    stop(id) {
      if (job?.id !== id || (state !== "starting" && state !== "recording")) return false;
      if (state === "starting") {
        job.canceled = true;
        fail(job);
      } else stop(job);
      return true;
    },
    snapshot() { return { state, activeId: job?.id ?? null, completedId, revision }; },
    claim(socket) {
      if (!job || job.failed || job.endpoint === undefined || socket.remoteAddress !== "127.0.0.1" ||
          socket.remotePort !== job.endpoint) return false;
      job.endpoint = undefined;
      return true;
    },
    download(id) {
      if (!id || id !== completedId) return;
      return openWav(latest);
    },
    async shutdown() {
      shuttingDown = true;
      const take = job;
      if (!take) return;
      if (state === "recording") stop(take);
      else if (state === "starting") fail(take);
      await take.done;
    },
  };
}

module.exports = { createRecorder, recorderPort };
