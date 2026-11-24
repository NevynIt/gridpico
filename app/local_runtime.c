#include "app/local_runtime.h"

#include <string.h>

enum {
    APC_KEYBED_PORT = 1,
    APC_FEEDBACK_PORT = 3,
    WEB_MONITOR_PORT = 5,
    MIDI_LOG_PORT = 6,
};

static const local_router_profile default_profile = {
    .registry = {
        .inventory = {
            .device_count = GRIDPICO_LOCAL_DEVICES,
            .port_count = GRIDPICO_LOCAL_PORTS,
            .devices = {
                {1, "Web APC", GRIDPICO_PATCH_DISCONNECTED},
                {2, "Diagnostics", GRIDPICO_PATCH_DISCONNECTED},
                {3, "Web Monitor", GRIDPICO_PATCH_DISCONNECTED},
                {4, "MIDI Log", GRIDPICO_PATCH_DISCONNECTED},
            },
            .ports = {
                {APC_KEYBED_PORT, 1, "Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
                {2, 1, "Panel", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, false},
                {APC_FEEDBACK_PORT, 1, "LED feedback", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, false},
                {4, 2, "LED Sequence", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, false},
                {WEB_MONITOR_PORT, 3, "Monitor", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
                {MIDI_LOG_PORT, 4, "Log", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            },
        },
        .port_capabilities = {
            GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_PANEL, GRIDPICO_LOCAL_PORT_RAW,
            GRIDPICO_LOCAL_PORT_RAW, GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
        },
    },
    .limits = {.midi_peer_limit = GRIDPICO_LOCAL_PORTS},
    .ports = {
        {LOCAL_ROLE_KEYBED, LOCAL_MIDI_QUEUE, true},
        {LOCAL_ROLE_PANEL, LOCAL_PANEL_QUEUE, true},
        {LOCAL_ROLE_LED, LOCAL_MIDI_QUEUE, true},
        {LOCAL_ROLE_SEQUENCE, LOCAL_MIDI_QUEUE, true},
        {LOCAL_ROLE_MONITOR, LOCAL_MIDI_QUEUE, true},
        {LOCAL_ROLE_LOG, LOCAL_MIDI_QUEUE, true},
    },
};

static size_t port_index(const local_runtime *runtime, gridpico_port_id id)
{
    const gridpico_patch_inventory *inventory = &runtime->profile->registry.inventory;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (inventory->ports[i].id == id) {
            return i;
        }
    }
    return inventory->port_count;
}

static local_binding *binding(local_runtime *runtime, gridpico_port_id id)
{
    const size_t index = port_index(runtime, id);
    return index < runtime->profile->registry.inventory.port_count ? &runtime->bindings[index] : NULL;
}

static bool socket_pool_full(const local_runtime *runtime)
{
    size_t admitted = 0;
    for (size_t i = 0; i < runtime->profile->registry.inventory.port_count; ++i) {
        if (runtime->bindings[i].attached && !runtime->bindings[i].adapter_owned) ++admitted;
    }
    return admitted >= runtime->profile->limits.midi_peer_limit;
}

static gridpico_result first_error(gridpico_result a, gridpico_result b)
{
    return a == GRIDPICO_OK ? b : a;
}

static void append_log(local_runtime *runtime, gridpico_diagnostic_log_entry entry)
{
    entry.sequence = ++runtime->log_sequence;
    const size_t i = (runtime->log_start + runtime->log_count) % GRIDPICO_DIAGNOSTIC_LOG_ROWS;
    runtime->logs[i] = entry;
    if (runtime->log_count == GRIDPICO_DIAGNOSTIC_LOG_ROWS) {
        runtime->log_start = (runtime->log_start + 1u) % GRIDPICO_DIAGNOSTIC_LOG_ROWS;
    } else {
        ++runtime->log_count;
    }
}

static size_t read_logs(void *context, gridpico_diagnostic_log_entry *rows, size_t capacity)
{
    const local_runtime *runtime = context;
    const size_t count = capacity < runtime->log_count ? capacity : runtime->log_count;
    for (size_t i = 0; i < count; ++i) {
        rows[i] = runtime->logs[(runtime->log_start + i) % GRIDPICO_DIAGNOSTIC_LOG_ROWS];
    }
    return count;
}

static gridpico_result stage_midi(local_runtime *runtime, gridpico_connection to,
                                  const uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES])
{
    const size_t index = port_index(runtime, to.endpoint);
    if (index == runtime->profile->registry.inventory.port_count) {
        return GRIDPICO_UNAVAILABLE;
    }
    local_binding *sink = &runtime->bindings[index];
    if (!sink->attached || sink->generation != to.generation || sink->fault) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (sink->queued >= runtime->profile->ports[index].queue_limit ||
        (sink->adapter.send == NULL &&
         runtime->output_count - runtime->fault_count >= LOCAL_MIDI_STAGE_FRAMES)) {
        sink->fault = true;
        return GRIDPICO_UNAVAILABLE;
    }
    if (sink->adapter.send != NULL) {
        const gridpico_result result = sink->adapter.send(sink->adapter.context, to, bytes);
        if (result != GRIDPICO_OK) {
            sink->fault = true;
        } else {
            ++sink->queued;
        }
        return result;
    }
    local_output *row = &runtime->output[runtime->output_count++];
    *row = (local_output){.destination = to};
    memcpy(row->bytes, bytes, sizeof(row->bytes));
    ++sink->queued;
    return GRIDPICO_OK;
}

static gridpico_result deliver_note(void *context, const gridpico_note_delivery *delivery)
{
    const gridpico_note note = delivery->note;
    const uint8_t bytes[] = {
        (uint8_t)((note.kind == GRIDPICO_NOTE_ON ? 0x90 : 0x80) | note.channel_index),
        note.key, note.velocity,
    };
    return stage_midi(context, delivery->destination, bytes);
}

static gridpico_result deliver_raw(void *context, const gridpico_local_raw_delivery *delivery)
{
    return stage_midi(context, delivery->destination, delivery->bytes);
}

static void deliver_feedback(void *context, const gridpico_apc_surface_message *message)
{
    local_runtime *runtime = context;
    const local_binding *sink = binding(runtime, runtime->feedback_port);
    if (sink != NULL && sink->attached && runtime->raw_deadline_ms == 0 &&
        (runtime->feedback_ready || runtime->refreshing)) {
        const gridpico_connection destination = {runtime->feedback_port, sink->generation};
        stage_midi(runtime, destination, message->bytes);
    }
}

static gridpico_result read_snapshot(void *context, gridpico_patch_snapshot *out)
{
    local_runtime *runtime = context;
    return gridpico_patch_session_read_snapshot(&runtime->session, out);
}

static gridpico_result read_color(void *context, gridpico_device_id id, gridpico_device_color *out)
{
    local_runtime *runtime = context;
    return gridpico_session_read_device_color(&runtime->session, id, out);
}

static gridpico_result set_color(void *context, gridpico_device_id id, gridpico_rgb8 rgb)
{
    local_runtime *runtime = context;
    const gridpico_result result = gridpico_session_set_device_color(&runtime->session, id, rgb);
    if (result == GRIDPICO_OK) {
        runtime->feedback_refresh = true;
    }
    return result;
}

static gridpico_result set_panel_route(void *context, const gridpico_patch_route_request *request)
{
    local_runtime *runtime = context;
    return gridpico_patch_session_set_route(&runtime->session, request);
}

static gridpico_result set_panel_color(void *context, gridpico_device_id id, gridpico_rgb8 rgb)
{
    local_runtime *runtime = context;
    return gridpico_session_set_device_color(&runtime->session, id, rgb);
}

static void reset_panel(local_runtime *runtime)
{
    if (runtime->profile->passive_surface) {
        gridpico_apc_patch_init_passive(&runtime->apc, &runtime->session,
                                       gridpico_patch_session_read_local_patch_page,
                                       runtime, deliver_feedback);
    } else {
        gridpico_apc_patch_init(&runtime->apc, runtime, read_snapshot, set_panel_route, read_color,
                                set_panel_color, runtime, deliver_feedback);
    }
}

static bool route_feedback_changed(const gridpico_patch_graph_state *before,
                                   const gridpico_patch_graph_state *after)
{
    if (before->branch_count != after->branch_count) {
        return true;
    }
    for (size_t i = 0; i < before->branch_count; ++i) {
        const gridpico_patch_branch *a = &before->branches[i];
        const gridpico_patch_branch *b = &after->branches[i];
        if (a->source != b->source || a->destination != b->destination || a->state != b->state) {
            return true;
        }
    }
    return false;
}

static gridpico_result apply_port(local_runtime *runtime, gridpico_connection connection,
                                  bool attached)
{
    const gridpico_patch_connection_state state = attached ? GRIDPICO_PATCH_CONNECTED
                                                           : GRIDPICO_PATCH_DISCONNECTED;
    const gridpico_patch_port_update update = {connection, state};
    local_binding *port = binding(runtime, connection.endpoint);
    port->generation = connection.generation;
    port->attached = attached;
    gridpico_patch_graph_state before, after;
    gridpico_patch_graph_read_graph(&runtime->graph, &before);
    const gridpico_result result = gridpico_patch_graph_update_port(&runtime->graph, &update);
    gridpico_patch_graph_read_graph(&runtime->graph, &after);
    if (route_feedback_changed(&before, &after)) {
        runtime->feedback_refresh = true;
    }
    append_log(runtime, (gridpico_diagnostic_log_entry){
        .kind = GRIDPICO_DIAGNOSTIC_PORT_STATE, .port = connection.endpoint, .detail.state = state,
    });
    if (!attached) {
        const local_midi_adapter adapter = port->adapter;
        port->adapter = (local_midi_adapter){0};
        if (adapter.retire != NULL) {
            adapter.retire(adapter.context, connection);
        }
    }
    return result;
}

static void stage_fault(local_runtime *runtime, gridpico_connection connection)
{
    local_binding *port = binding(runtime, connection.endpoint);
    if (!port->adapter_owned && !port->fault_staged) {
        port->fault_staged = true;
        ++runtime->fault_count;
        runtime->output[runtime->output_count++] =
            (local_output){.destination = connection, .fault = true};
    }
}

static gridpico_result end_raw(local_runtime *runtime)
{
    const gridpico_local_route route = runtime->raw_route;
    runtime->raw_deadline_ms = 0;
    runtime->raw_route = (gridpico_local_route){0};
    const gridpico_local_route_request disable = {route, false};
    gridpico_result result = gridpico_patch_session_set_local_route(&runtime->session, &disable);
    const local_binding *source = binding(runtime, route.source);
    if (source->attached) {
        const gridpico_connection from = {route.source, source->generation};
        result = first_error(result, apply_port(runtime, from, false));
        stage_fault(runtime, from);
    }
    runtime->feedback_ready = false;
    runtime->feedback_refresh = true;
    return result;
}

static gridpico_result set_local_route(void *context, const gridpico_local_route_request *request)
{
    local_runtime *runtime = context;
    const gridpico_local_route route = request->route;
    const bool raw = route.mode == GRIDPICO_LOCAL_RAW;
    const bool leased = runtime->raw_deadline_ms != 0;
    const bool current = leased && route.source == runtime->raw_route.source &&
                         route.destination == runtime->raw_route.destination;
    gridpico_result result = GRIDPICO_OK;
    if (raw && request->enabled) {
        gridpico_local_route_status state;
        result = gridpico_patch_session_read_local_route(&runtime->session, &route, &state);
        if (result == GRIDPICO_OK && !current &&
            (route.source != runtime->sequence_port || route.destination != runtime->feedback_port ||
             !binding(runtime, route.source)->attached ||
             !runtime->feedback_ready)) {
            result = GRIDPICO_UNAVAILABLE;
        }
    }
    if (result == GRIDPICO_OK) {
        result = raw && current && !request->enabled
            ? end_raw(runtime) : gridpico_patch_session_set_local_route(&runtime->session, request);
    }
    if (result == GRIDPICO_OK) {
        if (raw && request->enabled && !leased) {
            runtime->raw_route = route;
            runtime->raw_deadline_ms = runtime->now_ms + LOCAL_RAW_LEASE_MS;
            runtime->feedback_ready = false;
        } else if (!raw) {
            runtime->feedback_refresh = true;
        }
    } else {
        append_log(runtime, (gridpico_diagnostic_log_entry){
            .kind = GRIDPICO_DIAGNOSTIC_ROUTE_ERROR, .port = request->route.destination,
            .detail.error = result,
        });
    }
    return result;
}

static gridpico_result read_local_route(void *context, const gridpico_local_route *route,
                                       gridpico_local_route_status *out)
{
    local_runtime *runtime = context;
    return gridpico_patch_session_read_local_route(&runtime->session, route, out);
}

static void write_serial(void *context, const char *bytes, size_t size)
{
    local_runtime *runtime = context;
    if (!runtime->serial.fault && size <= sizeof(runtime->serial_bytes) - runtime->serial_count) {
        memcpy(runtime->serial_bytes + runtime->serial_count, bytes, size);
        runtime->serial_count += size;
    } else {
        runtime->serial.fault = true;
    }
}

static gridpico_result read_availability(
    void *context, const gridpico_local_registry *registry,
    gridpico_local_port_availability out[GRIDPICO_PATCH_MAX_PORTS])
{
    const local_runtime *runtime = context;
    const gridpico_patch_inventory *inventory = &runtime->profile->registry.inventory;
    if (registry->inventory.port_count != inventory->port_count) return GRIDPICO_INVALID;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (registry->inventory.ports[i].id != inventory->ports[i].id) return GRIDPICO_INVALID;
    }
    const bool full = socket_pool_full(runtime);
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const local_binding *port = &runtime->bindings[i];
        out[i] = port->attached ? GRIDPICO_LOCAL_IN_USE :
            runtime->profile->ports[i].socket && !port->adapter_owned &&
            port->generation != UINT32_MAX && !full ? GRIDPICO_LOCAL_FREE : GRIDPICO_LOCAL_UNAVAILABLE;
    }
    return GRIDPICO_OK;
}

