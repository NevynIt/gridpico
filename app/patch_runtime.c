#include "app/patch_runtime.h"

#include "app/patch_inventory.h"

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
    if (runtime->output.note_count == GRIDPICO_PATCH_BROWSER_RELEASE_CREDITS) {
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

static gridpico_result init_runtime(patch_runtime *runtime, bool browser)
{
    *runtime = (patch_runtime){.keybed = {KEYBED, 1}, .next_log_sequence = 1};
    gridpico_result result = gridpico_patch_fixture_init(&runtime->sim, &patch_inventory);
    if (result != GRIDPICO_OK) {
        return result;
    }
    result = browser
        ? gridpico_patch_graph_init_browser(&runtime->graph, &patch_inventory, stage_delivery, runtime)
        : gridpico_patch_graph_init(&runtime->graph, &patch_inventory, stage_delivery, runtime);
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
    return init_runtime(runtime, false);
}

gridpico_result patch_runtime_init_browser(patch_runtime *runtime)
{
    return init_runtime(runtime, true);
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
    gridpico_patch_snapshot before;
    gridpico_result result = read_snapshot(runtime, &before);
    if (result != GRIDPICO_OK) {
        return result;
    }
    result = gridpico_patch_fixture_update_port(&runtime->sim, update);
    if (result != GRIDPICO_OK) {
        return result;
    }

    result = gridpico_patch_graph_update_port(&runtime->graph, update);
    gridpico_patch_snapshot after;
    const gridpico_result observed = read_snapshot(runtime, &after);
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
    const gridpico_result refreshed = gridpico_apc_patch_refresh_snapshot(&runtime->apc, &after);
    return result == GRIDPICO_OK ? refreshed : result;
}

gridpico_result patch_runtime_update_port(patch_runtime *runtime,
                                         const gridpico_patch_port_update *update)
{
    return update_port(runtime, update, true);
}

gridpico_result patch_runtime_disconnect_keybed(patch_runtime *runtime)
{
    const gridpico_patch_port_update disconnect = {
        .connection = runtime->keybed, .state = GRIDPICO_PATCH_DISCONNECTED,
    };
    return update_port(runtime, &disconnect, false);
}
