#include "core/session/patch_session.h"

_Static_assert(GRIDPICO_PATCH_PORTS <= GRIDPICO_PATCH_SOURCE_ROWS &&
                   GRIDPICO_PATCH_PORTS <= GRIDPICO_PATCH_DESTINATION_COLUMNS,
               "A Patch page must hold all registered ports of either direction");

void gridpico_patch_session_init(gridpico_patch_session *session, void *graph_context,
                                gridpico_patch_read_graph_fn read_graph,
                                gridpico_patch_set_route_fn set_graph_route)
{
    *session = (gridpico_patch_session){
        .graph_context = graph_context,
        .read_graph = read_graph,
        .set_graph_route = set_graph_route,
    };
}

static gridpico_result read_graph(const gridpico_patch_session *session,
                                 gridpico_patch_graph_state *state)
{
    const gridpico_result result = session->read_graph(session->graph_context, state);
    if (result != GRIDPICO_OK) {
        return result;
    }
    if (state->inventory.device_count > GRIDPICO_PATCH_DEVICES ||
        state->inventory.port_count > GRIDPICO_PATCH_PORTS ||
        state->branch_count > GRIDPICO_PATCH_BRANCHES) {
        return GRIDPICO_CAPACITY;
    }
    return GRIDPICO_OK;
}

static size_t find_slot(const gridpico_port_id *slots, size_t count, gridpico_port_id id)
{
    for (size_t i = 0; i < count; ++i) {
        if (slots[i] == id) {
            return i;
        }
    }
    return count;
}

gridpico_result gridpico_patch_session_read_snapshot(void *context, gridpico_patch_snapshot *out)
{
    const gridpico_patch_session *session = context;
    if (session == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }

    gridpico_patch_graph_state graph;
    const gridpico_result result = read_graph(session, &graph);
    if (result != GRIDPICO_OK) {
        return result;
    }

    gridpico_patch_snapshot snapshot = {
        .revision = graph.revision,
        .inventory = graph.inventory,
    };
    size_t source_count = 0;
    size_t destination_count = 0;
    for (size_t i = 0; i < graph.inventory.port_count; ++i) {
        const gridpico_patch_port *port = &graph.inventory.ports[i];
        if (!port->patch_eligible) {
            continue;
        }
        if (port->direction == GRIDPICO_PATCH_INPUT) {
            snapshot.source_rows[source_count++] = port->id;
        } else if (port->direction == GRIDPICO_PATCH_OUTPUT) {
            snapshot.destination_columns[destination_count++] = port->id;
        }
    }

    for (size_t i = 0; i < graph.branch_count; ++i) {
        const gridpico_patch_branch *branch = &graph.branches[i];
        const size_t row = find_slot(snapshot.source_rows, source_count, branch->source);
        const size_t column = find_slot(snapshot.destination_columns, destination_count,
                                        branch->destination);
        if (row < source_count && column < destination_count) {
            snapshot.routes[row][column] = branch->state;
        }
    }

    *out = snapshot;
    return GRIDPICO_OK;
}

static bool is_eligible_port(const gridpico_patch_inventory *inventory, gridpico_port_id id,
                             gridpico_patch_direction direction)
{
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const gridpico_patch_port *port = &inventory->ports[i];
        if (port->id == id) {
            return port->patch_eligible && port->direction == direction;
        }
    }
    return false;
}

gridpico_result gridpico_patch_session_set_route(void *context,
                                               const gridpico_patch_route_request *request)
{
    const gridpico_patch_session *session = context;
    if (session == NULL || request == NULL || request->source == 0 ||
        request->destination == 0 || request->source == request->destination) {
        return GRIDPICO_INVALID;
    }

    gridpico_patch_graph_state graph;
    const gridpico_result result = read_graph(session, &graph);
    if (result != GRIDPICO_OK) {
        return result;
    }

    if (!is_eligible_port(&graph.inventory, request->source, GRIDPICO_PATCH_INPUT) ||
        !is_eligible_port(&graph.inventory, request->destination, GRIDPICO_PATCH_OUTPUT)) {
        return GRIDPICO_INVALID;
    }

    return session->set_graph_route(session->graph_context, request);
}

static gridpico_result find_device_slot(const gridpico_patch_session *session,
                                       gridpico_device_id device, size_t *slot)
{
    if (device == 0) {
        return GRIDPICO_INVALID;
    }

    gridpico_patch_graph_state graph;
    const gridpico_result result = read_graph(session, &graph);
    if (result != GRIDPICO_OK) {
        return result;
    }

    for (size_t i = 0; i < graph.inventory.device_count; ++i) {
        if (graph.inventory.devices[i].id == device) {
            *slot = i;
            return GRIDPICO_OK;
        }
    }
    return GRIDPICO_INVALID;
}

gridpico_result gridpico_session_read_device_color(void *context, gridpico_device_id device,
                                                 gridpico_device_color *out)
{
    const gridpico_patch_session *session = context;
    if (session == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }

    size_t slot;
    const gridpico_result result = find_device_slot(session, device, &slot);
    if (result != GRIDPICO_OK) {
        return result;
    }

    const gridpico_patch_color_preference *color = &session->colors[slot];
    *out = (gridpico_device_color){
        .revision = session->color_revision,
        .is_set = color->is_set,
        .rgb = color->rgb,
    };
    return GRIDPICO_OK;
}

gridpico_result gridpico_session_set_device_color(void *context, gridpico_device_id device,
                                                gridpico_rgb8 rgb)
{
    gridpico_patch_session *session = context;
    if (session == NULL) {
        return GRIDPICO_INVALID;
    }

    size_t slot;
    const gridpico_result result = find_device_slot(session, device, &slot);
    if (result != GRIDPICO_OK) {
        return result;
    }

    gridpico_patch_color_preference *color = &session->colors[slot];
    if (color->is_set && color->rgb.red == rgb.red && color->rgb.green == rgb.green &&
        color->rgb.blue == rgb.blue) {
        return GRIDPICO_OK;
    }
    if (session->color_revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }

    *color = (gridpico_patch_color_preference){.is_set = true, .rgb = rgb};
    ++session->color_revision;
    return GRIDPICO_OK;
}
