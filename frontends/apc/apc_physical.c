#include "apc_physical.h"

#include "apc_protocol.h"

#include <string.h>

enum {
    APC_TRACK_FIRST = 0x40,
    APC_LEFT = 0x42,
    APC_RIGHT = 0x43,
    APC_SCENE_FIRST = 0x52,
    APC_STOP_ALL = 0x51,
    APC_SHIFT = 0x62,
    APC_FIXED_LED = 0x90,
    APC_DIM_OFF = 0x01,
    APC_AMBER_DIM = 0x0a,
    APC_PREVIEW_COLOR = 0x25,
    APC_PREVIEW_ERROR = 0x05,
    APC_PENDING_PHASE_MS = 500,
    APC_ACTIVITY_MS = 100,
    APC_NO_SLOT = UINT8_MAX,
    APC_ASSIGN_PAD = 0x20,
    APC_AUDITION_PAD = 0x23,
    APC_UNASSIGNED_PAD = 0x06,
    APC_CONFIRM_PAD = 0x07,
    APC_CANDIDATE_FIRST = 0x08,
    APC_CANDIDATE_COUNT = APC_ASSIGN_PAD - APC_CANDIDATE_FIRST
};

static uint8_t pad_note(size_t row, size_t column)
{
    return (uint8_t)((GRIDPICO_PATCH_SOURCE_ROWS - 1 - row) *
                     GRIDPICO_PATCH_DESTINATION_COLUMNS + column);
}

static gridpico_apc_surface_message route_led(uint8_t note, gridpico_apc_patch_cell cell,
                                             uint64_t now_ms)
{
    if (!cell.compatible) {
        return gridpico_apc_encode_rgb_pad(note, GRIDPICO_PATCH_LED_OFF);
    }
    switch (cell.state) {
    case GRIDPICO_PATCH_ROUTE_ACTIVE:
        return gridpico_apc_encode_rgb_pad(note, GRIDPICO_PATCH_LED_ACTIVE);
    case GRIDPICO_PATCH_ROUTE_PENDING:
        return gridpico_apc_encode_rgb_pad(note, (now_ms / APC_PENDING_PHASE_MS) % 2 == 0
            ? GRIDPICO_PATCH_LED_PENDING : APC_AMBER_DIM);
    case GRIDPICO_PATCH_ROUTE_OFF:
        return gridpico_apc_encode_rgb_pad(note, cell.available ? APC_DIM_OFF : GRIDPICO_PATCH_LED_OFF);
    }
    return gridpico_apc_encode_rgb_pad(note, GRIDPICO_PATCH_LED_OFF);
}

static gridpico_apc_surface_message edge_led(uint8_t note, bool lit)
{
    return gridpico_apc_encode_led(APC_FIXED_LED, note, lit ? 1 : 0);
}

static const gridpico_apc_patch_port *find_port(const gridpico_apc_patch_snapshot *snapshot,
                                               gridpico_port_id id)
{
    for (size_t i = 0; i < snapshot->port_count; ++i) {
        if (snapshot->ports[i].id == id) {
            return &snapshot->ports[i];
        }
    }
    return NULL;
}

static const gridpico_port_id *axis_ports(const gridpico_apc_patch_snapshot *snapshot,
                                         gridpico_patch_direction axis)
{
    return axis == GRIDPICO_PATCH_INPUT ? snapshot->layout.rows : snapshot->layout.columns;
}

static bool can_assign(const gridpico_apc_physical *frontend,
                       const gridpico_apc_patch_snapshot *snapshot,
                       const gridpico_apc_patch_port *port)
{
    if (port == NULL || port->direction != frontend->axis) {
        return false;
    }
    const gridpico_port_id *slots = axis_ports(snapshot, frontend->axis);
    const size_t count = frontend->axis == GRIDPICO_PATCH_INPUT ? GRIDPICO_PATCH_SOURCE_ROWS
                                                               : GRIDPICO_PATCH_DESTINATION_COLUMNS;
    for (size_t i = 0; i < count; ++i) {
        if (i != frontend->slot && slots[i] == port->id) {
            return false;
        }
    }
    return true;
}

