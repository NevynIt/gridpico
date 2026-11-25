#include "core/session/patch_session.h"

#include <string.h>

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

void gridpico_patch_session_bind_local_routes(gridpico_patch_session *session,
                                             gridpico_local_set_route_fn set_route,
                                             gridpico_local_read_route_fn read_route)
{
    session->set_local_graph_route = set_route;
    session->read_local_graph_route = read_route;
}

void gridpico_patch_session_bind_local_routing(gridpico_patch_session *session,
                                              gridpico_local_read_graph_fn read_graph,
                                              gridpico_local_read_availability_fn read_availability,
                                              void *availability_context)
{
    session->read_local_graph = read_graph;
    session->read_availability = read_availability;
    session->availability_context = availability_context;
}

static gridpico_result read_graph(const gridpico_patch_session *session,
                                 gridpico_patch_graph_state *state)
{
    const gridpico_result result = session->read_graph(session->graph_context, state);
    if (result != GRIDPICO_OK) {
        return result;
    }
    if (state->inventory.device_count > GRIDPICO_PATCH_MAX_DEVICES ||
        state->inventory.port_count > GRIDPICO_PATCH_MAX_PORTS ||
        state->branch_count > GRIDPICO_PATCH_MAX_BRANCHES) {
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

    gridpico_port_id source_rows[GRIDPICO_PATCH_SOURCE_ROWS] = {0};
    gridpico_port_id destination_columns[GRIDPICO_PATCH_DESTINATION_COLUMNS] = {0};
    size_t source_count = 0;
    size_t destination_count = 0;
    for (size_t i = 0; i < graph.inventory.port_count; ++i) {
        const gridpico_patch_port *port = &graph.inventory.ports[i];
        if (!port->patch_eligible) {
            continue;
        }
        if (port->direction == GRIDPICO_PATCH_INPUT) {
            if (source_count == GRIDPICO_PATCH_SOURCE_ROWS) {
                if (session->read_local_graph != NULL) {
                    continue;
                }
                return GRIDPICO_CAPACITY;
            }
            source_rows[source_count++] = port->id;
        } else if (port->direction == GRIDPICO_PATCH_OUTPUT) {
            if (destination_count == GRIDPICO_PATCH_DESTINATION_COLUMNS) {
                if (session->read_local_graph != NULL) {
                    continue;
                }
                return GRIDPICO_CAPACITY;
            }
            destination_columns[destination_count++] = port->id;
        }
    }

    /* Finish all fallible checks before overwriting the caller's snapshot. */
    out->revision = graph.revision;
    out->inventory = graph.inventory;
    memcpy(out->source_rows, source_rows, sizeof(source_rows));
    memcpy(out->destination_columns, destination_columns, sizeof(destination_columns));
    memset(out->routes, 0, sizeof(out->routes));
    for (size_t i = 0; i < graph.branch_count; ++i) {
        const gridpico_patch_branch *branch = &graph.branches[i];
        const size_t row = find_slot(source_rows, source_count, branch->source);
        const size_t column = find_slot(destination_columns, destination_count,
                                        branch->destination);
        if (row < source_count && column < destination_count) {
            out->routes[row][column] = branch->state;
        }
    }

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

static bool valid_local_route(const gridpico_local_route *route)
{
    return route != NULL && route->source != 0 && route->destination != 0 &&
           route->source != route->destination &&
           (route->mode == GRIDPICO_LOCAL_MANAGED || route->mode == GRIDPICO_LOCAL_RAW);
}

gridpico_result gridpico_patch_session_set_local_route(void *context,
                                                     const gridpico_local_route_request *request)
{
    const gridpico_patch_session *session = context;
    if (session == NULL || request == NULL || !valid_local_route(&request->route)) {
        return GRIDPICO_INVALID;
    }
    if (session->set_local_graph_route == NULL) {
        return GRIDPICO_UNAVAILABLE;
    }

    return session->set_local_graph_route(session->graph_context, request);
}

gridpico_result gridpico_patch_session_read_local_route(void *context, const gridpico_local_route *route,
                                                      gridpico_local_route_status *out)
{
    const gridpico_patch_session *session = context;
    if (session == NULL || out == NULL || !valid_local_route(route)) {
        return GRIDPICO_INVALID;
    }
    if (session->read_local_graph_route == NULL) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_local_route_status status;
    const gridpico_result result = session->read_local_graph_route(session->graph_context, route,
                                                                  &status);
    if (result == GRIDPICO_OK) {
        *out = status;
    }
    return result;
}

static gridpico_result read_local_graph(const gridpico_patch_session *session,
                                       gridpico_local_graph_state *state)
{
    if (session->read_local_graph == NULL) {
        return GRIDPICO_UNAVAILABLE;
    }
    const gridpico_result result = session->read_local_graph(session->graph_context, state);
    if (result != GRIDPICO_OK) {
        return result;
    }
    if (state->registry.inventory.device_count > GRIDPICO_PATCH_MAX_DEVICES ||
        state->registry.inventory.port_count > GRIDPICO_PATCH_MAX_PORTS ||
        state->route_count > GRIDPICO_PATCH_MAX_BRANCHES) {
        return GRIDPICO_CAPACITY;
    }
    return GRIDPICO_OK;
}

static gridpico_device_color color_at(const gridpico_patch_session *session, size_t slot)
{
    const gridpico_patch_color_preference *color = &session->colors[slot];
    return (gridpico_device_color){
        .revision = session->color_revision,
        .is_set = color->is_set,
        .rgb = color->rgb,
    };
}

static bool route_precedes(gridpico_local_route left, gridpico_local_route right)
{
    if (left.source != right.source) {
        return left.source < right.source;
    }
    if (left.destination != right.destination) {
        return left.destination < right.destination;
    }
    return left.mode < right.mode;
}

static void sort_enabled_routes(gridpico_local_graph_state *graph)
{
    for (size_t i = 1; i < graph->route_count; ++i) {
        const gridpico_local_enabled_route entry = graph->routes[i];
        size_t slot = i;
        while (slot > 0 && route_precedes(entry.route, graph->routes[slot - 1].route)) {
            graph->routes[slot] = graph->routes[slot - 1];
            --slot;
        }
        graph->routes[slot] = entry;
    }
}

static gridpico_result publish_routing_revision(gridpico_patch_session *session,
                                               gridpico_local_routing_snapshot *snapshot)
{
    const size_t port_count = snapshot->graph.registry.inventory.port_count;
    /* Graph and color revisions cover their values. Availability is read separately. */
    bool changed = session->routing_revision == 0 ||
                   snapshot->graph.revision != session->routing_graph_revision ||
                   session->color_revision != session->routing_color_revision ||
                   port_count != session->routing_port_count;
    for (size_t i = 0; i < port_count; ++i) {
        const gridpico_local_port_availability availability = snapshot->availability[i];
        if (availability != GRIDPICO_LOCAL_UNAVAILABLE && availability != GRIDPICO_LOCAL_FREE &&
            availability != GRIDPICO_LOCAL_IN_USE) {
            return GRIDPICO_INVALID;
        }
        changed = changed || availability != session->routing_availability[i];
    }

    if (changed) {
        if (session->routing_revision == UINT64_MAX) {
            return GRIDPICO_CAPACITY;
        }
        ++session->routing_revision;
        session->routing_graph_revision = snapshot->graph.revision;
        session->routing_color_revision = session->color_revision;
        session->routing_port_count = port_count;
        memcpy(session->routing_availability, snapshot->availability,
               port_count * sizeof(snapshot->availability[0]));
    }
    snapshot->revision = session->routing_revision;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_session_read_local_routing(void *context,
                                                         gridpico_local_routing_snapshot *out)
{
    gridpico_patch_session *session = context;
    if (session == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }
    if (session->read_availability == NULL) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_local_routing_snapshot snapshot = {0};
    gridpico_result result = read_local_graph(session, &snapshot.graph);
    if (result != GRIDPICO_OK) {
        return result;
    }
    result = session->read_availability(session->availability_context, &snapshot.graph.registry,
                                        snapshot.availability);
    if (result != GRIDPICO_OK) {
        return result;
    }

    for (size_t i = 0; i < snapshot.graph.registry.inventory.device_count; ++i) {
        snapshot.colors[i] = color_at(session, i);
    }
    sort_enabled_routes(&snapshot.graph);
    result = publish_routing_revision(session, &snapshot);
    if (result == GRIDPICO_OK) {
        *out = snapshot;
    }
    return result;
}

static size_t project_axis(const gridpico_port_id *ports, size_t count,
                           gridpico_port_id *slots, size_t length, size_t *offset)
{
    const size_t max_offset = count > length ? count - length : 0;
    if (*offset > max_offset) {
        *offset = max_offset;
    }
    const size_t used = count < length ? count : length;
    memcpy(slots, ports + *offset, used * sizeof(slots[0]));
    return used;
}

gridpico_result gridpico_patch_session_read_local_patch_page(void *context,
                                                            const gridpico_local_patch_viewport *request,
                                                            gridpico_local_patch_page *out)
{
    const gridpico_patch_session *session = context;
    if (session == NULL || request == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }

    gridpico_local_graph_state graph;
    const gridpico_result result = read_local_graph(session, &graph);
    if (result != GRIDPICO_OK) {
        return result;
    }

    gridpico_local_patch_page page = {.revision = graph.revision, .viewport = *request};
    gridpico_port_id sources[GRIDPICO_PATCH_MAX_PORTS];
    gridpico_port_id destinations[GRIDPICO_PATCH_MAX_PORTS];
    for (size_t i = 0; i < graph.registry.inventory.port_count; ++i) {
        const gridpico_patch_port *port = &graph.registry.inventory.ports[i];
        if (!port->patch_eligible || graph.registry.port_capabilities[i] != GRIDPICO_LOCAL_PORT_MANAGED) {
            continue;
        }
        if (port->direction == GRIDPICO_PATCH_INPUT) {
            sources[page.source_count++] = port->id;
        } else if (port->direction == GRIDPICO_PATCH_OUTPUT) {
            destinations[page.destination_count++] = port->id;
        }
    }
    const size_t rows = project_axis(sources, page.source_count, page.source_rows,
                                     GRIDPICO_PATCH_SOURCE_ROWS, &page.viewport.source_offset);
    const size_t columns = project_axis(destinations, page.destination_count, page.destination_columns,
                                        GRIDPICO_PATCH_DESTINATION_COLUMNS,
                                        &page.viewport.destination_offset);

    for (size_t i = 0; i < graph.route_count; ++i) {
        const gridpico_local_enabled_route *entry = &graph.routes[i];
        if (entry->route.mode != GRIDPICO_LOCAL_MANAGED) {
            continue;
        }
        const size_t row = find_slot(page.source_rows, rows, entry->route.source);
        const size_t column = find_slot(page.destination_columns, columns, entry->route.destination);
        if (row < rows && column < columns) {
            page.routes[row][column] = entry->state;
        }
    }

    *out = page;
    return GRIDPICO_OK;
}

static bool apc_port_eligible(const gridpico_local_registry *registry, size_t index,
                              gridpico_port_id audition_source)
{
    const gridpico_patch_port *port = &registry->inventory.ports[index];
    return port->id != audition_source && port->patch_eligible &&
           registry->port_capabilities[index] == GRIDPICO_LOCAL_PORT_MANAGED;
}

static const gridpico_patch_port *find_apc_port(const gridpico_local_registry *registry,
                                               gridpico_port_id audition_source,
                                               gridpico_patch_direction axis, gridpico_port_id id)
{
    if (id != 0) {
        for (size_t i = 0; i < registry->inventory.port_count; ++i) {
            const gridpico_patch_port *port = &registry->inventory.ports[i];
            if (port->id == id && port->direction == axis &&
                apc_port_eligible(registry, i, audition_source)) {
                return port;
            }
        }
    }
    return NULL;
}

static bool valid_apc_axis(const gridpico_local_registry *registry, gridpico_port_id audition_source,
                           gridpico_patch_direction axis, const gridpico_port_id *slots, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (slots[i] != 0 &&
            (find_slot(slots, i, slots[i]) != i ||
             find_apc_port(registry, audition_source, axis, slots[i]) == NULL)) {
            return false;
        }
    }
    return true;
}

static bool valid_apc_layout(const gridpico_local_registry *registry, gridpico_port_id audition_source,
                             const gridpico_apc_patch_layout *layout)
{
    return valid_apc_axis(registry, audition_source, GRIDPICO_PATCH_INPUT,
                          layout->rows, GRIDPICO_PATCH_SOURCE_ROWS) &&
           valid_apc_axis(registry, audition_source, GRIDPICO_PATCH_OUTPUT,
                          layout->columns, GRIDPICO_PATCH_DESTINATION_COLUMNS);
}

gridpico_result gridpico_patch_session_bind_apc_patch(gridpico_patch_session *session,
                                                    const gridpico_apc_patch_layout *layout,
                                                    gridpico_port_id audition_source)
{
    if (session == NULL || layout == NULL || session->apc_layout_revision != 0) {
        return GRIDPICO_INVALID;
    }

    gridpico_local_graph_state graph;
    const gridpico_result result = read_local_graph(session, &graph);
    if (result != GRIDPICO_OK) {
        return result;
    }
    if (!valid_apc_layout(&graph.registry, audition_source, layout)) {
        return GRIDPICO_INVALID;
    }

    for (size_t i = 0; i < graph.registry.inventory.port_count; ++i) {
        const gridpico_patch_port *port = &graph.registry.inventory.ports[i];
        if (port->direction == GRIDPICO_PATCH_INPUT &&
            apc_port_eligible(&graph.registry, i, audition_source)) {
            session->apc_attack_sources[i] = port->id;
        }
    }
    session->apc_layout = *layout;
    session->apc_audition_source = audition_source;
    session->apc_layout_revision = 1;
    return GRIDPICO_OK;
}

static bool compatible_apc_cell(const gridpico_local_registry *registry,
                                const gridpico_patch_port *source, const gridpico_patch_port *destination)
{
    return source != NULL && destination != NULL &&
           (source->device != destination->device ||
            gridpico_local_native_virtual_cable_matches(&registry->native_virtual_cable,
                                                       source->id, destination->id));
}

gridpico_result gridpico_patch_session_export_apc_snapshot(const gridpico_patch_session *session,
                                                         gridpico_apc_session_snapshot *out)
{
    if (session == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }
    if (session->apc_layout_revision == 0) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_local_graph_state graph;
    const gridpico_result result = read_local_graph(session, &graph);
    if (result != GRIDPICO_OK) {
        return result;
    }

    sort_enabled_routes(&graph);
    gridpico_apc_session_snapshot snapshot = {.layout = session->apc_layout};
    for (size_t i = 0; i < graph.route_count; ++i) {
        const gridpico_local_route route = graph.routes[i].route;
        if (route.mode != GRIDPICO_LOCAL_MANAGED || route.source == session->apc_audition_source ||
            route.destination == session->apc_audition_source) {
            continue;
        }
        if (snapshot.route_count == GRIDPICO_APC_SESSION_SNAPSHOT_ROUTES) {
            return GRIDPICO_CAPACITY;
        }
        snapshot.routes[snapshot.route_count++] = (gridpico_apc_session_route){route.source,
                                                                              route.destination};
    }
    *out = snapshot;
    return GRIDPICO_OK;
}

static bool fresh_apc_graph(const gridpico_local_graph_state *graph)
{
    if (graph->revision != 0 || graph->route_count != 0) {
        return false;
    }
    for (size_t i = 0; i < graph->registry.inventory.port_count; ++i) {
        if (graph->registry.inventory.ports[i].state != GRIDPICO_PATCH_DISCONNECTED) {
            return false;
        }
    }
    return true;
}

static bool valid_apc_snapshot_routes(const gridpico_local_registry *registry,
                                      gridpico_port_id audition_source,
                                      const gridpico_apc_session_snapshot *snapshot)
{
    for (size_t i = 0; i < snapshot->route_count; ++i) {
        const gridpico_apc_session_route route = snapshot->routes[i];
        const gridpico_patch_port *source = find_apc_port(registry, audition_source,
                                                        GRIDPICO_PATCH_INPUT, route.source);
        const gridpico_patch_port *destination = find_apc_port(registry, audition_source,
                                                             GRIDPICO_PATCH_OUTPUT, route.destination);
        if (!compatible_apc_cell(registry, source, destination)) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (snapshot->routes[j].source == route.source &&
                snapshot->routes[j].destination == route.destination) {
                return false;
            }
        }
    }
    return true;
}

gridpico_result gridpico_patch_session_restore_apc_snapshot(gridpico_patch_session *session,
                                                          const gridpico_apc_session_snapshot *snapshot)
{
    if (session == NULL || snapshot == NULL) {
        return GRIDPICO_INVALID;
    }
    if (snapshot->route_count > GRIDPICO_APC_SESSION_SNAPSHOT_ROUTES) {
        return GRIDPICO_CAPACITY;
    }
    if (session->apc_layout_revision != 1 || session->apc_attack_sequence != 0 ||
        session->set_local_graph_route == NULL) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_local_graph_state graph;
    gridpico_result result = read_local_graph(session, &graph);
    if (result != GRIDPICO_OK) {
        return result;
    }
    if (!fresh_apc_graph(&graph)) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (!valid_apc_layout(&graph.registry, session->apc_audition_source, &snapshot->layout) ||
        !valid_apc_snapshot_routes(&graph.registry, session->apc_audition_source, snapshot)) {
        return GRIDPICO_INVALID;
    }

    for (size_t i = 0; i < snapshot->route_count; ++i) {
        const gridpico_apc_session_route route = snapshot->routes[i];
        const gridpico_local_route_request request = {{route.source, route.destination, GRIDPICO_LOCAL_MANAGED},
                                                     true};
        result = gridpico_patch_session_set_local_route(session, &request);
        if (result != GRIDPICO_OK) {
            return result;
        }
    }

    if (memcmp(session->apc_layout.rows, snapshot->layout.rows, sizeof(session->apc_layout.rows)) != 0 ||
        memcmp(session->apc_layout.columns, snapshot->layout.columns, sizeof(session->apc_layout.columns)) != 0) {
        session->apc_layout = snapshot->layout;
        ++session->apc_layout_revision;
    }
    return GRIDPICO_OK;
}

static void read_apc_cells(const gridpico_local_graph_state *graph, gridpico_port_id audition_source,
                           gridpico_apc_patch_snapshot *snapshot)
{
    const gridpico_patch_port *rows[GRIDPICO_PATCH_SOURCE_ROWS];
    const gridpico_patch_port *columns[GRIDPICO_PATCH_DESTINATION_COLUMNS];
    for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
        rows[row] = find_apc_port(&graph->registry, audition_source, GRIDPICO_PATCH_INPUT,
                                 snapshot->layout.rows[row]);
    }
    for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
        columns[column] = find_apc_port(&graph->registry, audition_source, GRIDPICO_PATCH_OUTPUT,
                                        snapshot->layout.columns[column]);
    }
    for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
        for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
            if (compatible_apc_cell(&graph->registry, rows[row], columns[column])) {
                gridpico_apc_patch_cell *cell = &snapshot->cells[row][column];
                cell->compatible = true;
                cell->available = rows[row]->state == GRIDPICO_PATCH_CONNECTED &&
                                  columns[column]->state == GRIDPICO_PATCH_CONNECTED;
            }
        }
    }
    for (size_t i = 0; i < graph->route_count; ++i) {
        const gridpico_local_enabled_route *entry = &graph->routes[i];
        if (entry->route.mode != GRIDPICO_LOCAL_MANAGED) {
            continue;
        }
        const size_t row = find_slot(snapshot->layout.rows, GRIDPICO_PATCH_SOURCE_ROWS,
                                     entry->route.source);
        const size_t column = find_slot(snapshot->layout.columns, GRIDPICO_PATCH_DESTINATION_COLUMNS,
                                        entry->route.destination);
        if (row < GRIDPICO_PATCH_SOURCE_ROWS && column < GRIDPICO_PATCH_DESTINATION_COLUMNS &&
            snapshot->cells[row][column].compatible) {
            snapshot->cells[row][column].state = entry->state;
        }
    }
}

