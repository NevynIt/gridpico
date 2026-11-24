# Serial CLI frontend

The command service is `gridpico_cli_execute_line`. Application commands from the terminal adapter and Diagnostics use this handler. The terminal's built-in `help` and `help <command>` are presentation commands supplied by embedded-cli. Bare `help` is also supported by the service handler. The CLI reads Patch state with `gridpico_patch_read_snapshot_fn`, colors with the typed device-color callbacks and up to 16 log rows through the optional Assembly provider. It owns no device, route, color or log state. Assembly serializes calls and wires the terminal and Diagnostics to the same service. Pass `&patch_session` with `gridpico_patch_session_read_snapshot`, `gridpico_session_read_device_color` and `gridpico_session_set_device_color`; use a separate log context for `gridpico_read_diagnostic_log_fn`.

Inventory and status validate against the shared storage extents `GRIDPICO_PATCH_MAX_DEVICES` and `GRIDPICO_PATCH_MAX_PORTS`, and iterate the actual snapshot counts. The CLI color scratch uses the shared maximum device extent. This covers stage-one `2/5/2` and the Monitor `3/6/3` profile without changing the reply records or commands.

```c
gridpico_cli_init(&cli, &patch_session,
                  gridpico_patch_session_read_snapshot,
                  gridpico_session_read_device_color,
                  gridpico_session_set_device_color,
                  log_context, read_logs, serial_write, serial_context);
```

The owning application provides `cli` storage. `serial_write` takes `(void *context, const char *bytes, size_t size)`. Feed serial input with `gridpico_cli_receive` and `gridpico_cli_process` from the serialized runner. `gridpico_cli_execute_line` accepts a complete command line without its terminator and streams the same reply records to a caller-supplied writer. A null log callback makes `logs` return `ERR UNAVAILABLE`.

Local-router route control is optional on the same CLI instance. When both typed callbacks are available, bind `cli.set_local_route` and `cli.read_local_route`; both receive the frontend's existing `session_context`. A runtime facade can wrap the write callback to apply its Raw lease policy and forward to Session. If either callback is absent, `route` retains the baseline unknown-command error.

Commands are limited to 63 ASCII bytes before the terminator. The examples below use the default local production profile in `api/local-router.md`:

```text
help
help inventory
inventory
status
logs
color get 1
color set 1 #FF0000
route 1 5 managed
route 1 5 managed on
route 1 5 managed off
route 4 3 raw
route 4 3 raw on
route 4 3 raw off
```

Device `1` is Web APC. For the Raw lease, enable `route 4 3 raw on` only while Diagnostics port 4 and APC feedback port 3 are connected and ready. Send `route 4 3 raw off` explicitly to end the lease; it also expires after 30 seconds.

Input accepts CR, LF or CRLF. Diagnostics sends UTF-8 bytes unchanged with LF. Application command replies are UTF-8 records terminated by CRLF, followed by `OK` or `ERR INVALID`, `ERR CAPACITY` or `ERR UNAVAILABLE`. The interactive terminal's built-in `help` and `help <command>` are the presentation exception: they list the commands supported by the bound callbacks as ` * command` and tab-indented syntax/detail lines, terminated by CRLF, without an `OK`/`ERR` record. Device and port records preserve snapshot order; each device is followed by its ports in port order. Names use JSON string escaping. `status` counts routes in one snapshot. `logs` emits the oldest retained entries first; an absent log callback returns `ERR UNAVAILABLE`. With route callbacks bound, `route SRC DST managed|raw` reads route state; adding `on` or `off` writes the request and rereads committed state. Its reply is `ROUTE SRC DST MANAGED|RAW OFF|PENDING|ACTIVE` then `OK`, or one `ERR ...` record.

Computer connections owns serial settings: 115200 baud, 8 data bits, no parity, 1 stop bit and no flow control. Diagnostics has no local echo, prompt or ANSI handling; it displays the same output byte stream as text. The interactive terminal adapter provides a prompt, echo and bounded line editing. ANSI cursor sequences from editing are terminal presentation output, never command reply records. `embedded-cli` autocompletion is disabled.

Caller-owned terminal storage is fixed: 128 bytes for the RX FIFO, 64 bytes of command text plus terminator, 128 bytes of history and a 4096-byte aligned library arena. `gridpico_cli_execute_line` rejects commands over 63 bytes before reading beyond the limit. Response records stream directly to the supplied writer without a response buffer.

`third_party/embedded_cli.h` is the unmodified upstream `funbiscuit/embedded-cli` v0.1.4 release header at commit `ffa8014c6230ba809c6a655ccc611a6c4689113f`. Its SHA-256 is `7b6db7fdd691790816f279f850be977433b01903f52f6dd53c0b0569718e6fe2`; the upstream MIT license notice is retained at the start of the header.

The native harness binds to `patch_runtime` through `patch_runtime_bind_cli`, so it exercises the real Session color functions, Patch snapshot and Assembly log ring. It applies a route and typed disconnect through the runtime, then checks inventory, color get/set and shared inventory color, status, logs and CR/LF/CRLF parsing. It also checks bare help and a following inventory command through terminal input, including callback-dependent help entries. The Monitor fixture verifies device 3 and port 6 in inventory/status and reads/sets device 3's shared Session color. A local graph/Session fixture binds typed route callbacks and tests managed/raw query, enable/disable and committed readback over direct commands and terminal bytes. The harness checks that terminal output contains the same application reply bytes as the service; terminal prompt and echo are additional presentation. Configure a clean build directory, then build the runtime library and harness with strict C11 diagnostics:

```sh
cmake -S . -B /tmp/gridpico-cli-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/gridpico-cli-build --target gridpico_patch_runtime
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -I. \
  frontends/cli/native_harness.c \
  /tmp/gridpico-cli-build/libgridpico_patch_runtime.a \
  -o /tmp/gridpico-cli-harness
/tmp/gridpico-cli-harness
```
