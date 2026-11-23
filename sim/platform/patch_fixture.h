#ifndef GRIDPICO_PATCH_FIXTURE_H
#define GRIDPICO_PATCH_FIXTURE_H

#include "api/patchbay.h"

typedef struct {
    gridpico_patch_port_update status;
    bool destination;
} gridpico_patch_fixture_port;

/* Caller-owned state for one serialized Patch fixture. */
typedef struct {
    size_t port_count;
    gridpico_patch_fixture_port ports[GRIDPICO_PATCH_MAX_PORTS];
    gridpico_note_delivery capture[GRIDPICO_PATCH_CAPTURE_ROWS];
    size_t capture_start;
    size_t capture_count;
} gridpico_patch_fixture;

/* Start registered ports at generation 1 with an empty shared capture.
 * The default initializer preserves the native and two-branch profile. */
gridpico_result gridpico_patch_fixture_init(gridpico_patch_fixture *fixture,
                                          const gridpico_patch_inventory *inventory);

/* Select the expanded profile with up to three eligible output ports. */
gridpico_result gridpico_patch_fixture_init_monitor(gridpico_patch_fixture *fixture,
                                                  const gridpico_patch_inventory *inventory);

/* Apply the simulated endpoint event before sending the same event to the graph. */
gridpico_result gridpico_patch_fixture_update_port(void *context,
                                                 const gridpico_patch_port_update *update);

gridpico_result gridpico_patch_fixture_deliver(void *context,
                                             const gridpico_note_delivery *delivery);

/* Copy up to capacity rows, starting at the oldest retained row. */
size_t gridpico_patch_fixture_read_capture(void *context,
                                          gridpico_note_delivery *rows,
                                          size_t capacity);

#endif