static gridpico_result read_routing(void *context, gridpico_local_routing_snapshot *out)
{
    local_runtime *runtime = context;
    return gridpico_patch_session_read_local_routing(&runtime->session, out);
}

static void enable_routing(local_runtime *runtime)
{
    gridpico_patch_session_bind_local_routing(&runtime->session, gridpico_patch_graph_read_local_state,
                                             read_availability, runtime);
    runtime->routing_enabled = true;
}

gridpico_result local_runtime_init_profile(local_runtime *runtime, const local_router_profile *profile)
{
    *runtime = (local_runtime){0};
    runtime->configured = profile != NULL;
    runtime->profile = runtime->configured ? profile : &default_profile;
    const gridpico_local_registry *registry = &runtime->profile->registry;
    const gridpico_result result = runtime->configured
        ? gridpico_patch_graph_init_local_profile(&runtime->graph, registry, &profile->limits,
                                                  deliver_note, deliver_raw, runtime)
        : gridpico_patch_graph_init_local(&runtime->graph, registry, deliver_note, deliver_raw, runtime);
    if (result != GRIDPICO_OK) {
        return result;
    }
    runtime->feedback_port = local_profile_role(runtime->profile, LOCAL_ROLE_LED);
    runtime->sequence_port = local_profile_role(runtime->profile, LOCAL_ROLE_SEQUENCE);
    for (size_t i = 0; i < registry->inventory.port_count; ++i) {
        runtime->bindings[i].generation = 1;
    }
    gridpico_patch_session_init(&runtime->session, &runtime->graph, gridpico_patch_graph_read_graph,
                               gridpico_patch_graph_set_route);
    gridpico_patch_session_bind_local_routes(&runtime->session, gridpico_patch_graph_set_local_route,
                                            gridpico_patch_graph_read_local_route);
    if (runtime->configured) enable_routing(runtime);
    reset_panel(runtime);
    return GRIDPICO_OK;
}

