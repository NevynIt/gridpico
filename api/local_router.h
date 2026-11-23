#ifndef GRIDPICO_LOCAL_ROUTER_H
#define GRIDPICO_LOCAL_ROUTER_H

#include "patchbay.h"

enum {
    GRIDPICO_LOCAL_DEVICES = 4,
    GRIDPICO_LOCAL_PORTS = 6,
    GRIDPICO_LOCAL_BRANCHES = 3,
    GRIDPICO_LOCAL_NOTE_OCCURRENCES = 25,
    GRIDPICO_LOCAL_RELEASE_CREDITS = 50,
    GRIDPICO_LOCAL_MESSAGE_BYTES = 3
};

typedef enum {
    GRIDPICO_LOCAL_PORT_MANAGED,
    GRIDPICO_LOCAL_PORT_RAW,
    GRIDPICO_LOCAL_PORT_PANEL
} gridpico_local_port_capability;

typedef struct {
    gridpico_patch_inventory inventory;
    /* One capability per registered port in inventory order. */
    gridpico_local_port_capability port_capabilities[GRIDPICO_LOCAL_PORTS];
} gridpico_local_registry;

typedef enum {
    GRIDPICO_LOCAL_MANAGED,
    GRIDPICO_LOCAL_RAW
} gridpico_local_route_mode;

typedef struct {
    gridpico_port_id source;
    gridpico_port_id destination;
    gridpico_local_route_mode mode;
} gridpico_local_route;

typedef struct {
    gridpico_local_route route;
    bool enabled;
} gridpico_local_route_request;

typedef struct {
    gridpico_revision revision;
    gridpico_patch_route_state state;
} gridpico_local_route_status;

typedef struct {
    gridpico_connection source;
    uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES];
} gridpico_local_raw_ingress;

typedef struct {
    gridpico_connection source;
    gridpico_connection destination;
    uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES];
} gridpico_local_raw_delivery;

typedef gridpico_result (*gridpico_local_set_route_fn)(
    void *context, const gridpico_local_route_request *request);
typedef gridpico_result (*gridpico_local_read_route_fn)(
    void *context, const gridpico_local_route *route, gridpico_local_route_status *out);
typedef gridpico_result (*gridpico_local_submit_raw_fn)(
    void *context, const gridpico_local_raw_ingress *input);
typedef gridpico_result (*gridpico_local_deliver_raw_fn)(
    void *context, const gridpico_local_raw_delivery *delivery);

#endif
