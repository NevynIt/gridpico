#ifndef GRIDPICO_ROUTING_H
#define GRIDPICO_ROUTING_H

#include "local_router.h"
#include "patchbay_controls.h"

typedef enum {
    GRIDPICO_LOCAL_UNAVAILABLE,
    GRIDPICO_LOCAL_FREE,
    GRIDPICO_LOCAL_IN_USE
} gridpico_local_port_availability;

typedef struct {
    /* Nonzero revision of the complete CLI projection. */
    gridpico_revision revision;
    gridpico_local_graph_state graph;
    /* Indexed by graph.registry inventory order. */
    gridpico_device_color colors[GRIDPICO_PATCH_MAX_DEVICES];
    gridpico_local_port_availability availability[GRIDPICO_PATCH_MAX_PORTS];
} gridpico_local_routing_snapshot;

typedef struct {
    size_t source_offset;
    size_t destination_offset;
} gridpico_local_patch_viewport;

typedef struct {
    /* Graph revision used for this projection. Counts cover both full axes. */
    gridpico_revision revision;
    size_t source_count;
    size_t destination_count;
    gridpico_local_patch_viewport viewport;
    gridpico_port_id source_rows[GRIDPICO_PATCH_SOURCE_ROWS];
    gridpico_port_id destination_columns[GRIDPICO_PATCH_DESTINATION_COLUMNS];
    gridpico_patch_route_state routes[GRIDPICO_PATCH_SOURCE_ROWS]
                                    [GRIDPICO_PATCH_DESTINATION_COLUMNS];
} gridpico_local_patch_page;

enum {
    GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS = 8,
    GRIDPICO_LOCAL_ROUTING_RECORD_BYTES = 256 /* Includes CRLF. */
};

/* Fill one availability per registered port in inventory order. All reads
 * run on the serialized context and borrow the registry only for the call.
 */
typedef gridpico_result (*gridpico_local_read_availability_fn)(
    void *context, const gridpico_local_registry *registry,
    gridpico_local_port_availability out[GRIDPICO_PATCH_MAX_PORTS]);
typedef gridpico_result (*gridpico_local_read_routing_fn)(
    void *context, gridpico_local_routing_snapshot *out);
/* Clamp the requested zero-based offsets and return the effective window. */
typedef gridpico_result (*gridpico_local_read_patch_page_fn)(
    void *context, const gridpico_local_patch_viewport *request, gridpico_local_patch_page *out);

typedef struct {
    /* Nonzero token for the completed automatic-route evaluation. */
    gridpico_revision revision;
    uint16_t candidates;
    uint16_t active;
    uint16_t pending;
    uint16_t user_off;
    uint16_t feedback;
    uint16_t limit;
    uint16_t unavailable;
} gridpico_auto_route_summary;

/* Copy a complete current summary. On failure, leave out unchanged. */
typedef gridpico_result (*gridpico_read_auto_route_summary_fn)(
    void *context, gridpico_auto_route_summary *out);

enum {
    GRIDPICO_AUTO_ROUTE_MAX_PAIRS = (GRIDPICO_PATCH_MAX_PORTS / 2) *
                                   (GRIDPICO_PATCH_MAX_PORTS - GRIDPICO_PATCH_MAX_PORTS / 2)
};

typedef enum {
    GRIDPICO_AUTO_SKIP_USER_OFF = 1,
    GRIDPICO_AUTO_SKIP_COMPUTER_RETURN,
    GRIDPICO_AUTO_SKIP_SAME_PARENT,
    GRIDPICO_AUTO_SKIP_KNOWN_FEEDBACK,
    GRIDPICO_AUTO_SKIP_CYCLE,
    GRIDPICO_AUTO_SKIP_AUDITION_RESERVED,
    GRIDPICO_AUTO_SKIP_BRANCH_LIMIT,
    GRIDPICO_AUTO_SKIP_FANOUT_LIMIT,
    GRIDPICO_AUTO_SKIP_UNAVAILABLE
} gridpico_auto_route_skip_reason;

typedef struct {
    gridpico_port_id source;
    gridpico_port_id destination;
    gridpico_auto_route_skip_reason reason;
} gridpico_auto_route_skip;

