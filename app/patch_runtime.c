#include "app/patch_runtime.h"

#include "app/patch_inventory.h"

typedef enum {
    PATCH_PROFILE_NATIVE,
    PATCH_PROFILE_BROWSER,
    PATCH_PROFILE_MONITOR,
} patch_profile;

enum { MONITOR_DORMANT_GENERATION = 1, MONITOR_FIRST_GENERATION = 2 };

static void append_log(patch_runtime *runtime, gridpico_diagnostic_log_entry entry)
{
    entry.sequence = runtime->next_log_sequence++;
    const size_t index = (runtime->log_start + runtime->log_count) % GRIDPICO_DIAGNOSTIC_LOG_ROWS;
    runtime->logs[index] = entry;
    if (runtime->log_count == GRIDPICO_DIAGNOSTIC_LOG_ROWS) {
        runtime->log_start = (runtime->log_start + 1) % GRIDPICO_DIAGNOSTIC_LOG_ROWS;
    } else {
        ++runtime->log_count;
    }
}

static size_t read_logs(void *context, gridpico_diagnostic_log_entry *rows, size_t capacity)
{
    const patch_runtime *runtime = context;
    const size_t count = capacity < runtime->log_count ? capacity : runtime->log_count;
    for (size_t i = 0; i < count; ++i) {
        rows[i] = runtime->logs[(runtime->log_start + i) % GRIDPICO_DIAGNOSTIC_LOG_ROWS];
    }
    return count;
}

static gridpico_result stage_delivery(void *context, const gridpico_note_delivery *delivery)
{
    patch_runtime *runtime = context;
    if (runtime->output.note_count == GRIDPICO_PATCH_MONITOR_RELEASE_CREDITS) {
        runtime->output.overflowed = true;
        return GRIDPICO_CAPACITY;
    }
    const gridpico_result result = gridpico_patch_fixture_deliver(&runtime->sim, delivery);
    if (result == GRIDPICO_OK) {
        runtime->output.notes[runtime->output.note_count++] = *delivery;
    }
    return result;
}

static void stage_led(void *context, const gridpico_apc_surface_message *message)
{
    patch_runtime *runtime = context;
    if (runtime->output.led_count == GRIDPICO_PATCH_LED_FRAMES) {
        runtime->output.overflowed = true;
        return;
    }
    runtime->output.leds[runtime->output.led_count++] = *message;
}

static void stage_page(void *context, const gridpico_patch_led_page *page)
{
    patch_runtime *runtime = context;
    if (runtime->output.has_page || runtime->output.led_count != 0) {
        runtime->output.overflowed = true;
        return;
    }
    runtime->output.page = *page;
    runtime->output.has_page = true;
}

static gridpico_result read_snapshot(void *context, gridpico_patch_snapshot *out)
{
    patch_runtime *runtime = context;
    return gridpico_patch_session_read_snapshot(&runtime->session, out);
}

static gridpico_result read_color(void *context, gridpico_device_id device,
                                  gridpico_device_color *out)
{
    patch_runtime *runtime = context;
    return gridpico_session_read_device_color(&runtime->session, device, out);
}

static gridpico_result set_color(void *context, gridpico_device_id device, gridpico_rgb8 rgb)
{
    patch_runtime *runtime = context;
    return gridpico_session_set_device_color(&runtime->session, device, rgb);
}

gridpico_result patch_runtime_set_route(void *context, const gridpico_patch_route_request *request)
{
    patch_runtime *runtime = context;
    const gridpico_result result = gridpico_patch_session_set_route(&runtime->session, request);
    if (result != GRIDPICO_OK && request != NULL) {
        append_log(runtime, (gridpico_diagnostic_log_entry){
            .kind = GRIDPICO_DIAGNOSTIC_ROUTE_ERROR,
            .port = request->destination,
            .detail.error = result,
        });
    }
    return result;
}