static gridpico_result read_snapshot(gridpico_apc_physical *frontend,
                                    gridpico_apc_patch_snapshot *snapshot)
{
    const gridpico_result result = frontend->read(frontend->session_context, snapshot);
    return result == GRIDPICO_OK && snapshot->port_count > GRIDPICO_PATCH_MAX_PORTS
        ? GRIDPICO_CAPACITY : result;
}

static void observe_learn(gridpico_apc_physical *frontend,
                          const gridpico_apc_patch_snapshot *snapshot)
{
    if (frontend->action != GRIDPICO_APC_ARRANGE_LEARN || frontend->slot == APC_NO_SLOT) {
        return;
    }
    for (size_t i = 0; i < snapshot->port_count; ++i) {
        const gridpico_apc_patch_port *port = &snapshot->ports[i];
        /* A valid attack remains evidence if its source detaches before
         * refresh. Connectivity is checked separately at confirmation. */
        if (port->direction != GRIDPICO_PATCH_INPUT ||
            port->last_attack_sequence <= frontend->learn_after) {
            continue;
        }
        if (frontend->candidate == 0) {
            frontend->candidate = port->id;
        } else if (frontend->candidate != port->id) {
            frontend->learn_ambiguous = true;
        }
    }
}

static bool row_activity(gridpico_apc_row_activity *activity, const gridpico_apc_patch_port *port,
                         uint64_t now_ms)
{
    if (port == NULL || !port->connected) {
        *activity = (gridpico_apc_row_activity){0};
        return false;
    }
    if (activity->port != port->id) {
        *activity = (gridpico_apc_row_activity){.port = port->id};
    }
    if (activity->observed != port->last_attack_sequence) {
        const uint64_t attack_ms = port->last_attack_us / 1000;
        if (port->last_attack_sequence != 0 && now_ms >= attack_ms &&
            now_ms - attack_ms < APC_ACTIVITY_MS &&
            (!activity->has_flashed || (attack_ms >= activity->started_ms &&
                                        attack_ms - activity->started_ms >= 2 * APC_ACTIVITY_MS))) {
            activity->started_ms = attack_ms;
            activity->has_flashed = true;
        }
        activity->observed = port->last_attack_sequence;
    }
    return activity->has_flashed && now_ms >= activity->started_ms &&
           now_ms - activity->started_ms < APC_ACTIVITY_MS;
}

static bool can_confirm(const gridpico_apc_physical *frontend,
                        const gridpico_apc_patch_snapshot *snapshot)
{
    if (frontend->slot == APC_NO_SLOT || frontend->preview_revision != snapshot->layout_revision) {
        return false;
    }
    if (frontend->action == GRIDPICO_APC_ARRANGE_MOVE) {
        return frontend->move_to != APC_NO_SLOT;
    }
    if (frontend->action == GRIDPICO_APC_ARRANGE_ASSIGN && frontend->unassigned_selected) {
        return true;
    }
    if (frontend->action != GRIDPICO_APC_ARRANGE_ASSIGN &&
        frontend->action != GRIDPICO_APC_ARRANGE_LEARN) {
        return false;
    }
    const gridpico_apc_patch_port *port = find_port(snapshot, frontend->candidate);
    return can_assign(frontend, snapshot, port) &&
        (frontend->action != GRIDPICO_APC_ARRANGE_LEARN ||
         (port->connected && !frontend->learn_ambiguous));
}

static bool highlighted_candidate(const gridpico_apc_physical *frontend,
                                  const gridpico_apc_patch_port *port)
{
    return port->id == frontend->candidate &&
           (frontend->action != GRIDPICO_APC_ARRANGE_LEARN ||
            (port->connected && !frontend->learn_ambiguous));
}

