window.addEventListener("DOMContentLoaded", () => {
  "use strict";

  const midi = window.gridpicoLocal;
  if (location.protocol !== "http:" || location.hostname !== "127.0.0.1" ||
      midi?.startupPorts.led !== 3 || midi.startupPorts.sequence !== 4) return;

  const cli = window.gridpicoDiagnosticsCli;
  const test = document.querySelector("#led-test");
  const startButton = document.querySelector("#led-test-run");
  const stopButton = document.querySelector("#led-test-stop");
  const status = document.querySelector("#led-test-status");
  const grid = document.querySelector("#led-test-pads");
  const padState = document.querySelector("#led-test-pad-state");
  const advancedReplay = document.querySelector("#local-midi");
  const connection = document.querySelector("#midi-connection");
  const server = connection.querySelector("form");
  if (server) document.querySelector("#led-test-server").append(server);

  const advanced = document.createElement("details");
  advanced.id = "diagnostics-advanced";
  const summary = document.createElement("summary");
  summary.textContent = "Advanced diagnostics";
  advanced.append(summary);
  const first = document.querySelector("#local-midi-only");
  first.before(advanced);
  while (advanced.nextElementSibling) advanced.append(advanced.nextElementSibling);
  function showLinkedTool() {
    if (["#routing", "#terminal"].includes(location.hash)) {
      advanced.open = true;
      document.getElementById(location.hash.slice(1))?.scrollIntoView();
    }
  }
  window.addEventListener("hashchange", showLinkedTool);
  showLinkedTool();

  // APC Key 25 mk2 RGB palette, indexed by the velocity of the received frame.
  const palette = `
000000 1E1E1E 7F7F7F FFFFFF FF4C4C FF0000 590000 190000
FFBD6C FF5400 591D00 271B00 FFFF4C FFFF00 595900 191900
88FF4C 54FF00 1D5900 142B00 4CFF4C 00FF00 005900 001900
4CFF5E 00FF19 00590D 001902 4CFF88 00FF55 00591D 001F12
4CFFB7 00FF99 005935 001912 4CC3FF 00A9FF 004152 001019
4C88FF 0055FF 001D59 000819 4C4CFF 0000FF 000059 000019
874CFF 5400FF 190064 0F0030 FF4CFF FF00FF 590059 190019
FF4C87 FF0054 59001D 220013 FF1500 993500 795100 436400
033900 005735 00547F 0000FF 00454F 2500CC 7F7F7F 202020
FF0000 BDFF2D AFED06 64FF09 108B00 00FF87 00A9FF 002AFF
3F00FF 7A00FF B21A7D 402100 FF4A00 88E106 72FF15 00FF00
3BFF26 59FF71 38FFCC 5B8AFF 3151C6 877FE9 D31DFF FF005D
FF7F00 B9B000 90FF00 835D07 392B00 144C10 0D5038 15152A
16205A 693C1C A8000A DE513D D86A1C FFE126 9EE12F 67B50F
1E1E30 DCFF6B 80FFBD 9A99FF 8E66FF 404040 757575 E0FFFF
A00000 350000 1AD000 074200 B9B000 3F3100 B35F00 4B1502
  `.trim().split(/\s+/);
  const order = [32, 24, 16, 8, 0].flatMap(base => Array.from({ length: 8 }, (_, index) => base + index));
  const cells = new Map();
  for (const pad of order) {
    const id = pad.toString(16).toUpperCase().padStart(2, "0");
    const cell = document.createElement("div");
    cell.className = "led-test-pad";
    cell.dataset.pad = id;
    cell.setAttribute("role", "img");
    if (pad === 0) {
      const label = document.createElement("span");
      label.textContent = id;
      cell.append(label);
    }
    cells.set(pad, cell);
    grid.append(cell);
  }

  function paintPad(pad, value) {
    const cell = cells.get(pad);
    const name = value === undefined ? "Waiting for feedback" :
      value === 0 ? "Off" : value === 21 ? "Green" : "Palette " + value;
    if (value === undefined) delete cell.dataset.value;
    else cell.dataset.value = value;
    cell.style.setProperty("--pad-color", value === undefined ? "" : "#" + palette[value]);
    cell.setAttribute("aria-label", "Pad " + cell.dataset.pad + " · " + name);
    if (pad === 0) padState.textContent = cell.getAttribute("aria-label");
  }

  function paintPage(values = []) {
    order.forEach((pad, index) => paintPad(pad, values[index]));
  }

  paintPage();
  test.hidden = false;
  let current;
  let changeServer = false;

  function progress(message, phase) {
    status.dataset.phase = phase;
    status.textContent = message;
  }

  function checkTime(run) {
    run.signal.throwIfAborted();
    if (performance.now() >= run.endsAt) throw new Error("The LED test timed out. Choose Retry.");
  }

  function until(promise, signal) {
    return new Promise((resolve, reject) => {
      const cancel = () => reject(signal.reason);
      if (signal.aborted) cancel();
      else signal.addEventListener("abort", cancel, { once: true });
      promise.then(resolve, reject).finally(() => signal.removeEventListener("abort", cancel));
    });
  }

  function delay(ms, signal) {
    let timer;
    return until(new Promise(resolve => { timer = setTimeout(resolve, ms); }), signal)
      .finally(() => clearTimeout(timer));
  }

  async function painted(signal) {
    let frame;
    const visible = new Promise(resolve => {
      frame = requestAnimationFrame(() => { frame = requestAnimationFrame(resolve); });
    });
    await until(visible, signal).finally(() => cancelAnimationFrame(frame));
  }

  function observe(run, match, signal) {
    const pending = new Promise(resolve => {
      run.receive = (bytes) => {
        const result = match(bytes);
        if (result !== undefined) {
          run.receive = undefined;
          resolve(result);
        }
      };
    });
    return until(pending, signal).finally(() => { run.receive = undefined; });
  }

  function pageMatcher() {
    let values = [];
    return (bytes) => {
      const [status, pad, value] = bytes;
      const valid = status === 0x96 && value < palette.length;
      if (!valid || pad !== order[values.length]) values = [];
      if (valid && pad === order[values.length]) values.push(value);
      if (values.length === order.length) return values;
    };
  }

  function openMidi(run, port, direction) {
    const failure = direction === "receive" ? run.rxLoss : run.txLoss;
    return new Promise((resolve, reject) => {
      let opened = false;
      try {
        run[direction === "receive" ? "rx" : "tx"] = midi.openMidi(port, {
          direction,
          provider: "websocket",
          onData(bytes) {
            if (current === run) run.receive?.(bytes);
          },
          onState(state, reason) {
            if (current !== run) return;
            if (state === "connected") {
              opened = true;
              resolve();
            }
            if (state !== "disconnected" || (direction === "send" && run.txClosing)) return;
            const error = new Error(reason || "MIDI port " + port + " disconnected.");
            if (opened) failure.abort(error);
            else reject(error);
          },
        });
      } catch (error) {
        reject(error);
      }
    });
  }

  async function claimCli(run) {
    run.cli = cli.claimLedTest();
    const session = run.cli;
    const check = () => {
      if (!session.live) run.cliLoss.abort(new Error("The Loopback CLI disconnected."));
    };
    run.checkCli = check;
    window.addEventListener("gridpico-local-cli-binding", check);
    check();
    let ready;
    try {
      await until(new Promise(resolve => {
        ready = () => { if (session.ready) resolve(); };
        window.addEventListener("gridpico-local-cli-queue", ready);
        ready();
      }), run.signal);
    } finally {
      window.removeEventListener("gridpico-local-cli-queue", ready);
    }
  }

  async function receiveLed(run, value) {
    checkTime(run);
    const received = observe(run, ([status, pad, incoming]) => {
      if (status === 0x96 && pad === 0 && incoming === value) return incoming;
    }, run.signal);
    if (run.tx.send(Uint8Array.of(0x96, 0, value))) {
      progress("Sent · awaiting receiver", "sent");
    } else {
      run.cancel.abort(new Error("Couldn’t send the test frame."));
    }
    paintPad(0, await received);
    progress(value ? "Pad 00 is green." : "Pad 00 is off.", value ? "green" : "off");
    await painted(run.signal);
  }

  async function restore(run) {
    progress(run.stopped ? "Stopped · waiting for committed LED feedback." :
      "Waiting for committed LED feedback.", "restoring");
    const signal = AbortSignal.any([run.deadline, run.rxLoss.signal]);
    const restored = observe(run, pageMatcher(), signal);
    run.txClosing = true;
    run.tx?.close();
    paintPage(await restored);
    if (run.stopped && document.hidden) return;
    await painted(signal);
  }

  async function execute(run) {
    progress("Connecting to APC LED feedback…", "connecting");
    try {
      await until(openMidi(run, 3, "receive"), run.signal);
    } catch (error) {
      if (run.signal.aborted) throw error;
      let occupied = error.message === "Port 3 is busy on this page.";
      if (!occupied) {
        try {
          await claimCli(run);
          occupied = await until(run.cli.receiverOccupied(), run.signal);
        } catch {
          // A refused upgrade supplies no occupancy detail. Only a proven binding is called busy.
        }
      }
      run.signal.throwIfAborted();
      throw new Error(occupied ?
        "LED receiver is in use. Release it in the other tool and choose Retry." :
        "Couldn’t connect to LED feedback. Check the selected server or release LED feedback in the other tool, then choose Retry.");
    }

    progress("Waiting for the live LED baseline…", "baseline");
    paintPage(await observe(run, pageMatcher(), run.signal));
    await until(openMidi(run, 4, "send"), run.signal);
    await claimCli(run);
    progress("Checking the LED test route…", "checking");
    if (await until(run.cli.readRoute(), run.signal) !== "OFF") {
      throw new Error("The LED test route is already enabled. Finish that test before trying again.");
    }
    checkTime(run);
    // A command already sent may use the normal five-second CLI deadline.
    if (performance.now() + 5000 >= run.endsAt) throw new Error("The LED test ran out of setup time.");
    run.mayOwnRoute = true;
    run.enabling = run.cli.readRoute(true);
    if (await until(run.enabling, run.signal) !== "ACTIVE" ||
        await until(run.cli.readRoute(), run.signal) !== "ACTIVE") {
      throw new Error("The router did not confirm an active LED test route.");
    }
    await receiveLed(run, 21);
    await delay(1000, run.signal);
    await receiveLed(run, 0);
  }

  function stop() {
    if (!current || current.stopped) return;
    current.stopped = true;
    stopButton.setAttribute("aria-disabled", "true");
    progress("Stopping the LED test…", "stopping");
    current.cancel.abort(new Error("Stopped."));
  }

  async function runTest() {
    if (current || document.hidden) return;
    const run = {
      endsAt: performance.now() + 20000,
      deadline: AbortSignal.timeout(20000),
      cancel: new AbortController(),
      rxLoss: new AbortController(),
      txLoss: new AbortController(),
      cliLoss: new AbortController(),
    };
    run.signal = AbortSignal.any([run.deadline, run.cancel.signal, run.rxLoss.signal,
      run.txLoss.signal, run.cliLoss.signal]);
    current = run;
    startButton.setAttribute("aria-disabled", "true");
    startButton.textContent = "Running…";
    stopButton.disabled = false;
    stopButton.removeAttribute("aria-disabled");
    advancedReplay.inert = true;
    paintPage();

    let failure;
    try {
      await execute(run);
    } catch (error) {
      failure = error;
    }
    if (run.mayOwnRoute) {
      // An orderly Stop keeps both MIDI generations reserved until Raw on settles.
      if (!run.rxLoss.signal.aborted && !run.txLoss.signal.aborted) {
        await run.enabling.catch(() => {});
      }
      try {
        await restore(run);
        if (run.stopped) progress("Stopped · committed LED feedback restored.", "stopped");
        else if (failure) progress(failure.message + " Committed LED feedback restored.", "failed");
        else progress("Done · committed LED feedback restored.", "done");
      } catch {
        paintPage();
        progress(run.stopped ? "Stopped · Restore unconfirmed." : "Restore unconfirmed.", "unconfirmed");
      }
    } else if (run.stopped) {
      progress("Stopped.", "stopped");
    } else {
      progress(run.deadline.aborted ? "The LED test timed out. Choose Retry." : failure.message, "failed");
    }

    current = undefined;
    if (run.checkCli) window.removeEventListener("gridpico-local-cli-binding", run.checkCli);
    if (!run.txClosing) run.tx?.close();
    run.rx?.close();
    run.cli?.release();
    advancedReplay.inert = false;
    startButton.removeAttribute("aria-disabled");
    startButton.textContent = ["failed", "unconfirmed"].includes(status.dataset.phase) ? "Retry" : "Run LED test";
    if (document.activeElement === stopButton) startButton.focus();
    stopButton.disabled = true;
    if (changeServer) {
      changeServer = false;
      server.requestSubmit();
    }
  }

  startButton.addEventListener("click", runTest);
  stopButton.addEventListener("click", stop);
  document.addEventListener("visibilitychange", () => { if (document.hidden) stop(); });
  server?.addEventListener("submit", event => {
    if (!current) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    changeServer = true;
    stop();
  }, { capture: true });
  window.addEventListener("pagehide", () => {
    stop();
    current?.tx?.close();
    current?.rx?.close();
  });
});
