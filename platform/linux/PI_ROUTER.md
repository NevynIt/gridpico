# Pi router deployment

The Pi Zero 2 W runs `gridpico-router.service` as the `pi` user. The canonical Pi release and the attached Pico's normal firmware are both built from [`3ae97ba4b2f37b486812ab389066c6bc2bc551a0`](https://github.com/NevynIt/gridpico/commit/3ae97ba4b2f37b486812ab389066c6bc2bc551a0). The Pi uses the portable AArch64 router, Debian Trixie Node 20.19.2, and locked `ws` 8.23.0. The recorder uses stock FluidSynth 2.4.4 and the separately packaged TimGM6mb SoundFont.

| Endpoint | Access |
| --- | --- |
| `http://192.168.0.223:18768/` | Trusted private LAN, bound to `wlan0`, with actual source addresses in `192.168.0.0/24`. Serves APC, Recordings, and same-origin Pico Diagnostics over plaintext HTTP/WS. MIDI publication is Keybed input 1, Panel input 2, and LED output 3. |
| `http://127.0.0.1:18767/` | Pi loopback fallback and the Pi router's operator CLI at `/serial`. Access from another host uses the existing authenticated SSH tunnel. |

The Pi router's `/serial` is loopback-only. The LAN Pico console uses the selected USB device through `/cdc`. The recorder reserves managed output 12 for its own local connection. WSS is a separate opt-in profile and is off on this deployment. The fixed address is a deployment setting, and the observed WLAN lease is dynamic. Changing the address requires a deliberate review of the pinned LAN profile and configuration before restarting.

Use direct LAN access from trusted devices on the approved subnet. The LAN listener admits by source address and has no per-user login. Eligible browsers share the same router, APC bindings and recorder slot.

Both listeners serve the same verified shared UI bundle, with manifest SHA-256 `a1eece7ed45e6ac982a6e0bae14cf6073c247b1b92d7db44f1f7c2896a7d529c`. The per-deployment capability descriptor is served separately at `/ui-capabilities.json`.

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

The launcher supplies `--runner` with the selected release's `gridpico_local_router` and `--profile` with its `app/profiles/pi.conf`. It sets `--port 18767 --no-open --web-ui` and passes the private `--trusted-lan-config`, `--trusted-lan-pico-cdc-config`, and `--recorder-config` files. This release uses AArch64 runner SHA-256 `9d13d3878b09d0542348cccbabb43b27c3b8cbb637a544b70992ce6c65e1476e`. The recorder helper was carried forward byte-for-byte.

The user service is enabled in `default.target` with linger. A fresh SSH session verified that the service remained active after the launch connection ended. Its retry delay is ten seconds if startup fails while waiting for the configured interface. Boot configuration has been verified. Startup after an actual reboot remains untested.

From an authenticated Pi user shell:

```sh
systemctl --user status gridpico-router.service
journalctl --user -u gridpico-router.service -n 40 --no-pager
cd ~/.local/share/gridpico-router
cat current/RELEASE
(cd current && sha256sum -c SHA256SUMS)
```

### Update and rollback

Build a reviewed commit, verify the archive SHA-256 after SFTP, and extract it to a new `release/<commit>` directory. Verify its file manifest and record the size and SHA-256 of `recordings/latest.wav`. Defer cutover while the recorder is active, clients are connected, or the Pico tty is held. Use an atomic symlink rename and confirm `/recordings/status` reports `state: idle` with `activeId: null` immediately before restarting `gridpico-router.service`. After restart, check both listeners, the pinned bundle, and the latest WAV hash and size. The unit never pulls from Git or deploys a new release unattended.

The prior canonical release `5a12c9fced437b3f37ccda4a481084ae239889c0` remains installed. The prior symlink, launcher, configuration and WAV snapshot are retained in the private `state/cli-promotion.DHYqOJ/` directory. If health checks fail, restore the prior release symlink, launcher and exact configuration for the rollback restart.

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

## CLI and Pico Diagnostics

Open **Diagnostics** at `http://127.0.0.1:18767/diagnostics/` through the SSH forward. Select **Loopback CLI** and click **Connect**. Use `help` or `help inventory` for command syntax, and `inventory` or `status` for readback. This CLI operates the Pi router.

### Pico console from Diagnostics

Open **Diagnostics** at `http://192.168.0.223:18768/diagnostics/`. Select **Pico via Pi · LAN WebSocket CDC** and click **Connect**. Send `TRACE` or `HUBTRACE` for readback. Click **Check Pico commands** to query live help and enable `help`, advertised help topics, and the advertised `inventory` and `status` commands. This console admits read-only commands to the selected Pico.

Live browser testing verified both paths and the rollout preserved the latest WAV. [Current release evidence](../../ROADMAP.md#current-product-status)

The attached Waveshare RP2040-Zero enumerates at USB path `1-1` as `1209:0001`, `bcdDevice 0104`. Firmware identity, direct CDC recovery and flash rollback are documented with the target. [Pico target and recovery](../../api/pico-target.md)
