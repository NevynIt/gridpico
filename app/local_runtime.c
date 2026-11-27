#include "app/local_runtime.h"
#include "core/processors/managed_note.h"
#include "frontends/apc/apc_protocol.h"

#include <string.h>

enum {
    APC_KEYBED_PORT = 1,
    APC_FEEDBACK_PORT = 3,
    WEB_MONITOR_PORT = 5,
    MIDI_LOG_PORT = 6,
    RECORDER_PORT = 12,
    APC_ACTIVITY_MS = 100,
    APC_PENDING_PHASE_MS = 500,
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
    const gridpico_patch_inventory *inventory = &runtime->graph.inventory;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (inventory->ports[i].id == id) {
            return i;
        }
    }
    return inventory->port_count;
}

static bool managed_port(const local_runtime *runtime, size_t index,
                         gridpico_patch_direction direction)
{
    return index < runtime->graph.inventory.port_count &&
        runtime->graph.inventory.ports[index].direction == direction &&
        runtime->graph.port_capabilities[index] == GRIDPICO_LOCAL_PORT_MANAGED;
}

static const local_profile_port *port_metadata(const local_runtime *runtime, size_t index)
{
    static const local_profile_port fm_port = {LOCAL_ROLE_FM, LOCAL_MIDI_QUEUE, true};
    return index < runtime->profile->registry.inventory.port_count
        ? &runtime->profile->ports[index] : &fm_port;
}

static local_binding *binding(local_runtime *runtime, gridpico_port_id id)
{
    const size_t index = port_index(runtime, id);
    return index < runtime->graph.inventory.port_count ? &runtime->bindings[index] : NULL;
}

