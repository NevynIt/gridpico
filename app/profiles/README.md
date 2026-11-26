# Desktop router profiles

With the checkout's Compose `dev` service running, launch the 18-port example from the Docker host or WSL.

```sh
./scripts/web-ui.sh --profile app/profiles/desktop.conf
```

Open `http://127.0.0.1:8767/` and select **Routing**. The launcher supplies the starter routes and keeps ports 1–6 available to their usual browser roles. The selected profile must be a regular file within this checkout. The example also selects the passive APC viewport. Its route page displays the current routes, and edits are available through Routing or the CLI.

To start the same profile directly with routes initially off, run from the checkout:

```sh
cmake -S . -B build/web-ui -DGRIDPICO_ENABLE_ALSA=OFF
cmake --build build/web-ui --target gridpico_local_router
npm --prefix connections ci --ignore-scripts
node connections/local-host.js --profile app/profiles/desktop.conf build/web-ui/gridpico_local_router 8767
```

For example, enter `route 11 18 managed on` in Diagnostics. The route is Pending until its input and output attach. The default `./scripts/web-ui.sh` uses the six-port profile. Running the router without flags starts the bare CLI profile.

In Diagnostics, `help` lists the commands available on the connected router. [Shared CLI reference](../../frontends/cli/README.md)

## 32-direction portable profile

From the checkout with the Compose `dev` service running:

```sh
./scripts/web-ui.sh --profile app/profiles/desktop-32.conf
```

This profile registers 12 devices and 36 directional ports. It admits 35 simultaneous MIDI peers, with room for 32 Managed directions while all three control directions are attached. One registered Managed direction is spare at full occupancy. The route budget is 12 Managed branches and one Raw branch.

To try a route near the end of the inventory, attach a sender to `/midi/143` and a receiver to `/midi/239` on the loopback host. In **Routing**, select **Keys A 3 → Instrument B 3**.

## Transpose or remap an active route

The shipped desktop profile enables live transforms with `note-transform on`. Use Diagnostics or the loopback host's same-origin `/serial` connection. Attach an input and an output and enable their public Managed socket route. For the desktop profile, `/midi/1` is Keybed and `/midi/12` is Instruments A / A1 when recording is disabled.

```text
route 1 12 managed on
routing
transform 1 12 7 4 if REV
```

Replace `REV` with the current revision from `ROUTING`. The example shifts notes up seven semitones and sends them on MIDI channel 4. Semitones range from −127 to 127. Notes whose shifted pitch falls outside the MIDI range are filtered. Use channel `1`–`16` to remap or `0` to preserve the input channel. A successful `TRANSFORM` reply includes the committed revision. Use `transform 1 12 0 0 if REV` with the current revision to restore identity.

Each incoming Note On or Note Off uses the active routes and their transforms at ingress. Changing a transform sends no MIDI by itself. A later Note Off uses the new pitch and channel, which may leave the former note sounding. Other branches keep their settings. New branches start at identity. Settings last for the life of an enabled branch in the running router.

To reproduce the loopback check with the stock runner, build and install the dependencies as above, then run `node scripts/transform-smoke.cjs build/web-ui/gridpico_local_router`. It starts the ordinary Web UI host on a free loopback port and verifies exact Note On and Note Off bytes on two editable branches and an unchanged control branch.

## File format

Use a whitespace-separated file. Blank lines and lines starting with `#` are ignored. Names take the rest of their line and may contain spaces. Keep names to 31 UTF-8 bytes. Each device and port ID is a positive 16-bit integer. The order of device rows and port rows sets their inventory order.

```text
limits PEERS MANAGED RAW FANOUT OCCURRENCES CREDITS
surface passive|legacy
note-transform on
device ID NAME
port ID DEVICE in|out managed|raw|panel QUEUE ROLE socket|unavailable NAME
```

Use one `limits` row and one `surface` row. The normal portable build supports up to 12 devices, 36 ports, 12 Managed routes, one Raw route and four Managed destinations per source. The parser still accepts the legacy `OCCURRENCES` and `CREDITS` fields. They do not limit current-message forwarding. Set `PEERS` to a positive limit within the number of socket ports. Each queue has 1–128 frames. The LED output needs at least 40 frames for a complete page.

`note-transform on` is optional and may appear once. Without it the profile omits the transform command. The row enables the command and leaves every new branch at identity.

Use `aux` for additional managed inputs or outputs. Dedicated roles are `keybed`, `panel`, `led`, `sequence`, `monitor` and `log`. Each can appear once with the direction and capability shown in the example. Only the configured Sequence input and LED output can use the Raw lease. An `unavailable` port stays registered for routing and has no socket attachment path. Use the direct command for a custom role or port layout. The launcher with `--web-ui` expects the six starter roles to be claimable on ports 1–6 and a peer limit of at least six.

The optional `fm` role may appear once on a socket-backed Managed output. The [FM loopback example](fm-loopback.conf) registers it at port `42`. Enable an explicit Managed route to that output after the peers attach. An active recorder reservation cannot use the same output.
