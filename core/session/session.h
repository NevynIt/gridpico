#ifndef GRIDPICO_SESSION_H
#define GRIDPICO_SESSION_H

#include "api/native_proof.h"

typedef struct {
    void *graph_context;
    gridpico_apply_route_fn reconcile_route;
    gridpico_read_route_fn read_graph_route;
    gridpico_set_route_enabled_fn reconcile_route_enabled;
    gridpico_revision applied_revision;
} gridpico_session;

/* Bind a session to an empty graph before the first Apply. The graph
 * reconciles cleanup before committing route edits and desired enablement.
 * read_graph_route supplies the route fields, including live availability.
 * Assembly serializes Apply, queries and other graph operations.
 */
void gridpico_session_init(gridpico_session *session, void *graph_context,
                          gridpico_apply_route_fn reconcile_route,
                          gridpico_read_route_fn read_graph_route,
                          gridpico_set_route_enabled_fn reconcile_route_enabled);

gridpico_result gridpico_session_apply_route(void *context,
                                            const gridpico_route_request *request);
gridpico_result gridpico_session_set_route_enabled(void *context, bool enabled);
gridpico_route_snapshot gridpico_session_read_route(void *context);

#endif