typedef struct {
    gridpico_revision revision; /* Completed evaluation token, nonzero. */
    gridpico_revision routing_revision; /* Exact named Routing token, nonzero. */
    uint16_t total_count;
    uint16_t next_offset; /* Equal to total_count on the last page. */
    uint8_t record_count;
    gridpico_auto_route_skip records[GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS];
} gridpico_auto_route_explain_page;

/* A zero expected revision permits only offset zero. Copy from one complete
 * current evaluation. On failure leave out unchanged.
 */
typedef gridpico_result (*gridpico_read_auto_route_explain_fn)(
    void *context, gridpico_revision expected_revision, uint16_t offset,
    gridpico_auto_route_explain_page *out);

/* A runtime-scoped surface handle is independent of USB and logical port IDs. */
typedef uint16_t gridpico_apc_surface_id;

typedef enum {
    GRIDPICO_APC_ROLE_NONE,
    GRIDPICO_APC_ROLE_BOUND,
    GRIDPICO_APC_ROLE_RESERVED,
    GRIDPICO_APC_ROLE_RETIRING,
    GRIDPICO_APC_ROLE_UNRESOLVED
} gridpico_apc_role_state;

typedef struct {
    /* Nonzero token for the target's APC inventory and ownership state. */
    gridpico_revision revision;
    gridpico_apc_surface_id surface;
    gridpico_port_id panel_input;
    gridpico_port_id led_output;
    gridpico_apc_role_state panel_state;
    gridpico_apc_role_state led_state;
} gridpico_apc_roles_status;

typedef enum {
    GRIDPICO_APC_ROLE_SET_PANEL,
    GRIDPICO_APC_ROLE_MARK_LED,
    GRIDPICO_APC_ROLE_UNMARK_LED
} gridpico_apc_role_action;

typedef struct {
    gridpico_revision expected_revision;
    gridpico_apc_surface_id surface;
    /* Zero is valid only for clearing Panel input. */
    gridpico_port_id port;
    gridpico_apc_role_action action;
} gridpico_apc_role_request;

typedef gridpico_result (*gridpico_apc_read_roles_fn)(
    void *context, gridpico_apc_roles_status *out);
/* Check the guard and apply in one serialized operation. On success, publish
 * actual state, including LED retirement in progress. On failure leave out
 * unchanged.
 */
typedef gridpico_result (*gridpico_apc_apply_role_fn)(
    void *context, const gridpico_apc_role_request *request, gridpico_apc_roles_status *out);

/* Volatile editable physical APC page. The registered-order passive viewport
 * continues to use gridpico_local_patch_page.
 */
typedef struct {
    gridpico_port_id rows[GRIDPICO_PATCH_SOURCE_ROWS];
    gridpico_port_id columns[GRIDPICO_PATCH_DESTINATION_COLUMNS];
} gridpico_apc_patch_layout;

/* Logical state for the version 1 snapshot codec. Storage encodes its fields
 * explicitly. This structure does not define the byte representation.
 */
enum { GRIDPICO_APC_SESSION_SNAPSHOT_ROUTES = 12 };

typedef struct {
    gridpico_port_id source;
    gridpico_port_id destination;
} gridpico_apc_session_route;

typedef struct {
    gridpico_apc_patch_layout layout;
    size_t route_count;
    gridpico_apc_session_route routes[GRIDPICO_APC_SESSION_SNAPSHOT_ROUTES];
} gridpico_apc_session_snapshot;

typedef struct {
    gridpico_port_id id;
    gridpico_patch_direction direction;
    bool connected;
    uint64_t last_attack_sequence;
    uint64_t last_attack_us;
} gridpico_apc_patch_port;

typedef struct {
    gridpico_patch_route_state state;
    bool compatible;
    bool available;
} gridpico_apc_patch_cell;

typedef struct {
    gridpico_revision layout_revision;
    gridpico_revision graph_revision;
    gridpico_apc_patch_layout layout;
    size_t port_count;
    gridpico_apc_patch_port ports[GRIDPICO_PATCH_MAX_PORTS];
    uint64_t attack_sequence;
    gridpico_apc_patch_cell cells[GRIDPICO_PATCH_SOURCE_ROWS][GRIDPICO_PATCH_DESTINATION_COLUMNS];
} gridpico_apc_patch_snapshot;

typedef gridpico_result (*gridpico_apc_patch_read_fn)(
    void *context, gridpico_apc_patch_snapshot *out);
