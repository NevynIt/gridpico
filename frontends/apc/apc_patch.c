#include "apc_patch.h"

#include "apc_protocol.h"

enum {
    APC_CLIP_STOP_FIRST = 0x40,
    APC_SCENE_FIRST = 0x52,
    APC_SHIFT = 0x62,
    APC_COLOR_PREVIEW_PAD = 0x08
};

static const struct {
    gridpico_rgb8 rgb;
    uint8_t palette;
} swatches[] = {
    {{0xff, 0x00, 0x00}, 0x05},
    {{0x7f, 0x7f, 0x7f}, 0x02},
    {{0x59, 0x1d, 0x00}, 0x0a},
    {{0x1e, 0x1e, 0x1e}, 0x01}
};

#define APC_SWATCH_COUNT (sizeof(swatches) / sizeof(swatches[0]))

static uint8_t route_palette(gridpico_patch_route_state state)
{
    switch (state) {
    case GRIDPICO_PATCH_ROUTE_ACTIVE:
        return GRIDPICO_PATCH_LED_ACTIVE;
    case GRIDPICO_PATCH_ROUTE_PENDING:
        return GRIDPICO_PATCH_LED_PENDING;
    case GRIDPICO_PATCH_ROUTE_OFF:
        return GRIDPICO_PATCH_LED_OFF;
    }
    return GRIDPICO_PATCH_LED_OFF;
}

static uint32_t color_distance(gridpico_rgb8 first, gridpico_rgb8 second)
{
    const int32_t red = (int32_t)first.red - second.red;
    const int32_t green = (int32_t)first.green - second.green;
    const int32_t blue = (int32_t)first.blue - second.blue;
    return (uint32_t)(red * red + green * green + blue * blue);
}

static uint8_t preview_palette(gridpico_device_color color)
{
    if (!color.is_set || (color.rgb.red == 0 && color.rgb.green == 0 && color.rgb.blue == 0)) {
        return GRIDPICO_PATCH_LED_OFF;
    }

    size_t nearest = 0;
    uint32_t best_distance = color_distance(color.rgb, swatches[0].rgb);
    for (size_t index = 1; index < APC_SWATCH_COUNT; ++index) {
        const uint32_t distance = color_distance(color.rgb, swatches[index].rgb);
        if (distance < best_distance) {
            nearest = index;
            best_distance = distance;
        }
    }
    return swatches[nearest].palette;
}

static size_t pad_in_row(size_t row, size_t column)
{
    return (GRIDPICO_PATCH_SOURCE_ROWS - 1 - row) * GRIDPICO_PATCH_DESTINATION_COLUMNS + column;
}

static void emit_page(gridpico_apc_patch *frontend, gridpico_device_id device,
                      const uint8_t *palette)
{
    if (frontend->page_feedback != NULL) {
        const gridpico_patch_led_page page = {
            .layer = device == 0 ? GRIDPICO_PATCH_LED_LAYER_ROUTE
                                 : GRIDPICO_PATCH_LED_LAYER_DEVICE_COLOR,
            .device = device
        };
        frontend->page_feedback(frontend->feedback_context, &page);
    }
    for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
        for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
            const size_t pad = pad_in_row(row, column);
            const gridpico_apc_surface_message message =
                gridpico_apc_encode_rgb_pad((uint8_t)pad, palette[pad]);
            frontend->feedback(frontend->feedback_context, &message);
        }
    }
}

static gridpico_result render_color_page(gridpico_apc_patch *frontend, gridpico_device_id device)
{
    gridpico_device_color color;
    const gridpico_result result = frontend->read_color(frontend->session_context, device, &color);
    if (result != GRIDPICO_OK) {
        return result;
    }

    uint8_t palette[GRIDPICO_PATCH_LED_FRAMES] = {0};
    for (size_t index = 0; index < APC_SWATCH_COUNT; ++index) {
        palette[index] = swatches[index].palette;
    }
    palette[APC_COLOR_PREVIEW_PAD] = preview_palette(color);
    emit_page(frontend, device, palette);
    return GRIDPICO_OK;
}

void gridpico_apc_patch_init(gridpico_apc_patch *frontend, void *session_context,
                            gridpico_patch_read_snapshot_fn read_snapshot,
                            gridpico_patch_set_route_fn set_route,
                            gridpico_patch_read_device_color_fn read_color,
                            gridpico_patch_set_device_color_fn set_color,
                            void *feedback_context,
                            gridpico_apc_surface_feedback_fn feedback)
{
    *frontend = (gridpico_apc_patch){
        .session_context = session_context,
        .read_snapshot = read_snapshot,
        .set_route = set_route,
        .read_color = read_color,
        .set_color = set_color,
        .feedback_context = feedback_context,
        .feedback = feedback
    };
}

void gridpico_apc_patch_set_page_feedback(gridpico_apc_patch *frontend,
                                         gridpico_patch_led_page_fn page_feedback)
{
    frontend->page_feedback = page_feedback;
}