gridpico_result local_runtime_init(local_runtime *runtime)
{
    return local_runtime_init_profile(runtime, NULL);
}

gridpico_result local_runtime_enable_web_ui_routes(local_runtime *runtime)
{
    gridpico_local_route_request request = {
        .route = {APC_KEYBED_PORT, WEB_MONITOR_PORT, GRIDPICO_LOCAL_MANAGED},
        .enabled = true,
    };
    const gridpico_result result = set_local_route(runtime, &request);
    if (result != GRIDPICO_OK) {
        return result;
    }
    request.route.destination = MIDI_LOG_PORT;
    const gridpico_result logged = set_local_route(runtime, &request);
    if (logged == GRIDPICO_OK && !runtime->routing_enabled) enable_routing(runtime);
    return logged;
}

void local_runtime_begin(local_runtime *runtime, uint64_t now_ms)
{
    runtime->now_ms = now_ms;
    runtime->output_count = 0;
    runtime->fault_count = 0;
    runtime->serial_count = 0;
    runtime->serial.fault = false;
    for (size_t i = 0; i < runtime->profile->registry.inventory.port_count; ++i) {
        runtime->bindings[i].queued = 0;
        runtime->bindings[i].fault_staged = false;
    }
}

gridpico_result local_runtime_port(const local_runtime *runtime, gridpico_port_id id,
                                  local_port_state *out)
{
    const size_t index = port_index(runtime, id);
    if (index == runtime->profile->registry.inventory.port_count) {
        return GRIDPICO_INVALID;
    }
    const local_binding *binding = &runtime->bindings[index];
    *out = (local_port_state){
        .direction = runtime->profile->registry.inventory.ports[index].direction,
        .limit = runtime->profile->ports[index].queue_limit,
        .generation = binding->generation,
        .attached = binding->attached,
        .adapter_owned = binding->adapter_owned,
        .socket = runtime->profile->ports[index].socket,
    };
    return GRIDPICO_OK;
}

