# Serial CLI frontend

The command service is `gridpico_cli_execute_line`. The terminal adapter and Diagnostics call this same handler. It reads Patch state with `gridpico_patch_read_snapshot_fn`, colors with the typed device-color callbacks and up to 16 log rows through the optional Assembly provider. It owns no device, route, color or log state. Assembly serializes calls and wires the terminal and Diagnostics to the same service. Pass `&patch_session` with `gridpico_patch_session_read_snapshot`, `gridpico_session_read_device_color` and `gridpico_session_set_device_color`; use a separate log context for `gridpico_read_diagnostic_log_fn`.

```c
gridpico_cli_init(&cli, &patch_session,
                  gridpico_patch_session_read_snapshot,
                  gridpico_session_read_device_color,
                  gridpico_session_set_device_color,
                  log_context, read_logs, serial_write, serial_context);
```

The owning application provides `cli` storage. `serial_write` takes `(void *context, const char *bytes, size_t size)`. Feed serial input with `gridpico_cli_receive` and `gridpico_cli_process` from the serialized runner. `gridpico_cli_execute_line` accepts a complete command line without its terminator and streams the same reply records to a caller-supplied writer. A null log callback makes `logs` return `ERR UNAVAILABLE`.

Commands are limited to 63 ASCII bytes before the terminator:

```text
inventory
status
logs
color get 7
color set 7 #FF0000
```

Input accepts CR, LF or CRLF. Diagnostics sends UTF-8 bytes unchanged with LF. Replies are UTF-8 records terminated by CRLF, followed by `OK` or `ERR INVALID`, `ERR CAPACITY` or `ERR UNAVAILABLE`. Device and port records preserve snapshot order; each device is followed by its ports in port order. Names use JSON string escaping. `status` counts routes in one snapshot. `logs` emits the oldest retained entries first; an absent log callback returns `ERR UNAVAILABLE`.

Computer connections owns serial settings: 115200 baud, 8 data bits, no parity, 1 stop bit and no flow control. Diagnostics has no local echo, prompt or ANSI handling; it displays the same output byte stream as text. The interactive terminal adapter provides a prompt, echo and bounded line editing. ANSI cursor sequences from editing are terminal presentation output, never command reply records. `embedded-cli` autocompletion is disabled.

Caller-owned terminal storage is fixed: 128 bytes for the RX FIFO, 64 bytes of command text plus terminator, 128 bytes of history and a 4096-byte aligned library arena. `gridpico_cli_execute_line` rejects commands over 63 bytes before reading beyond the limit. Response records stream directly to the supplied writer without a response buffer.

`third_party/embedded_cli.h` is the unmodified upstream `funbiscuit/embedded-cli` v0.1.4 release header at commit `ffa8014c6230ba809c6a655ccc611a6c4689113f`. Its SHA-256 is `7b6db7fdd691790816f279f850be977433b01903f52f6dd53c0b0569718e6fe2`; the upstream MIT license notice is retained at the start of the header.

The native harness binds to `patch_runtime` through `patch_runtime_bind_cli`, so it exercises the real Session color functions, Patch snapshot and Assembly log ring. It applies a route and typed disconnect through the runtime, then checks inventory, color get/set and shared inventory color, status, logs and CR/LF/CRLF parsing. For each command it compares the shared service's reply bytes with the terminal adapter's reply bytes exactly; terminal prompt and echo remain presentation around those records. After building `gridpico_patch_runner`, build and run the harness with strict C11 diagnostics:

```sh
cmake --build build/browser-demo --target gridpico_patch_runner
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -I. \
  frontends/cli/native_harness.c \
  build/browser-demo/libgridpico_patch_runtime.a \
  -o /tmp/gridpico-cli-harness
/tmp/gridpico-cli-harness
```
