# Pi LAN router and recorder

**Owner** Architect. **Status** Pi release `048accf54233d0865404bb93e7b1158fdd2142ec` is deployed. Independent testing verified the trusted-LAN browser recorder, the SSH fallback and service survival across SSH disconnects. This profile uses the production router and a separate FluidSynth peer. [Pi maintenance](../platform/linux/PI_ROUTER.md) · [Local router](local-router.md) · [Instrument peers](instrument-peers.md) · [Authenticated WSS](websocket-midi.md)

## Pi service and admission

Run one portable `gridpico_local_router` under the existing Node host with the locked `ws` dependency. The Pi deployment uses a least-privileged boot service, independent of an SSH session or the laptop instance. HAL owns the on-Pi runtime and service setup. Assembly owns the build and router profile. Connections owns HTTP, WebSocket admission, the recorder presentation and download. The app can run with neither of the new options. Existing localhost and WSS profiles keep their published behavior.

The explicit host option `--trusted-lan-config FILE` reads this JSON configuration for the intended private WLAN:

```json
{"interface":"wlan0","address":"192.168.0.223","port":18768,"cidr":"192.168.0.0/24"}
```

Require the exact configured IPv4 address and subnet on the named interface. Bind the named address only. Check the actual TCP peer IPv4 address against that subnet. Ignore forwarded headers. Require one exact `Host` of `192.168.0.223:18768`, and one `Origin` of `http://192.168.0.223:18768` on upgrades and writes. A supplied read Origin must also match. Reject `cross-site` and `same-site` Fetch Metadata on recorder requests. The listener enables no CORS or proxy mode. Startup configuration, interface or bind failure exits nonzero with a local diagnostic so the boot supervisor can retry. Establish interface address stability, restart on interface loss and verify startup after a real boot. The LAN profile trusts peers on the allowed subnet and uses plaintext HTTP and WS. Keep it on the operator-authorized private LAN.

Serve `http://192.168.0.223:18768/` with links to the available APC controller and, when configured, Recordings. Publish existing per-port binary MIDI only on `1` Keybed input, `2` Panel input and `3` LED output, with direction checked against the runtime before upgrade. The APC response alone marks its adapter with `data-gridpico-connection="trusted-lan"`. That adapter exposes `gridpicoLocal.trustedLan` and `startupPorts` for `keybed:1`, `panel:2`, `led:3`. Attach those roles once per page and show real busy or unavailable states. Present the Home link and keep Diagnostics and unpublished roles out of this profile's auto-connect controls. Reject `/serial`, undisclosed MIDI IDs, administration and unserved assets. LAN status is recorder-only. Existing WSS remains an independent, operator-enrolled profile and serves no added recorder endpoints in this increment.

Pi loopback is `127.0.0.1:18767`. The authorized operator can forward laptop port `18767` to Pi loopback port `18767` over the existing strict SSH path. The browser uses the exact loopback Host and Origin, and the laptop's independent app keeps its existing `8767`. Use the Pi loopback CLI for inventory and routing. Keep the listener on loopback and limit the SSH forward itself to laptop loopback. [OpenSSH local forwards](https://man.openbsd.org/ssh#L)

An ordinary browser on the LAN uses the same-origin WebSocket MIDI binding. This plain LAN origin does not provide the secure context for browser Web MIDI or Web Serial. Future physical Pico acceptance keeps the selected MIDI ports separate from the CDC serial CLI. Test a supported browser's permission and selection flows on a secure or supported local origin, including the separately gated mounted read-only GRIDPICO pages. [Web MIDI](https://www.w3.org/TR/webmidi/) · [Web Serial](https://wicg.github.io/serial/)

## Optional recording

Supply `--recorder-config FILE` to Node with absolute operator-owned `soundfont`, `alsaSender` and `spool` paths. Attach the recording controls only on the Pi loopback and configured trusted-LAN listeners. Use a private spool and an installed SoundFont. The browser supplies no command, file path, MIDI ID or engine setting. A validated recorder option passes `--recorder` to the portable runtime with the Pi registration profile. Require socket-admitted managed input `1` and managed output `12`. Enable `1 → 12` through the existing typed Session setter before ready. It is Pending until both endpoints attach. Preserve the behavior of the existing no-recorder startups.

