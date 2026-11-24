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

/* Volatile editable physical APC page. The registered-order passive viewport
 * continues to use gridpico_local_patch_page.
 */
typedef struct {
    gridpico_port_id rows[GRIDPICO_PATCH_SOURCE_ROWS];
    gridpico_port_id columns[GRIDPICO_PATCH_DESTINATION_COLUMNS];
} gridpico_apc_patch_layout;

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

#endif