static gridpico_apc_surface_message arrange_led(const gridpico_apc_physical *frontend,
                                               const gridpico_apc_patch_snapshot *snapshot,
                                               size_t candidate_offset, uint8_t note)
{
    uint8_t color = 0;
    if (note >= APC_ASSIGN_PAD && note <= APC_AUDITION_PAD) {
        const unsigned action = note - APC_ASSIGN_PAD + GRIDPICO_APC_ARRANGE_ASSIGN;
        color = action == (unsigned)frontend->action ? APC_PREVIEW_COLOR : APC_DIM_OFF;
    } else if (note == APC_CONFIRM_PAD) {
        color = frontend->preview_error || frontend->learn_ambiguous ? APC_PREVIEW_ERROR
            : can_confirm(frontend, snapshot) ? GRIDPICO_PATCH_LED_ACTIVE : APC_DIM_OFF;
    } else if (note == APC_UNASSIGNED_PAD && frontend->action == GRIDPICO_APC_ARRANGE_ASSIGN &&
               frontend->slot != APC_NO_SLOT) {
        color = frontend->unassigned_selected ? APC_PREVIEW_COLOR : APC_DIM_OFF;
    } else if (frontend->slot != APC_NO_SLOT && note >= APC_CANDIDATE_FIRST &&
               note < APC_ASSIGN_PAD &&
               (frontend->action == GRIDPICO_APC_ARRANGE_ASSIGN ||
                frontend->action == GRIDPICO_APC_ARRANGE_LEARN)) {
        const size_t index = candidate_offset + (size_t)(note - APC_CANDIDATE_FIRST);
        if (index < snapshot->port_count) {
            const gridpico_apc_patch_port *port = &snapshot->ports[index];
            if (can_assign(frontend, snapshot, port)) {
                color = highlighted_candidate(frontend, port) ? APC_PREVIEW_COLOR : APC_DIM_OFF;
            }
        }
    }
    return gridpico_apc_encode_rgb_pad(note, color);
}

static size_t candidate_window(const gridpico_apc_physical *frontend,
                               const gridpico_apc_patch_snapshot *snapshot)
{
    if (frontend->candidate != 0) {
        for (size_t i = 0; i < snapshot->port_count; ++i) {
            const gridpico_apc_patch_port *port = &snapshot->ports[i];
            if (highlighted_candidate(frontend, port) && can_assign(frontend, snapshot, port)) {
                return i - i % APC_CANDIDATE_COUNT;
            }
        }
    }
    return 0;
}

static bool selected(const gridpico_apc_physical *frontend, gridpico_patch_direction axis,
                     size_t slot)
{
    if (!frontend->arranging) {
        return slot == (axis == GRIDPICO_PATCH_INPUT ? frontend->selected_row
                                                   : frontend->selected_column);
    }
    return frontend->axis == axis && (slot == frontend->slot || slot == frontend->move_to);
}

static gridpico_result stop_audition(gridpico_apc_physical *frontend)
{
    if (frontend->audition_destination == 0) {
        return GRIDPICO_OK;
    }
    const gridpico_result result =
        frontend->audition(frontend->panel_context, frontend->audition_destination, false);
    if (result == GRIDPICO_OK) {
        frontend->audition_destination = 0;
    }
    return result;
}

