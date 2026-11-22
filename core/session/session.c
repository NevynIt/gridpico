#include "core/session/session.h"

void gridpico_session_init(gridpico_session *session, void *graph_context,
                          gridpico_apply_route_fn reconcile_route,
                          gridpico_read_route_fn read_graph_route)
{
    *session = (gridpico_session){
        .graph_context = graph_context,
        .reconcile_route = reconcile_route,
        .read_graph_route = read_graph_route,
    };
}

gridpico_result gridpico_session_apply_route(void *context,
                                            const gridpico_route_request *request)
{
    gridpico_session *session = context;

    if (request == NULL || request->source == 0 || request->destination == 0 ||
        request->source == request->destination || request->transpose_count == 0 ||
        request->transpose_offsets == NULL) {
        return GRIDPICO_INVALID;
    }

    if (request->transpose_count > GRIDPICO_PROOF_TRANSPOSE_INSTANCES) {
        return GRIDPICO_CAPACITY;
    }

    if (request->transpose_offsets[0] == INT8_MIN) {
        return GRIDPICO_INVALID;
    }

    if (session->applied_revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }

    const gridpico_result result = session->reconcile_route(session->graph_context, request);
    if (result == GRIDPICO_OK) {
        ++session->applied_revision;
    }

    return result;
}

gridpico_route_snapshot gridpico_session_read_route(void *context)
{
    const gridpico_session *session = context;
    gridpico_route_snapshot snapshot = session->read_graph_route(session->graph_context);
    snapshot.applied_revision = session->applied_revision;
    return snapshot;
}