static bool socket_pool_full(const local_runtime *runtime)
{
    size_t admitted = 0;
    for (size_t i = 0; i < runtime->profile->registry.inventory.port_count; ++i) {
        if (runtime->bindings[i].attached && !runtime->bindings[i].adapter_owned) ++admitted;
    }
    if (runtime->fm_runtime_epoch != 0) {
        for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
            if (runtime->fm_allocations[i].state != LOCAL_FM_FREE) ++admitted;
        }
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
                                  const uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES],
                                  gridpico_revision forwarding_revision, bool panic)
{
    const size_t index = port_index(runtime, to.endpoint);
    if (index == runtime->graph.inventory.port_count) {
        return GRIDPICO_UNAVAILABLE;
    }
    local_binding *sink = &runtime->bindings[index];
    if (!sink->attached || sink->generation != to.generation || sink->fault ||
        (panic && sink->adapter_owned)) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (sink->queued >= port_metadata(runtime, index)->queue_limit ||
        (sink->adapter.send == NULL &&
         runtime->output_count - runtime->fault_count >= LOCAL_MIDI_STAGE_FRAMES)) {
        if (!panic) sink->fault = true;
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
    *row = (local_output){.destination = to, .forwarding_revision = forwarding_revision,
                          .panic = panic};
    memcpy(row->bytes, bytes, sizeof(row->bytes));
    ++sink->queued;
    return GRIDPICO_OK;
}

static gridpico_result deliver_note(void *context, const gridpico_note_delivery *delivery)
{
    local_runtime *runtime = context;
    const gridpico_note note = delivery->note;
    const uint8_t bytes[] = {
        (uint8_t)((note.kind == GRIDPICO_NOTE_ON ? 0x90 : 0x80) | note.channel_index),
        note.key, note.velocity,
    };
    return stage_midi(runtime, delivery->destination, bytes,
                      runtime->forwarding_enabled ? runtime->forwarding_revision : 0, false);
}

static gridpico_result deliver_raw(void *context, const gridpico_local_raw_delivery *delivery)
{
    return stage_midi(context, delivery->destination, delivery->bytes, 0, false);
}

static void deliver_feedback(void *context, const gridpico_apc_surface_message *message)
{
    local_runtime *runtime = context;
    const local_binding *sink = binding(runtime, runtime->feedback_port);
    if (sink != NULL && sink->attached && runtime->raw_deadline_ms == 0 &&
        (runtime->feedback_ready || runtime->refreshing)) {
        const gridpico_connection destination = {runtime->feedback_port, sink->generation};
        stage_midi(runtime, destination, message->bytes, 0, false);
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

static bool valid_triplet(const uint8_t *bytes)
{
    const unsigned status = bytes[0] >> 4;
    return (status == 8 || status == 9 || status == 10 || status == 11 || status == 14) &&
           bytes[1] <= 127 && bytes[2] <= 127;
}

static bool same_connection(gridpico_connection left, gridpico_connection right)
{
    return left.endpoint == right.endpoint && left.generation == right.generation;
}

static bool current_apc_pair(local_runtime *runtime, gridpico_connection *panel,
                             gridpico_connection *led)
{
    const local_binding *input = binding(runtime, runtime->editable_apc.panel_port);
    const local_binding *output = binding(runtime, runtime->feedback_port);
    if (input == NULL || !input->attached || input->adapter_owned || input->fault ||
        output == NULL || !output->attached || output->adapter_owned || output->fault) return false;
    *panel = (gridpico_connection){runtime->editable_apc.panel_port, input->generation};
    *led = (gridpico_connection){runtime->feedback_port, output->generation};
    return true;
}

static bool current_apc_version(const local_runtime *runtime,
                                const gridpico_apc_physical_page_version *version)
{
    return version->route_page == !runtime->editable_apc.frontend.arranging &&
           version->layout_revision == runtime->session.apc_layout_revision &&
           version->graph_revision == runtime->graph.revision;
}

static void advance_apc_epoch(local_runtime *runtime)
{
    local_apc_surface *apc = &runtime->editable_apc;
    if (apc->epoch == UINT64_MAX) apc->flight.epoch = 0;
    else ++apc->epoch;
    runtime->feedback_ready = false;
}

static void fence_apc_visibility(local_runtime *runtime)
{
    local_apc_surface *apc = &runtime->editable_apc;
    advance_apc_epoch(runtime);
    apc->frontend.route_page_ready = false;
    apc->inhibited = true;
    apc->latest_valid = false;
    apc->refresh_deadline_ms = 0;
    runtime->feedback_refresh = true;
}

static uint64_t earlier_deadline(uint64_t current, uint64_t candidate)
{
    return current == 0 || candidate < current ? candidate : current;
}

static uint64_t apc_refresh_deadline(const local_runtime *runtime, bool route_page)
{
    const local_apc_surface *apc = &runtime->editable_apc;
    const uint64_t now = runtime->now_ms;
    uint64_t deadline = 0;
    for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
        const gridpico_apc_row_activity *activity = &apc->frontend.activity[row];
        if (activity->has_flashed && now >= activity->started_ms &&
            now - activity->started_ms < APC_ACTIVITY_MS) {
            deadline = earlier_deadline(deadline, activity->started_ms + APC_ACTIVITY_MS);
        }
        for (size_t col = 0; route_page && col < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++col) {
            if (apc->snapshot.cells[row][col].state == GRIDPICO_PATCH_ROUTE_PENDING) {
                const uint64_t phase_end = now + APC_PENDING_PHASE_MS - now % APC_PENDING_PHASE_MS;
                deadline = earlier_deadline(deadline, phase_end);
            }
        }
    }
    return deadline;
}

static gridpico_result capture_apc_page(void *context, const gridpico_apc_surface_message *messages,
                                       size_t count, const gridpico_apc_physical_page_version *version)
{
    local_runtime *runtime = context;
    local_apc_surface *apc = &runtime->editable_apc;
    if (count != GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES || !current_apc_version(runtime, version)) {
        return GRIDPICO_INVALID;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!valid_triplet(messages[i].bytes)) return GRIDPICO_INVALID;
    }
    memcpy(apc->latest, messages, sizeof(apc->latest));
    apc->latest_version = *version;
    apc->latest_valid = true;
    apc->refresh_deadline_ms = apc_refresh_deadline(runtime, version->route_page);
    runtime->feedback_refresh = false;
    return GRIDPICO_OK;
}

static gridpico_result read_editable_apc(void *context, gridpico_apc_patch_snapshot *out)
{
    local_runtime *runtime = context;
    return gridpico_patch_session_read_apc_patch(&runtime->session, out);
}

static gridpico_result set_editable_apc_cell(void *context, gridpico_revision layout,
                                            gridpico_revision graph, uint8_t row, uint8_t column,
                                            bool enabled)
{
    local_runtime *runtime = context;
    const gridpico_result result = gridpico_patch_session_set_apc_patch_cell(
        &runtime->session, layout, graph, row, column, enabled);
    runtime->editable_apc.edit_accepted = result == GRIDPICO_OK;
    return result;
}

static gridpico_result assign_editable_apc(void *context, gridpico_revision layout,
                                          gridpico_patch_direction axis, uint8_t slot,
                                          gridpico_port_id port)
{
    local_runtime *runtime = context;
    const gridpico_result result =
        gridpico_patch_session_assign_apc_patch(&runtime->session, layout, axis, slot, port);
    runtime->editable_apc.edit_accepted = result == GRIDPICO_OK;
    return result;
}

static gridpico_result move_editable_apc(void *context, gridpico_revision layout,
                                        gridpico_patch_direction axis, uint8_t from, uint8_t to)
{
    local_runtime *runtime = context;
    const gridpico_result result =
        gridpico_patch_session_move_apc_patch(&runtime->session, layout, axis, from, to);
    runtime->editable_apc.edit_accepted = result == GRIDPICO_OK;
    return result;
}

static gridpico_result init_editable_apc(local_runtime *runtime)
{
    const gridpico_apc_patch_layout empty = {0};
    const gridpico_result result = gridpico_patch_session_bind_apc_patch(&runtime->session, &empty, 0);
    if (result != GRIDPICO_OK) return result;
    gridpico_patch_session_enable_flat_managed_routes(&runtime->session);
    local_apc_surface *apc = &runtime->editable_apc;
    apc->panel_port = local_profile_role(runtime->profile, LOCAL_ROLE_PANEL);
    apc->epoch = 1;
    apc->inhibited = true;
    gridpico_apc_physical_init(&apc->frontend, runtime, read_editable_apc, set_editable_apc_cell,
                              assign_editable_apc, move_editable_apc, runtime, NULL, capture_apc_page,
                              &apc->snapshot);
    return GRIDPICO_OK;
}

static void refresh_editable_apc(local_runtime *runtime)
{
    gridpico_connection panel, led;
    const local_apc_surface *apc = &runtime->editable_apc;
    if (runtime->raw_deadline_ms != 0 || !current_apc_pair(runtime, &panel, &led)) return;
    if (runtime->feedback_refresh || !current_apc_version(runtime, &apc->latest_version) ||
        (apc->refresh_deadline_ms != 0 && runtime->now_ms >= apc->refresh_deadline_ms)) {
        if (gridpico_apc_physical_refresh(&runtime->editable_apc.frontend, runtime->now_ms) != GRIDPICO_OK)
            fence_apc_visibility(runtime);
    }
}

const local_apc_flight *local_runtime_take_apc_page(
    local_runtime *runtime, const gridpico_apc_surface_message **messages)
{
    local_apc_surface *apc = &runtime->editable_apc;
    gridpico_connection panel, led;
    if (!apc->enabled || !apc->latest_valid || apc->flight.token != 0 ||
        runtime->raw_deadline_ms != 0 || !current_apc_pair(runtime, &panel, &led) ||
        !current_apc_version(runtime, &apc->latest_version)) return NULL;
    if (apc->last_token == UINT64_MAX) {
        fence_apc_visibility(runtime);
        return NULL;
    }
    apc->flight = (local_apc_flight){
        .panel = panel, .led = led, .token = ++apc->last_token, .epoch = apc->epoch,
        .version = apc->latest_version,
    };
    apc->latest_valid = false;
    *messages = apc->latest;
    return &apc->flight;
}

gridpico_result local_runtime_complete_apc_page(local_runtime *runtime, gridpico_connection panel,
                                               gridpico_connection led, uint64_t token, bool written)
{
    local_apc_surface *apc = &runtime->editable_apc;
    if (!apc->enabled || token == 0 || token > apc->last_token ||
        panel.endpoint != apc->panel_port || led.endpoint != runtime->feedback_port ||
        panel.generation == 0 || led.generation == 0) return GRIDPICO_INVALID;
    const local_apc_flight flight = apc->flight;
    if (token != flight.token) return GRIDPICO_OK;
    if (!same_connection(panel, flight.panel) || !same_connection(led, flight.led)) return GRIDPICO_INVALID;
    apc->flight = (local_apc_flight){0};
    gridpico_connection current_panel, current_led;
    if (written && flight.epoch == apc->epoch && runtime->raw_deadline_ms == 0 &&
        current_apc_pair(runtime, &current_panel, &current_led) &&
        same_connection(panel, current_panel) && same_connection(led, current_led) &&
        current_apc_version(runtime, &flight.version)) {
        gridpico_apc_physical_feedback_complete(&apc->frontend, &flight.version);
        apc->inhibited = false;
        runtime->feedback_ready = true;
    } else {
        runtime->feedback_ready = false;
    }
    return GRIDPICO_OK;
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
    if (!port->adapter_owned && !port->fault_staged &&
        runtime->fault_count < GRIDPICO_PATCH_MAX_PORTS) {
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
    const size_t destination = port_index(runtime, route.destination);
    if (request->enabled && destination < runtime->graph.inventory.port_count &&
        runtime->graph.ports[destination].ephemeral_managed_output &&
        !runtime->bindings[destination].attached) return GRIDPICO_UNAVAILABLE;
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
             !runtime->feedback_ready ||
             (runtime->editable_apc.enabled && (runtime->feedback_refresh ||
              runtime->editable_apc.latest_valid || runtime->editable_apc.flight.token != 0 ||
              !current_apc_version(runtime, &runtime->editable_apc.latest_version))))) {
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
            if (runtime->editable_apc.enabled) fence_apc_visibility(runtime);
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

static gridpico_result read_forwarding(void *context, gridpico_forwarding_status *out)
{
    const local_runtime *runtime = context;
    if (!runtime->forwarding_enabled || !runtime->serial.attached) return GRIDPICO_UNAVAILABLE;
    *out = (gridpico_forwarding_status){.revision = runtime->forwarding_revision,
                                       .running = runtime->forwarding_running};
    return GRIDPICO_OK;
}

static void purge_ordinary_output(local_runtime *runtime)
{
    size_t kept = 0;
    for (size_t i = 0; i < runtime->output_count; ++i) {
        const local_output *row = &runtime->output[i];
        if (row->forwarding_revision == 0) {
            runtime->output[kept++] = *row;
        } else {
            local_binding *sink = binding(runtime, row->destination.endpoint);
            if (sink != NULL && sink->generation == row->destination.generation && sink->queued != 0) {
                --sink->queued;
            }
        }
    }
    runtime->output_count = kept;
}

static gridpico_result apply_forwarding(void *context, bool running, gridpico_revision expected_revision,
                                        gridpico_forwarding_status *out)
{
    local_runtime *runtime = context;
    if (expected_revision == 0) return GRIDPICO_INVALID;
    if (!runtime->forwarding_enabled || !runtime->serial.attached || runtime->serial.fault ||
        expected_revision != runtime->forwarding_revision) return GRIDPICO_UNAVAILABLE;
    if (running == runtime->forwarding_running) return read_forwarding(context, out);

    const size_t reply_size = sizeof("FORWARDING RUNNING 18446744073709551615\r\nOK\r\n> ") - 1u;
    if (runtime->forwarding_revision == UINT64_MAX ||
        runtime->forwarding_barrier_count == LOCAL_FORWARDING_BARRIERS ||
        sizeof(runtime->serial_bytes) - runtime->serial_count < reply_size) return GRIDPICO_CAPACITY;

    ++runtime->forwarding_revision;
    runtime->forwarding_running = running;
    purge_ordinary_output(runtime);
    runtime->forwarding_barriers[runtime->forwarding_barrier_count++] = (local_forwarding_barrier){
        .revision = runtime->forwarding_revision,
        .serial_offset = runtime->serial_count,
        .running = running,
    };
    return read_forwarding(context, out);
}

enum { PANIC_CHANNELS = 16, PANIC_PASS_TRIPLETS = 8 };

static bool panic_serial_current(const local_runtime *runtime)
{
    return runtime->forwarding_enabled && runtime->serial.attached && !runtime->serial.fault;
}

static bool panic_output_eligible(const local_runtime *runtime, size_t slot)
{
    if (!managed_port(runtime, slot, GRIDPICO_PATCH_OUTPUT)) return false;
    const local_profile_port *port = port_metadata(runtime, slot);
    const local_binding *sink = &runtime->bindings[slot];
    return port->socket && port->role != LOCAL_ROLE_LOG && !sink->adapter_owned &&
           sink->attached && !sink->fault;
}

static gridpico_result read_panic(void *context, gridpico_panic_stage_report *out)
{
    const local_runtime *runtime = context;
    if (!panic_serial_current(runtime)) return GRIDPICO_UNAVAILABLE;
    *out = runtime->panic_report;
    return GRIDPICO_OK;
}

static gridpico_result start_panic(void *context, gridpico_panic_stage_report *out)
{
    local_runtime *runtime = context;
    if (!panic_serial_current(runtime) || runtime->panic_report.state == GRIDPICO_PANIC_STAGE_RUNNING) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_panic_stage_report report = {.state = GRIDPICO_PANIC_STAGE_COMPLETE};
    const gridpico_patch_inventory *inventory = &runtime->graph.inventory;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (!panic_output_eligible(runtime, i)) continue;
        if (report.count == GRIDPICO_PATCH_MAX_PORTS) return GRIDPICO_CAPACITY;
        const gridpico_port_id id = inventory->ports[i].id;
        size_t slot = report.count++;
        while (slot != 0 && report.outputs[slot - 1].output.endpoint > id) {
            report.outputs[slot] = report.outputs[slot - 1];
            --slot;
        }
        report.outputs[slot] = (gridpico_panic_output){
            .output = {id, runtime->bindings[i].generation},
            .requested_count = PANIC_CHANNELS,
            .state = GRIDPICO_PANIC_OUTPUT_PENDING,
        };
    }

    /* The activation reply must fit before the job starts accepting MIDI. */
    const size_t reply_bound = sizeof("PANIC_STAGE CC123 COMPLETE 65535\r\nOK\r\n") - 1 +
        report.count * (sizeof("PANIC_OUT 65535 4294967295 16 16 PENDING\r\n") - 1);
    if (reply_bound > sizeof(runtime->serial_bytes) - runtime->serial_count) return GRIDPICO_CAPACITY;

    if (report.count != 0) report.state = GRIDPICO_PANIC_STAGE_RUNNING;
    runtime->panic_report = report;
    runtime->panic_owner = runtime->serial.generation;
    runtime->panic_cursor = 0;
    runtime->panic_pending = report.count;
    *out = report;
    return GRIDPICO_OK;
}

static void finish_panic_output(local_runtime *runtime, gridpico_panic_output *row,
                                gridpico_panic_output_state state)
{
    row->state = state;
    if (--runtime->panic_pending == 0) runtime->panic_report.state = GRIDPICO_PANIC_STAGE_COMPLETE;
}

static void cancel_panic_output(local_runtime *runtime, gridpico_connection connection)
{
    if (runtime->panic_report.state != GRIDPICO_PANIC_STAGE_RUNNING) return;
    for (size_t i = 0; i < runtime->panic_report.count; ++i) {
        gridpico_panic_output *row = &runtime->panic_report.outputs[i];
        if (row->state == GRIDPICO_PANIC_OUTPUT_PENDING &&
            row->output.endpoint == connection.endpoint &&
            row->output.generation == connection.generation) {
            finish_panic_output(runtime, row, GRIDPICO_PANIC_OUTPUT_CANCELLED);
            return;
        }
    }
}

static void cancel_panic(local_runtime *runtime)
{
    gridpico_panic_stage_report *report = &runtime->panic_report;
    if (report->state != GRIDPICO_PANIC_STAGE_RUNNING) return;
    for (size_t i = 0; i < report->count; ++i) {
        if (report->outputs[i].state == GRIDPICO_PANIC_OUTPUT_PENDING) {
            report->outputs[i].state = GRIDPICO_PANIC_OUTPUT_CANCELLED;
        }
    }
    report->state = GRIDPICO_PANIC_STAGE_CANCELLED;
    runtime->panic_pending = 0;
}

void local_runtime_service_panic(local_runtime *runtime)
{
    gridpico_panic_stage_report *report = &runtime->panic_report;
    if (report->state != GRIDPICO_PANIC_STAGE_RUNNING) return;
    if (!panic_serial_current(runtime) || runtime->serial.generation != runtime->panic_owner) {
        cancel_panic(runtime);
        return;
    }
    for (unsigned attempts = 0; runtime->panic_pending != 0 && attempts < PANIC_PASS_TRIPLETS;) {
        gridpico_panic_output *row = &report->outputs[runtime->panic_cursor];
        if (++runtime->panic_cursor == report->count) runtime->panic_cursor = 0;
        if (row->state != GRIDPICO_PANIC_OUTPUT_PENDING) continue;
        const size_t slot = port_index(runtime, row->output.endpoint);
        if (!panic_output_eligible(runtime, slot) ||
            runtime->bindings[slot].generation != row->output.generation) {
            finish_panic_output(runtime, row, GRIDPICO_PANIC_OUTPUT_CANCELLED);
            continue;
        }
        const uint8_t bytes[] = {(uint8_t)(0xb0u + row->accepted_count), 123, 0};
        ++attempts;
        if (stage_midi(runtime, row->output, bytes, 0, true) != GRIDPICO_OK) {
            finish_panic_output(runtime, row, GRIDPICO_PANIC_OUTPUT_REFUSED);
        } else if (++row->accepted_count == row->requested_count) {
            finish_panic_output(runtime, row, GRIDPICO_PANIC_OUTPUT_COMPLETE);
        }
    }
}

static gridpico_result read_availability(
    void *context, const gridpico_local_registry *registry,
    gridpico_local_port_availability out[GRIDPICO_PATCH_MAX_PORTS])
{
    const local_runtime *runtime = context;
    const gridpico_patch_inventory *inventory = &runtime->graph.inventory;
    if (registry->inventory.port_count != inventory->port_count) return GRIDPICO_INVALID;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (registry->inventory.ports[i].id != inventory->ports[i].id) return GRIDPICO_INVALID;
    }
    const bool full = socket_pool_full(runtime);
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const local_binding *port = &runtime->bindings[i];
        out[i] = port->attached ? GRIDPICO_LOCAL_IN_USE :
            !runtime->graph.ports[i].ephemeral_managed_output &&
            port_metadata(runtime, i)->socket && !port->adapter_owned &&
            port->generation != UINT32_MAX && !full ? GRIDPICO_LOCAL_FREE : GRIDPICO_LOCAL_UNAVAILABLE;
    }
    return GRIDPICO_OK;
}

