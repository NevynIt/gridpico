#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
case ${1:-} in
  apc) script=web-apc.sh; label='Web APC'; port=8765 ;;
  ui) script=web-ui.sh; label='Web UI'; port=8767 ;;
  *) printf 'Expected the apc or ui launcher profile.\n' >&2; exit 2 ;;
esac
profile=$1
shift
if [[ $# == 1 && $1 == --help ]]; then
  printf '%s\n' "Usage: ./scripts/$script" \
    "Use the running Compose dev service to serve http://127.0.0.1:$port/." \
    'Run from the host or WSL with Docker Compose and Python 3. Press Ctrl+C to stop.'
  exit 0
fi
if (( $# )); then
  printf 'Usage: ./scripts/%s [--help]\n' "$script" >&2
  exit 2
fi
for command in docker python3; do
  if ! command -v "$command" >/dev/null; then
    printf '%s requires %s on the host or WSL.\n' "$label" "$command" >&2
    exit 1
  fi
done

exec python3 - "$root" "$profile" "$label" "$port" <<'PY'
import asyncio
import os
import shutil
import signal
import socket
import subprocess
import sys


PROFILE, LABEL = sys.argv[2:4]
ADDRESS = ("127.0.0.1", int(sys.argv[4]))
URL = f"http://{ADDRESS[0]}:{ADDRESS[1]}/"
COMPOSE = ["docker", "compose", "-f", os.path.join(sys.argv[1], ".devcontainer/compose.yaml")]
DEV_EXEC = COMPOSE + ["exec", "-T", "-w", "/workspace/gridpico", "dev"]
if PROFILE == "apc":
    BUILD = """
cmake -S . -B build/browser-demo
cmake --build build/browser-demo --target gridpico_browser_fixture
npm --prefix connections ci --ignore-scripts
"""
    HOST_ARGS = ("connections/demo-host.js", "build/browser-demo/gridpico_browser_fixture")
    READY = f"Gridpico Web APC listening at {URL}\n"
    BUILD_MESSAGE = "Building the browser fixture and installing locked dependencies in dev."
    BUILD_FAILURE = "Browser fixture build or npm install failed in dev."
    HOST_LABEL = "Demo host"
else:
    BUILD = """
cmake -S . -B build/web-ui -DGRIDPICO_ENABLE_ALSA=OFF
cmake --build build/web-ui --target gridpico_local_router
npm --prefix connections ci --ignore-scripts --no-audit --no-fund
"""
    HOST_ARGS = ("connections/local-host.js", "--web-ui", "build/web-ui/gridpico_local_router",
                 str(ADDRESS[1]))
    READY = f"Gridpico local router listening at {URL}\n"
    BUILD_MESSAGE = "Building the production local router and installing locked dependencies in dev."
    BUILD_FAILURE = "Local router build or npm install failed in dev."
    HOST_LABEL = "Local host"

# The exec stdin is a lifetime pipe. Signal only the demo child we started.
DEMO = """
const { spawn } = require("node:child_process");
const demo = spawn(process.execPath,
  process.argv.slice(1),
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
const peer = require("node:net").connect({ host: "127.0.0.1", port: Number(process.argv[1]) });
let deadline;
function fail(error) {
  console.error(process.argv[2] + " container relay: " + error.message);
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


async def start_exec(code, *args):
    return await asyncio.create_subprocess_exec(
        *DEV_EXEC, "node", "-e", code, *args,
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
        print(f"{LABEL} timed out waiting for a Compose exec process to stop.", file=sys.stderr)
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
        bridge = await start_exec(RELAY, str(ADDRESS[1]), LABEL)
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
        print(f"{LABEL} client relay failed: {error or 'timed out while draining'}", file=sys.stderr)
    finally:
        for task in pumps:
            task.cancel()
        await asyncio.gather(*pumps, return_exceptions=True)
        writer.close()
        if bridge is not None:
            status = await finish_exec(bridge, 7)
            if status and not stopped.is_set():
                print(f"{LABEL} client relay exited with status {status}.", file=sys.stderr)
        try:
            await asyncio.wait_for(writer.wait_closed(), 2)
        except (OSError, asyncio.TimeoutError):
            writer.transport.abort()


def open_browser():
    candidates = []
    if os.environ.get("WSL_DISTRO_NAME") or os.environ.get("WSL_INTEROP"):
        candidates.extend(("wslview", "explorer.exe"))
    if sys.platform == "darwin":
        candidates.append("open")
    elif os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
        candidates.append("xdg-open")
    for name in candidates:
        command = shutil.which(name)
        if command is None:
            continue
        try:
            subprocess.Popen([command, URL], stdin=subprocess.DEVNULL,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                             start_new_session=True)
        except OSError:
            continue
        break


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

    print(f"Starting {LABEL} in the Compose dev service.", flush=True)
    demo = await start_exec(DEMO, *HOST_ARGS)
    server = None
    stop = asyncio.create_task(stopped.wait())
    ready = asyncio.create_task(demo.stdout.readline())
    exited = asyncio.create_task(demo.wait())
    try:
        done, _ = await asyncio.wait((ready, stop), timeout=30,
                                     return_when=asyncio.FIRST_COMPLETED)
        if stop in done:
            return
        if ready not in done or ready.result() != READY.encode():
            raise LauncherError(
                f"{HOST_LABEL} did not start on dev loopback {ADDRESS[0]}:{ADDRESS[1]}. "
                "Check the output above and whether the port is already in use in dev."
            )
        server = await asyncio.start_server(accept, sock=listener)
        print(f"Open {URL} in your browser. Press Ctrl+C to stop.", flush=True)
        if PROFILE == "ui":
            open_browser()
        done, _ = await asyncio.wait((exited, stop), return_when=asyncio.FIRST_COMPLETED)
        if stop not in done:
            raise LauncherError(f"{HOST_LABEL} in dev exited with status {exited.result()}.")
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
            raise LauncherError(f"Cannot listen on host loopback {ADDRESS[0]}:{ADDRESS[1]} ({error.strerror}).") from error
        print(BUILD_MESSAGE, flush=True)
        if subprocess.run(DEV_EXEC + ["sh", "-ec", BUILD], stdin=subprocess.DEVNULL).returncode:
            raise LauncherError(BUILD_FAILURE)
        asyncio.run(serve(listener))


try:
    main()
except (LauncherError, OSError) as error:
    print(f"{LABEL}: {error}", file=sys.stderr)
    sys.exit(1)
except KeyboardInterrupt:
    sys.exit(130)
PY