static gridpico_result init_runtime(patch_runtime *runtime, patch_profile profile)
{
    *runtime = (patch_runtime){.keybed = {KEYBED, 1}, .next_log_sequence = 1};
    gridpico_patch_inventory inventory = patch_inventory;
    const bool monitor = profile == PATCH_PROFILE_MONITOR;
    if (monitor) {
        runtime->monitor_generation = MONITOR_DORMANT_GENERATION;
        inventory.devices[inventory.device_count++] = (gridpico_patch_device){
            GRIDPICO_PATCH_MONITOR_DEVICE_ID, "Web Monitor", GRIDPICO_PATCH_DISCONNECTED,
        };
        inventory.ports[inventory.port_count++] = (gridpico_patch_port){
            GRIDPICO_PATCH_MONITOR_PORT_ID, GRIDPICO_PATCH_MONITOR_DEVICE_ID, "Monitor",
            GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true,
        };
    }
    gridpico_result result = monitor
        ? gridpico_patch_fixture_init_monitor(&runtime->sim, &inventory)
        : gridpico_patch_fixture_init(&runtime->sim, &inventory);
    if (result != GRIDPICO_OK) {
        return result;
    }
    switch (profile) {
    case PATCH_PROFILE_NATIVE:
        result = gridpico_patch_graph_init(&runtime->graph, &inventory, stage_delivery, runtime);
        break;
    case PATCH_PROFILE_BROWSER:
        result = gridpico_patch_graph_init_browser(&runtime->graph, &inventory, stage_delivery, runtime);
        break;
    case PATCH_PROFILE_MONITOR:
        result = gridpico_patch_graph_init_monitor(&runtime->graph, &inventory, stage_delivery, runtime);
        break;
    }
    if (result != GRIDPICO_OK) {
        return result;
    }
    gridpico_patch_session_init(&runtime->session, &runtime->graph, gridpico_patch_graph_read_graph,
                                gridpico_patch_graph_set_route);
    gridpico_apc_patch_init(&runtime->apc, runtime, read_snapshot, patch_runtime_set_route, read_color,
                            set_color, runtime, stage_led);
    gridpico_apc_patch_set_page_feedback(&runtime->apc, stage_page);
    return GRIDPICO_OK;
}

gridpico_result patch_runtime_init(patch_runtime *runtime)
{
    return init_runtime(runtime, PATCH_PROFILE_NATIVE);
}

gridpico_result patch_runtime_init_browser(patch_runtime *runtime)
{
    return init_runtime(runtime, PATCH_PROFILE_BROWSER);
}

gridpico_result patch_runtime_init_monitor(patch_runtime *runtime)
{
    return init_runtime(runtime, PATCH_PROFILE_MONITOR);
}

gridpico_result patch_runtime_bind_cli(patch_runtime *runtime, gridpico_cli_frontend *cli,
                                      gridpico_cli_write_fn write, void *write_context)
{
    return gridpico_cli_init(cli, runtime, read_snapshot, read_color, set_color,
                             runtime, read_logs, write, write_context);
}

void patch_runtime_clear_output(patch_runtime *runtime)
{
    runtime->output = (patch_output){0};
}

gridpico_result patch_runtime_refresh(patch_runtime *runtime, gridpico_patch_snapshot *snapshot)
{
    const gridpico_result result = read_snapshot(runtime, snapshot);
    return result == GRIDPICO_OK ? gridpico_apc_patch_refresh_snapshot(&runtime->apc, snapshot)
                                 : result;
}

gridpico_result patch_runtime_submit(patch_runtime *runtime, const gridpico_ingress_note *input)
{
    const gridpico_result result = gridpico_patch_graph_submit_note(&runtime->graph, input);
    if (result != GRIDPICO_OK && input != NULL) {
        append_log(runtime, (gridpico_diagnostic_log_entry){
            .kind = GRIDPICO_DIAGNOSTIC_NOTE_ERROR,
            .port = input->source.endpoint,
            .detail.error = result,
        });
    }
    return result;
}

