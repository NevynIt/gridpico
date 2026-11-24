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

#endif