static gridpico_result read_routing(void *context, gridpico_local_routing_snapshot *out)
{
    local_runtime *runtime = context;
    return gridpico_patch_session_read_local_routing(&runtime->session, out);
}

static bool transformable_port(const local_runtime *runtime, gridpico_port_id id,
                               gridpico_patch_direction direction)
{
    const size_t index = port_index(runtime, id);
    return managed_port(runtime, index, direction) && port_metadata(runtime, index)->socket &&
        (!runtime->graph.ports[index].ephemeral_managed_output || runtime->bindings[index].attached) &&
        !runtime->bindings[index].adapter_owned && (!runtime->recording_enabled || id != RECORDER_PORT);
}

static gridpico_result apply_note_transform(
    void *context, const gridpico_managed_note_transform_request *request,
    gridpico_managed_note_transform_status *out)
{
    local_runtime *runtime = context;
    if (request == NULL || out == NULL || request->source == 0 ||
        request->destination == 0 || request->expected_revision == 0 ||
        request->transform.transpose_semitones < -127 ||
        request->transform.output_channel > 16) {
        return GRIDPICO_INVALID;
    }
    if (!transformable_port(runtime, request->source, GRIDPICO_PATCH_INPUT) ||
        !transformable_port(runtime, request->destination, GRIDPICO_PATCH_OUTPUT)) {
        return GRIDPICO_UNAVAILABLE;
    }
    const gridpico_local_route route = {request->source, request->destination, GRIDPICO_LOCAL_MANAGED};

    gridpico_local_routing_snapshot snapshot;
    const gridpico_result read = read_routing(runtime, &snapshot);
    if (read != GRIDPICO_OK) return read;
    if (snapshot.revision != request->expected_revision) return GRIDPICO_UNAVAILABLE;

    gridpico_local_route_status branch;
    const gridpico_result lookup = gridpico_patch_graph_read_local_route(&runtime->graph,
                                                                        &route, &branch);
    if (lookup != GRIDPICO_OK) return lookup;
    if (branch.state != GRIDPICO_PATCH_ROUTE_ACTIVE) return GRIDPICO_UNAVAILABLE;

    gridpico_managed_note_transform current;
    const gridpico_result settings = gridpico_patch_graph_read_managed_note_transform(
        &runtime->graph, &route, &current);
    if (settings != GRIDPICO_OK) return settings;

    gridpico_patch_session *session = &runtime->session;
    if (current.transpose_semitones != request->transform.transpose_semitones ||
        current.output_channel != request->transform.output_channel) {
        if (snapshot.revision == UINT64_MAX) return GRIDPICO_CAPACITY;
        const gridpico_result result = gridpico_patch_graph_set_managed_note_transform(
            &runtime->graph, &route, &request->transform);
        if (result != GRIDPICO_OK) return result;

        /* This serialized edit advances the Graph revision without changing the
         * availability, descriptors or colors in the guarded snapshot. */
        session->routing_graph_revision = runtime->graph.revision;
        session->routing_revision = snapshot.revision + 1;
        current = request->transform;
    }
    *out = (gridpico_managed_note_transform_status){current, session->routing_revision};
    return GRIDPICO_OK;
}

