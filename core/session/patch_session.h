#ifndef GRIDPICO_PATCH_SESSION_H
#define GRIDPICO_PATCH_SESSION_H

#include "api/patchbay_controls.h"

typedef struct {
    bool is_set;
    gridpico_rgb8 rgb;
} gridpico_patch_color_preference;

typedef struct {
    void *graph_context;
    gridpico_patch_read_graph_fn read_graph;
    gridpico_patch_set_route_fn set_graph_route;
    gridpico_revision color_revision;
    /* Colors follow the graph's stable device registration slots. */
    gridpico_patch_color_preference colors[GRIDPICO_PATCH_MAX_DEVICES];
} gridpico_patch_session;

/* Bind both callbacks to the same graph. Assembly serializes commands and
 * reads with graph operations. The graph publishes the Patch revision.
 */
void gridpico_patch_session_init(gridpico_patch_session *session, void *graph_context,
                                gridpico_patch_read_graph_fn read_graph,
                                gridpico_patch_set_route_fn set_graph_route);

gridpico_result gridpico_patch_session_read_snapshot(void *context, gridpico_patch_snapshot *out);
gridpico_result gridpico_patch_session_set_route(void *context,
                                               const gridpico_patch_route_request *request);
gridpico_result gridpico_session_read_device_color(void *context, gridpico_device_id device,
                                                 gridpico_device_color *out);
gridpico_result gridpico_session_set_device_color(void *context, gridpico_device_id device,
                                                gridpico_rgb8 rgb);

#endif