gridpico_result local_runtime_reserve_adapter(local_runtime *runtime, gridpico_port_id id)
{
    local_binding *port = binding(runtime, id);
    if (port == NULL) {
        return GRIDPICO_INVALID;
    }
    if (port->attached || port->generation != 1) {
        return GRIDPICO_UNAVAILABLE;
    }
    port->adapter_owned = true;
    return GRIDPICO_OK;
}

static gridpico_result bind_port(local_runtime *runtime, gridpico_connection connection,
                                 local_midi_adapter adapter, local_port_state *port)
{
    const gridpico_result known = local_runtime_port(runtime, connection.endpoint, port);
    if (known != GRIDPICO_OK || connection.generation == 0) {
        return GRIDPICO_INVALID;
    }
    if (port->adapter_owned != (adapter.retire != NULL) ||
        (adapter.retire == NULL && (!port->socket || socket_pool_full(runtime))) ||
        port->attached || connection.generation <= port->generation ||
        (port->generation == 1 && connection.generation != 2)) {
        return GRIDPICO_UNAVAILABLE;
    }
    binding(runtime, connection.endpoint)->adapter = adapter;
    const gridpico_result result = apply_port(runtime, connection, true);
    if (result != GRIDPICO_OK) {
        apply_port(runtime, connection, false);
        stage_fault(runtime, connection);
    } else if (connection.endpoint == runtime->feedback_port) {
        runtime->feedback_refresh = true;
    }
    return result;
}

