#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat <<'HELP'
Usage: ./scripts/web-ui.sh [--profile CHECKOUT_FILE]
       ./scripts/web-ui.sh --runner PATH [--port PORT] [--no-open] [--web-ui]
           [--profile FILE] [--trusted-lan-config FILE]
           [--trusted-lan-pico-cdc-config FILE]
           [--recorder-config FILE] [--wss-config FILE]

With no runner specified, build and serve through the running Compose dev service.
A prepared runner uses this release's installed Node dependencies and UI bundle.
The default loopback URL is http://127.0.0.1:8767/. Press Ctrl+C to stop.
HELP
}

invalid() {
    printf 'Web UI: %s\n' "$1" >&2
    usage >&2
    exit 2
}

if [[ $# == 1 && $1 == --help ]]; then
    usage
    exit 0
fi
if [[ $# == 0 || ( $# == 2 && $1 == --profile && -n $2 ) ]]; then
    exec bash "$root/scripts/web-loopback.sh" ui "$@"
fi

runner= port= profile= lan_config= pico_cdc_config= recorder_config= wss_config=
web_ui=false
no_open=false
read_option() {
    if [[ -n ${!1} || -z $3 || $3 == --* ]]; then
        invalid "$2 needs one value and may be specified only once."
    fi
    printf -v "$1" '%s' "$3"
}
while (( $# )); do
    case $1 in
        --runner) read_option runner "$1" "${2-}"; shift ;;
        --port) read_option port "$1" "${2-}"; shift ;;
        --profile) read_option profile "$1" "${2-}"; shift ;;
        --trusted-lan-config) read_option lan_config "$1" "${2-}"; shift ;;
        --trusted-lan-pico-cdc-config) read_option pico_cdc_config "$1" "${2-}"; shift ;;
        --recorder-config) read_option recorder_config "$1" "${2-}"; shift ;;
        --wss-config) read_option wss_config "$1" "${2-}"; shift ;;
        --web-ui)
            "$web_ui" && invalid "$1 may be specified only once."
            web_ui=true ;;
        --no-open)
            "$no_open" && invalid "$1 may be specified only once."
            no_open=true ;;
        *) invalid "Unknown option $1." ;;
    esac
    shift
done

[[ -n $runner ]] || invalid "Prepared mode requires --runner PATH."
port=${port:-8767}
if [[ ! $port =~ ^[1-9][0-9]{0,4}$ ]] || (( port > 65535 )); then
    invalid "--port requires an integer from 1 to 65535."
fi
if ! command -v node >/dev/null; then
    printf 'Web UI prepared mode requires Node.js on PATH.\n' >&2
    exit 1
fi
host=("$root/connections/local-host.js" --ui-descriptor)
if "$web_ui"; then host+=(--web-ui); fi
if [[ -n $profile ]]; then host+=(--profile "$profile"); fi
if [[ -n $lan_config ]]; then host+=(--trusted-lan-config "$lan_config"); fi
if [[ -n $pico_cdc_config ]]; then host+=(--trusted-lan-pico-cdc-config "$pico_cdc_config"); fi
if [[ -n $recorder_config ]]; then host+=(--recorder-config "$recorder_config"); fi
if [[ -n $wss_config ]]; then host+=(--wss-config "$wss_config"); fi
host+=("$runner" "$port")

if "$no_open"; then
    exec node "${host[@]}"
fi

# Wait for the owned host's readiness banner before opening the loopback URL.
exec node -e '
const { spawn } = require("node:child_process");
const [port, ...args] = process.argv.slice(1);
const url = "http://127.0.0.1:" + port + "/";
const expected = "Gridpico local router listening at " + url;
const host = spawn(process.execPath, args, { stdio: ["ignore", "pipe", "inherit"] });
let first = "";
let deadline;
function stop() {
  if (deadline || host.exitCode !== null || host.signalCode !== null) return;
  host.kill("SIGTERM");
  deadline = setTimeout(() => host.kill("SIGKILL"), 12000);
}
process.on("SIGINT", stop);
process.on("SIGTERM", stop);
host.on("error", error => { console.error(error.message); process.exitCode = 1; });
host.on("close", code => {
  clearTimeout(deadline);
  process.exitCode = code ?? 1;
});
function openBrowser() {
  const commands = [];
  if (process.env.WSL_DISTRO_NAME || process.env.WSL_INTEROP) commands.push("wslview", "explorer.exe");
  if (process.platform === "darwin") commands.push("open");
  else if (process.env.DISPLAY || process.env.WAYLAND_DISPLAY) commands.push("xdg-open");
  function tryNext() {
    const command = commands.shift();
    if (!command) return;
    const opener = spawn(command, [url], { stdio: "ignore", detached: true });
    opener.once("error", tryNext);
    opener.unref();
  }
  tryNext();
}
function observe(chunk) {
  first += chunk;
  const newline = first.indexOf("\n");
  if (newline === -1 && first.length <= expected.length) return;
  host.stdout.off("data", observe);
  if (!deadline && first.slice(0, newline) === expected) openBrowser();
}
host.stdout.setEncoding("utf8");
host.stdout.on("data", observe);
host.stdout.pipe(process.stdout);
' -- "$port" "${host[@]}"