Reserve `12` for the exact process-created loopback client used by this recorder. Reject normal browser, forwarded and LAN claims to this port. Node binds the local output using the existing receive-only binary MIDI contract and a fresh generation. Assembly starts a fresh stock FluidSynth file-driver engine for each take. The local recorder forwards the accepted Note On and Off triplets over an isolated ALSA source to the unique writable engine port corroborated by the launched engine PID and exact port name. HAL owns that corroboration, checked subscription and loss reporting. Ambiguity or departure fails the take. A failed local ALSA send is distinct from observed synthesis. There is no numeric-address fallback or automatic rebind. The router and firmware remain MIDI-only. [ALSA client PID](https://www.alsa-project.org/alsa-doc/alsa-lib/group___seq_client.html) · [FluidSynth file driver](https://www.fluidsynth.org/api/settings_audio.html)

Allow one active take. Start a monotonic 10-second readiness deadline at Start. Report `recording` only after the engine destination is verified and the WS binding is admitted. Capture for at most 60 seconds after readiness. On Stop or timeout, stop accepting attacks, release the take's held channel/key pairs at its verified synth, then close only the recorder WS. Allow up to one second of release tail and up to five seconds for a clean engine exit. Other routed sinks and source bindings continue independently. Recorder loss, watchdog expiry, ALSA ambiguity or engine failure stops the take, retires its binding and preserves the previous completed file. A fresh engine starts empty and the recorder ignores unmatched Offs, including router reconciliation for previously retired output generations. A fresh take waits for fresh attacks.

Write WAV to a fresh private take directory. After clean helper completion and zero exit, validate finalized stereo 44.1 kHz signed 16-bit PCM, nonempty data, consistent RIFF and data lengths, and total size no greater than 12 MiB. Only then atomically expose the latest completed file. Preserve the previous completed file across failed takes. Keep at most one current artifact and one in-progress take. A restarted service must never promote a leftover partial file.

### Browser workflow

Use exact paths and methods. The recorder's HTTP calls control this host-local job and retrieve its result. MIDI and serial CLI retain their existing independent interfaces across product components.

| Method and path | Result |
| --- | --- |
| `GET /recordings/` | Start, Stop, status and finalized download presentation |
| `GET /recordings/status` | `{state,activeId,completedId,actionToken}` with state `idle`, `starting`, `recording`, `stopping` or `failed`. Missing IDs and unavailable tokens are `null`. |
| `POST /recordings/start` | Starts a new take when no take is active |
| `POST /recordings/<id>/stop` | Stops only the matching active take, or cancels it before readiness |
| `GET /recordings/<id>.wav` | Streams only the current completed take as `audio/wav` with an attachment filename |

Generate IDs as 32 lowercase hex digits. Keep a bounded, unpredictable action token per listener and current allowed transition. POSTs have empty bodies and the exact `X-Gridpico-Record-Action` token from status. Consume it atomically on an admitted mutation and invalidate it on the state change. Reject stale tokens, wrong take IDs, duplicate Starts and concurrent Stop. Serve these responses with `Cache-Control: no-store`, same-origin resource policy and frame denial. Resolve only the server-owned completed ID and fixed file under the spool. In-progress and failed takes have no download. Bound streamed downloads independently of the recording and MIDI queues.

## Verification and remaining checks

Open [Pi Home](http://192.168.0.223:18768/) on the trusted LAN. Use **Web APC** to play and **Recordings** to start, stop and download a finished take. The SSH fallback opens [Pi loopback](http://127.0.0.1:18767/) with the forward active. The laptop's separate local app uses port `8767`. The Pi's authenticated WSS option was disabled for this deployment.

On the Pi, the earlier recorder-imposed `RLIMIT_FSIZE` caused stock FluidSynth 2.4.4 to exit with `SIGXFSZ` before readiness. The deployed fix samples the actual private WAV size every 100 ms and checks it again before closing the engine. It retains the 60-second capture deadline and the 12 MiB finalized-file gate. A stock Pi take reached readiness and completed with the watchdog active.

Independent Chrome testing sent `90 30 60` and `80 30 00` from the LAN APC. The stock FluidSynth recording had an attack and decaying release. Start and Stop returned `202`. The active take's download returned an empty `404`, and a duplicate Start returned `409`. The 479,020-byte, signed 16-bit stereo 44.1 kHz WAV downloaded through both the LAN page and the SSH fallback matched the Pi's finalized file. Its SHA-256 was `7344493a735b87c1290d4ba68f643dcc31c0fd95b58c99bdb21eb6d828ed2118`. The owner separately stopped a held note on the actual Pi and retrieved a matching WAV with SHA-256 `29000c85260d97308a3556654557f41c9b436add8183d24cc43bfa23f0eafc2f`.

The automatic capture limit produced a 10,778,668-byte WAV, 61.1033 seconds including the release tail. The LAN download matched the Pi file at SHA-256 `24f6455f1ba9f1f2fa4a4a154c2c2ca222fe6150888fdd9dd8d47ef6602f0309`. A second browser download of that larger WAV over the renewed SSH tunnel exceeded the test harness's eight-second wait. On the actual Pi, LAN admission denied wrong Host or Origin, serial over HTTP and WS, and unpublished MIDI ports. Recorder HTTP rejected cross-site Fetch Metadata. An isolated test verified out-of-subnet denial. The real binary serial CLI over the SSH fallback read `1 → 12` Active during the take. A separate development router served port `8767` alongside the Pi.

The Pi service stayed on the same PID through SSH disconnect and reconnect, with the unit enabled and linger active. Direct LAN recheck returned HTTP `200` after both tunnels were closed. A controlled restart test recovered the previous valid WAV under a fresh download ID after an interrupted recording. The deployed Pi run exercised SSH reconnect without restarting the service. A real Pi reboot, the operator's laptop, physical APC and Pico MIDI/CDC, and speaker playback remain unverified.