typedef gridpico_result (*gridpico_apc_patch_assign_fn)(
    void *context, gridpico_revision layout_revision, gridpico_patch_direction axis,
    uint8_t slot, gridpico_port_id port);
typedef gridpico_result (*gridpico_apc_patch_move_fn)(
    void *context, gridpico_revision layout_revision, gridpico_patch_direction axis,
    uint8_t from, uint8_t to);
typedef gridpico_result (*gridpico_apc_patch_set_cell_fn)(
    void *context, gridpico_revision layout_revision, gridpico_revision graph_revision,
    uint8_t row, uint8_t column, bool enabled);

/* Context identifies the current panel owner. Stop affects only that owner. */
typedef gridpico_result (*gridpico_apc_patch_audition_fn)(
    void *context, gridpico_port_id destination, bool start);

typedef struct {
    gridpico_port_id source;
    gridpico_port_id destination;
    gridpico_managed_note_transform transform;
    gridpico_revision expected_revision; /* Nonzero Routing revision. */
} gridpico_managed_note_transform_request;

typedef struct {
    gridpico_managed_note_transform transform;
    gridpico_revision revision; /* Committed Routing revision. */
} gridpico_managed_note_transform_status;

/* Validate and apply on the selected target in one serialized call.
 * On success return the committed setting. On failure leave out untouched.
 */
typedef gridpico_result (*gridpico_apply_managed_note_transform_fn)(
    void *context, const gridpico_managed_note_transform_request *request,
    gridpico_managed_note_transform_status *out);

/* Master forwarding revision is independent of the Routing revision. */
typedef struct {
    gridpico_revision revision;
    bool running;
} gridpico_forwarding_status;

typedef enum {
    GRIDPICO_PANIC_STAGE_NONE,
    GRIDPICO_PANIC_STAGE_RUNNING,
    GRIDPICO_PANIC_STAGE_COMPLETE,
    GRIDPICO_PANIC_STAGE_CANCELLED
} gridpico_panic_stage_state;

typedef enum {
    GRIDPICO_PANIC_OUTPUT_PENDING,
    GRIDPICO_PANIC_OUTPUT_COMPLETE,
    GRIDPICO_PANIC_OUTPUT_REFUSED,
    GRIDPICO_PANIC_OUTPUT_CANCELLED,
    GRIDPICO_PANIC_OUTPUT_UNCONFIRMED
} gridpico_panic_output_state;

typedef struct {
    gridpico_connection output;
    uint16_t requested_count;
    uint16_t accepted_count; /* Zero when state is UNCONFIRMED. */
    gridpico_panic_output_state state;
} gridpico_panic_output;

typedef struct {
    gridpico_panic_stage_state state;
    size_t count;
    gridpico_panic_output outputs[GRIDPICO_PATCH_MAX_PORTS];
} gridpico_panic_stage_report;

/* Serialized calls borrow out for the call and leave it untouched on failure. */
typedef gridpico_result (*gridpico_read_forwarding_fn)(
    void *context, gridpico_forwarding_status *out);
typedef gridpico_result (*gridpico_apply_forwarding_fn)(
    void *context, bool running, gridpico_revision expected_revision,
    gridpico_forwarding_status *out);
typedef gridpico_result (*gridpico_read_panic_fn)(
    void *context, gridpico_panic_stage_report *out);
typedef gridpico_result (*gridpico_apply_panic_fn)(
    void *context, gridpico_panic_stage_report *out);

typedef enum {
    GRIDPICO_PANIC_IDLE,
    GRIDPICO_PANIC_WAIT,
    GRIDPICO_PANIC_OPEN,
    GRIDPICO_PANIC_CLOSED
} gridpico_panic_window;

typedef struct {
    uint64_t cycle;
    gridpico_panic_window window;
    uint32_t wait_us;
    uint32_t remaining_us;
    gridpico_panic_stage_report cc123;
    gridpico_panic_stage_report sweep;
} gridpico_panic_sweep_status;

typedef gridpico_result (*gridpico_read_panic_sweep_fn)(
    void *context, gridpico_panic_sweep_status *out);
typedef gridpico_result (*gridpico_apply_panic_sweep_fn)(
    void *context, uint64_t expected_cycle, gridpico_panic_sweep_status *out);

#endif
