# Pi router deployment

The Pi Zero 2 W runs `gridpico-router.service` as the `pi` user. The router release is pinned to `40161d9ca5cafd7eab1b47c6b0816c236cd13565` and uses the portable AArch64 router, Debian Trixie Node 20.19.2, and locked `ws` 8.23.0. The recorder uses stock FluidSynth 2.4.4 and the separately packaged TimGM6mb SoundFont.

| Endpoint | Access |
| --- | --- |
| `http://192.168.0.223:18768/` | Trusted private LAN, bound to `wlan0`, with actual source addresses in `192.168.0.0/24`. Serves APC and Recordings over plaintext HTTP/WS. MIDI publication is Keybed input 1, Panel input 2, and LED output 3. |
| `http://127.0.0.1:18767/` | Pi loopback fallback and the operator serial CLI. Access from another host uses the existing authenticated SSH tunnel. |

LAN serial is unavailable. The recorder reserves managed output 12 for its own local connection. WSS is a separate opt-in profile and is off on this deployment. The fixed address is a deployment setting, and the observed WLAN lease is dynamic. Changing the address requires a deliberate review of the pinned LAN profile and configuration before restarting.

Use direct LAN access from trusted devices on the approved subnet. The LAN listener admits by source address and has no per-user login. Eligible browsers share the same router, APC bindings and recorder slot.

Both listeners serve the same verified shared UI bundle. The per-deployment capability descriptor is served separately at `/ui-capabilities.json`.