static void enable_routing(local_runtime *runtime)
{
    gridpico_patch_session_bind_local_routing(&runtime->session, gridpico_patch_graph_read_local_state,
                                             read_availability, runtime);
    runtime->routing_enabled = true;
}

static gridpico_result init_profile(local_runtime *runtime, const local_router_profile *profile,
                                   bool prepare_fm)
{
    *runtime = (local_runtime){.forwarding_running = true, .forwarding_revision = 1};
    runtime->configured = profile != NULL;
    runtime->profile = runtime->configured ? profile : &default_profile;
    if (runtime->profile->editable_surface && !local_profile_supports_editable_apc(runtime->profile))
        return GRIDPICO_INVALID;
    const gridpico_local_registry *registry = &runtime->profile->registry;
    gridpico_local_profile limits = runtime->profile->limits;
    if (prepare_fm) {
        limits.dynamic_device_limit = GRIDPICO_PATCH_MAX_DEVICES;
        limits.dynamic_port_limit = GRIDPICO_PATCH_MAX_PORTS;
    }
    const gridpico_result result = runtime->configured
        ? gridpico_patch_graph_init_local_profile(&runtime->graph, registry, &limits,
                                                  deliver_note, deliver_raw, runtime)
        : gridpico_patch_graph_init_local(&runtime->graph, registry, deliver_note, deliver_raw, runtime);
    if (result != GRIDPICO_OK) {
        return result;
    }
    runtime->next_fm_device = 1;
    runtime->next_fm_port = 1;
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
    if (runtime->profile->editable_surface) return init_editable_apc(runtime);
    reset_panel(runtime);
    return GRIDPICO_OK;
}