gridpico_result gridpico_apc_patch_refresh_snapshot(
    gridpico_apc_patch *frontend, const gridpico_patch_snapshot *snapshot)
{
    if (frontend->color_device != 0) {
        return render_color_page(frontend, frontend->color_device);
    }

    uint8_t palette[GRIDPICO_PATCH_LED_FRAMES];
    for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
        for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
            palette[pad_in_row(row, column)] = route_palette(snapshot->routes[row][column]);
        }
    }
    emit_page(frontend, 0, palette);
    return GRIDPICO_OK;
}

gridpico_result gridpico_apc_patch_refresh(gridpico_apc_patch *frontend)
{
    if (frontend->color_device != 0) {
        return render_color_page(frontend, frontend->color_device);
    }

    gridpico_patch_snapshot snapshot;
    const gridpico_result result = frontend->read_snapshot(frontend->session_context, &snapshot);
    return result == GRIDPICO_OK ? gridpico_apc_patch_refresh_snapshot(frontend, &snapshot)
                                : result;
}

static gridpico_result toggle_route(gridpico_apc_patch *frontend, uint8_t pad)
{
    gridpico_patch_snapshot snapshot;
    const gridpico_result read = frontend->read_snapshot(frontend->session_context, &snapshot);
    if (read != GRIDPICO_OK) {
        return read;
    }

    const size_t row = GRIDPICO_PATCH_SOURCE_ROWS - 1 -
                       (size_t)pad / GRIDPICO_PATCH_DESTINATION_COLUMNS;
    const size_t column = (size_t)pad % GRIDPICO_PATCH_DESTINATION_COLUMNS;
    const gridpico_patch_route_request request = {
        .source = snapshot.source_rows[row],
        .destination = snapshot.destination_columns[column],
        .enabled = snapshot.routes[row][column] == GRIDPICO_PATCH_ROUTE_OFF
    };
    if (request.source == 0 || request.destination == 0) {
        return GRIDPICO_OK;
    }

    const gridpico_result command = frontend->set_route(frontend->session_context, &request);
    const gridpico_result refresh = gridpico_apc_patch_refresh(frontend);
    return command == GRIDPICO_OK ? refresh : command;
}

static gridpico_result select_color_device(gridpico_apc_patch *frontend, uint8_t note)
{
    gridpico_patch_snapshot snapshot;
    const gridpico_result read = frontend->read_snapshot(frontend->session_context, &snapshot);
    if (read != GRIDPICO_OK) {
        return read;
    }

    const gridpico_port_id port = note >= APC_SCENE_FIRST
        ? snapshot.source_rows[note - APC_SCENE_FIRST]
        : snapshot.destination_columns[note - APC_CLIP_STOP_FIRST];
    if (port == 0) {
        return GRIDPICO_OK;
    }

    for (size_t index = 0; index < snapshot.inventory.port_count; ++index) {
        const gridpico_patch_port *entry = &snapshot.inventory.ports[index];
        if (entry->id == port) {
            const gridpico_result result = render_color_page(frontend, entry->device);
            if (result == GRIDPICO_OK) {
                frontend->color_device = entry->device;
            }
            return result;
        }
    }
    return GRIDPICO_OK;
}

static gridpico_result press_color_pad(gridpico_apc_patch *frontend, uint8_t pad)
{
    if (frontend->color_device == 0) {
        return GRIDPICO_OK;
    }
    if (pad == APC_COLOR_PREVIEW_PAD) {
        return render_color_page(frontend, frontend->color_device);
    }
    if (pad >= APC_SWATCH_COUNT) {
        return GRIDPICO_OK;
    }

    const gridpico_result command =
        frontend->set_color(frontend->session_context, frontend->color_device, swatches[pad].rgb);
    const gridpico_result refresh = render_color_page(frontend, frontend->color_device);
    return command == GRIDPICO_OK ? refresh : command;
}

gridpico_result gridpico_apc_patch_receive(
    gridpico_apc_patch *frontend, const gridpico_apc_surface_message *message)
{
    uint8_t note = 0;
    const gridpico_apc_pad_event event = gridpico_apc_decode_button(message, &note);
    switch (event) {
    case GRIDPICO_APC_PAD_INVALID:
        return GRIDPICO_INVALID;
    case GRIDPICO_APC_PAD_IGNORED:
        return GRIDPICO_OK;
    case GRIDPICO_APC_PAD_RELEASED:
        frontend->note_held[note] = false;
        if (note == APC_SHIFT && frontend->color_device != 0) {
            frontend->color_device = 0;
            return gridpico_apc_patch_refresh(frontend);
        }
        return GRIDPICO_OK;
    case GRIDPICO_APC_PAD_PRESSED:
        break;
    }

    if (frontend->note_held[note]) {
        return GRIDPICO_OK;
    }
    frontend->note_held[note] = true;
    const bool shift_held = frontend->note_held[APC_SHIFT];
    if (note < GRIDPICO_PATCH_LED_FRAMES) {
        return shift_held ? press_color_pad(frontend, note) : toggle_route(frontend, note);
    }

    if (shift_held &&
        ((note >= APC_SCENE_FIRST && note < APC_SCENE_FIRST + GRIDPICO_PATCH_SOURCE_ROWS) ||
         (note >= APC_CLIP_STOP_FIRST &&
          note < APC_CLIP_STOP_FIRST + GRIDPICO_PATCH_DESTINATION_COLUMNS))) {
        return select_color_device(frontend, note);
    }
    return GRIDPICO_OK;
}