| Listener | Bundle files |
| --- | --- |
| Pi LAN | [Manifest](http://192.168.0.223:18768/web-ui/SHA256SUMS) · [ZIP](http://192.168.0.223:18768/web-ui.zip) |
| Pi loopback through the SSH forward | [Manifest](http://127.0.0.1:18767/web-ui/SHA256SUMS) · [ZIP](http://127.0.0.1:18767/web-ui.zip) |

The LAN configuration grants the exact browser origin `http://127.0.0.1:8767` read access to the capability descriptor and WebSocket MIDI access to Keybed input 1, Panel input 2, and LED output 3. The alternate descriptor reports CLI, LED test and recording disabled. Open Recordings directly from the Pi's own Home page to use recorder controls and downloads on the same origin.

From the operator laptop, keep the forward on loopback and open the literal `http://127.0.0.1:18767/` URL. The laptop router on port 8767 is independent.

```sh
ssh -NT -o ExitOnForwardFailure=yes -L 127.0.0.1:18767:127.0.0.1:18767 pi@192.168.0.223
```

## Service and state

The Pi user owns `~/.local/share/gridpico-router/` and installs the unit at `~/.config/systemd/user/gridpico-router.service`. The following paths are relative to that data directory.

| Path | Purpose |
| --- | --- |
| `current` | Symlink to the explicitly selected `release/<commit>`. |
| `current/RELEASE` and `current/SHA256SUMS` | Pin and per-file manifest for the installed snapshot. |
| `launch.sh` | Supplies the private runtime and invokes the selected release's canonical `scripts/web-ui.sh` in prepared mode. |
| `config/trusted-lan.json` and `config/recorder.json` | Selected LAN address and recorder paths. |
| `runtime/node/` and `runtime/fluidsynth/` | Privately extracted runtime dependencies. |
| `recordings/latest.wav` | Most recent successfully completed recording. An active or failed take has no public download. |
| `state/` | SFTP staging and deployment validation artifacts. |

The launcher supplies `--runner` and the selected release's `app/profiles/pi.conf`, sets `--port 18767 --no-open --web-ui`, and passes the existing `--trusted-lan-config` and `--recorder-config` files. The previous release `048accf54233d0865404bb93e7b1158fdd2142ec` remains installed. Its exact launcher and configuration are backed up in the private `state/ui-promotion.Dr8ta8/` directory.

The user service is enabled in `default.target` with linger. A fresh SSH session verified that the service remained active after the launch connection ended. Its retry delay is ten seconds if startup fails while waiting for the configured interface. Boot configuration has been verified. Startup after an actual reboot remains untested.

From an authenticated Pi user shell:

```sh
systemctl --user status gridpico-router.service
journalctl --user -u gridpico-router.service -n 40 --no-pager
cd ~/.local/share/gridpico-router
cat current/RELEASE
(cd current && sha256sum -c SHA256SUMS)
```

An update is explicit. Build a reviewed commit, verify the archive SHA after SFTP, and extract it to a new `release/<commit>` directory. Verify its file manifest before changing `current` and record the size and SHA-256 of `recordings/latest.wav`. Defer cutover while the recorder is active. Use an atomic symlink rename and confirm `/recordings/status` reports `state: idle` with `activeId: null` immediately before restarting `gridpico-router.service`. After restart, check both listeners, the pinned bundle and the latest WAV hash and size. If health checks fail, restore the prior release symlink, launcher and exact configuration for the rollback restart. The unit never pulls from Git or deploys a new release unattended.

## Record and download

1. Open the Pi LAN Home page, then open **Web APC** and **Recordings** in separate tabs of the same origin. Wait for the APC Keybed binding.
2. In **Recordings**, click **Start** and wait for **Recording**. Play and release notes in the APC Keybed.
3. Click **Stop** and wait for **Download**. The recorder also stops automatically after 60 seconds of capture and adds a short release tail.

The Pi keeps one finalized WAV, up to 12 MiB. A successful new take replaces it. A failed take leaves the previous completed file available. To retrieve the latest file over the existing authenticated SSH connection, open SFTP and use `get`.

```text
sftp -o StrictHostKeyChecking=yes pi@192.168.0.223
get .local/share/gridpico-router/recordings/latest.wav
```

Check the SHA-256 immediately on both hosts while the same take remains latest. A new successful take replaces the Pi copy.

```sh
# On the Pi
cd ~/.local/share/gridpico-router
sha256sum recordings/latest.wav

# On the download host
sha256sum latest.wav
```

The WAV records the file-driver output of the scoped FluidSynth process. Physical speaker playback is a separate check.

## Physical Pico MIDI and CDC

The Pi's USB-connected Waveshare RP2040-Zero currently runs the independently audited, permanently flashed Gridpico RP2040 Router from source [`a3a6112`](https://github.com/NevynIt/gridpico/commit/a3a6112b121c879e3f48b06610bb884c08c7957b), USB `1209:0001`, `bcdDevice 0104`. Full-flash readback SHA-256: `88a12a212fb69104f55a75755e20ca15372bc351d4fcbffeac6765e1e263d048`. Native USB MIDI routing and CDC readiness passed the scoped bench audit; physical key presses and audible instrument output were not established. The normal Pi router service and its loopback SSH CLI remain separate.

The live direct-LAN Pico CDC console is `http://192.168.0.223:18768/pico-cdc/`, from overlay source `0fece4a` (archive SHA-256 `5f5b884bc5226b3f635359732d7dcbef58e2d7816a64fdf837149136820e727b`). Chromium passed Connect, TRACE, Send, and Disconnect/Reconnect. The rollout left existing Pi assets and the latest WAV unchanged. Ordinary `/serial` remains loopback-only; it is not exposed on LAN. The earlier original-bridge SSH status helper is superseded by the Gridpico firmware and is not its CLI. Browser-local Web Serial is separate from this Pi LAN console.

### Pico console from Diagnostics

From the Pi LAN Home page, open **Diagnostics** on the same origin. In **Serial provider**, select **Pico via Pi · LAN WebSocket CDC**, click **Connect**, enter `TRACE` or `HUBTRACE`, and click **Send**. If the connection drops, click **Connect** again after the UI reports disconnection. The live check passed in stock Chromium, including reconnect. This Diagnostics console accepts only these read-only trace commands; it does not configure routes or send MIDI. The feature is in source [`0bf9f21`](https://github.com/NevynIt/gridpico/commit/0bf9f21c19de6d8bdfddf04123785fef0867ff42).

Use the Diagnostics page and its shared MIDI provider from the same deployed Pi bundle. The overlay updates both assets, so a stale or mixed cached bundle may not show or connect the Pico provider. The live rollout verified bundle parity and left the latest WAV unchanged. The same-origin Pi LAN selector is separate from loopback CLI, ordinary `/serial`, and browser-local Web Serial.
