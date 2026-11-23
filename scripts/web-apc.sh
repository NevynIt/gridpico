#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# == 1 && $1 == --help ]]; then
  printf '%s\n' 'Usage: ./scripts/web-apc.sh' \
    'Use the running Compose dev service to serve http://127.0.0.1:8765/.' \
    'Run from the host or WSL with Docker Compose and Python 3. Press Ctrl+C to stop.'
  exit 0
fi
if (( $# )); then
  printf 'Usage: ./scripts/web-apc.sh [--help]\n' >&2
  exit 2
fi
for command in docker python3; do
  if ! command -v "$command" >/dev/null; then
    printf 'Web APC requires %s on the host or WSL.\n' "$command" >&2
    exit 1
  fi
done

exec python3 - "$root" <<'PY'
import asyncio
import os
import signal
import socket
import subprocess
import sys


ADDRESS = ("127.0.0.1", 8765)
URL = f"http://{ADDRESS[0]}:{ADDRESS[1]}/"
COMPOSE = ["docker", "compose", "-f", os.path.join(sys.argv[1], ".devcontainer/compose.yaml")]
DEV_EXEC = COMPOSE + ["exec", "-T", "-w", "/workspace/gridpico", "dev"]
BUILD = """
cmake -S . -B build/browser-demo
cmake --build build/browser-demo --target gridpico_browser_fixture
npm --prefix connections ci --ignore-scripts
"""

# The exec stdin is a lifetime pipe. Signal only the demo child we started.
DEMO = """
const { spawn } = require("node:child_process");
const demo = spawn(process.execPath,
  ["connections/demo-host.js", "build/browser-demo/gridpico_browser_fixture"],
  { stdio: ["ignore", "inherit", "inherit"] });
let deadline;
function stop() {
  if (deadline || demo.exitCode !== null || demo.signalCode !== null) return;
  demo.kill("SIGTERM");
  deadline = setTimeout(() => demo.kill("SIGKILL"), 7000);
}
process.stdin.on("end", stop);
process.stdin.on("error", stop);
process.on("SIGINT", stop);
process.on("SIGTERM", stop);
process.stdin.resume();
demo.on("error", error => { console.error(error.message); process.exit(1); });
demo.on("exit", code => {
  clearTimeout(deadline);
  process.exit(code ?? 1);
});
"""

# Use binary pipes through Compose. A TCP FIN closes only the sending direction.
RELAY = """
const peer = require("node:net").connect({ host: "127.0.0.1", port: 8765 });
let deadline;
function fail(error) {
  console.error("Web APC container relay: " + error.message);
  process.exitCode = 1;
  peer.destroy();
}
process.stdin.on("error", fail);
process.stdout.on("error", fail);
peer.on("error", fail);
process.stdin.on("end", () => {
  deadline = setTimeout(() => peer.destroy(), 5000).unref();
});
peer.on("close", () => {
  clearTimeout(deadline);
  process.stdin.unpipe(peer);
  process.stdin.pause();
});
process.stdin.pipe(peer);
peer.pipe(process.stdout);
"""


class LauncherError(Exception):
    pass


async def start_exec(code):
    return await asyncio.create_subprocess_exec(
        *DEV_EXEC, "node", "-e", code,
        stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
        start_new_session=True,
    )


async def finish_exec(process, timeout):
    process.stdin.close()

    async def drain():
        while await process.stdout.read(65536):
            pass
        return await process.wait()

    try:
        return await asyncio.wait_for(drain(), timeout)
    except asyncio.TimeoutError:
        print("Web APC timed out waiting for a Compose exec process to stop.", file=sys.stderr)
        try:
            process.kill()
        except ProcessLookupError:
            pass
        return await drain()


async def copy_bytes(reader, writer):
    while True:
        data = await reader.read(65536)
        if not data:
            writer.write_eof()
            return
        writer.write(data)
        await writer.drain()


async def relay_client(reader, writer, stopped):
    bridge = None
    pumps = ()
    try:
        bridge = await start_exec(RELAY)
        incoming = asyncio.create_task(copy_bytes(reader, bridge.stdin))
        outgoing = asyncio.create_task(copy_bytes(bridge.stdout, writer))
        pumps = (incoming, outgoing)
        done, _ = await asyncio.wait(pumps, return_when=asyncio.FIRST_COMPLETED)
        for task in done:
            task.result()
        if outgoing not in done:
            await asyncio.wait_for(outgoing, 6)
    except ConnectionError:
        pass
    except (OSError, asyncio.TimeoutError) as error:
        print(f"Web APC client relay failed: {error or 'timed out while draining'}", file=sys.stderr)
    finally:
        for task in pumps:
            task.cancel()
        await asyncio.gather(*pumps, return_exceptions=True)
        writer.close()
        if bridge is not None:
            status = await finish_exec(bridge, 7)
            if status and not stopped.is_set():
                print(f"Web APC client relay exited with status {status}.", file=sys.stderr)
        try:
            await asyncio.wait_for(writer.wait_closed(), 2)
        except (OSError, asyncio.TimeoutError):
            writer.transport.abort()


async def serve(listener):
    stopped = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stopped.set)

    clients = set()

    def accept(reader, writer):
        if stopped.is_set():
            writer.close()
            return

        def release(task):
            writer.close()
            clients.discard(task)

        task = asyncio.create_task(relay_client(reader, writer, stopped))
        clients.add(task)
        task.add_done_callback(release)

    print("Starting Web APC in the Compose dev service.", flush=True)
    demo = await start_exec(DEMO)
    server = None
    stop = asyncio.create_task(stopped.wait())
    ready = asyncio.create_task(demo.stdout.readline())
    exited = asyncio.create_task(demo.wait())
    try:
        done, _ = await asyncio.wait((ready, stop), timeout=30,
                                     return_when=asyncio.FIRST_COMPLETED)
        if stop in done:
            return
        if ready not in done or ready.result() != f"Gridpico Web APC listening at {URL}\n".encode():
            raise LauncherError(
                "Demo host did not start on dev loopback 127.0.0.1:8765. "
                "Check the output above and whether the port is already in use in dev."
            )
        server = await asyncio.start_server(accept, sock=listener)
        print(f"Open {URL} in your browser. Press Ctrl+C to stop.", flush=True)
        done, _ = await asyncio.wait((exited, stop), return_when=asyncio.FIRST_COMPLETED)
        if stop not in done:
            raise LauncherError(f"Demo host in dev exited with status {exited.result()}.")
    finally:
        stopped.set()
        if server is not None:
            server.close()
        for task in (ready, exited, stop):
            task.cancel()
        await asyncio.gather(ready, exited, stop, return_exceptions=True)
        active_clients = tuple(clients)
        for task in active_clients:
            task.cancel()
        await asyncio.gather(finish_exec(demo, 9), *active_clients, return_exceptions=True)
        if server is not None:
            await server.wait_closed()


def main():
    services = subprocess.run(
        COMPOSE + ["ps", "--status", "running", "--services", "dev"],
        stdout=subprocess.PIPE, text=True,
    )
    if services.returncode:
        raise LauncherError("Could not query the checkout's Compose dev service.")
    if "dev" not in services.stdout.splitlines():
        raise LauncherError("The checkout's Compose dev service is not running.")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            listener.bind(ADDRESS)
            listener.listen()
        except OSError as error:
            raise LauncherError(f"Cannot listen on host loopback 127.0.0.1:8765 ({error.strerror}).") from error
        print("Building the browser fixture and installing locked dependencies in dev.", flush=True)
        if subprocess.run(DEV_EXEC + ["sh", "-ec", BUILD], stdin=subprocess.DEVNULL).returncode:
            raise LauncherError("Browser fixture build or npm install failed in dev.")
        asyncio.run(serve(listener))


try:
    main()
except (LauncherError, OSError) as error:
    print(f"Web APC: {error}", file=sys.stderr)
    sys.exit(1)
except KeyboardInterrupt:
    sys.exit(130)
PY
