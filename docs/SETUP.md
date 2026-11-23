# Development environment and first session

The initial environment is a new gridpico container built from the supplied recipe, adapted from rpico-bridge's tooling. It does not use the old product repository as a source dependency and does not share its working tree.

## 1. Use the same Docker daemon

Extract/clone gridpico into its own writable folder, outside another Git checkout. On Windows, use WSL and the same Docker Desktop context as rpico-bridge. A Linux filesystem checkout is preferable to mixing shell scripts with Windows line endings.

Check the existing external volumes without printing their contents:

```sh
docker context show
docker volume inspect pico-dev-codex pico-dev-gh
```

Compose declares the exact volume names with `external: true`. It will fail if they are missing instead of silently giving you unrelated empty, project-prefixed credential volumes. Do not create new empty volumes to hide a wrong Docker context. Matching volume names on another Docker daemon do not share data. [S3]

The container retains user `pico`, UID/GID 1000, `HOME=/home/pico`, `CODEX_HOME=/home/pico/.codex` and `GH_CONFIG_DIR=/home/pico/.config/gh`. Check host checkout permissions if the container cannot write it; do not blanket-change credential-volume ownership.

## 2. Clone the existing repository

```sh
git clone https://github.com/NevynIt/gridpico.git
cd gridpico
```

The human created `NevynIt/gridpico` as a public repository with an MIT license. Use a separate checkout; do not copy it inside the rpico-bridge working tree. A supplied ZIP can also be extracted as a standalone folder, but cloning is preferable when continuing development with Git.

For Git writes inside the container, the existing `gh` login can be used with an HTTPS remote. If needed, configure the helper **only in this checkout**, without rewriting shared configuration:

```sh
git config --local credential.https://github.com.helper ''
git config --local --add credential.https://github.com.helper '!gh auth git-credential'
```

Set your Git author identity in this checkout if it is not already available; no credentials or private SSH keys are placed in the repository. [S4]

## 3. Build and enter the new development container

```sh
./scripts/build.sh
./scripts/start.sh
docker compose -f .devcontainer/compose.yaml exec dev bash
```

After authentication (section 4), open the interactive Herdr TUI from the host:

```sh
./scripts/herdr.sh
```

These scripts use the existing Compose definition and the current server's Docker daemon. Building does not start anything; starting does not rebuild; opening Herdr does not create an agent. They work from any working directory. If you only want to enter the container shell, use the Compose `exec dev bash` command above.

Alternatively, open the folder in VS Code and select **Dev Containers: Reopen in Container**. Both paths use `.devcontainer/compose.yaml`. Closing VS Code does not automatically stop the container; lifecycle remains deliberate.

The recipe retains Debian Bookworm, Pico SDK/picotool 2.2.0, TinyUSB 0.21.0, Pico-PIO-USB 0.7.3, ARM GCC 12.2 and Herdr 0.9.5 from the inspected source. It adds Linux MIDI development headers and small shell/terminal utilities. Codex uses the source recipe's official installer; its exact installed version is not pinned, so inspect it locally. No copied model policy or global Codex configuration is installed.

The recipe includes amd64/arm64 host selection; successful image builds on each still need verification. Container architecture is **not** evidence that the future musical application supports every Linux ARM ABI. Raspberry Pi CPU/OS targets and Windows build/test environments must be selected and verified by the planning team. Windows product validation is not supplied by running a Linux container.

The base container runs without USB, sound-device or Docker-socket passthrough. The local Linux ALSA bench can opt in to the sequencer device below.

## 3a. Opt in to the ALSA sequencer on a Linux Docker host

Run the following from the checkout on the Linux host that runs the Docker daemon. The opt-in maps the single character device `/dev/snd/seq` read/write and keeps the existing Compose service and persistent mounts. For a bench that also needs PCM or control access, repeat `--alsa-node` with the exact paths of the devices you intend to use. The script checks that each path is a present character device and derives its numeric group separately.

Check the existing image with a disposable container while the current dev service stays up:

```sh
./scripts/start.sh --alsa-probe
```

The disposable container uses the existing local image, a read-only root filesystem, and no network or credential mounts. The probe derives each supplementary group from the numeric device GID. For a device in group zero it uses the established `pico` group and checks effective access through the device permissions or ACL. It verifies the non-root container UID, numeric groups and matched device GIDs, then calls ALSA `snd_seq_open` in duplex mode. An optional PCM or control node is separately opened read/write to check effective device access. This uses kernel and ALSA checks and makes no assumption about the host login UID. Resolve denied access through the host's approved group or ACL setup before enabling the override.

Validate the opt-in Compose configuration without starting anything:

```sh
./scripts/start.sh --alsa-config
```

For example, with those exact playback and control nodes present and intended for use:

```sh
./scripts/start.sh --alsa-probe --alsa-node /dev/snd/pcmC0D0p --alsa-node /dev/snd/controlC0
./scripts/start.sh --alsa-config --alsa-node /dev/snd/pcmC0D0p --alsa-node /dev/snd/controlC0
```

