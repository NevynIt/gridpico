# Serial CLI

The CLI is one bounded command service shared by the Pi terminal, Pico CDC and Diagnostics. `gridpico_cli_execute_line` handles a complete line; the caller supplies the writer. The command table drives dispatch and `help`, and only lists forms available from the bound providers. Known commands without a required provider return `ERR UNAVAILABLE`; malformed or unknown commands return `ERR INVALID`.

Common read commands are:

```text
help
help inventory
inventory
status
```

Depending on the selected runtime, help may also list `color get/set`, `logs`, `route`, `routing`, `trace`, `hubtrace` and `BOOTSEL`. Use IDs printed by `inventory`. The default local profile's IDs and route examples are in the [local router contract](../../api/local-router.md#serial-cli-route-control). Typed color, status and log providers are described in [Patch controls](../../api/patchbay-controls.md#read-only-diagnostics-commands); optional Routing pagination is in [Routing](../../api/routing.md#routing-through-the-local-cli).

On a profile with the target Routing binder, `help route` includes the guarded managed form `route SRC DST managed on|off if REV`. It rechecks the complete Routing revision before mutation; stale revisions return `ERR UNAVAILABLE` without a write. Raw continues to use its separate lease.

## Binding

Keep `gridpico_cli_frontend` in static or other caller-owned long-lived storage. It contains the embedded-cli arena and Patch snapshot scratch. For an interactive terminal, initialize it with the shared Session callbacks, then feed bytes with `gridpico_cli_receive` and process them on the serialized command context:

```c
gridpico_cli_init(&cli, &patch_session,
                  gridpico_patch_session_read_snapshot,
                  gridpico_session_read_device_color,
                  gridpico_session_set_device_color,
                  log_context, read_logs, serial_write, serial_context);
```

A caller using only `gridpico_cli_execute_line` may zero-initialize the frontend and set `session_context` and `read_snapshot` directly; bind optional providers before the first command. `gridpico_cli_bind_apc_patch` takes caller-owned snapshot scratch. When bound, inventory uses live APC row/column IDs and status uses the APC graph revision and visible cell states. With no color provider, this Pico profile emits `-`; a known unset Pi color remains `unset`.

## Serial framing

The common line limit is 63 printable ASCII bytes before the line terminator. Pi accepts CR, LF or CRLF. Pico CDC accepts LF or CRLF; overlong input is discarded through LF and reports one `ERR CAPACITY`. Diagnostics submits one LF-terminated command at a time.

Authorized portable loopback CLI leases start in Human reply format; this per-lease behavior does not imply a Pi deployment. At an idle command boundary, `format machine` or `format human` selects the format for that lease. Each successful selection, including a repeated one, returns exactly `FORMAT MACHINE\r\nOK\r\n` or `FORMAT HUMAN\r\nOK\r\n`, without editor echo or prompt. The next Human command uses the editor framing. A replacement lease starts in Human again.

Human replies show readable outcomes and names with editor echo and prompt. Machine replies use bounded structured records, end in exact `OK\r\n` or `ERR ...\r\n`, and have no echo or prompt. Both formats keep the same commands, providers and operation results. The portable loopback Routing helper selects Machine on its current lease and waits for the complete matching acknowledgement before parsing any reply. If the acknowledgement is missing or unexpected, it stops typed operations and leaves the transcript for review. The portable interactive Pi terminal uses the same service output with the Human editor framing described above. This behavior does not change Pico CDC defaults or platform framing. The Pico adapter stages a bounded complete response before sending it and does not interleave it with a trace.

Pico `trace` and `hubtrace` (also the exact uppercase aliases `TRACE` and `HUBTRACE`) start asynchronous platform streams. A successful start emits no immediate `OK`; trace rows and `END TRACE` / `END HUBTRACE` use LF. Refusals use the normal CRLF `ERR` record.

Recovery is a raw platform command: exact uppercase bytes `BOOTSEL` followed by LF while DTR is asserted. Only the platform byte recognizer accepts it; the normalized CLI executor never resets the board. When available, `help BOOTSEL` documents the operation. The successful recovery acknowledgement is exactly `OK BOOTSEL\n`.

See the [shared Patch CLI contract](../../api/patchbay.md#serial-cli-and-standalone-diagnostics), [Pico/Pi target contract](../../api/pico-target.md#first-shared-cli-on-pico-and-pi) and [Diagnostics browser contract](../../api/browser-midi.md#shared-in-page-adapter) for provider, queue, response-staging and target acceptance requirements.

## Native harness

The native harness exercises the real Patch runtime, shared Session colors, APC projection, log provider, route callbacks, help and terminal/direct reply parity. Configure a clean Release build and run it with strict C11 diagnostics:

```sh
cmake -S . -B /tmp/gridpico-cli-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/gridpico-cli-build --target gridpico_patch_runtime gridpico_local_runtime
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -I. \
  frontends/cli/native_harness.c \
  /tmp/gridpico-cli-build/libgridpico_patch_runtime.a \
  /tmp/gridpico-cli-build/libgridpico_local_runtime.a \
  -o /tmp/gridpico-cli-harness
/tmp/gridpico-cli-harness
```

The terminal adapter uses upstream `funbiscuit/embedded-cli` v0.1.4, pinned at commit `ffa8014c6230ba809c6a655ccc611a6c4689113f`. The vendored header SHA-256 is `7b6db7fdd691790816f279f850be977433b01903f52f6dd53c0b0569718e6fe2`; its upstream MIT notice is retained. See [library selection and bounds](../../api/patchbay.md#cli-library-choice).