static gridpico_result update_port(patch_runtime *runtime, const gridpico_patch_port_update *update,
                                   bool refresh)
{
    gridpico_patch_graph_state before;
    gridpico_result result = gridpico_patch_graph_read_graph(&runtime->graph, &before);
    if (result != GRIDPICO_OK) {
        return result;
    }
    result = gridpico_patch_fixture_update_port(&runtime->sim, update);
    if (result != GRIDPICO_OK) {
        return result;
    }

    result = gridpico_patch_graph_update_port(&runtime->graph, update);
    if (update->connection.endpoint == GRIDPICO_PATCH_MONITOR_PORT_ID &&
        (result == GRIDPICO_OK || result == GRIDPICO_UNAVAILABLE)) {
        /* Both adapters enforce the same generation rules. An unavailable
         * cleanup can accompany a committed attachment or detachment. */
        runtime->monitor_generation = update->connection.generation;
        runtime->monitor_attached = update->state == GRIDPICO_PATCH_CONNECTED;
    }
    gridpico_patch_graph_state after;
    const gridpico_result observed = gridpico_patch_graph_read_graph(&runtime->graph, &after);
    if (observed != GRIDPICO_OK) {
        return result == GRIDPICO_OK ? observed : result;
    }

    for (size_t i = 0; i < after.inventory.port_count; ++i) {
        const gridpico_patch_port *port = &after.inventory.ports[i];
        if (port->state != before.inventory.ports[i].state) {
            append_log(runtime, (gridpico_diagnostic_log_entry){
                .kind = GRIDPICO_DIAGNOSTIC_PORT_STATE,
                .port = port->id,
                .detail.state = port->state,
            });
            if (port->id == KEYBED && port->state == GRIDPICO_PATCH_CONNECTED) {
                runtime->keybed = update->connection;
            }
        }
    }
    if (!refresh || before.revision == after.revision) {
        return result;
    }
    gridpico_patch_snapshot snapshot;
    const gridpico_result refreshed = patch_runtime_refresh(runtime, &snapshot);
    return result == GRIDPICO_OK ? refreshed : result;
}

gridpico_result patch_runtime_update_port(patch_runtime *runtime,
                                         const gridpico_patch_port_update *update)
{
    return update_port(runtime, update, true);
}

static gridpico_patch_port_update monitor_update(uint32_t generation, bool attached)
{
    return (gridpico_patch_port_update){
        .connection = {GRIDPICO_PATCH_MONITOR_PORT_ID, generation},
        .state = attached ? GRIDPICO_PATCH_CONNECTED : GRIDPICO_PATCH_DISCONNECTED,
    };
}

gridpico_result patch_runtime_set_monitor(patch_runtime *runtime, uint32_t generation, bool attached,
                                          gridpico_patch_snapshot *snapshot, bool *has_inventory)
{
    *has_inventory = false;
    const uint32_t current = runtime->monitor_generation;
    if (current == 0 || generation == 0) {
        return GRIDPICO_INVALID;
    }
    if (!attached || runtime->monitor_attached) {
        if (generation != current) {
            return GRIDPICO_UNAVAILABLE;
        }
        if (!attached && !runtime->monitor_attached) {
            return GRIDPICO_OK;
        }
    } else if (generation <= current ||
               (current == MONITOR_DORMANT_GENERATION && generation != MONITOR_FIRST_GENERATION)) {
        return GRIDPICO_UNAVAILABLE;
    }

    const gridpico_patch_port_update update = monitor_update(generation, attached);
    const gridpico_result result = update_port(runtime, &update, false);
    if (runtime->monitor_generation != generation || runtime->monitor_attached != attached) {
        return result;
    }
    runtime->output.has_monitor_state = true;
    const gridpico_result refreshed = patch_runtime_refresh(runtime, snapshot);
    *has_inventory = refreshed == GRIDPICO_OK;
    return result == GRIDPICO_OK ? refreshed : result;
}

gridpico_result patch_runtime_disconnect_keybed(patch_runtime *runtime)
{
    const gridpico_patch_port_update disconnect = {
        .connection = runtime->keybed, .state = GRIDPICO_PATCH_DISCONNECTED,
    };
    return update_port(runtime, &disconnect, false);
}

gridpico_result patch_runtime_retire(patch_runtime *runtime)
{
    const gridpico_result cleanup = patch_runtime_disconnect_keybed(runtime);
    if (!runtime->monitor_attached) {
        return cleanup;
    }
    const gridpico_patch_port_update detach = monitor_update(runtime->monitor_generation, false);
    const gridpico_result retired = update_port(runtime, &detach, false);
    return cleanup == GRIDPICO_OK ? retired : cleanup;
}
