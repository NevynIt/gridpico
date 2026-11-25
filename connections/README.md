# Gridpico computer connections

For the production web tools, run the [canonical launcher](#local-router)
and open Gridpico Home.
The one-pad and Patch views below are separate demo profiles.

## One-pad Web APC demo

From the repository root, with CMake, a C compiler, Node.js 18 or later and npm:

```sh
cmake -S . -B build/browser-demo
cmake --build build/browser-demo --target gridpico_browser_fixture
npm --prefix connections ci --ignore-scripts
node connections/demo-host.js build/browser-demo/gridpico_browser_fixture
```

Open [the local Web APC application](http://127.0.0.1:8765/). The host binds
IPv4 loopback and accepts `/demo/midi` upgrades from that exact origin. To
change the local port, append it to the host command and use the printed URL.

The host serves Web APC with the connection adapter attached. One
player owns the fixture at a time. After a disconnect, click
**Reconnect** to start a fresh fixture once cleanup finishes. Start playback
with a fresh key press and enable Monitor audio again if needed. Stop the
host with Ctrl+C.

### Smoke check

1. Wait for **Connected** and check that the bottom-left **Route** pad is green.
   Hold **Q** (key 60, channel 1) and check **Captured MIDI output** for
   **Note On · 1 · 72 · 96**.
2. With **Q** held, press the **Route** pad. Check that **Note Off · 1 · 72 · 0**
   arrives before the pad goes dark. Release the pad and **Q**. There should be
   one release row. Press and release **Q** while the pad is dark. The capture
   should stay at two rows.
3. Hold **Q** again while the pad is dark. Press and release **Route** to turn
   it green, then release **Q**. The capture should still have two rows. Press
   and release **Q** again and check for a new **72 · 96** On and **72 · 0** Off.
   To hear accepted notes, enable audio in **Monitor**.
4. Hold **Q**, press Escape while still holding **Q**, then release **Q**. Check
   for one additional On/Off pair with the same values.
5. Hold **Q** and disconnect the WebSocket, for example by stopping the host.
   Check that the pad's received LED state clears.
   Start the host again if needed, click **Reconnect**, and wait for **Connected**.
   Confirm the new capture starts empty and the pad is green.
   To see the fixture's EOF release directly, submit a press and let the pipe
   close while the note is held:

   ```sh
   printf 'on 1 60 96\n' | build/browser-demo/gridpico_browser_fixture
   ```

   The expected lines are `ready`, `on 1 72 96`, `ok`, and `off 1 72 0`.

The host requests startup feedback from the fixture and sends it after the
browser is ready. The browser fixture supports 25 held notes. Keybed and surface
requests share a queue of up to 64, with one runner dispatch in flight. The host
forwards up to 25 accepted capture records per dispatch in order and then
sends LED feedback for that dispatch on success. Monitor uses
the same accepted note records as the capture table. On socket loss or
transport saturation, the host closes runner stdin and drains stdout through
fixture exit, including disconnect releases. Browser output buffering has a
byte limit and a drain timeout. The WebSocket dependency is pinned in the npm
lockfile.

## Connected Patch view

From the repository root:

```sh
cmake -S . -B build/browser-demo
cmake --build build/browser-demo --target gridpico_patch_runner
npm --prefix connections ci --ignore-scripts
node connections/patch-host.js build/browser-demo/gridpico_patch_runner
```

Open [the local Patch view](http://127.0.0.1:8766/patch/). It uses the
same-origin `/patch/midi` socket. An optional port argument overrides 8766.

At startup check the named devices and ports, including the unassigned
**Panel** and **LED feedback** ports and **Monitor** in destination column 3.
Turn on the top-left instrument route and the Monitor route in the first
row. Enable audio in **Monitor** and hold **Q**. Check that Capture shows
both destinations. Turn off the instrument route and verify its Note Off
arrives before the LED update while Monitor keeps sounding. Release **Q**
and verify the Monitor Note Off and silence.

The default Patch profile supports 25 held keys across three routes and
up to 75 accepted note records per dispatch. Keybed, pad, refresh and
Monitor requests run in order with one dispatch in flight and up to 162
queued. The host caps outgoing buffering at 12 KiB, and the browser
retains its 8 KiB write limit. A successful refresh delivers a validated
inventory block before the 40-pad LED page. The Monitor receiver
attaches when the Patch view is ready, and audio remains a separate
local control.

An ordinary command rejection reports its code after any accepted
capture and complete LED page. If a new key press exceeds the held-note
limit, the connection delivers the accepted releases, reports the
capacity error and closes.
After a disconnect, click **Reconnect** for a fresh Patch connection
with routes off and an empty capture. Use fresh route and key gestures
and enable audio again if needed.

### Two-branch regression

```sh
node connections/patch-host.js --two-branch build/browser-demo/gridpico_patch_runner
```

This profile retains two routes, up to 50 accepted note records per
dispatch, 160 queued requests and the original untagged capture format.
Browser playback is silent in this profile. Default Patch plays through
the attached **Web Monitor** destination. The one-pad view keeps its own
browser audio on the `/demo/midi` connection.

## Local router

With the Docker Compose `dev` service running, run `./scripts/web-ui.sh`
from the repository root on the Docker host or WSL with Python 3. The
launcher opens [Gridpico Home](http://127.0.0.1:8767/) or prints that URL
for you to open. Choose **Web APC**, **Diagnostics**, **Routing**, or an
installed optional tool. In the served bundle, **Circuit** opens a
16-step sequencer surface preview. You can inspect the steps without
connecting hardware. Playback and physical Circuit Tracks control remain
outside the preview. Run `./scripts/web-ui.sh --help` for launcher usage.

The launcher selects the `--web-ui` profile in the host and router. Before
readiness, the router enables managed Keybed → Monitor (`1 → 5`) and
Keybed → MIDI Log (`1 → 6`). Each route starts Pending until its endpoints
connect. Raw starts Off. The loopback pages try their starter bindings once:

| Tool | Starter bindings |
| --- | --- |
| Web APC | Keybed TX `1`, Panel TX `2`, LED RX `3` |
| Diagnostics | Monitor RX `5`, MIDI Log RX `6` |

Open Diagnostics in a separate tab from Web APC and choose **Enable audio**
in Monitor. The keybed can send as soon as its own binding connects. Each
receiver starts with the next fresh note after it connects. MIDI Log shows
what its own binding receives. Diagnostics LED Sequence TX `4` and FM use
manual connections.

### Served browser release

The canonical launcher builds one browser release under `build/web-ui/bundle`.
The Home download at `/web-ui.zip` contains the same static tools and
`SHA256SUMS` served at `/web-ui/SHA256SUMS`. The same-origin
`/ui-capabilities.json` is separate from the download. It reports which roles
and actions the current listener publishes. A MIDI binding still needs a
successful connection, and Routing still reads the actual CLI.

When installed, **MIDI File** opens at `/midi-file/`. Its browser sends
through the existing authenticated WSS Keybed input when that role is
published. Release the Keybed binding in Web APC before connecting MIDI File.

For a prepared runner, build the browser release once with
`python3 connections/build-web-ui.py` from the release checkout. Ship the
whole bundle directory with that release. For example, start a prepared
loopback desktop with:

```sh
./scripts/web-ui.sh --runner build/local-router/gridpico_local_router --web-ui --port 8767 --no-open
```

On a trusted-LAN bundle listener, Web APC uses the published controller
bindings. Diagnostics can offer the attached Pico console, while the Pi
router CLI and Routing use the SSH loopback path. Recordings appears on
Home when configured.
[Shared bundle contract](../api/browser-midi.md#shared-bundle-and-endpoint-selection)

### Manual profile

To start the manual profile directly:

```sh
cmake -S . -B build/local-router -DGRIDPICO_ENABLE_ALSA=OFF
cmake --build build/local-router --target gridpico_local_router
npm --prefix connections ci --ignore-scripts
node connections/local-host.js build/local-router/gridpico_local_router
```

An optional port argument overrides 8767. The direct command starts with
those routes Off and leaves binding selection to the operator. To select the
starter profile directly, pass `--web-ui` before the runner path.
[Production startup contract](../api/local-router.md#production-web-ui-startup)

In Diagnostics, choose **Connect local CLI** and send `help` to see the
attached CLI's available commands and short syntax. Use `inventory` to find
the directional port IDs. The production CLI supports
`route SRC DST managed|raw [on|off]`. Omit `on` or `off` to read the current
state. In the manual profile, bind the relevant ports and enable the default
keybed routes with:

```text
route 1 5 managed on
route 1 6 managed on
```

For the solid/off check, connect Diagnostics MIDI to port 4 and Web APC LED
RX to port 3. Run `route 4 3 raw on` in the router CLI, then check
`route 4 3 raw` for Active. Click **Solid green** and **Off** and check the
bottom-left APC pad. Run
`route 4 3 raw off` to end the run and restore committed APC feedback.
A new run requires a fresh Diagnostics MIDI connection and an explicit
`on`. The Raw lease also ends after 30 seconds or if an affected MIDI
peer disconnects.

The local host keeps one router running for all bindings. It accepts MIDI
as three-byte binary frames at `/midi/<port-id>` and serial CLI bytes at
`/serial`. The serial connection has its own input and output limits.

### Routing

On the local router host, open **Routing** from Gridpico Home. It opens
`/routing/`. The old served `/diagnostics/#routing` bookmark opens this tool.
Routing and Diagnostics use the same serial client. Connect again in the next
tool after navigating. A router accepts one serial session at a time.
Connect **Loopback CLI** to read the named inputs, outputs, and routes. Routing reads
all pages before displaying a snapshot. Use **Refresh routing** to check
current availability, which can change after a snapshot is read.
The standard `./scripts/web-ui.sh` launch shows the six-port profile.

Managed inputs are rows and outputs are columns. Each compatible crosspoint
shows its confirmed enabled state and whether the route is Off, Pending, or
Active. Free, In use, and Unavailable describe each port separately. Toggle a
crosspoint to request an edit. Routing keeps the confirmed value until it
finishes reading back the current snapshot. The Raw LED pair and Panel
inventory are under **Raw routes and port inventory**. The Raw lease still
requires an explicit enable. Terminal commands are available on Routing under
**Serial transcript and CLI commands**.

To use a local device, select **Web Serial CDC** and choose its port. The
offline Diagnostics page keeps its in-file `#routing` entry. Identify the
attached device in the plain serial transcript,
then choose **Check Routing** to read its capabilities. Confirm the selected
router before editing. A device without a matching Routing reply stays in
the plain terminal. Changing the serial provider clears the prior snapshot
and requires a fresh connection.

For the expanded desktop profile, run
`./scripts/web-ui.sh --profile app/profiles/desktop.conf` on the launcher host.
For a direct launch, pass `--profile app/profiles/desktop.conf` to
`connections/local-host.js` before the runner path. The router controls
the profile's admission and route limits. [Routing contract](../api/routing.md)

To pan the desktop profile's passive APC matrix, focus the on-screen
**Shift**, hold **Space**, and click a Clip Stop arrow. On touch, hold
on-screen **Shift** and tap the arrow. Use the on-screen control to send
the APC Shift message.

### MIDI provider selection

Served pages default to **WebSocket**. Select the directional numeric port
from CLI inventory or the router operator. Choose **Web MIDI** to use an
OS MIDI port. Grant access from the page, select a named system output for
sending or input for receiving, then connect. Refresh the list after adding
or replacing a port. A lost port needs a fresh selection. Chrome with real
ALSA ports verified this browser selection and device-loss behavior.

In Diagnostics, LED Sequence, Monitor and MIDI Log each have their own
provider and port selection. Direct Web MIDI sends use the selected system
output connected to APC Port 1. Check the lamp to verify receipt. This send
uses no Raw lease.

The OS manages RTP-MIDI sessions. Select an RTP port in Web MIDI if the OS
exposes it and the browser lists it. The Linux two-host RTP bench verified
receipt, rebind and scoped release. Session repeatability, in-session Note Off
loss recovery and browser playback over an OS-exposed RTP port remain open.
[RTP bench results](../ROADMAP.md#4-verify-rtp-midi-and-productize-cross-host-websocket-midi)

Web APC, Diagnostics and FM share the **MIDI server origin** field.
**Open server** navigates to the same tool at the selected origin
(`/apc/`, `/diagnostics/` or `/fm/`). Enter the scheme, host and optional port,
with an optional trailing slash. HTTP and WS are accepted on `127.0.0.1`;
use the configured HTTPS or WSS origin for a remote server. The field
converts WS to HTTP and WSS to HTTPS for navigation. The tool opens its
WebSocket at that page's origin. The browser applies the destination's TLS
and client-certificate rules. Use Back to return to the previous page.
[Server selection and status](../api/browser-midi.md#server-selection-and-status)

In the isolated endpoint-switch profile, Web APC at
`http://127.0.0.1:8767/apc/` offers **Switch router** for the exact candidate
`http://192.168.0.223:18768`. The trusted-LAN configuration opts in with
`"uiOrigin": "http://127.0.0.1:8767"` and requires descriptor mode. The grant
covers the capability read and the three published APC MIDI sockets only.
Web APC checks that the destination has the same browser release before
retiring the old bindings. If a switch cannot be verified, the current
router stays selected and the page offers to open Web APC on the destination.
If a MIDI role fails after selection, retry that role or use the same
navigation link.
[Isolated endpoint grant](../api/browser-midi.md#shared-bundle-and-endpoint-selection)

### Offline tools

Build the checksummed `GRIDPICO` asset bundle:

```sh
cmake -S . -B build/web-ui -DGRIDPICO_ENABLE_ALSA=OFF
cmake --build build/web-ui --target gridpico_offline_assets
```

Open `build/web-ui/GRIDPICO/HOME.HTM`. Its relative links open `INDEX.HTM`
(Web APC), `DIAG.HTM` (Diagnostics) and `FM.HTM` (FM Instrument).
`VERSION.TXT` lists their SHA-256 checksums and the home checksum. The tools
include the same shared MIDI adapter and use Web MIDI where the browser
grants access. Diagnostics opens Web Serial separately for its transcript.
Staged direct files were exercised offline. Browser-local USB CDC and
mounted-drive acceptance remain open. The original Pico bridge passed
remote WSS CDC through strict SSH forwarding.
[Pico CDC evidence](../api/pico-target.md#dedicated-pico-wss-service)

### Timed RGB pads

In Diagnostics, select the MIDI output. For WebSocket, connect the APC LED
receiver, enter the shown `route SRC DST raw on` command and wait for the
CLI to report Active. With **Loopback CLI** connected in the same page,
Diagnostics checks the selected Raw pair before a sequence or a single
Solid green/Off send. With Web Serial CDC, an external CLI, or WSS, confirm the readback
manually in Diagnostics. Keep Diagnostics visible during the run. The
queued status reports local submission. Check the live APC pad for the result.
For Web MIDI, choose a system output connected to the APC LED input.
**Step** queues one row at your pace. **Replay** starts
from row 1 and queues each row once with these minimum waits.

| Hex sent and expected at LED RX | Minimum wait after send | Expected at 120 BPM fixture tempo |
| --- | --- | --- |
| `9A 00 15` | 100 ms | Pad 00 green pulse, 1,000 ms cycle |
| `9E 01 2D` | 1,200 ms | Pad 01 blue blink, 500 ms cycle |
| `96 00 05` | 100 ms | Pad 00 solid red replaces the pulse |
| `96 01 00` | 100 ms | Pad 01 off |
| `96 00 00` | End | Pad 00 off |

The sequence uses the APC RGB pad messages. The 120 BPM label describes the
browser renderer's fixture tempo. [Akai APC Key 25 mk2 Communication Protocol](https://cdn.inmusicbrands.com/akai/attachments/APC%20Key%2025%20mk2%20-%20Communication%20Protocol%20-%20v1.1.pdf)

Completion, **Stop**, **Reset**, and the 20-second client limit close the
Diagnostics TX binding. **Reset** also returns the cursor to row 1.
For WebSocket runs the router ends the Raw lease on close and restores
current committed APC feedback. The typed `route SRC DST raw off` command,
peer loss, or the router's 30-second lease expiry also ends that run.
Connect a fresh Diagnostics TX binding before starting again. WebSocket
runs also need a fresh Raw on command.

### Pi trusted LAN and recordings

The Pi service uses the canonical launcher in prepared mode. See the
[Pi router deployment guide](../platform/linux/PI_ROUTER.md) for service
setup, the trusted LAN profile, SSH forwarding and recorder operations.
Open [Pi Home](http://192.168.0.223:18768/) and use
**Web APC** and **Recordings** in separate tabs. This private LAN uses
plaintext HTTP and WebSocket with no per-user account. Devices admitted by
the listener share the first-claim APC MIDI roles, the single recorder, and
its latest download. Use the authorized SSH forward on literal loopback
`127.0.0.1:18767` for the Pi CLI. The laptop app still defaults to `8767`.

### Pico via Pi in Diagnostics

From Pi Home, open **Diagnostics**, choose **Pico via Pi · LAN WebSocket CDC**
and click **Connect**. Check that the terminal shows the connected Pico
and the Pi relay address. `TRACE` and `HUBTRACE` are available without a
command check. Choose **Check Pico commands** while the terminal is idle.
Diagnostics sends `help` for this check once per attachment. After a
complete reply, it allows the commands the firmware advertised from
`help [topic]`, `inventory` and `status`. Use a command name shown in the
reply for the optional help topic.

If the check times out or the reply is incomplete or invalid, use
`TRACE` and `HUBTRACE`. Reconnect to check a fresh attachment. After
overlapping trace requests, reconnect before checking commands.
This read-only form leaves maintenance commands, including `BOOTSEL`, to
the separately configured [Pico console](#separate-pico-service). For Pi
router commands use **Loopback CLI** through the SSH forward. For a
direct USB terminal use **Web Serial CDC**.

### Opt-in HTTPS/WSS MIDI

On the selected deployment, create a JSON configuration outside the
repository and use absolute paths to the operator-provisioned TLS files.
An example with placeholder paths and leaf fingerprint:

```json
{
  "address": "192.0.2.17",
  "port": 9443,
  "origin": "https://gridpico.example:9443",
  "certificate": "/path/to/server-chain.pem",
  "key": "/path/to/server-key.pem",
  "clientCA": "/path/to/client-ca.pem",
  "allowedClientFingerprints": ["<64 lowercase SHA-256 hex digits>"],
  "midiPorts": [
    { "id": 1, "direction": "in" },
    { "id": 3, "direction": "out" },
    { "id": 5, "direction": "out" }
  ]
}
```

Use the selected IPv4 interface address, HTTPS origin and Graph port
directions for the deployment. Run from the repository root:

```sh
node connections/local-host.js --wss-config /path/to/wss.json build/local-router/gridpico_local_router
```

Clients trust the server chain and connect using the certificate name
in the configured origin. Each client presents a leaf certificate signed
by the configured client CA with an enrolled SHA-256 fingerprint. Open
`/apc/`, `/diagnostics/` or `/fm/` when available on that HTTPS origin.
Select bindings explicitly. A native client can use a
standard WebSocket library with the same origin, enrolled certificate and
binary MIDI frames at `/midi/<port-id>`.

Ask the router operator for the published directional Port IDs before
binding. The operator can check them in the local CLI's `inventory`.
Route configuration and CLI inventory remain at
[loopback Diagnostics](http://127.0.0.1:8767/diagnostics/) on the router host.
HTTPS/WSS serves MIDI only. The standalone Diagnostics file keeps its
direct-file Web Serial connection.

Both listeners share one runtime and binding table. Restart the host to
apply changes to credentials or publication. Native clients on the
development host verified mutual TLS against a Pi listener, exact managed
and Raw MIDI receipt, the 40-pad restore, scoped release, and fresh
binding without replay. Desktop Chromium client-certificate selection
and browser MIDI receipt through the authenticated remote listener remain
pending. [Two-host evidence](../ROADMAP.md#approved-tonight-delivery)
[Cross-host admission and routing contract](../api/websocket-midi.md)
## Separate Pico service

The Pico relay is an independent, opt-in process on the nominated isolated
`https://192.168.0.223:19445` origin. Build the browser bundle and launch it
with the private service config and the two HAL helpers:

```sh
python3 connections/build-web-ui.py
node connections/pico-host.js /absolute/private/pico-service.json /absolute/hal/pico_cdc.py /absolute/hal/pico_midi.py
```

Use a dedicated enrolled maintenance certificate for `/pico-cdc/`. The
console reports the configured Pico identity, then enables commands after
the selected helper verifies and opens CDC. It carries opaque serial
bytes and has one attachment. Enrolled MIDI clients use the separate Pico
Home and slots 1, 2, 3 and 5. Both roles validate the server chain,
client enrollment, Host and WebSocket Origin.

For a maintenance-only service, set `"midiClientFingerprints": []`
explicitly. Enroll at least one maintenance leaf in
`maintenanceClientFingerprints`. Those clients can open the Pico console
and its CDC socket. Musical tools and ports require a separate MIDI
enrollment.

To give one client both roles, enroll its leaf fingerprint in the optional
`midiCdcClientFingerprints` list. Keep all three lists disjoint. A combined
client can open **Pico CDC maintenance** from Pico Home and use the existing
MIDI ports on the same authenticated origin. CDC still admits one attachment
across all clients. You can leave both split lists empty when the combined
list contains at least one enrolled leaf.

The MIDI helper owns release cleanup for bytes it accepted on its
verified cable. If that cleanup cannot be confirmed, the service blocks
that send slot pending native recovery. Verify native recovery before
restarting after a service failure while the device is still attached.
[Pico service contract](../api/pico-target.md#dedicated-pico-wss-service)

For an explicitly enabled private-LAN Pico console, pass
`--trusted-lan-pico-cdc-config /absolute/private/lan-pico-cdc.json` to
`connections/local-host.js` with its existing trusted-LAN options. Give that
file absolute `target` and `cdcHelper` paths. The selector must identify a
specific Pico in `usb-serial` mode. Open
`http://192.168.0.223:18768/pico-cdc/` directly. Any admitted client on the
private subnet can claim the single console attachment and issue the
Pico's serial commands.