gridpico_result gridpico_patch_session_read_apc_patch(void *context, gridpico_apc_patch_snapshot *out)
{
    const gridpico_patch_session *session = context;
    if (session == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }
    if (session->apc_layout_revision == 0) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_local_graph_state graph;
    const gridpico_result result = read_local_graph(session, &graph);
    if (result != GRIDPICO_OK) {
        return result;
    }

    gridpico_apc_patch_snapshot snapshot = {
        .layout_revision = session->apc_layout_revision,
        .graph_revision = graph.revision,
        .layout = session->apc_layout,
        .attack_sequence = session->apc_attack_sequence,
    };
    for (size_t i = 0; i < graph.registry.inventory.port_count; ++i) {
        if (apc_port_eligible(&graph.registry, i, session->apc_audition_source)) {
            const gridpico_patch_port *port = &graph.registry.inventory.ports[i];
            snapshot.ports[snapshot.port_count++] = (gridpico_apc_patch_port){
                .id = port->id,
                .direction = port->direction,
                .connected = port->state == GRIDPICO_PATCH_CONNECTED,
                .last_attack_sequence = session->apc_attacks[i].sequence,
                .last_attack_us = session->apc_attacks[i].time_us,
            };
        }
    }
    read_apc_cells(&graph, session->apc_audition_source, &snapshot);

    *out = snapshot;
    return GRIDPICO_OK;
}

