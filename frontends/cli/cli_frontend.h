#ifndef GRIDPICO_CLI_FRONTEND_H
#define GRIDPICO_CLI_FRONTEND_H

#include "api/patchbay.h"
#include "api/patchbay_controls.h"
#include "api/local_router.h"
#include "api/routing.h"
#include "third_party/embedded_cli.h"

enum {
    GRIDPICO_CLI_LINE_BYTES = GRIDPICO_CLI_COMMAND_BYTES + 1,
    GRIDPICO_CLI_RX_BYTES = 128,
    GRIDPICO_CLI_HISTORY_BYTES = 128,
    GRIDPICO_CLI_LIBRARY_BYTES = 4096,
    GRIDPICO_CLI_RECORD_BYTES = 192
};

typedef void (*gridpico_cli_write_fn)(void *context, const char *bytes, size_t size);

typedef struct {
    void *session_context;
    gridpico_patch_read_snapshot_fn read_snapshot;
    gridpico_patch_read_device_color_fn read_device_color;
    gridpico_patch_set_device_color_fn set_device_color;
    /* Optional local-router controls. They share session_context and are
     * assigned by the owning runtime after gridpico_cli_init. */
    gridpico_local_set_route_fn set_local_route;
    gridpico_local_read_route_fn read_local_route;
    /* Optional full Routing projection, supplied by configured profiles. */
    gridpico_local_read_routing_fn read_local_routing;
    void *log_context;
    gridpico_read_diagnostic_log_fn read_logs;
    CLI_UINT library_storage[BYTES_TO_CLI_UINTS(GRIDPICO_CLI_LIBRARY_BYTES)];
    EmbeddedCli *terminal;
    bool help_bindings_registered;
    gridpico_cli_write_fn terminal_write;
    void *terminal_write_context;
} gridpico_cli_frontend;

/* Bind a typed Session snapshot reader and caller-owned terminal output. */
gridpico_result gridpico_cli_init(gridpico_cli_frontend *frontend,
                                 void *session_context,
                                 gridpico_patch_read_snapshot_fn read_snapshot,
                                 gridpico_patch_read_device_color_fn read_device_color,
                                 gridpico_patch_set_device_color_fn set_device_color,
                                 void *log_context,
                                 gridpico_read_diagnostic_log_fn read_logs,
                                 gridpico_cli_write_fn terminal_write,
                                 void *terminal_write_context);

/* Execute a complete UTF-8 command line. Diagnostics and serial terminal
 * command callbacks use this same handler. Output consists only of CRLF
 * terminated protocol records and one final OK/ERR record.
 */
gridpico_result gridpico_cli_execute_line(gridpico_cli_frontend *frontend,
                                         const char *line,
                                         gridpico_cli_write_fn write,
                                         void *write_context);

/* Feed terminal input bytes to embedded-cli. Call process from the serialized
 * command context; neither function is for interrupt-context mutation.
 */
void gridpico_cli_receive(gridpico_cli_frontend *frontend, const char *bytes, size_t size);
void gridpico_cli_process(gridpico_cli_frontend *frontend);

#endif
