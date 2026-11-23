#ifndef GRIDPICO_PATCHBAY_H
#define GRIDPICO_PATCHBAY_H

#include "native_proof.h"

/* Limits for the first native Patch fixture. */
enum {
    GRIDPICO_PATCH_DEVICES = 2,
    GRIDPICO_PATCH_PORTS = 5,
    GRIDPICO_PATCH_BRANCHES = 2,
    GRIDPICO_PATCH_SOURCE_ROWS = 5,
    GRIDPICO_PATCH_DESTINATION_COLUMNS = 8,
    GRIDPICO_PATCH_NAME_BYTES = 32,
    GRIDPICO_PATCH_NOTE_OCCURRENCES = 2,
    GRIDPICO_PATCH_RELEASE_CREDITS = 4,
    GRIDPICO_PATCH_CAPTURE_ROWS = 16
};

typedef uint16_t gridpico_device_id;
typedef gridpico_endpoint_id gridpico_port_id;

typedef enum {
    GRIDPICO_PATCH_DISCONNECTED,
    GRIDPICO_PATCH_CONNECTED,
    GRIDPICO_PATCH_AMBIGUOUS
} gridpico_patch_connection_state;

typedef enum {
    GRIDPICO_PATCH_INPUT,
    GRIDPICO_PATCH_OUTPUT
} gridpico_patch_direction;

typedef struct {
    gridpico_device_id id;
    char name[GRIDPICO_PATCH_NAME_BYTES];
    gridpico_patch_connection_state state;
} gridpico_patch_device;

typedef struct {
    gridpico_port_id id;
    gridpico_device_id device;
    char name[GRIDPICO_PATCH_NAME_BYTES];
    gridpico_patch_direction direction;
    gridpico_patch_connection_state state;
    bool patch_eligible;
} gridpico_patch_port;

typedef struct {
    size_t device_count;
    size_t port_count;
    gridpico_patch_device devices[GRIDPICO_PATCH_DEVICES];
    gridpico_patch_port ports[GRIDPICO_PATCH_PORTS];
} gridpico_patch_inventory;

typedef struct {
    gridpico_connection connection;
    gridpico_patch_connection_state state;
} gridpico_patch_port_update;

typedef enum {
    GRIDPICO_PATCH_ROUTE_OFF,
    GRIDPICO_PATCH_ROUTE_PENDING,
    GRIDPICO_PATCH_ROUTE_ACTIVE
} gridpico_patch_route_state;

typedef struct {
    gridpico_port_id source;
    gridpico_port_id destination;
    bool enabled;
} gridpico_patch_route_request;

typedef struct {
    gridpico_port_id source;
    gridpico_port_id destination;
    gridpico_patch_route_state state;
} gridpico_patch_branch;

typedef struct {
    gridpico_revision revision;
    gridpico_patch_inventory inventory;
    size_t branch_count;
    gridpico_patch_branch branches[GRIDPICO_PATCH_BRANCHES];
} gridpico_patch_graph_state;

typedef struct {
    gridpico_revision revision;
    gridpico_patch_inventory inventory;
    gridpico_port_id source_rows[GRIDPICO_PATCH_SOURCE_ROWS];
    gridpico_port_id destination_columns[GRIDPICO_PATCH_DESTINATION_COLUMNS];
    gridpico_patch_route_state routes[GRIDPICO_PATCH_SOURCE_ROWS]
                                    [GRIDPICO_PATCH_DESTINATION_COLUMNS];
} gridpico_patch_snapshot;

typedef gridpico_result (*gridpico_patch_update_port_fn)(
    void *context, const gridpico_patch_port_update *update);
typedef gridpico_result (*gridpico_patch_set_route_fn)(
    void *context, const gridpico_patch_route_request *request);
typedef gridpico_result (*gridpico_patch_read_graph_fn)(
    void *context, gridpico_patch_graph_state *out);
typedef gridpico_result (*gridpico_patch_read_snapshot_fn)(
    void *context, gridpico_patch_snapshot *out);
typedef size_t (*gridpico_patch_read_capture_fn)(
    void *context, gridpico_note_delivery *rows, size_t capacity);

#endif