gridpico_result local_runtime_bind(local_runtime *runtime, gridpico_connection connection,
                                  gridpico_patch_direction *direction, unsigned *limit)
{
    local_port_state port;
    const gridpico_result result = bind_port(runtime, connection, (local_midi_adapter){0}, &port);
    if (result == GRIDPICO_OK) {
        *direction = port.direction;
        *limit = port.limit;
    }
    return result;
}

gridpico_result local_runtime_bind_managed_out(local_runtime *runtime,
                                              gridpico_connection connection,
                                              gridpico_patch_direction *direction, unsigned *limit)
{
    const gridpico_local_registry *registry = &runtime->profile->registry;
    const size_t index = port_index(runtime, connection.endpoint);
    if (index == registry->inventory.port_count ||
        registry->inventory.ports[index].direction != GRIDPICO_PATCH_OUTPUT ||
        registry->port_capabilities[index] != GRIDPICO_LOCAL_PORT_MANAGED) {
        return GRIDPICO_INVALID;
    }
    return local_runtime_bind(runtime, connection, direction, limit);
}

gridpico_result local_runtime_bind_adapter(local_runtime *runtime, gridpico_connection connection,
                                           local_midi_adapter adapter)
{
    local_port_state port;
    return bind_port(runtime, connection, adapter, &port);
}