static size_t apc_axis_length(gridpico_patch_direction axis)
{
    if (axis == GRIDPICO_PATCH_INPUT) {
        return GRIDPICO_PATCH_SOURCE_ROWS;
    }
    if (axis == GRIDPICO_PATCH_OUTPUT) {
        return GRIDPICO_PATCH_DESTINATION_COLUMNS;
    }
    return 0;
}

gridpico_result gridpico_patch_session_assign_apc_patch(void *context, gridpico_revision layout_revision,
                                                      gridpico_patch_direction axis, uint8_t slot,
                                                      gridpico_port_id port)
{
    gridpico_patch_session *session = context;
    const size_t count = apc_axis_length(axis);
    if (session == NULL || slot >= count) {
        return GRIDPICO_INVALID;
    }
    if (session->apc_layout_revision == 0 || layout_revision != session->apc_layout_revision) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_port_id *slots = axis == GRIDPICO_PATCH_INPUT ? session->apc_layout.rows
                                                         : session->apc_layout.columns;
    if (slots[slot] == port) {
        return GRIDPICO_OK;
    }
    if (port != 0) {
        if (find_slot(slots, count, port) != count) {
            return GRIDPICO_INVALID;
        }
        gridpico_local_graph_state graph;
        const gridpico_result result = read_local_graph(session, &graph);
        if (result != GRIDPICO_OK) {
            return result;
        }
        if (find_apc_port(&graph.registry, session->apc_audition_source, axis, port) == NULL) {
            return GRIDPICO_INVALID;
        }
    }
    if (session->apc_layout_revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }

    slots[slot] = port;
    ++session->apc_layout_revision;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_session_move_apc_patch(void *context, gridpico_revision layout_revision,
                                                    gridpico_patch_direction axis, uint8_t from,
                                                    uint8_t to)
{
    gridpico_patch_session *session = context;
    const size_t count = apc_axis_length(axis);
    if (session == NULL || from >= count || to >= count) {
        return GRIDPICO_INVALID;
    }
    if (session->apc_layout_revision == 0 || layout_revision != session->apc_layout_revision) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_port_id *slots = axis == GRIDPICO_PATCH_INPUT ? session->apc_layout.rows
                                                         : session->apc_layout.columns;
    if (slots[from] == slots[to]) {
        return GRIDPICO_OK;
    }
    if (session->apc_layout_revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }

    const gridpico_port_id port = slots[from];
    slots[from] = slots[to];
    slots[to] = port;
    ++session->apc_layout_revision;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_session_set_apc_patch_cell(void *context,
                                                        gridpico_revision layout_revision,
                                                        gridpico_revision graph_revision,
                                                        uint8_t row, uint8_t column, bool enabled)
{
    const gridpico_patch_session *session = context;
    if (session == NULL || row >= GRIDPICO_PATCH_SOURCE_ROWS ||
        column >= GRIDPICO_PATCH_DESTINATION_COLUMNS) {
        return GRIDPICO_INVALID;
    }
    if (session->apc_layout_revision == 0 || layout_revision != session->apc_layout_revision) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_local_graph_state graph;
    const gridpico_result result = read_local_graph(session, &graph);
    if (result != GRIDPICO_OK) {
        return result;
    }
    if (graph_revision != graph.revision) {
        return GRIDPICO_UNAVAILABLE;
    }

    const gridpico_patch_port *source = find_apc_port(&graph.registry, session->apc_audition_source,
                                                    GRIDPICO_PATCH_INPUT, session->apc_layout.rows[row]);
    const gridpico_patch_port *destination = find_apc_port(
        &graph.registry, session->apc_audition_source, GRIDPICO_PATCH_OUTPUT,
        session->apc_layout.columns[column]);
    if (!compatible_apc_cell(&graph.registry, source, destination)) {
        return GRIDPICO_INVALID;
    }

    const gridpico_local_route_request request = {{source->id, destination->id, GRIDPICO_LOCAL_MANAGED},
                                                 enabled};
    return gridpico_patch_session_set_local_route(context, &request);
}

gridpico_result gridpico_patch_session_add_apc_attack_source(
    gridpico_patch_session *session, size_t registry_index, gridpico_port_id source)
{
    if (session == NULL || registry_index >= GRIDPICO_PATCH_MAX_PORTS || source == 0 ||
        source == session->apc_audition_source) {
        return GRIDPICO_INVALID;
    }
    if (session->apc_layout_revision == 0) {
        return GRIDPICO_UNAVAILABLE;
    }

    const gridpico_port_id current = session->apc_attack_sources[registry_index];
    if (current == source) {
        return GRIDPICO_OK;
    }
    if (current != 0 ||
        find_slot(session->apc_attack_sources, GRIDPICO_PATCH_MAX_PORTS, source) != GRIDPICO_PATCH_MAX_PORTS) {
        return GRIDPICO_INVALID;
    }

    session->apc_attack_sources[registry_index] = source;
    return GRIDPICO_OK;
}

void gridpico_patch_session_observe_apc_patch_note(gridpico_patch_session *session,
                                                const gridpico_ingress_note *input, uint64_t now_us)
{
    if (session == NULL || input == NULL || session->apc_layout_revision == 0 ||
        input->source.endpoint == 0 || input->source.generation == 0 ||
        input->note.kind != GRIDPICO_NOTE_ON || input->note.channel_index > 15 ||
        input->note.key > 127 || input->note.velocity == 0 || input->note.velocity > 127 ||
        session->apc_attack_sequence == UINT64_MAX) {
        return;
    }

    const size_t index = find_slot(session->apc_attack_sources, GRIDPICO_PATCH_MAX_PORTS,
                                   input->source.endpoint);
    if (index < GRIDPICO_PATCH_MAX_PORTS) {
        ++session->apc_attack_sequence;
        session->apc_attacks[index] = (gridpico_patch_attack){session->apc_attack_sequence, now_us};
    }
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

    *out = color_at(session, slot);
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