static gridpico_result publish_page(gridpico_apc_physical *frontend,
                                    const gridpico_apc_patch_snapshot *snapshot, uint64_t now_ms)
{
    observe_learn(frontend, snapshot);
    gridpico_result stop = GRIDPICO_OK;
    if (frontend->audition_destination != 0) {
        const gridpico_apc_patch_port *port = find_port(snapshot, frontend->audition_destination);
        if (port == NULL || !port->connected) {
            stop = stop_audition(frontend);
        }
    }

    gridpico_apc_surface_message messages[GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES];
    const size_t candidate_offset = frontend->arranging ? candidate_window(frontend, snapshot) : 0;
    size_t count = 0;
    for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
        for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
            const uint8_t note = pad_note(row, column);
            messages[count++] = frontend->arranging
                ? arrange_led(frontend, snapshot, candidate_offset, note)
                : route_led(note, snapshot->cells[row][column], now_ms);
        }
    }
    for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
        const gridpico_apc_patch_port *port = find_port(snapshot, snapshot->layout.rows[row]);
        const bool flash = row_activity(&frontend->activity[row], port, now_ms);
        messages[count++] = edge_led((uint8_t)(APC_SCENE_FIRST + row),
                                     selected(frontend, GRIDPICO_PATCH_INPUT, row) != flash);
    }
    for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
        messages[count++] = edge_led((uint8_t)(APC_TRACK_FIRST + column),
                                     selected(frontend, GRIDPICO_PATCH_OUTPUT, column));
    }
    const gridpico_apc_physical_page_version version = {
        .route_page = !frontend->arranging,
        .layout_revision = snapshot->layout_revision,
        .graph_revision = snapshot->graph_revision
    };
    const gridpico_result feedback =
        frontend->feedback(frontend->panel_context, messages, count, &version);
    return stop == GRIDPICO_OK ? feedback : stop;
}

static void clear_preview(gridpico_apc_physical *frontend)
{
    frontend->slot = APC_NO_SLOT;
    frontend->move_to = APC_NO_SLOT;
    frontend->candidate = 0;
    frontend->unassigned_selected = false;
    frontend->preview_revision = 0;
    frontend->preview_error = false;
    frontend->learn_after = 0;
    frontend->learn_ambiguous = false;
}

void gridpico_apc_physical_init(gridpico_apc_physical *frontend, void *session_context,
                               gridpico_apc_patch_read_fn read,
                               gridpico_apc_patch_set_cell_fn set_cell,
                               gridpico_apc_patch_assign_fn assign,
                               gridpico_apc_patch_move_fn move, void *panel_context,
                               gridpico_apc_patch_audition_fn audition,
                               gridpico_apc_physical_feedback_fn feedback,
                               gridpico_apc_patch_snapshot *scratch)
{
    *frontend = (gridpico_apc_physical){
        .session_context = session_context, .read = read, .set_cell = set_cell,
        .assign = assign, .move = move, .panel_context = panel_context,
        .audition = audition, .feedback = feedback, .scratch = scratch,
        .selected_row = APC_NO_SLOT, .selected_column = APC_NO_SLOT,
        .slot = APC_NO_SLOT, .move_to = APC_NO_SLOT
    };
}

gridpico_result gridpico_apc_physical_detach(gridpico_apc_physical *frontend)
{
    const gridpico_result result = stop_audition(frontend);
    frontend->audition_destination = 0;
    memset(frontend->held, 0, sizeof(frontend->held));
    memset(frontend->activity, 0, sizeof(frontend->activity));
    frontend->arranging = false;
    frontend->route_page_ready = false;
    frontend->action = GRIDPICO_APC_ARRANGE_NONE;
    frontend->selected_row = APC_NO_SLOT;
    frontend->selected_column = APC_NO_SLOT;
    clear_preview(frontend);
    return result;
}

static gridpico_result refresh_from_session(gridpico_apc_physical *frontend,
                                            gridpico_apc_patch_snapshot *snapshot, uint64_t now_ms)
{
    const gridpico_result read = read_snapshot(frontend, snapshot);
    return read == GRIDPICO_OK ? publish_page(frontend, snapshot, now_ms) : read;
}

gridpico_result gridpico_apc_physical_refresh(gridpico_apc_physical *frontend, uint64_t now_ms)
{
    return refresh_from_session(frontend, frontend->scratch, now_ms);
}

void gridpico_apc_physical_feedback_complete(
    gridpico_apc_physical *frontend, const gridpico_apc_physical_page_version *version)
{
    if (!frontend->arranging && version->route_page) {
        frontend->visible_layout = version->layout_revision;
        frontend->visible_graph = version->graph_revision;
        frontend->route_page_ready = true;
    }
}

