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

enum {
    GRIDPICO_CLI_PLATFORM_TRACE = 1u,
    GRIDPICO_CLI_PLATFORM_HUBTRACE = 2u,
    GRIDPICO_CLI_PLATFORM_BOOTSEL = 4u
};

typedef void (*gridpico_cli_write_fn)(void *context, const char *bytes, size_t size);
typedef gridpico_result (*gridpico_cli_start_platform_fn)(void *context, unsigned command);

typedef struct {
    void *session_context;
    gridpico_patch_read_snapshot_fn read_snapshot;
    gridpico_patch_read_device_color_fn read_device_color;
    gridpico_patch_set_device_color_fn set_device_color;
    /* Reused synchronously by inventory and status; keep frontend in static
     * or other caller-owned long-lived storage. */
    gridpico_patch_snapshot snapshot_scratch;
    /* Optional editable APC projection, with caller-owned scratch. */
    gridpico_apc_patch_read_fn read_apc_patch;
    gridpico_apc_patch_snapshot *apc_patch_scratch;
    /* Optional local-router controls. They share session_context and are
     * assigned by the owning runtime after gridpico_cli_init. */
    gridpico_local_set_route_fn set_local_route;
    gridpico_local_read_route_fn read_local_route;
    /* Optional full Routing projection, supplied by configured profiles. */
    gridpico_local_read_routing_fn read_local_routing;
    /* Target-specific revisioned Routing and guarded route callbacks. These
     * use routing_context, not the shared terminal/Session provider context. */
    void *routing_context;
    gridpico_local_read_routing_fn routing_read;
    gridpico_local_read_route_fn routing_read_route;
    gridpico_local_set_route_fn routing_set_route;
    gridpico_local_routing_snapshot *routing_scratch;
    /* Optional target-scoped explicit APC role provider. */
    void *apc_roles_context;
    gridpico_apc_read_roles_fn read_apc_roles;
    gridpico_apc_apply_role_fn apply_apc_role;
    /* Optional isolated dev-run managed transform provider. */
    void *note_transform_context;
    gridpico_apply_managed_note_transform_fn apply_note_transform;
    /* Optional independent Auto summary and paged explain providers. */
    void *auto_route_summary_context;
    gridpico_read_auto_route_summary_fn read_auto_route_summary;
    void *auto_route_explain_context;
    gridpico_read_auto_route_explain_fn read_auto_route_explain;
    /* Optional serialized master forwarding gate. */
    void *musical_control_context;
    gridpico_read_forwarding_fn read_forwarding;
    gridpico_apply_forwarding_fn apply_forwarding;
    /* Shared M1a Panic providers, optionally extended by M1b. */
    gridpico_read_panic_fn read_panic;
    gridpico_apply_panic_fn apply_panic;
    gridpico_panic_stage_report *panic_scratch;
#if !defined(GRIDPICO_RP2040_MATRIX_CLI)
    gridpico_read_panic_sweep_fn read_panic_sweep;
    gridpico_apply_panic_sweep_fn apply_panic_sweep;
    gridpico_panic_sweep_status *panic_sweep_scratch;
#endif
    unsigned platform_capabilities;
    gridpico_cli_start_platform_fn start_platform;
    void *platform_context;
    void *log_context;
    gridpico_read_diagnostic_log_fn read_logs;
    CLI_UINT library_storage[BYTES_TO_CLI_UINTS(GRIDPICO_CLI_LIBRARY_BYTES)];
    EmbeddedCli *terminal;
    bool help_bindings_registered;
    gridpico_cli_write_fn terminal_write;
    void *terminal_write_context;
#if !defined(GRIDPICO_RP2040_MATRIX_CLI)
    /* Reply format is scoped to this initialized CLI session. */
    bool machine_format;
    /* Embedded terminal editor output is filtered around format negotiation. */
    char editor_prefix[GRIDPICO_CLI_LINE_BYTES];
    size_t editor_prefix_size;
    unsigned editor_suppress_linebreak;
    unsigned editor_prompt_pending;
    bool editor_suppress_prompt;
#endif
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

/* Bind optional platform operations after init and before the first process.
 * start receives TRACE or HUBTRACE only; BOOTSEL stays in the platform byte
 * parser and is exposed here only as conditional help metadata.
 */
void gridpico_cli_bind_platform(gridpico_cli_frontend *frontend,
                                unsigned capabilities,
                                gridpico_cli_start_platform_fn start,
                                void *context);

/* Bind optional live APC placement/status projection before first command.
 * The snapshot storage remains caller-owned and is borrowed only for a read.
 */
void gridpico_cli_bind_apc_patch(gridpico_cli_frontend *frontend,
                                 gridpico_apc_patch_read_fn read_apc_patch,
                                 gridpico_apc_patch_snapshot *scratch);

/* Bind target-specific revisioned Routing and guarded managed-route edits.
 * The projection scratch is caller-owned and reused synchronously for page
 * reads and guard checks; no Routing snapshot is placed on the CLI stack.
 */
void gridpico_cli_bind_routing(gridpico_cli_frontend *frontend, void *context,
                              gridpico_local_read_routing_fn read_routing,
                              gridpico_local_read_route_fn read_route,
                              gridpico_local_set_route_fn set_route,
                              gridpico_local_routing_snapshot *scratch);

/* Bind optional explicit APC Panel/LED role control before input processing. */
void gridpico_cli_bind_apc_roles(gridpico_cli_frontend *frontend, void *context,
                                gridpico_apc_read_roles_fn read_roles,
                                gridpico_apc_apply_role_fn apply_role);

/* Bind the optional serialized transform provider before command processing. */
void gridpico_cli_bind_note_transform(
    gridpico_cli_frontend *frontend, void *context,
    gridpico_apply_managed_note_transform_fn apply);

/* Bind an optional completed Auto summary reader before input processing. */
void gridpico_cli_bind_auto_route_summary(
    gridpico_cli_frontend *frontend, void *context,
    gridpico_read_auto_route_summary_fn read_summary);

/* Bind an optional completed Auto explain page reader before input processing. */
void gridpico_cli_bind_auto_route_explain(
    gridpico_cli_frontend *frontend, void *context,
    gridpico_read_auto_route_explain_fn read_explain);

/* Bind the shared M1a context. Panic providers remain unbound here. */
void gridpico_cli_bind_musical_control(
    gridpico_cli_frontend *frontend, void *context,
    gridpico_read_forwarding_fn read_forwarding,
    gridpico_apply_forwarding_fn apply_forwarding,
    gridpico_read_panic_fn read_panic,
    gridpico_apply_panic_fn apply_panic,
    gridpico_panic_stage_report *panic_scratch);

#if !defined(GRIDPICO_RP2040_MATRIX_CLI)
/* Bind the optional M1b extension on the existing musical-control context. */
void gridpico_cli_bind_panic_sweep(
    gridpico_cli_frontend *frontend,
    gridpico_read_panic_sweep_fn read_sweep,
    gridpico_apply_panic_sweep_fn apply_sweep,
    gridpico_panic_sweep_status *sweep_scratch);
#endif

/* Execute a complete UTF-8 command line. Diagnostics and serial terminal
 * command callbacks use this same handler. Synchronous commands emit CRLF
 * records and a final OK/ERR. A successful TRACE/HUBTRACE start is
 * asynchronous and emits no immediate record; BOOTSEL is never started here.
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