gridpico_result local_runtime_init_profile(local_runtime *runtime, const local_router_profile *profile)
{
    return init_profile(runtime, profile, false);
}

gridpico_result local_runtime_prepare_fm_profile(local_runtime *runtime, const local_router_profile *profile)
{
    if (runtime == NULL || profile == NULL || !profile->passive_surface) return GRIDPICO_INVALID;
    return init_profile(runtime, profile, true);
}

gridpico_result local_runtime_init_fm_profile(local_runtime *runtime, const local_router_profile *profile,
                                             uint64_t runtime_epoch)
{
    if (runtime_epoch == 0) return GRIDPICO_INVALID;
    const gridpico_result result = local_runtime_prepare_fm_profile(runtime, profile);
    if (result == GRIDPICO_OK) runtime->fm_runtime_epoch = runtime_epoch;
    return result;
}

gridpico_result local_runtime_init(local_runtime *runtime)
{
    return local_runtime_init_profile(runtime, NULL);
}

static bool valid_fm_name(const char *name, size_t *length)
{
    size_t n = 0;
    while (n < GRIDPICO_PATCH_NAME_BYTES && name[n] != '\0') ++n;
    if (n == 0 || n == GRIDPICO_PATCH_NAME_BYTES) return false;
    for (size_t i = 0; i < n;) {
        const unsigned char first = (unsigned char)name[i++];
        if (first < 0x20 || first == 0x7f) return false;
        if (first < 0x80) continue;
        if (first < 0xc2 || first > 0xf4) return false;
        const size_t rest = first < 0xe0 ? 1 : first < 0xf0 ? 2 : 3;
        if (rest > n - i) return false;
        const unsigned char second = (unsigned char)name[i];
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) return false;
        for (size_t j = 0; j < rest; ++j) {
            if (((unsigned char)name[i++] & 0xc0u) != 0x80u) return false;
        }
    }
    *length = n;
    return true;
}

static uint32_t fresh_fm_id(const local_runtime *runtime, uint32_t id, bool device)
{
    const gridpico_patch_inventory *prefix = &runtime->profile->registry.inventory;
    const size_t count = device ? prefix->device_count : prefix->port_count;
    for (; id <= UINT16_MAX; ++id) {
        size_t slot = 0;
        for (; slot < count; ++slot) {
            if (id == (device ? prefix->devices[slot].id : prefix->ports[slot].id)) break;
        }
        if (slot == count) break;
    }
    return id;
}

static local_fm_allocation *find_fm(local_runtime *runtime, gridpico_port_id port)
{
    for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
        local_fm_allocation *allocation = &runtime->fm_allocations[i];
        if (allocation->state != LOCAL_FM_FREE && allocation->lease.output.endpoint == port) {
            return allocation;
        }
    }
    return NULL;
}

static local_fm_allocation *claim_fm(local_runtime *runtime, local_fm_lease lease,
                                    const void *reservation_owner)
{
    if (lease.runtime_epoch != runtime->fm_runtime_epoch || reservation_owner == NULL) return NULL;
    local_fm_allocation *allocation = find_fm(runtime, lease.output.endpoint);
    return allocation != NULL && allocation->reservation_owner == reservation_owner &&
        allocation->lease.device == lease.device &&
        same_connection(allocation->lease.output, lease.output) ? allocation : NULL;
}

local_fm_state local_runtime_fm_lease_state(local_runtime *runtime, local_fm_lease lease,
                                          const void *reservation_owner)
{
    const local_fm_allocation *allocation = claim_fm(runtime, lease, reservation_owner);
    return allocation == NULL ? LOCAL_FM_FREE : allocation->state;
}

uint64_t local_runtime_fm_deadline(const local_runtime *runtime)
{
    uint64_t deadline = 0;
    if (runtime->fm_runtime_epoch == 0) return 0;
    for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
        const local_fm_allocation *allocation = &runtime->fm_allocations[i];
        if (allocation->state == LOCAL_FM_PENDING &&
            (deadline == 0 || allocation->deadline_ms < deadline)) {
            deadline = allocation->deadline_ms;
        }
    }
    return deadline;
}

