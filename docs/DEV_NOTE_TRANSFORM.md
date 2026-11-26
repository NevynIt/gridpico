# DEV transpose and channel remap

This source release runs the normal Linux Gridpico router and localhost WebSocket host. Its opt-in launcher allows one managed route, Keybed `1` to Instruments A `12`, to be transformed through the existing serial CLI. The route must be Active. The transform starts at identity and lives only for the running process.

## Quickstart

From the `dev/managed-note-transform` checkout with a C compiler, CMake, Ninja, Node.js 18+ and npm:

```sh
npm ci --prefix connections --ignore-scripts --no-audit --no-fund
cmake -S . -B build/dev-transform -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/dev-transform --target gridpico_local_router
node scripts/dev-transform-smoke.cjs
```

The smoke test opens temporary localhost listeners, activates the route, then sends generated Note On and Note Off to `/midi/1`. With +7 semitones and output channel 4, `90 3D 62` and `80 3D 1E` arrive as `93 44 62` and `83 44 1E` at `/midi/12`. The comparison receiver at `/midi/6` gets the original bytes. The test checks release of a held note during a change, restores identity and closes both hosts. Look for `PASS exact selected-route transform and identity restoration`.

## Use the route

Run the normal host with the opt-in runner. Use a free loopback port and press Ctrl+C to stop.

```sh
node connections/local-host.js --profile app/profiles/desktop.conf ./scripts/dev-transform-runner 8767
```

Use the same-origin WebSocket clients for `/serial`, source `/midi/1` and receiver `/midi/12`. On the serial client, use `route 1 12 managed on`, then `routing` to obtain the current revision. Apply `transform 1 12 7 4 if REV` with that revision. Use `transform 1 12 0 0 if REV` with a fresh revision to return to the original pitch and channel. The CLI rejects stale revisions and other pairs. A transform change releases affected held notes on the selected route before accepting fresh notes.

Verified on Linux x86-64 with generated MIDI bytes over real localhost WebSockets. The GNU 12.2.0 Release build with `-Wall -Wextra -Wpedantic -Werror` reproduced runner SHA-256 `9bff66d4d628c8f9a06028b03f115b354c9b87e03dbe84f8b73cd1f3449509f9`. This release covers the DEV route and wire bytes. Physical instruments, Pico, Pi, acoustic output and persistence were outside this run.