static bool slot_button(uint8_t note, gridpico_patch_direction *axis, uint8_t *slot)
{
    if (note >= APC_SCENE_FIRST && note < APC_SCENE_FIRST + GRIDPICO_PATCH_SOURCE_ROWS) {
        *axis = GRIDPICO_PATCH_INPUT;
        *slot = (uint8_t)(note - APC_SCENE_FIRST);
        return true;
    }
    if (note >= APC_TRACK_FIRST && note < APC_TRACK_FIRST + GRIDPICO_PATCH_DESTINATION_COLUMNS) {
        *axis = GRIDPICO_PATCH_OUTPUT;
        *slot = (uint8_t)(note - APC_TRACK_FIRST);
        return true;
    }
    return false;
}

static void browse(gridpico_apc_physical *frontend, const gridpico_apc_patch_snapshot *snapshot,
                   bool forward)
{
    gridpico_port_id choices[GRIDPICO_PATCH_MAX_PORTS + 1] = {0};
    size_t count = 1;
    size_t current = frontend->unassigned_selected ? 0 : SIZE_MAX;
    for (size_t i = 0; i < snapshot->port_count; ++i) {
        const gridpico_apc_patch_port *port = &snapshot->ports[i];
        if (can_assign(frontend, snapshot, port)) {
            if (port->id == frontend->candidate) {
                current = count;
            }
            choices[count++] = port->id;
        }
    }
    if (current == SIZE_MAX) {
        /* An empty slot can preview the first port, but cannot clear itself
         * until the operator explicitly browses to Unassigned. */
        if (forward && count == 1) {
            return;
        }
        current = forward ? 1 : 0;
    } else if (forward && current + 1 < count) {
        ++current;
    } else if (!forward && current > 0) {
        --current;
    }
    frontend->candidate = choices[current];
    frontend->unassigned_selected = current == 0;
    frontend->preview_error = false;
}

static gridpico_result choose_slot(gridpico_apc_physical *frontend,
                                   const gridpico_apc_patch_snapshot *snapshot,
                                   gridpico_patch_direction axis, uint8_t slot)
{
    const gridpico_port_id port = axis_ports(snapshot, axis)[slot];
    switch (frontend->action) {
    case GRIDPICO_APC_ARRANGE_ASSIGN:
        if (frontend->slot == APC_NO_SLOT) {
            frontend->axis = axis;
            frontend->slot = slot;
            frontend->preview_revision = snapshot->layout_revision;
            frontend->candidate = port;
            frontend->preview_error = false;
            if (port == 0) {
                browse(frontend, snapshot, true);
            }
        }
        break;
    case GRIDPICO_APC_ARRANGE_MOVE:
        if ((frontend->slot == APC_NO_SLOT ||
             frontend->preview_revision != snapshot->layout_revision) && port != 0) {
            clear_preview(frontend);
            frontend->axis = axis;
            frontend->slot = slot;
            frontend->preview_revision = snapshot->layout_revision;
        } else if (axis == frontend->axis && frontend->slot != APC_NO_SLOT && slot != frontend->slot) {
            frontend->move_to = slot;
            frontend->preview_error = false;
        }
        break;
    case GRIDPICO_APC_ARRANGE_LEARN:
        if (axis == GRIDPICO_PATCH_INPUT) {
            clear_preview(frontend);
            frontend->axis = axis;
            frontend->slot = slot;
            frontend->preview_revision = snapshot->layout_revision;
            frontend->learn_after = snapshot->attack_sequence;
        }
        break;
    case GRIDPICO_APC_ARRANGE_AUDITION:
        if (axis == GRIDPICO_PATCH_OUTPUT) {
            const gridpico_result stopped = stop_audition(frontend);
            if (stopped != GRIDPICO_OK) {
                return stopped;
            }
            frontend->axis = axis;
            frontend->slot = slot;
            frontend->preview_revision = snapshot->layout_revision;
            frontend->candidate = port;
            frontend->preview_error = false;
        }
        break;
    case GRIDPICO_APC_ARRANGE_NONE:
        break;
    }
    return GRIDPICO_OK;
}