gridpico_result local_runtime_reserve_fm(local_runtime *runtime, const char *name, uint64_t deadline_ms,
                                       const void *reservation_owner, local_fm_lease *out)
{
    size_t length;
    if (runtime == NULL || name == NULL || out == NULL || reservation_owner == NULL ||
        !valid_fm_name(name, &length)) return GRIDPICO_INVALID;
    if (runtime->fm_runtime_epoch == 0 || runtime->recording_enabled ||
        deadline_ms <= runtime->now_ms) return GRIDPICO_UNAVAILABLE;

    local_fm_allocation *free_slot = NULL;
    size_t pending = 0;
    for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
        local_fm_allocation *allocation = &runtime->fm_allocations[i];
        if (allocation->state == LOCAL_FM_FREE) {
            if (free_slot == NULL) free_slot = allocation;
        } else {
            if (allocation->reservation_owner == reservation_owner) return GRIDPICO_UNAVAILABLE;
            if (allocation->state == LOCAL_FM_PENDING) ++pending;
        }
    }
    if (free_slot == NULL || socket_pool_full(runtime) ||
        runtime->graph.inventory.device_count + pending >= runtime->graph.local_profile.dynamic_device_limit ||
        runtime->graph.inventory.port_count + pending >= runtime->graph.local_profile.dynamic_port_limit ||
        runtime->graph.revision == UINT64_MAX || runtime->session.routing_revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }

    const uint32_t device = fresh_fm_id(runtime, runtime->next_fm_device, true);
    const uint32_t port = fresh_fm_id(runtime, runtime->next_fm_port, false);
    if (device > UINT16_MAX || port > UINT16_MAX) return GRIDPICO_CAPACITY;
    const local_fm_lease lease = {
        .runtime_epoch = runtime->fm_runtime_epoch,
        .device = (gridpico_device_id)device,
        .output = {(gridpico_port_id)port, 2},
    };
    *free_slot = (local_fm_allocation){
        .lease = lease, .reservation_owner = reservation_owner,
        .deadline_ms = deadline_ms, .state = LOCAL_FM_PENDING,
    };
    memcpy(free_slot->name, name, length);
    runtime->next_fm_device = device + 1;
    runtime->next_fm_port = port + 1;
    *out = lease;
    return GRIDPICO_OK;
}

gridpico_result local_runtime_publish_fm(local_runtime *runtime, local_fm_lease lease,
                                       const void *reservation_owner, const void *socket_owner)
{
    if (runtime == NULL || socket_owner == NULL) return GRIDPICO_INVALID;
    local_fm_allocation *allocation = claim_fm(runtime, lease, reservation_owner);
    if (allocation == NULL || allocation->state != LOCAL_FM_PENDING) return GRIDPICO_UNAVAILABLE;
    if (runtime->recording_enabled || runtime->now_ms >= allocation->deadline_ms) {
        *allocation = (local_fm_allocation){0};
        return GRIDPICO_UNAVAILABLE;
    }
    for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
        if (runtime->fm_allocations[i].socket_owner == socket_owner) return GRIDPICO_UNAVAILABLE;
    }
    if (runtime->session.routing_revision == UINT64_MAX) {
        *allocation = (local_fm_allocation){0};
        return GRIDPICO_CAPACITY;
    }

    gridpico_patch_device device = {.id = lease.device, .state = GRIDPICO_PATCH_DISCONNECTED};
    gridpico_patch_port port = {
        .id = lease.output.endpoint, .device = lease.device,
        .direction = GRIDPICO_PATCH_OUTPUT, .state = GRIDPICO_PATCH_DISCONNECTED, .patch_eligible = true,
    };
    memcpy(device.name, allocation->name, sizeof device.name);
    memcpy(port.name, allocation->name, sizeof port.name);
    const size_t device_slot = runtime->graph.inventory.device_count;
    const size_t port_slot = runtime->graph.inventory.port_count;
    const gridpico_result result = gridpico_patch_graph_publish_ephemeral_managed_output(
        &runtime->graph, &device, &port, lease.output);
    if (result != GRIDPICO_OK) {
        *allocation = (local_fm_allocation){0};
        return result;
    }
    runtime->bindings[port_slot] = (local_binding){.generation = lease.output.generation, .attached = true};
    runtime->session.colors[device_slot] = (gridpico_patch_color_preference){0};
    allocation->socket_owner = socket_owner;
    allocation->state = LOCAL_FM_ATTACHED;
    runtime->feedback_refresh = true;
    return GRIDPICO_OK;
}