gridpico_result local_runtime_detach(local_runtime *runtime, gridpico_connection connection)
{
    local_binding *port = binding(runtime, connection.endpoint);
    if (port == NULL || connection.generation == 0) {
        return GRIDPICO_INVALID;
    }
    if (!port->attached || port->generation != connection.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    port->fault = false;
    gridpico_result result = apply_port(runtime, connection, false);
    if (runtime->profile->registry.port_capabilities[port_index(runtime, connection.endpoint)] == GRIDPICO_LOCAL_PORT_PANEL) {
        if (runtime->profile->passive_surface) gridpico_apc_patch_reset_gestures(&runtime->apc);
        else reset_panel(runtime);
        runtime->feedback_refresh = true;
    } else if (connection.endpoint == runtime->feedback_port) {
        runtime->feedback_ready = false;
    }
    if (runtime->raw_deadline_ms != 0 &&
        (connection.endpoint == runtime->raw_route.source ||
         connection.endpoint == runtime->raw_route.destination)) {
        result = first_error(result, end_raw(runtime));
    }
    return result;
}

static bool valid_triplet(const uint8_t *bytes)
{
    const unsigned status = bytes[0] >> 4;
    return (status == 8 || status == 9 || status == 10 || status == 11 || status == 14) &&
           bytes[1] <= 127 && bytes[2] <= 127;
}

gridpico_result local_runtime_midi(local_runtime *runtime, const gridpico_local_raw_ingress *input)
{
    const size_t index = port_index(runtime, input->source.endpoint);
    if (index == runtime->profile->registry.inventory.port_count ||
        runtime->profile->registry.inventory.ports[index].direction != GRIDPICO_PATCH_INPUT ||
        !valid_triplet(input->bytes) || input->source.generation == 0) {
        return GRIDPICO_INVALID;
    }
    local_binding *port = &runtime->bindings[index];
    if (!port->attached || port->generation != input->source.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    switch (runtime->profile->registry.port_capabilities[index]) {
    case GRIDPICO_LOCAL_PORT_PANEL: {
        gridpico_apc_surface_message message;
        memcpy(message.bytes, input->bytes, sizeof(message.bytes));
        return gridpico_apc_patch_receive(&runtime->apc, &message);
    }
    case GRIDPICO_LOCAL_PORT_RAW: {
        const gridpico_result result = gridpico_patch_graph_submit_raw(&runtime->graph, input);
        return result != GRIDPICO_OK && runtime->raw_deadline_ms != 0
            ? first_error(result, end_raw(runtime)) : result;
    }
    case GRIDPICO_LOCAL_PORT_MANAGED: {
        const uint8_t status = input->bytes[0];
        if ((status >> 4) != 8 && (status >> 4) != 9) {
            return GRIDPICO_INVALID;
        }
        const gridpico_ingress_note note = {
            .source = input->source,
            .note = {
                .kind = (status >> 4) == 9 && input->bytes[2] != 0 ? GRIDPICO_NOTE_ON : GRIDPICO_NOTE_OFF,
                .channel_index = status & 0x0f,
                .key = input->bytes[1],
                .velocity = input->bytes[2],
            },
        };
        const gridpico_result result = gridpico_patch_graph_submit_note(&runtime->graph, &note);
        if (result != GRIDPICO_OK) {
            append_log(runtime, (gridpico_diagnostic_log_entry){
                .kind = GRIDPICO_DIAGNOSTIC_NOTE_ERROR, .port = note.source.endpoint,
                .detail.error = result,
            });
            runtime->feedback_refresh = true;
            port->fault = result == GRIDPICO_CAPACITY;
        }
        return result;
    }
    }
    return GRIDPICO_INVALID;
}

gridpico_result local_runtime_serial_bind(local_runtime *runtime, uint32_t generation)
{
    if (generation == 0) {
        return GRIDPICO_INVALID;
    }
    if (runtime->serial.attached || generation <= runtime->serial.generation ||
        (runtime->serial.generation == 0 && generation != 1)) {
        return GRIDPICO_UNAVAILABLE;
    }
    const gridpico_result result = gridpico_cli_init(&runtime->cli, runtime, read_snapshot, read_color,
                                                   set_color, runtime, read_logs, write_serial,
                                                   runtime);
    if (result == GRIDPICO_OK) {
        runtime->cli.set_local_route = set_local_route;
        runtime->cli.read_local_route = read_local_route;
        if (runtime->routing_enabled) runtime->cli.read_local_routing = read_routing;
        runtime->serial = (local_binding){.generation = generation, .attached = true};
        gridpico_cli_process(&runtime->cli);
    }
    return result;
}

gridpico_result local_runtime_serial_detach(local_runtime *runtime, uint32_t generation)
{
    if (!runtime->serial.attached || generation != runtime->serial.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    runtime->serial.attached = false;
    runtime->serial_count = 0;
    return GRIDPICO_OK;
}

gridpico_result local_runtime_serial(local_runtime *runtime, uint32_t generation,
                                    const char *bytes, size_t size)
{
    if (size == 0 || size > LOCAL_SERIAL_CHUNK || generation == 0) {
        return GRIDPICO_INVALID;
    }
    if (!runtime->serial.attached || generation != runtime->serial.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    while (size != 0 && !runtime->serial.fault) {
        const size_t chunk = size < GRIDPICO_CLI_LINE_BYTES ? size : GRIDPICO_CLI_LINE_BYTES;
        gridpico_cli_receive(&runtime->cli, bytes, chunk);
        gridpico_cli_process(&runtime->cli);
        bytes += chunk;
        size -= chunk;
    }
    return runtime->serial.fault ? GRIDPICO_CAPACITY : GRIDPICO_OK;
}

static gridpico_result retire_faults(local_runtime *runtime)
{
    gridpico_result result = GRIDPICO_OK;
    for (size_t i = 0; i < runtime->profile->registry.inventory.port_count; ++i) {
        local_binding *port = &runtime->bindings[i];
        if (port->attached && port->fault) {
            const gridpico_connection connection = {runtime->profile->registry.inventory.ports[i].id, port->generation};
            result = first_error(result, local_runtime_detach(runtime, connection));
            stage_fault(runtime, connection);
        }
    }
    if (runtime->serial.fault && runtime->serial.attached) {
        local_runtime_serial_detach(runtime, runtime->serial.generation);
    }
    return result;
}

gridpico_result local_runtime_finish(local_runtime *runtime, gridpico_result result)
{
    result = first_error(result, retire_faults(runtime));
    if (!runtime->feedback_refresh) {
        return result;
    }
    runtime->feedback_refresh = false;
    local_binding *sink = binding(runtime, runtime->feedback_port);
    if (sink == NULL || !sink->attached || runtime->raw_deadline_ms != 0) {
        return result;
    }
    const size_t before = sink->queued;
    const size_t output_before = runtime->output_count;
    gridpico_result refreshed = GRIDPICO_UNAVAILABLE;
    const size_t index = port_index(runtime, runtime->feedback_port);
    const size_t limit = runtime->profile->ports[index].queue_limit;
    if (before + GRIDPICO_PATCH_LED_FRAMES <= limit) {
        runtime->refreshing = true;
        refreshed = gridpico_apc_patch_refresh(&runtime->apc);
        runtime->refreshing = false;
    }
    runtime->feedback_ready = refreshed == GRIDPICO_OK &&
                              sink->queued - before == GRIDPICO_PATCH_LED_FRAMES && !sink->fault;
    if (!runtime->feedback_ready) {
        runtime->output_count = output_before;
        sink->queued = before;
        sink->fault = true;
        refreshed = first_error(refreshed, GRIDPICO_UNAVAILABLE);
    }
    return first_error(first_error(result, refreshed), retire_faults(runtime));
}

gridpico_result local_runtime_expire(local_runtime *runtime)
{
    return runtime->raw_deadline_ms != 0 && runtime->now_ms >= runtime->raw_deadline_ms
        ? end_raw(runtime) : GRIDPICO_OK;
}

gridpico_result local_runtime_retire(local_runtime *runtime)
{
    gridpico_result result = GRIDPICO_OK;
    for (unsigned direction = GRIDPICO_PATCH_INPUT; direction <= GRIDPICO_PATCH_OUTPUT; ++direction) {
        for (size_t i = 0; i < runtime->profile->registry.inventory.port_count; ++i) {
            const gridpico_patch_port *port = &runtime->profile->registry.inventory.ports[i];
            const local_binding *state = &runtime->bindings[i];
            if (port->direction == direction && state->attached) {
                const gridpico_connection connection = {port->id, state->generation};
                result = first_error(result, local_runtime_detach(runtime, connection));
            }
        }
    }
    runtime->serial.attached = false;
    return result;
}
