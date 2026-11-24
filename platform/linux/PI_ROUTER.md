# Pi router deployment

The Pi Zero 2 W runs `gridpico-router.service` as the `pi` user. The router release is pinned to `048accf54233d0865404bb93e7b1158fdd2142ec` and uses the portable AArch64 router, Debian Trixie Node 20.19.2, and locked `ws` 8.23.0. The recorder uses stock FluidSynth 2.4.4 and the separately packaged TimGM6mb SoundFont.

| Endpoint | Access |
| --- | --- |
| `http://192.168.0.223:18768/` | Trusted private LAN, bound to `wlan0`, with actual source addresses in `192.168.0.0/24`. Serves APC and Recordings over plaintext HTTP/WS. MIDI publication is Keybed input 1, Panel input 2, and LED output 3. |
| `http://127.0.0.1:18767/` | Pi loopback fallback and the operator serial CLI. Access from another host uses the existing authenticated SSH tunnel. |

LAN serial is unavailable. The recorder reserves managed output 12 for its own local connection. WSS is a separate opt-in profile and is off on this deployment. The fixed address is a deployment setting, and the observed WLAN lease is dynamic. Changing the address requires a deliberate review of the pinned LAN profile and configuration before restarting.

Use direct LAN access from trusted devices on the approved subnet. The LAN listener admits by source address and has no per-user login. Eligible browsers share the same router, APC bindings and recorder slot.

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
| `launch.sh` | Starts the portable router and Node host with the Pi profile and enabled local configuration. |
| `config/trusted-lan.json` and `config/recorder.json` | Selected LAN address and recorder paths. |
| `runtime/node/` and `runtime/fluidsynth/` | Privately extracted runtime dependencies. |
| `recordings/latest.wav` | Most recent successfully completed recording. An active or failed take has no public download. |
| `state/` | SFTP staging and deployment validation artifacts. |

The user service is enabled in `default.target` with linger. A fresh SSH session verified that the service remained active after the launch connection ended. Its retry delay is ten seconds if startup fails while waiting for the configured interface. Boot configuration has been verified. Startup after an actual reboot remains untested.

From an authenticated Pi user shell:

```sh
systemctl --user status gridpico-router.service
journalctl --user -u gridpico-router.service -n 40 --no-pager
cd ~/.local/share/gridpico-router
cat current/RELEASE
(cd current && sha256sum -c SHA256SUMS)
```

An update is explicit. Build a reviewed commit, verify the archive SHA after SFTP, and extract it to a new `release/<commit>` directory. Verify its file manifest before changing `current`. Use an atomic symlink rename and restart only `gridpico-router.service`. Check both listeners and the selected recorder configuration. Keep the prior release and corresponding config until health checks pass, and restore them if the new release fails. The unit never pulls from Git or deploys a new release unattended.

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

If a Pico is connected and the host enumerates its USB MIDI ports and CDC serial port, select the intended MIDI input or output separately from the CDC CLI connection. Use an environment where the browser supports Web MIDI and Web Serial and grants access to those devices. The Pi's plaintext LAN page uses WebSocket MIDI and cannot provide that browser secure context. The routed recorder and SSH CLI do not require a Pico. Physical Pico MIDI and CDC acceptance is still pending.