static gridpico_result retire_fm_allocation(local_runtime *runtime, local_fm_allocation *allocation)
{
    if (allocation->state == LOCAL_FM_PENDING) {
        *allocation = (local_fm_allocation){0};
        return GRIDPICO_OK;
    }
    const local_fm_lease lease = allocation->lease;
    const size_t port_slot = port_index(runtime, lease.output.endpoint);
    const gridpico_patch_inventory *inventory = &runtime->graph.inventory;
    const gridpico_patch_inventory *prefix = &runtime->profile->registry.inventory;
    if (port_slot < prefix->port_count || port_slot >= inventory->port_count ||
        !runtime->graph.ports[port_slot].ephemeral_managed_output ||
        runtime->graph.ports[port_slot].generation != lease.output.generation ||
        runtime->bindings[port_slot].generation != lease.output.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    size_t device_slot = 0;
    while (device_slot < inventory->device_count && inventory->devices[device_slot].id != lease.device) {
        ++device_slot;
    }
    if (device_slot < prefix->device_count || device_slot >= inventory->device_count ||
        inventory->ports[port_slot].device != lease.device) return GRIDPICO_UNAVAILABLE;

    /* The old lease becomes undeliverable even if Graph retirement refuses. */
    allocation->state = LOCAL_FM_FENCED;
    runtime->bindings[port_slot].attached = false;
    runtime->bindings[port_slot].fault = false;
    runtime->bindings[port_slot].queued = 0;
    size_t kept = 0;
    for (size_t i = 0; i < runtime->output_count; ++i) {
        const local_output *row = &runtime->output[i];
        if (row->fault || !same_connection(row->destination, lease.output)) {
            runtime->output[kept++] = *row;
        }
    }
    runtime->output_count = kept;
    cancel_panic_output(runtime, lease.output);
    if (runtime->session.routing_revision == UINT64_MAX) return GRIDPICO_CAPACITY;
    const gridpico_result result =
        gridpico_patch_graph_retire_ephemeral_managed_output(&runtime->graph, lease.output);
    if (result != GRIDPICO_OK) return result;

    const size_t port_tail = inventory->port_count;
    const size_t device_tail = inventory->device_count;
    memmove(&runtime->bindings[port_slot], &runtime->bindings[port_slot + 1],
            (port_tail - port_slot) * sizeof runtime->bindings[0]);
    runtime->bindings[port_tail] = (local_binding){0};
    memmove(&runtime->session.colors[device_slot], &runtime->session.colors[device_slot + 1],
            (device_tail - device_slot) * sizeof runtime->session.colors[0]);
    runtime->session.colors[device_tail] = (gridpico_patch_color_preference){0};
    *allocation = (local_fm_allocation){0};
    runtime->feedback_refresh = true;
    return GRIDPICO_OK;
}

gridpico_result local_runtime_retire_fm(local_runtime *runtime, local_fm_lease lease,
                                      const void *reservation_owner, const void *socket_owner)
{
    if (runtime == NULL) return GRIDPICO_INVALID;
    local_fm_allocation *allocation = claim_fm(runtime, lease, reservation_owner);
    if (allocation == NULL || allocation->socket_owner != socket_owner) return GRIDPICO_UNAVAILABLE;
    return retire_fm_allocation(runtime, allocation);
}

void local_runtime_enable_desktop_note_transform(local_runtime *runtime)
{
    if (runtime->routing_enabled && runtime->profile->note_transform) {
        gridpico_patch_graph_bind_note_transform(&runtime->graph, gridpico_transform_managed_note);
        runtime->note_transform_enabled = true;
    }
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

gridpico_result local_runtime_enable_recorder_route(local_runtime *runtime)
{
    if (runtime->fm_runtime_epoch != 0) return GRIDPICO_UNAVAILABLE;
    const size_t source = port_index(runtime, APC_KEYBED_PORT);
    const size_t destination = port_index(runtime, RECORDER_PORT);
    if (!managed_port(runtime, source, GRIDPICO_PATCH_INPUT) ||
        !managed_port(runtime, destination, GRIDPICO_PATCH_OUTPUT) ||
        !runtime->profile->ports[source].socket || !runtime->profile->ports[destination].socket ||
        runtime->profile->ports[destination].role == LOCAL_ROLE_FM ||
        runtime->profile->limits.midi_peer_limit < 2) {
        return GRIDPICO_INVALID;
    }
    const gridpico_local_route_request request = {
        .route = {APC_KEYBED_PORT, RECORDER_PORT, GRIDPICO_LOCAL_MANAGED},
        .enabled = true,
    };
    const gridpico_result result = set_local_route(runtime, &request);
    if (result == GRIDPICO_OK) runtime->recording_enabled = true;
    return result;
}

gridpico_result local_runtime_bind_forwarding_transport(local_runtime *runtime)
{
    if (!runtime->forwarding_transport) return GRIDPICO_INVALID;
    if (runtime->forwarding_enabled || runtime->serial.generation != 0) return GRIDPICO_UNAVAILABLE;
    for (size_t i = 0; i < runtime->graph.inventory.port_count; ++i) {
        if (runtime->bindings[i].generation != 1 || runtime->bindings[i].adapter_owned) {
            return GRIDPICO_UNAVAILABLE;
        }
    }
    runtime->forwarding_enabled = true;
    runtime->forwarding_barriers[0] = (local_forwarding_barrier){
        .revision = runtime->forwarding_revision,
        .running = runtime->forwarding_running,
    };
    runtime->forwarding_barrier_count = 1;
    return GRIDPICO_OK;
}

gridpico_result local_runtime_bind_apc_transport(local_runtime *runtime)
{
    if (!runtime->profile->editable_surface || !runtime->forwarding_enabled) return GRIDPICO_INVALID;
    if (runtime->editable_apc.enabled) return GRIDPICO_UNAVAILABLE;
    runtime->editable_apc.enabled = true;
    return GRIDPICO_OK;
}

void local_runtime_begin(local_runtime *runtime, uint64_t now_ms)
{
    runtime->now_ms = now_ms;
    runtime->output_count = 0;
    runtime->fault_count = 0;
    runtime->forwarding_barrier_count = 0;
    runtime->serial_count = 0;
    runtime->serial.fault = false;
    runtime->editable_apc.refusal = GRIDPICO_OK;
    for (size_t i = 0; i < runtime->graph.inventory.port_count; ++i) {
        runtime->bindings[i].queued = 0;
        runtime->bindings[i].fault_staged = false;
    }
}

gridpico_result local_runtime_port(const local_runtime *runtime, gridpico_port_id id,
                                  local_port_state *out)
{
    const size_t index = port_index(runtime, id);
    if (index == runtime->graph.inventory.port_count) {
        return GRIDPICO_INVALID;
    }
    const local_binding *binding = &runtime->bindings[index];
    const local_profile_port *metadata = port_metadata(runtime, index);
    *out = (local_port_state){
        .direction = runtime->graph.inventory.ports[index].direction,
        .limit = metadata->queue_limit,
        .generation = binding->generation,
        .attached = binding->attached,
        .adapter_owned = binding->adapter_owned,
        .socket = metadata->socket,
        .managed = runtime->graph.port_capabilities[index] == GRIDPICO_LOCAL_PORT_MANAGED,
    };
    return GRIDPICO_OK;
}

gridpico_result local_runtime_reserve_adapter(local_runtime *runtime, gridpico_port_id id)
{
    const size_t index = port_index(runtime, id);
    if (index == runtime->graph.inventory.port_count) {
        return GRIDPICO_INVALID;
    }
    if (runtime->graph.ports[index].ephemeral_managed_output) return GRIDPICO_UNAVAILABLE;
    local_binding *port = &runtime->bindings[index];
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
    if (runtime->graph.ports[port_index(runtime, connection.endpoint)].ephemeral_managed_output ||
        port->adapter_owned != (adapter.retire != NULL) ||
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
    } else if (connection.endpoint == runtime->feedback_port ||
               (runtime->editable_apc.enabled && connection.endpoint == runtime->editable_apc.panel_port)) {
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
    const size_t index = port_index(runtime, connection.endpoint);
    if (!managed_port(runtime, index, GRIDPICO_PATCH_OUTPUT)) {
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
    const size_t index = port_index(runtime, connection.endpoint);
    if (index == runtime->graph.inventory.port_count || connection.generation == 0) {
        return GRIDPICO_INVALID;
    }
    local_binding *port = &runtime->bindings[index];
    if (runtime->graph.ports[index].ephemeral_managed_output ||
        !port->attached || port->generation != connection.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    port->fault = false;
    gridpico_result result = apply_port(runtime, connection, false);
    cancel_panic_output(runtime, connection);
    if (runtime->editable_apc.enabled &&
        (connection.endpoint == runtime->editable_apc.panel_port ||
         connection.endpoint == runtime->feedback_port)) {
        if (connection.endpoint == runtime->editable_apc.panel_port) {
            result = first_error(result, gridpico_apc_physical_detach(&runtime->editable_apc.frontend));
        }
        fence_apc_visibility(runtime);
    } else if (runtime->graph.port_capabilities[index] == GRIDPICO_LOCAL_PORT_PANEL) {
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

static gridpico_result receive_editable_apc(local_runtime *runtime, gridpico_connection panel,
                                            const gridpico_apc_surface_message *message)
{
    local_apc_surface *apc = &runtime->editable_apc;
    if (!apc->enabled || panel.endpoint != apc->panel_port) return GRIDPICO_UNAVAILABLE;
    gridpico_result result;
    apc->edit_accepted = false;
    if (apc->inhibited) {
        uint8_t note;
        const gridpico_apc_pad_event event = gridpico_apc_decode_button(message, &note);
        if (event == GRIDPICO_APC_PAD_INVALID) return GRIDPICO_INVALID;
        if (event != GRIDPICO_APC_PAD_PRESSED) {
            return gridpico_apc_physical_receive(&apc->frontend, message, runtime->now_ms);
        }
        /* Keep a suppressed down held so its repeat cannot edit after rearm. */
        if (apc->frontend.held[note]) return GRIDPICO_OK;
        apc->frontend.held[note] = true;
        if (apc->flight.token == 0 && !apc->latest_valid) runtime->feedback_refresh = true;
        result = GRIDPICO_UNAVAILABLE;
    } else {
        const bool arranging = apc->frontend.arranging;
        result = gridpico_apc_physical_receive(&apc->frontend, message, runtime->now_ms);
        if (apc->frontend.arranging != arranging) advance_apc_epoch(runtime);
    }
    if (result != GRIDPICO_OK) {
        if (apc->edit_accepted) {
            fence_apc_visibility(runtime);
        } else {
            apc->refused_owner = panel;
            apc->refusal = result;
        }
    }
    return GRIDPICO_OK;
}

gridpico_result local_runtime_midi_at_revision(local_runtime *runtime,
                                               const gridpico_local_raw_ingress *input,
                                               gridpico_revision revision)
{
    const size_t index = port_index(runtime, input->source.endpoint);
    if (index == runtime->graph.inventory.port_count ||
        runtime->graph.inventory.ports[index].direction != GRIDPICO_PATCH_INPUT ||
        !valid_triplet(input->bytes) || input->source.generation == 0) {
        return GRIDPICO_INVALID;
    }
    local_binding *port = &runtime->bindings[index];
    if (!port->attached || port->generation != input->source.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    const gridpico_local_port_capability capability = runtime->graph.port_capabilities[index];
    if (revision != 0 && (!runtime->forwarding_enabled || capability != GRIDPICO_LOCAL_PORT_MANAGED)) {
        return GRIDPICO_INVALID;
    }
    switch (capability) {
    case GRIDPICO_LOCAL_PORT_PANEL: {
        gridpico_apc_surface_message message;
        memcpy(message.bytes, input->bytes, sizeof(message.bytes));
        if (runtime->profile->editable_surface) {
            if (port->adapter_owned || port->fault) return GRIDPICO_UNAVAILABLE;
            return receive_editable_apc(runtime, input->source, &message);
        }
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
        if (runtime->forwarding_enabled) {
            if (revision == 0 || revision > runtime->forwarding_revision) return GRIDPICO_INVALID;
            if (!runtime->forwarding_running || revision != runtime->forwarding_revision) return GRIDPICO_OK;
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
        if (runtime->editable_apc.enabled && note.note.kind == GRIDPICO_NOTE_ON) {
            gridpico_patch_session_observe_apc_patch_note(&runtime->session, &note, runtime->now_ms * 1000);
            runtime->feedback_refresh = true;
        }
        const gridpico_result result = gridpico_patch_graph_submit_note(&runtime->graph, &note);
        if (result != GRIDPICO_OK) {
            append_log(runtime, (gridpico_diagnostic_log_entry){
                .kind = GRIDPICO_DIAGNOSTIC_NOTE_ERROR, .port = note.source.endpoint,
                .detail.error = result,
            });
            runtime->feedback_refresh = true;
        }
        return result;
    }
    }
    return GRIDPICO_INVALID;
}

gridpico_result local_runtime_midi(local_runtime *runtime, const gridpico_local_raw_ingress *input)
{
    return local_runtime_midi_at_revision(runtime, input, 0);
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
        if (runtime->routing_enabled) {
            gridpico_cli_bind_routing(&runtime->cli, runtime, read_routing, read_local_route,
                                     set_local_route, &runtime->routing_scratch);
        } else {
            runtime->cli.set_local_route = set_local_route;
            runtime->cli.read_local_route = read_local_route;
        }
        if (runtime->note_transform_enabled) {
            gridpico_cli_bind_note_transform(&runtime->cli, runtime, apply_note_transform);
        }
        if (runtime->forwarding_enabled) {
            gridpico_cli_bind_musical_control(&runtime->cli, runtime, read_forwarding, apply_forwarding,
                                              read_panic, start_panic, &runtime->panic_scratch);
        }
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
    cancel_panic(runtime);
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
    for (size_t i = 0; i < runtime->graph.inventory.port_count;) {
        local_binding *port = &runtime->bindings[i];
        if (port->attached && port->fault) {
            const gridpico_connection connection = {runtime->graph.inventory.ports[i].id, port->generation};
            if (runtime->graph.ports[i].ephemeral_managed_output) {
                local_fm_allocation *allocation = find_fm(runtime, connection.endpoint);
                if (allocation != NULL && same_connection(allocation->lease.output, connection)) {
                    stage_fault(runtime, connection);
                    const gridpico_result retired = retire_fm_allocation(runtime, allocation);
                    result = first_error(result, retired);
                    if (retired == GRIDPICO_OK) continue;
                }
            } else {
                result = first_error(result, local_runtime_detach(runtime, connection));
                stage_fault(runtime, connection);
            }
        }
        ++i;
    }
    if (runtime->serial.fault && runtime->serial.attached) {
        local_runtime_serial_detach(runtime, runtime->serial.generation);
    }
    return result;
}

gridpico_result local_runtime_finish(local_runtime *runtime, gridpico_result result)
{
    result = first_error(result, retire_faults(runtime));
    if (runtime->editable_apc.enabled) {
        refresh_editable_apc(runtime);
        return result;
    }
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
    const size_t limit = port_metadata(runtime, index)->queue_limit;
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
    if (runtime->fm_runtime_epoch != 0) {
        for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
            local_fm_allocation *allocation = &runtime->fm_allocations[i];
            if (allocation->state == LOCAL_FM_PENDING && runtime->now_ms >= allocation->deadline_ms) {
                *allocation = (local_fm_allocation){0};
            }
        }
    }
    return runtime->raw_deadline_ms != 0 && runtime->now_ms >= runtime->raw_deadline_ms
        ? end_raw(runtime) : GRIDPICO_OK;
}

gridpico_result local_runtime_retire(local_runtime *runtime)
{
    gridpico_result result = GRIDPICO_OK;
    cancel_panic(runtime);
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
    for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
        local_fm_allocation *allocation = &runtime->fm_allocations[i];
        if (allocation->state != LOCAL_FM_FREE) {
            result = first_error(result, retire_fm_allocation(runtime, allocation));
        }
    }
    runtime->serial.attached = false;
    return result;
}