static gridpico_result start_audition(gridpico_apc_physical *frontend,
                                     const gridpico_apc_patch_snapshot *snapshot)
{
    const gridpico_apc_patch_port *port = find_port(snapshot, frontend->candidate);
    if (frontend->audition == NULL || frontend->audition_destination != 0 ||
        frontend->preview_revision != snapshot->layout_revision ||
        port == NULL || !port->connected || port->direction != GRIDPICO_PATCH_OUTPUT) {
        frontend->preview_error = true;
        return GRIDPICO_UNAVAILABLE;
    }
    const gridpico_result result = frontend->audition(frontend->panel_context, port->id, true);
    frontend->preview_error = result != GRIDPICO_OK;
    if (result == GRIDPICO_OK) {
        frontend->audition_destination = port->id;
    }
    return result;
}

static gridpico_result confirm(gridpico_apc_physical *frontend,
                               gridpico_apc_patch_snapshot *snapshot, uint64_t now_ms)
{
    observe_learn(frontend, snapshot);
    if (!can_confirm(frontend, snapshot)) {
        const bool stale = frontend->preview_revision != snapshot->layout_revision;
        frontend->preview_error = true;
        const gridpico_result feedback = publish_page(frontend, snapshot, now_ms);
        return stale && frontend->slot != APC_NO_SLOT ? GRIDPICO_UNAVAILABLE : feedback;
    }

    const gridpico_result edit = frontend->action == GRIDPICO_APC_ARRANGE_MOVE
        ? frontend->move(frontend->session_context, frontend->preview_revision, frontend->axis,
                         frontend->slot, frontend->move_to)
        : frontend->assign(frontend->session_context, frontend->preview_revision, frontend->axis,
                           frontend->slot, frontend->candidate);
    if (edit == GRIDPICO_OK) {
        clear_preview(frontend);
    } else {
        frontend->preview_error = true;
    }
    const gridpico_result refresh = refresh_from_session(frontend, snapshot, now_ms);
    return edit == GRIDPICO_OK ? refresh : edit;
}

static gridpico_result arrange_press(gridpico_apc_physical *frontend,
                                     gridpico_apc_patch_snapshot *snapshot,
                                     uint8_t note, uint64_t now_ms)
{
    gridpico_result action = GRIDPICO_OK;
    if (note >= APC_ASSIGN_PAD && note <= APC_AUDITION_PAD) {
        const gridpico_apc_arrange_action next = (gridpico_apc_arrange_action)
            (note - APC_ASSIGN_PAD + GRIDPICO_APC_ARRANGE_ASSIGN);
        if (next == GRIDPICO_APC_ARRANGE_AUDITION && next == frontend->action &&
            frontend->slot != APC_NO_SLOT) {
            action = start_audition(frontend, snapshot);
        } else {
            action = stop_audition(frontend);
            clear_preview(frontend);
            frontend->action = next;
        }
    } else if (note == APC_CONFIRM_PAD) {
        if (frontend->action == GRIDPICO_APC_ARRANGE_NONE ||
            frontend->action == GRIDPICO_APC_ARRANGE_AUDITION) {
            return GRIDPICO_OK;
        }
        return confirm(frontend, snapshot, now_ms);
    } else if (frontend->action == GRIDPICO_APC_ARRANGE_ASSIGN &&
               frontend->slot != APC_NO_SLOT && (note == APC_LEFT || note == APC_RIGHT)) {
        browse(frontend, snapshot, note == APC_RIGHT);
    } else {
        gridpico_patch_direction axis;
        uint8_t slot;
        if (!slot_button(note, &axis, &slot)) {
            return GRIDPICO_OK;
        }
        action = choose_slot(frontend, snapshot, axis, slot);
    }
    const gridpico_result feedback = publish_page(frontend, snapshot, now_ms);
    return action == GRIDPICO_OK ? feedback : action;
}