When ready for the deliberate dev service change, build the ALSA-enabled image and start the existing service with the override:

```sh
seq_gid="$(stat -Lc '%g' -- /dev/snd/seq)"
export GRIDPICO_ALSA_SEQ_GID="$seq_gid"
if [ "$seq_gid" = 0 ]; then export GRIDPICO_ALSA_SEQ_GID=1000; fi
docker compose -f .devcontainer/compose.yaml -f .devcontainer/compose.alsa.yaml build dev
./scripts/start.sh --alsa
docker compose -f .devcontainer/compose.yaml exec -T dev aconnect -l
```

Building leaves the running service in place. Starting with the new device or image can replace the dev container; choose the restart window around active sessions. The start script repeats the disposable open before invoking Compose up. The checkout, Codex and GitHub volumes, and Herdr state remain mounted through the same service. Build with the override for the targeted `alsa-utils` tools and use `--alsa` with the same optional `--alsa-node` arguments on subsequent starts to retain the selection. The base `./scripts/start.sh` uses the base configuration. Herdr and the Web scripts attach to the same running service.

## 4. Sign in and verify authentication without exposing secrets

Inside the interactive container shell, run the shared-login helper:

```sh
bash .devcontainer/sign-in.sh
```

It reuses valid existing logins; otherwise it starts Codex device authentication and GitHub CLI's web-based device flow. The helper checks that both configured credential directories are real persistent mounts and refuses to log in to an ephemeral directory. For an M5/server install, the volumes exist on **that server's Docker daemon**, not on the laptop.

Then check status:


```sh
gh auth status --hostname github.com
codex login status
codex --version
herdr --version
python3 tools/check-starter.py
```

The existing file-backed logins should be available when they are valid and the same volumes/UID are used. Expired credentials or different credential-store settings still need normal authentication handling; matching mount names cannot guarantee validity. Do not print `auth.json`, token values or `hosts.yml`. [S5]

The shared Codex volume contains **configuration and sessions as well as credentials**. Keep gridpico role instructions and local settings in this checkout. Do not overwrite `~/.codex/config.toml`, change global model settings, log out globally or delete sessions belonging to rpico-bridge. Concurrent projects must not attach to the same native Codex session.

Herdr state is separate in `gridpico-herdr`; runtime roster metadata goes under ignored `.team/local/`. `HERDR_CONFIG_PATH` selects this checkout's `.team/herdr.toml`, which disables automatic restoration of all agents. The intended sequence is explicit human startup of Session Manager followed by requested lifecycle actions. Inspect installed behavior before relying on any Herdr session command. [S6]

## 4a. M5 Codex execution mode

The M5 cannot start Codex's nested Bubblewrap sandbox; the attempted Landlock option also failed. The human explicitly approved **Docker-only isolation** for gridpico. Do not run `scripts/check-codex-sandbox.sh` as a startup gate. Read [Codex runtime](CODEX_RUNTIME.md) for this deployment's security trade-off and session flags.

## 5. Start only Session Manager

From `/workspace/gridpico`, start Herdr's TUI:

```sh
herdr
```

In a **shell pane inside Herdr**, start Session Manager using the approved Docker-only execution flags:

```sh
bash scripts/start-session-manager.sh
```

The script sets `--sandbox danger-full-access --ask-for-approval never` for Session Manager **itself** and provides its proposed model/effort. Session Manager uses the **same permission flags**, plus role-specific model/effort from [MODEL_PLAN.md](../.team/MODEL_PLAN.md), when creating Strategist, Architect and Project Manager. Check model availability in the installed Codex CLI before launch. The human then works through Strategist. Initial startup does not create a development team, implement modules or require checkpoint files.

Native Codex resume accepts an explicit session ID; Session Manager records actual IDs and resumes those same sessions. Do not use a guessed pane number or an ambiguous `--last`, and do not use fork as a substitute. Herdr's native integration/session restore must be checked against the installed release. Stored conversation continuity is not an unlimited model context window. [S5, S6]

## 6. Persistence and verification

The checkout bind mount holds source/documents and ignored local roster metadata. The external Codex volume holds native session history/authentication; the external GitHub volume holds its configuration/authentication; the separate Herdr volume holds its runtime state. Preserve these volumes when rebuilding. Do not use cleanup/pruning commands that remove session or credential data.

No product CMake targets, firmware, browser application, native backends or product tests are included yet. Starter validation checks documentation/configuration consistency; it does not prove Docker builds, authentication, session resumption, real USB operation or timing. Its four-seat/no-roadmap assertions describe this initial snapshot; update or retire those assertions as approved planning progresses. Checks and omissions are recorded in [Validation](VALIDATION.md).

References `[S3]`–`[S6]` are listed in [Sources](SOURCES.md).
