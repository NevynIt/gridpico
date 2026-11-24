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

## File format

Use a whitespace-separated file. Blank lines and lines starting with `#` are ignored. Names take the rest of their line and may contain spaces. Keep names to 31 UTF-8 bytes. Each device and port ID is a positive 16-bit integer. The order of device rows and port rows sets their inventory order.

```text
limits PEERS MANAGED RAW FANOUT OCCURRENCES CREDITS
surface passive|legacy
device ID NAME
port ID DEVICE in|out managed|raw|panel QUEUE ROLE socket|unavailable NAME
```

Use one `limits` row and one `surface` row. The desktop build backs up to 8 devices, 18 ports, 12 managed routes, one Raw route, four managed destinations per source, 50 held occurrences and 200 release credits. Set `PEERS` to a positive limit within the number of socket ports. Each queue has 1–128 frames. Managed outputs need room for the largest owed release batch, and the LED output needs at least 40 frames for a complete page.

Use `aux` for additional managed inputs or outputs. Dedicated roles are `keybed`, `panel`, `led`, `sequence`, `monitor` and `log`. Each can appear once with the direction and capability shown in the example. Only the configured Sequence input and LED output can use the Raw lease. An `unavailable` port stays registered for routing and has no socket attachment path. Use the direct command for a custom role or port layout. The launcher with `--web-ui` expects the six starter roles to be claimable on ports 1–6 and a peer limit of at least six.