static gridpico_result patch_press(gridpico_apc_physical *frontend,
                                   gridpico_apc_patch_snapshot *snapshot,
                                   uint8_t note, uint64_t now_ms)
{
    if (note < GRIDPICO_PATCH_LED_FRAMES) {
        if (!frontend->route_page_ready || frontend->visible_layout != snapshot->layout_revision ||
            frontend->visible_graph != snapshot->graph_revision) {
            const gridpico_result feedback = publish_page(frontend, snapshot, now_ms);
            return feedback == GRIDPICO_OK ? GRIDPICO_UNAVAILABLE : feedback;
        }
        const uint8_t row = (uint8_t)(GRIDPICO_PATCH_SOURCE_ROWS - 1 -
                                      note / GRIDPICO_PATCH_DESTINATION_COLUMNS);
        const uint8_t column = (uint8_t)(note % GRIDPICO_PATCH_DESTINATION_COLUMNS);
        const gridpico_apc_patch_cell cell = snapshot->cells[row][column];
        if (!cell.compatible) {
            return GRIDPICO_OK;
        }
        const gridpico_result edit = frontend->set_cell(
            frontend->session_context, snapshot->layout_revision, snapshot->graph_revision,
            row, column, cell.state == GRIDPICO_PATCH_ROUTE_OFF);
        const gridpico_result refresh = refresh_from_session(frontend, snapshot, now_ms);
        return edit == GRIDPICO_OK ? refresh : edit;
    }
    gridpico_patch_direction axis;
    uint8_t slot;
    if (slot_button(note, &axis, &slot)) {
        if (axis == GRIDPICO_PATCH_INPUT) {
            frontend->selected_row = slot;
        } else {
            frontend->selected_column = slot;
        }
        return publish_page(frontend, snapshot, now_ms);
    }
    return GRIDPICO_OK;
}

gridpico_result gridpico_apc_physical_receive(
    gridpico_apc_physical *frontend, const gridpico_apc_surface_message *message, uint64_t now_ms)
{
    uint8_t note = 0;
    const gridpico_apc_pad_event event = gridpico_apc_decode_button(message, &note);
    if (event == GRIDPICO_APC_PAD_INVALID) {
        return GRIDPICO_INVALID;
    }
    if (event == GRIDPICO_APC_PAD_IGNORED) {
        return GRIDPICO_OK;
    }
    if (event == GRIDPICO_APC_PAD_RELEASED) {
        frontend->held[note] = false;
        return note == APC_AUDITION_PAD ? stop_audition(frontend) : GRIDPICO_OK;
    }
    if (frontend->held[note]) {
        return GRIDPICO_OK;
    }
    frontend->held[note] = true;

    if (note == APC_STOP_ALL) {
        const gridpico_result stopped = stop_audition(frontend);
        frontend->arranging = !frontend->arranging;
        frontend->route_page_ready = false;
        frontend->action = GRIDPICO_APC_ARRANGE_NONE;
        clear_preview(frontend);
        const gridpico_result refresh = gridpico_apc_physical_refresh(frontend, now_ms);
        return stopped == GRIDPICO_OK ? refresh : stopped;
    }
    gridpico_patch_direction axis;
    uint8_t slot;
    if (frontend->held[APC_SHIFT] ||
        (note >= GRIDPICO_PATCH_LED_FRAMES && !slot_button(note, &axis, &slot))) {
        return GRIDPICO_OK;
    }

    gridpico_apc_patch_snapshot *snapshot = frontend->scratch;
    const gridpico_result read = read_snapshot(frontend, snapshot);
    if (read != GRIDPICO_OK) {
        return read;
    }
    return frontend->arranging ? arrange_press(frontend, snapshot, note, now_ms)
                              : patch_press(frontend, snapshot, note, now_ms);
}
