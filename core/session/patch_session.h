#ifndef GRIDPICO_PATCH_SESSION_H
#define GRIDPICO_PATCH_SESSION_H

#include "api/patchbay.h"

typedef struct {
    void *graph_context;
    gridpico_patch_read_graph_fn read_graph;
    gridpico_patch_set_route_fn set_graph_route;
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

#endif
