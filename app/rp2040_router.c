#include "app/rp2040_router.h"

#include <string.h>

#ifdef GRIDPICO_PICO_GENERIC_MIDI
enum { WEB_KEYS = 2, WEB_EXPORT = 3, AUDITION_KEYS = 5 };
#else
enum {
    APC_KEYS = 1,
    WEB_KEYS,
    WEB_EXPORT,
    SYNTH_NOTES,
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    AUDITION_KEYS,
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    MIDI_B_IN,
    MIDI_B_OUT,
#endif
#endif
};
#endif

#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
enum { AUDITION_DEVICE = 4 };
#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
static const gridpico_local_registry registry = {
    .inventory = {
        .device_count = 2,
        .port_count = 3,
        .devices = {
            {2, "Computer", GRIDPICO_PATCH_DISCONNECTED},
            {AUDITION_DEVICE, "Audition", GRIDPICO_PATCH_DISCONNECTED},
        },
        .ports = {
            {WEB_KEYS, 2, "Computer MIDI In", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {WEB_EXPORT, 2, "Computer MIDI Out", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {AUDITION_KEYS, AUDITION_DEVICE, "Audition", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
        },
    },
    .port_capabilities = {GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED},
    .native_virtual_cable = {WEB_KEYS, WEB_EXPORT},
};
#else
static const gridpico_local_route routes[] = {
    {APC_KEYS, SYNTH_NOTES, GRIDPICO_LOCAL_MANAGED},
    {APC_KEYS, WEB_EXPORT, GRIDPICO_LOCAL_MANAGED},
    {WEB_KEYS, SYNTH_NOTES, GRIDPICO_LOCAL_MANAGED},
};

static const gridpico_local_registry registry = {
    .inventory = {
#ifdef GRIDPICO_RP2040_KEYS_BENCH
        .device_count = 4,
        .port_count = 7,
#elif defined(GRIDPICO_RP2040_PATCHBAY_BENCH)
        .device_count = 4,
        .port_count = 5,
#else
        .device_count = 3,
        .port_count = 4,
#endif
        .devices = {
            {1, "APC Key 25 mk2", GRIDPICO_PATCH_DISCONNECTED},
            {2, "Computer", GRIDPICO_PATCH_DISCONNECTED},
            {3, "MicroFreak", GRIDPICO_PATCH_DISCONNECTED},
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
            {AUDITION_DEVICE, "Audition", GRIDPICO_PATCH_DISCONNECTED},
#endif
        },
        .ports = {
            {APC_KEYS, 1, "APC Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
#ifdef GRIDPICO_RP2040_KEYS_BENCH
            {WEB_KEYS, 2, "MIDI A In", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {WEB_EXPORT, 2, "MIDI A Out", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
#else
            {WEB_KEYS, 2, "Web Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {WEB_EXPORT, 2, "Physical Keybed", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
#endif
            {SYNTH_NOTES, 3, "MicroFreak MIDI", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
            {AUDITION_KEYS, 4, "Audition", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
            {MIDI_B_IN, 2, "MIDI B In", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {MIDI_B_OUT, 2, "MIDI B Out", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
#endif
        },
    },
    .port_capabilities = {
        GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
#ifdef GRIDPICO_RP2040_KEYS_BENCH
        GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
#endif
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
        GRIDPICO_LOCAL_PORT_MANAGED,
#endif
        GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
    },
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    .native_virtual_cable = {WEB_KEYS, WEB_EXPORT},
#endif
};

#endif

static size_t note_queue_index(gridpico_pico_peer peer)
{
    return (size_t)(peer - GRIDPICO_PICO_WEB);
}

#ifdef GRIDPICO_RP2040_KEYS_BENCH
static bool is_native_output(gridpico_port_id destination)
{
    return destination == WEB_EXPORT || destination == MIDI_B_OUT;
}
#endif

static gridpico_pico_peer output_peer(const gridpico_pico_router *router, gridpico_port_id destination)
{
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    for (unsigned peer = GRIDPICO_PICO_DISCOVERED; destination != 0 && peer < GRIDPICO_PICO_PEERS; ++peer) {
        if (router->discovered[peer - GRIDPICO_PICO_DISCOVERED].output == destination) {
            return (gridpico_pico_peer)peer;
        }
    }
#else
    (void)router;
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    if (is_native_output(destination)) return GRIDPICO_PICO_WEB;
#else
    if (destination == WEB_EXPORT) return GRIDPICO_PICO_WEB;
#endif
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    return GRIDPICO_PICO_PEERS;
#else
    return destination == SYNTH_NOTES ? GRIDPICO_PICO_SYNTH : GRIDPICO_PICO_PEERS;
#endif
}

static gridpico_result deliver_note(void *context, const gridpico_note_delivery *delivery)
{
    gridpico_pico_router *router = context;
    const gridpico_connection destination = delivery->destination;
    const gridpico_pico_peer peer = output_peer(router, destination.endpoint);
    if (peer == GRIDPICO_PICO_PEERS || !router->attached[peer] ||
        router->generation[peer] != destination.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (peer == GRIDPICO_PICO_WEB && (router->keys_mode || router->keys_cutover)) return GRIDPICO_UNAVAILABLE;
#endif
    gridpico_pico_queue *queue = &router->notes[note_queue_index(peer)];
    const gridpico_note note = delivery->note;
    const bool attack = note.kind == GRIDPICO_NOTE_ON;
    if (queue->count == GRIDPICO_PICO_QUEUE_SIZE) return GRIDPICO_CAPACITY;
    const uint8_t code = attack ? 9u : 8u;
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    const uint8_t header = (uint8_t)(code | (destination.endpoint == MIDI_B_OUT ? 0x20u : 0u));
#else
    const uint8_t header = code;
#endif
    const size_t tail = (queue->head + queue->count) % GRIDPICO_PICO_QUEUE_SIZE;
    queue->packets[tail] = (gridpico_pico_packet){{header, (uint8_t)((code << 4) | note.channel_index),
                                                note.key, note.velocity}};
    ++queue->count;
    return GRIDPICO_OK;
}

static gridpico_result reject_raw(void *context, const gridpico_local_raw_delivery *delivery)
{
    (void)context;
    (void)delivery;
    return GRIDPICO_INVALID;
}

#ifndef GRIDPICO_RP2040_PATCHBAY_BENCH
static gridpico_result set_route(void *context, bool enabled)
{
    gridpico_pico_router *router = context;
    const gridpico_local_route_request request = {.route = routes[0], .enabled = enabled};
    const gridpico_result result = gridpico_patch_session_set_local_route(&router->session, &request);
    if (result != GRIDPICO_OK) gridpico_apc_frontend_refresh(&router->panels[0]);
    return result;
}
#endif

gridpico_route_snapshot gridpico_pico_router_pad_status(gridpico_pico_router *router)
{
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    (void)router;
    return (gridpico_route_snapshot){0};
#else
    gridpico_local_route_status status = {0};
    gridpico_patch_session_read_local_route(&router->session, &routes[0], &status);
    return (gridpico_route_snapshot){
        .applied_revision = status.revision,
        .configured = true,
        .active = status.state == GRIDPICO_PATCH_ROUTE_ACTIVE,
        .source = APC_KEYS,
        .destination = SYNTH_NOTES,
        .enabled = status.state != GRIDPICO_PATCH_ROUTE_OFF,
    };
#endif
}

#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
static const uint8_t audition_keys[] = {48, 55, 60, 67, 72};

static gridpico_result set_audition_port(gridpico_pico_router *router, bool connected)
{
    const gridpico_patch_port_update update = {
        .connection = {AUDITION_KEYS, router->audition.source_generation},
        .state = connected ? GRIDPICO_PATCH_CONNECTED : GRIDPICO_PATCH_DISCONNECTED,
    };
    return gridpico_patch_graph_update_port(&router->graph, &update);
}

static gridpico_local_route_request audition_route(gridpico_port_id destination, bool enabled)
{
    return (gridpico_local_route_request){{AUDITION_KEYS, destination, GRIDPICO_LOCAL_MANAGED}, enabled};
}

static void cancel_audition(gridpico_pico_router *router)
{
    gridpico_pico_audition *audition = &router->audition;
    if (audition->destination == 0 || audition->stopping) return;
    audition->stopping = true;
    audition->end_pending = false;
    (void)set_audition_port(router, false);
    const gridpico_local_route_request request = audition_route(audition->destination, false);
    if (gridpico_patch_graph_set_local_route(&router->graph, &request) == GRIDPICO_OK) {
        audition->destination = 0;
    }
}

static bool audition_target_current(gridpico_pico_router *router)
{
    const gridpico_pico_audition *audition = &router->audition;
    const gridpico_pico_peer sink = output_peer(router, audition->destination);
    if (sink == GRIDPICO_PICO_PEERS || !router->attached[sink] ||
        router->generation[sink] != audition->destination_generation ||
        !router->attached[audition->owner] || router->generation[audition->owner] != audition->owner_generation) {
        return false;
    }
    const gridpico_local_route_request request = audition_route(audition->destination, true);
    gridpico_local_route_status status;
    return gridpico_patch_session_read_local_route(&router->session, &request.route, &status) == GRIDPICO_OK &&
           status.state == GRIDPICO_PATCH_ROUTE_ACTIVE;
}

static gridpico_result emit_audition_note(gridpico_pico_router *router, gridpico_note note)
{
    const gridpico_ingress_note input = {
        .source = {AUDITION_KEYS, router->audition.source_generation},
        .note = note,
    };
    return gridpico_patch_graph_submit_note(&router->graph, &input);
}

static gridpico_result audition_request(void *context, gridpico_port_id destination,
                                       gridpico_apc_physical_audition_action action)
{
    gridpico_pico_patch_panel *panel = context;
    gridpico_pico_router *router = panel->router;
    gridpico_pico_audition *audition = &router->audition;
    const uint32_t generation = router->generation[panel->peer];
    if (action != GRIDPICO_APC_AUDITION_START) {
        if (audition->destination != destination || audition->owner != panel->peer ||
            audition->owner_generation != generation) return GRIDPICO_OK;
        if (action == GRIDPICO_APC_AUDITION_END && audition->end_pending &&
            audition_target_current(router)) {
            audition->end_pending = false;
            (void)emit_audition_note(router, audition->authored_end);
        }
        cancel_audition(router);
        return GRIDPICO_OK;
    }
    const gridpico_pico_peer sink = output_peer(router, destination);
    if (sink == GRIDPICO_PICO_PEERS) return GRIDPICO_INVALID;
    if (audition->destination != 0 || audition->source_generation == UINT32_MAX ||
        !router->attached[panel->peer] || !router->attached[sink]) return GRIDPICO_UNAVAILABLE;
    const gridpico_local_route_request request = audition_route(destination, true);
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    gridpico_patch_managed_enable_decision decision;
    const gridpico_result probe = gridpico_patch_graph_check_managed_enable(&router->graph, &request.route,
                                                                            &decision);
    if (probe != GRIDPICO_OK) return probe;
    if (decision != GRIDPICO_PATCH_MANAGED_ENABLE_ALLOWED) return GRIDPICO_CAPACITY;
#endif
    *audition = (gridpico_pico_audition){
        .destination = destination, .owner = panel->peer, .owner_generation = generation,
        .destination_generation = router->generation[sink],
        .source_generation = audition->source_generation + 1, .due_us = router->now_us,
    };
    gridpico_result result = set_audition_port(router, true);
    if (result == GRIDPICO_OK) result = gridpico_patch_graph_set_local_route(&router->graph, &request);
    if (result == GRIDPICO_OK && !audition_target_current(router)) result = GRIDPICO_UNAVAILABLE;
    if (result != GRIDPICO_OK) cancel_audition(router);
    return result;
}

static void advance_audition(gridpico_pico_router *router)
{
    gridpico_pico_audition *audition = &router->audition;
    if (audition->destination == 0 || audition->stopping || router->now_us < audition->due_us) return;
    if (!audition_target_current(router)) {
        cancel_audition(router);
        return;
    }

    const bool ending_step = audition->end_pending;
    const gridpico_note note = ending_step ? audition->authored_end :
        (gridpico_note){GRIDPICO_NOTE_ON, 0, audition_keys[audition->step], 96};
    audition->end_pending = false;
    if (emit_audition_note(router, note) != GRIDPICO_OK) {
        cancel_audition(router);
        return;
    }
    if (ending_step) {
        if (++audition->step == sizeof(audition_keys)) {
            cancel_audition(router);
            return;
        }
        audition->due_us = router->now_us + 50000u;
    } else {
        audition->authored_end = (gridpico_note){GRIDPICO_NOTE_OFF, 0, note.key, 0};
        audition->end_pending = true;
        audition->due_us = router->now_us + 150000u;
    }
}

static gridpico_revision feedback_generation(const gridpico_pico_router *router, gridpico_pico_peer peer)
{
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (peer == GRIDPICO_PICO_APC) return router->physical_feedback_generation;
    if (peer == GRIDPICO_PICO_WEB && !router->native_output_ready) return 0;
#endif
    return router->attached[peer] ? router->generation[peer] : 0;
}

#ifdef GRIDPICO_PICO_GENERIC_MIDI
void gridpico_pico_router_set_physical_feedback(gridpico_pico_router *router, gridpico_revision generation)
{
    if (router->physical_feedback_generation == generation) return;
    router->physical_feedback_generation = generation;
    gridpico_pico_patch_panel *panel = &router->panels[GRIDPICO_PICO_APC];
    panel->latest_valid = false;
    panel->pending = false;
    panel->awaiting_completion = false;
    panel->cursor = GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES;
    panel->frontend.route_page_ready = false;
    if (generation != 0) gridpico_apc_physical_refresh(&panel->frontend, router->now_us / 1000);
}
#endif

static void start_led_page(gridpico_pico_patch_panel *panel)
{
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (panel->router->apc_placement_end != 0) return;
#endif
    if (panel->pending && !panel->awaiting_completion && panel->cursor == GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES) {
        memcpy(panel->sending, panel->latest, sizeof(panel->sending));
        panel->sending_version = panel->latest_version;
        panel->sending_generation = feedback_generation(panel->router, panel->peer);
        panel->cursor = 0;
        panel->pending = false;
    }
}

void gridpico_pico_router_offer_led_page(gridpico_pico_router *router, gridpico_pico_peer peer)
{
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    if (router->keys.mode != GRIDPICO_APC_CHOOSER_PATCHBAY || router->keys.next_mode != GRIDPICO_APC_CHOOSER_NO_REQUEST) return;
#endif
    gridpico_pico_patch_panel *panel = &router->panels[peer];
    if (panel->latest_valid && panel->cursor == GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES) {
        panel->pending = true;
        start_led_page(panel);
    }
}

static gridpico_result publish_led_page(void *context, const gridpico_apc_surface_message *messages, size_t count,
                                      const gridpico_apc_physical_page_version *version)
{
    gridpico_pico_patch_panel *panel = context;
    if (count != GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES) return GRIDPICO_INVALID;
    if (feedback_generation(panel->router, panel->peer) == 0) return GRIDPICO_UNAVAILABLE;
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (panel->router->apc_placement_end != 0) return GRIDPICO_OK;
#endif

    bool changed = !panel->latest_valid || panel->latest_version.route_page != version->route_page ||
                   panel->latest_version.layout_revision != version->layout_revision ||
                   panel->latest_version.graph_revision != version->graph_revision;
    for (size_t i = 0; i < count; ++i) {
        changed |= memcmp(&panel->latest[i].bytes[1], messages[i].bytes, sizeof(messages[i].bytes)) != 0;
    }
    if (!changed) return GRIDPICO_OK;
    for (size_t i = 0; i < count; ++i) {
        gridpico_pico_packet *packet = &panel->latest[i];
        packet->bytes[0] = (uint8_t)(0x10u | (messages[i].bytes[0] >> 4));
        memcpy(&packet->bytes[1], messages[i].bytes, sizeof(messages[i].bytes));
    }
    panel->latest_valid = true;
    panel->latest_version = *version;
    panel->pending = true;
    start_led_page(panel);
    return GRIDPICO_OK;
}

static void reset_panel(gridpico_pico_router *router, gridpico_pico_peer peer)
{
    gridpico_pico_patch_panel *panel = &router->panels[peer];
    memset(panel, 0, sizeof(*panel));
    panel->router = router;
    panel->peer = peer;
    panel->cursor = GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES;
    gridpico_apc_physical_init(&panel->frontend, &router->session, gridpico_patch_session_read_apc_patch,
                              gridpico_patch_session_set_apc_patch_cell, gridpico_patch_session_assign_apc_patch,
                              gridpico_patch_session_move_apc_patch, panel, audition_request, publish_led_page,
                              &router->panel_scratch);
}

static void refresh_panels(gridpico_pico_router *router)
{
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (router->keys_mode || router->keys_cutover) return;
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    if (router->keys.mode != GRIDPICO_APC_CHOOSER_PATCHBAY || router->keys.next_mode != GRIDPICO_APC_CHOOSER_NO_REQUEST) return;
#endif
    for (gridpico_pico_peer peer = GRIDPICO_PICO_APC; peer <= GRIDPICO_PICO_WEB; ++peer) {
        if (feedback_generation(router, peer) != 0) {
            gridpico_apc_physical_refresh(&router->panels[peer].frontend, router->now_us / 1000);
        }
    }
}

#ifdef GRIDPICO_PICO_GENERIC_MIDI
void gridpico_pico_router_commit_keys_mode(gridpico_pico_router *router, bool keys_mode)
{
    gridpico_pico_queue *queue = &router->notes[note_queue_index(GRIDPICO_PICO_WEB)];
    queue->head = queue->count = 0;
    for (gridpico_pico_peer peer = GRIDPICO_PICO_APC; peer <= GRIDPICO_PICO_WEB; ++peer) {
        (void)gridpico_apc_physical_detach(&router->panels[peer].frontend);
        reset_panel(router, peer);
    }
    router->keys_mode = keys_mode;
    router->keys_cutover = false;
    refresh_panels(router);
}

bool gridpico_pico_router_queue_keys_packet(gridpico_pico_router *router, const gridpico_pico_packet *packet)
{
    if (!router->keys_mode || router->keys_cutover || !router->native_output_ready) return false;
    gridpico_pico_queue *queue = &router->notes[note_queue_index(GRIDPICO_PICO_WEB)];
    if (queue->count == GRIDPICO_PICO_QUEUE_SIZE) return false;
    const size_t tail = (queue->head + queue->count) % GRIDPICO_PICO_QUEUE_SIZE;
    queue->packets[tail] = *packet;
    ++queue->count;
    return true;
}
#endif

void gridpico_pico_router_tick(gridpico_pico_router *router, uint64_t now_us)
{
    router->now_us = now_us;
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (!router->keys_cutover && !router->keys_mode)
#endif
        advance_audition(router);
    if (now_us >= router->next_refresh_us) {
        router->next_refresh_us = now_us + 50000;
        refresh_panels(router);
    }
}
#else
static gridpico_route_snapshot read_route(void *context)
{
    return gridpico_pico_router_pad_status(context);
}

static void publish_led(void *context, const gridpico_apc_surface_message *message)
{
    gridpico_pico_router *router = context;
    router->led = (gridpico_pico_packet){{0x19, message->bytes[0], message->bytes[1], message->bytes[2]}};
    router->led_pending[GRIDPICO_PICO_APC] = router->attached[GRIDPICO_PICO_APC];
    router->led_pending[GRIDPICO_PICO_WEB] = router->attached[GRIDPICO_PICO_WEB];
}

static void reset_panel(gridpico_pico_router *router, gridpico_pico_peer peer)
{
    gridpico_apc_frontend_init(&router->panels[peer], router, read_route, set_route, router, publish_led);
}
#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
_Static_assert(GRIDPICO_PATCH_MAX_PORTS <= UINT8_MAX, "placement intervals fit byte indices");

gridpico_result gridpico_pico_router_register_device(gridpico_pico_router *router,
                                                    gridpico_pico_peer first_peer, const char *name,
                                                    uint8_t inputs, uint8_t outputs)
{
    const uint8_t cables = inputs | outputs;
    const unsigned span = cables > 1 ? 2u : 1u;
    if (router == NULL || name == NULL || name[0] == '\0' || cables == 0 || cables > 3 ||
        first_peer < GRIDPICO_PICO_DISCOVERED || first_peer > GRIDPICO_PICO_PEERS - span) {
        return GRIDPICO_INVALID;
    }
    const size_t first_slot = (size_t)(first_peer - GRIDPICO_PICO_DISCOVERED);
    for (size_t i = 0; i < span; ++i) {
        if (router->discovered[first_slot + i].input != 0 || router->discovered[first_slot + i].output != 0) {
            return GRIDPICO_CAPACITY;
        }
    }
    size_t length = 0;
    while (length < GRIDPICO_PATCH_NAME_BYTES && name[length] != '\0') ++length;
    if (length == GRIDPICO_PATCH_NAME_BYTES) return GRIDPICO_INVALID;

    gridpico_local_graph_state *state = &router->projection_scratch.graph;
    gridpico_result result = gridpico_patch_graph_read_local_state(&router->graph, state);
    if (result != GRIDPICO_OK) return result;
    const gridpico_patch_inventory *inventory = &state->registry.inventory;
    gridpico_patch_device device = {.state = GRIDPICO_PATCH_DISCONNECTED};
    for (gridpico_device_id id = 1; device.id == 0; ++id) {
        size_t i = 0;
        while (i < inventory->device_count && inventory->devices[i].id != id) ++i;
        if (i == inventory->device_count) device.id = id;
    }
    memcpy(device.name, name, length);

    gridpico_patch_port ports[2 * GRIDPICO_PICO_USB_CABLES] = {0};
    uint8_t cable_for_port[2 * GRIDPICO_PICO_USB_CABLES];
    size_t count = 0;
    gridpico_port_id next_id = 1;
    for (uint8_t cable = 0; cable < span; ++cable) {
        for (unsigned output = 0; output < 2; ++output) {
            const uint8_t mask = output ? outputs : inputs;
            if ((mask & (1u << cable)) == 0) continue;
            gridpico_patch_port *port = &ports[count];
            for (; port->id == 0; ++next_id) {
                size_t i = 0;
                while (i < inventory->port_count && inventory->ports[i].id != next_id) ++i;
                if (i == inventory->port_count) port->id = next_id;
            }
            port->device = device.id;
            port->direction = output ? GRIDPICO_PATCH_OUTPUT : GRIDPICO_PATCH_INPUT;
            port->state = GRIDPICO_PATCH_DISCONNECTED;
            port->patch_eligible = true;
            const size_t prefix = length < GRIDPICO_PATCH_NAME_BYTES - 7u ? length : GRIDPICO_PATCH_NAME_BYTES - 7u;
            memcpy(port->name, name, prefix);
            const char *suffix = output ? " Out" : " In";
            const size_t suffix_length = output ? 4u : 3u;
            memcpy(port->name + prefix, suffix, suffix_length);
            if (span > 1) {
                port->name[prefix + suffix_length] = ' ';
                port->name[prefix + suffix_length + 1u] = (char)('1' + cable);
            }
            cable_for_port[count++] = cable;
        }
    }
    const size_t first_port = inventory->port_count;
    result = gridpico_patch_graph_append_managed_device(&router->graph, &device, ports, count);
    if (result != GRIDPICO_OK) return result;
    for (size_t i = 0; i < count; ++i) {
        const size_t slot = first_slot + cable_for_port[i];
        if (ports[i].direction == GRIDPICO_PATCH_INPUT) {
            result = gridpico_patch_session_add_apc_attack_source(&router->session, first_port + i, ports[i].id);
            if (result != GRIDPICO_OK) return result;
            router->discovered[slot].input = ports[i].id;
            router->discovered[slot].input_generation = 1;
        } else {
            router->discovered[slot].output = ports[i].id;
        }
    }
    if (router->apc_placement_end == 0) router->apc_placement_first = (uint8_t)first_port;
    router->apc_placement_end = (uint8_t)(first_port + count);
    return GRIDPICO_OK;
}

static bool placement_withheld(const gridpico_pico_router *router, const gridpico_patch_port *port,
                               uint32_t mask)
{
    for (size_t slot = 0; mask != 0 && slot < GRIDPICO_PICO_USB_CABLE_GROUPS; ++slot, mask >>= 1) {
        const gridpico_port_id id = port->direction == GRIDPICO_PATCH_INPUT
            ? router->discovered[slot].input : router->discovered[slot].output;
        if ((mask & 1u) != 0 && id == port->id) return true;
    }
    return false;
}

static bool place_in_first_free(gridpico_port_id *positions, size_t count, gridpico_port_id port)
{
    size_t free = count;
    for (size_t slot = 0; slot < count; ++slot) {
        if (positions[slot] == port) return false;
        if (positions[slot] == 0 && free == count) free = slot;
    }
    if (free == count) return false;
    positions[free] = port;
    return true;
}

gridpico_result gridpico_pico_router_place_discovered(
    gridpico_pico_router *router, gridpico_revision graph_revision, gridpico_revision layout_revision,
    uint32_t blocked_inputs, uint32_t blocked_outputs)
{
    if (router->apc_placement_end == 0) return GRIDPICO_OK;
    gridpico_patch_session *session = &router->session;
    if (layout_revision == 0 || layout_revision != session->apc_layout_revision ||
        graph_revision != router->graph.revision) return GRIDPICO_UNAVAILABLE;

    gridpico_apc_patch_layout layout = session->apc_layout;
    size_t edits = 0;
    for (size_t slot = router->apc_placement_first; slot < router->apc_placement_end; ++slot) {
        const gridpico_patch_port *port = &router->graph.inventory.ports[slot];
        if (port->state != GRIDPICO_PATCH_CONNECTED || !port->patch_eligible ||
            router->graph.port_capabilities[slot] != GRIDPICO_LOCAL_PORT_MANAGED) continue;
        const bool input = port->direction == GRIDPICO_PATCH_INPUT;
        if (placement_withheld(router, port, input ? blocked_inputs : blocked_outputs)) continue;
        edits += input ? place_in_first_free(layout.rows, GRIDPICO_PATCH_SOURCE_ROWS, port->id)
                       : place_in_first_free(layout.columns, GRIDPICO_PATCH_DESTINATION_COLUMNS, port->id);
    }

    /* Preflight both axes and revision room before the first serialized Assign. */
    gridpico_result result = GRIDPICO_OK;
    if (edits > UINT64_MAX - layout_revision) {
        result = GRIDPICO_CAPACITY;
    } else {
        for (uint8_t slot = 0; slot < GRIDPICO_PATCH_SOURCE_ROWS; ++slot) {
            if (layout.rows[slot] == session->apc_layout.rows[slot]) continue;
            result = gridpico_patch_session_assign_apc_patch(session, layout_revision++,
                         GRIDPICO_PATCH_INPUT, slot, layout.rows[slot]);
            if (result != GRIDPICO_OK) return result;
        }
        for (uint8_t slot = 0; slot < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++slot) {
            if (layout.columns[slot] == session->apc_layout.columns[slot]) continue;
            result = gridpico_patch_session_assign_apc_patch(session, layout_revision++,
                         GRIDPICO_PATCH_OUTPUT, slot, layout.columns[slot]);
            if (result != GRIDPICO_OK) return result;
        }
    }
    /* Withheld control lanes and overflow remain outside the first window. */
    router->apc_placement_end = 0;
    refresh_panels(router);
    return result;
}
#endif

#ifdef GRIDPICO_RP2040_MATRIX_CLI
static gridpico_result read_routing_availability(
    void *context, const gridpico_local_registry *current,
    gridpico_local_port_availability out[GRIDPICO_PATCH_MAX_PORTS])
{
    (void)context;
    /* The Graph's directional states come from verified USB attachment updates.
     * Pico has no separate claimable socket pool.
     */
    for (size_t i = 0; i < current->inventory.port_count; ++i) {
        out[i] = current->inventory.ports[i].state == GRIDPICO_PATCH_CONNECTED
            ? GRIDPICO_LOCAL_IN_USE : GRIDPICO_LOCAL_UNAVAILABLE;
    }
    return GRIDPICO_OK;
}

static bool public_managed_route(const gridpico_local_route *route)
{
    return route->mode == GRIDPICO_LOCAL_MANAGED &&
           route->source != AUDITION_KEYS && route->destination != AUDITION_KEYS;
}

gridpico_result gridpico_pico_router_read_routing(void *context, gridpico_local_routing_snapshot *out)
{
    gridpico_pico_router *router = context;
    if (router == NULL) return GRIDPICO_INVALID;
    const gridpico_result result = gridpico_patch_session_read_local_routing(&router->session, out);
    if (result != GRIDPICO_OK) return result;

    gridpico_local_graph_state *graph = &out->graph;
    gridpico_patch_inventory *inventory = &graph->registry.inventory;
    size_t kept = 0;
    for (size_t i = 0; i < inventory->device_count; ++i) {
        if (inventory->devices[i].id == AUDITION_DEVICE) continue;
        inventory->devices[kept] = inventory->devices[i];
        out->colors[kept] = out->colors[i];
        ++kept;
    }
    inventory->device_count = kept;

    kept = 0;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (inventory->ports[i].id == AUDITION_KEYS) continue;
        inventory->ports[kept] = inventory->ports[i];
        graph->registry.port_capabilities[kept] = graph->registry.port_capabilities[i];
        out->availability[kept] = out->availability[i];
        ++kept;
    }
    inventory->port_count = kept;

    kept = 0;
    for (size_t i = 0; i < graph->route_count; ++i) {
        if (!public_managed_route(&graph->routes[i].route)) continue;
        graph->routes[kept] = graph->routes[i];
        ++kept;
    }
    graph->route_count = kept;
    return GRIDPICO_OK;
}

gridpico_result gridpico_pico_router_read_managed_route(
    void *context, const gridpico_local_route *route, gridpico_local_route_status *out)
{
    gridpico_pico_router *router = context;
    if (router == NULL || route == NULL || !public_managed_route(route)) return GRIDPICO_INVALID;
    return gridpico_patch_session_read_local_route(&router->session, route, out);
}

gridpico_result gridpico_pico_router_set_managed_route(
    void *context, const gridpico_local_route_request *request)
{
    gridpico_pico_router *router = context;
    if (router == NULL || request == NULL || !public_managed_route(&request->route)) return GRIDPICO_INVALID;
    return gridpico_patch_session_set_local_route(&router->session, request);
}
#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
gridpico_result gridpico_pico_router_commit_local_route(
    gridpico_pico_router *router, const gridpico_local_route_request *request)
{
    gridpico_patch_graph *graph = &router->graph;
    const gridpico_revision previous = graph->revision;
    const gridpico_result result = gridpico_patch_graph_set_local_route(graph, request);
    if (result == GRIDPICO_OK && graph->revision != previous &&
        request->route.mode == GRIDPICO_LOCAL_MANAGED && request->route.source != AUDITION_KEYS &&
        request->route.destination == router->audition.destination) {
        cancel_audition(router);
    }
    return result;
}

static gridpico_result set_session_route(void *context, const gridpico_local_route_request *request)
{
    gridpico_patch_graph *graph = context;
    return gridpico_pico_router_commit_local_route(graph->delivery_context, request);
}
#endif

#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
gridpico_result gridpico_pico_router_init_snapshot(gridpico_pico_router *router,
                                                  const gridpico_apc_session_snapshot *snapshot)
#else
gridpico_result gridpico_pico_router_init(gridpico_pico_router *router)
#endif
{
    static const gridpico_local_profile profile = {
#ifdef GRIDPICO_RP2040_KEYS_BENCH
        .midi_peer_limit = 7, .managed_branch_limit = 6, .managed_fanout_limit = 3,
#elif defined(GRIDPICO_PICO_GENERIC_MIDI)
        .midi_peer_limit = 3,
        .dynamic_device_limit = 2 + GRIDPICO_PICO_USB_DEVICES,
        .dynamic_port_limit = 3 + 2 * GRIDPICO_PICO_USB_CABLE_GROUPS,
#elif defined(GRIDPICO_RP2040_PATCHBAY_BENCH)
        .midi_peer_limit = 5, .managed_branch_limit = 4, .managed_fanout_limit = 2,
#else
        .midi_peer_limit = 4, .managed_branch_limit = 3, .managed_fanout_limit = 2,
#endif
    };
    memset(router, 0, sizeof(*router));
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    router->keys.mode = GRIDPICO_APC_CHOOSER_PATCHBAY;
    for (size_t i = 0; i < 3; ++i) router->keys.source_generation[i] = 1;
#endif
    for (size_t i = 0; i < GRIDPICO_PICO_PEERS; ++i) router->generation[i] = 1;
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    const gridpico_result result = gridpico_patch_graph_init_local_managed_storage(
        &router->graph, &registry, &profile, deliver_note, reject_raw, router);
#else
    const gridpico_result result = gridpico_patch_graph_init_local_profile(
        &router->graph, &registry, &profile, deliver_note, reject_raw, router);
#endif
    if (result != GRIDPICO_OK) return result;
    gridpico_patch_session_init(&router->session, &router->graph, gridpico_patch_graph_read_graph,
                              gridpico_patch_graph_set_route);
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    gridpico_patch_session_bind_projection_scratch(&router->session, &router->projection_scratch);
#endif
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    gridpico_patch_session_bind_manual_routes(&router->session, &router->manual_routes);
    gridpico_patch_session_bind_local_routes(&router->session, set_session_route,
                                            gridpico_patch_graph_read_local_route);
    gridpico_patch_session_enable_flat_managed_routes(&router->session);
#else
    gridpico_patch_session_bind_local_routes(&router->session, gridpico_patch_graph_set_local_route,
                                            gridpico_patch_graph_read_local_route);
#endif
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    const gridpico_apc_patch_layout layout = {0};
#else
    const gridpico_apc_patch_layout layout = {.rows = {APC_KEYS, WEB_KEYS},
                                            .columns = {SYNTH_NOTES, WEB_EXPORT}};
#endif
    router->audition.source_generation = 1;
    gridpico_patch_session_bind_local_routing(&router->session, gridpico_patch_graph_read_local_state,
#ifdef GRIDPICO_RP2040_MATRIX_CLI
                                             read_routing_availability,
#else
                                             NULL,
#endif
                                             NULL);
    const gridpico_result layout_result = gridpico_patch_session_bind_apc_patch(&router->session, &layout,
                                                                              AUDITION_KEYS);
    if (layout_result != GRIDPICO_OK) return layout_result;
#endif
    reset_panel(router, GRIDPICO_PICO_APC);
    reset_panel(router, GRIDPICO_PICO_WEB);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    if (snapshot != NULL) return gridpico_patch_session_restore_apc_snapshot(&router->session, snapshot);
#endif
#ifndef GRIDPICO_PICO_GENERIC_MIDI
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        const gridpico_local_route_request request = {.route = routes[i], .enabled = true};
        const gridpico_result enabled = gridpico_patch_session_set_local_route(&router->session, &request);
        if (enabled != GRIDPICO_OK) return enabled;
    }
#endif
    return GRIDPICO_OK;
}

#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
gridpico_result gridpico_pico_router_init(gridpico_pico_router *router)
{
    return gridpico_pico_router_init_snapshot(router, NULL);
}
#endif

static void update_port(gridpico_pico_router *router, gridpico_port_id port, gridpico_pico_peer peer)
{
    bool connected = router->attached[peer];
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (port == WEB_EXPORT) connected &= router->native_output_ready;
#endif
    const gridpico_patch_port_update update = {
        .connection = {port, router->generation[peer]},
        .state = connected ? GRIDPICO_PATCH_CONNECTED : GRIDPICO_PATCH_DISCONNECTED,
    };
    gridpico_patch_graph_update_port(&router->graph, &update);
}

#ifdef GRIDPICO_PICO_GENERIC_MIDI
static gridpico_result update_discovered_input(gridpico_pico_router *router, size_t slot, bool connected)
{
    const gridpico_patch_port_update update = {
        .connection = {router->discovered[slot].input, router->discovered[slot].input_generation},
        .state = connected ? GRIDPICO_PATCH_CONNECTED : GRIDPICO_PATCH_DISCONNECTED,
    };
    return gridpico_patch_graph_update_port(&router->graph, &update);
}

gridpico_result gridpico_pico_router_renew_managed_input(gridpico_pico_router *router, gridpico_pico_peer peer)
{
    if (peer < GRIDPICO_PICO_DISCOVERED || peer >= GRIDPICO_PICO_PEERS) return GRIDPICO_INVALID;
    const size_t slot = peer - GRIDPICO_PICO_DISCOVERED;
    if (!router->attached[peer] || router->discovered[slot].input == 0) return GRIDPICO_UNAVAILABLE;
    if (router->discovered[slot].input_generation == UINT32_MAX) return GRIDPICO_CAPACITY;
    gridpico_result result = update_discovered_input(router, slot, false);
    if (result != GRIDPICO_OK) return result;
    ++router->discovered[slot].input_generation;
    return update_discovered_input(router, slot, true);
}
#endif

#ifdef GRIDPICO_RP2040_KEYS_BENCH
enum { APC_KEY_STREAM, APC_PANEL_STREAM, A_STREAM, B_STREAM, WEB_PANEL_STREAM, KEYS_RELEASE_RESERVE = 80 };
static const gridpico_port_id managed_inputs[] = {APC_KEYS, WEB_KEYS, MIDI_B_IN};

static void set_managed_inputs(gridpico_pico_router *router)
{
    for (size_t i = 0; i < sizeof(managed_inputs) / sizeof(managed_inputs[0]); ++i) {
        const gridpico_pico_peer peer = i == 0 ? GRIDPICO_PICO_APC : GRIDPICO_PICO_WEB;
        const bool connected = router->attached[peer] && router->keys.mode == GRIDPICO_APC_CHOOSER_PATCHBAY &&
                               router->keys.next_mode == GRIDPICO_APC_CHOOSER_NO_REQUEST;
        if (connected == router->keys.source_connected[i]) continue;
        if (connected) {
            if (router->keys.source_generation[i] == UINT32_MAX) continue;
            ++router->keys.source_generation[i];
        }
        router->keys.source_connected[i] = connected;
        const gridpico_patch_port_update update = {
            .connection = {managed_inputs[i], router->keys.source_generation[i]},
            .state = connected ? GRIDPICO_PATCH_CONNECTED : GRIDPICO_PATCH_DISCONNECTED,
        };
        gridpico_patch_graph_update_port(&router->graph, &update);
    }
}

static bool enqueue_packet(gridpico_pico_queue *queue, const gridpico_pico_packet *packet, unsigned limit)
{
    if (queue->count >= limit) return false;
    queue->packets[(queue->head + queue->count) % GRIDPICO_PICO_QUEUE_SIZE] = *packet;
    ++queue->count;
    return true;
}

static void discard_pending_attacks(gridpico_pico_queue *queue)
{
    uint8_t kept = 0;
    for (uint8_t i = 0; i < queue->count; ++i) {
        const gridpico_pico_packet packet = queue->packets[(queue->head + i) % GRIDPICO_PICO_QUEUE_SIZE];
        const uint8_t kind = packet.bytes[1] >> 4;
        if ((kind == 9 && packet.bytes[3] != 0) ||
            (kind == 11 && packet.bytes[2] == 64 && packet.bytes[3] >= 64)) continue;
        queue->packets[(queue->head + kept++) % GRIDPICO_PICO_QUEUE_SIZE] = packet;
    }
    queue->count = kept;
}

static void retire_keys_holds(gridpico_pico_router *router, bool deliver_releases)
{
    gridpico_pico_queue *queue = &router->notes[note_queue_index(GRIDPICO_PICO_WEB)];
    for (size_t i = 0; i < GRIDPICO_PICO_KEYS_HOLDS; ++i) {
        gridpico_pico_keys_hold *hold = &router->keys.holds[i];
        if (hold->count == 0 || hold->blocked) continue;
        if (deliver_releases && hold->stream <= APC_PANEL_STREAM) {
            const bool sustain = (hold->status >> 4) == 11;
            const uint8_t kind = sustain ? 11u : 8u;
            const gridpico_pico_packet off = {{
                (uint8_t)((hold->stream == APC_PANEL_STREAM ? 0x10u : 0u) | kind),
                (uint8_t)((kind << 4) | (hold->status & 15u)), hold->data, 0,
            }};
            for (uint8_t n = 0; n < hold->count; ++n) {
                /* Ordinary traffic leaves room for every live Keys hold. */
                enqueue_packet(queue, &off, GRIDPICO_PICO_QUEUE_SIZE);
            }
        }
        hold->blocked = true;
    }
}

static void clear_peer_holds(gridpico_pico_router *router, gridpico_pico_peer peer)
{
    for (size_t i = 0; i < GRIDPICO_PICO_KEYS_HOLDS; ++i) {
        gridpico_pico_keys_hold *hold = &router->keys.holds[i];
        if ((hold->stream <= APC_PANEL_STREAM) == (peer == GRIDPICO_PICO_APC)) hold->count = 0;
    }
}

static void begin_mode_change(gridpico_pico_router *router, gridpico_apc_chooser_mode mode)
{
    if (mode == router->keys.mode || router->keys.next_mode != GRIDPICO_APC_CHOOSER_NO_REQUEST) return;
    router->keys.next_mode = mode;
    discard_pending_attacks(&router->notes[0]);
    discard_pending_attacks(&router->notes[1]);
    cancel_audition(router);
    set_managed_inputs(router);
    retire_keys_holds(router, router->keys.mode == GRIDPICO_APC_CHOOSER_KEYS && router->attached[GRIDPICO_PICO_WEB]);
}

static uint8_t voice_length(const gridpico_pico_packet *packet)
{
    const uint8_t kind = packet->bytes[1] >> 4;
    if (kind < 8 || kind > 14 || (packet->bytes[0] & 15u) != kind || packet->bytes[2] > 127) return 0;
    if (kind == 12 || kind == 13) return packet->bytes[3] == 0 ? 2u : 0u;
    return packet->bytes[3] <= 127 ? 3u : 0u;
}

static void chooser_feedback(gridpico_pico_router *router, bool open)
{
    gridpico_pico_queue *queue = &router->keys.apc_queue;
    if (!router->attached[GRIDPICO_PICO_APC] || queue->count > GRIDPICO_PICO_QUEUE_SIZE - 2) return;
    if (!open && router->keys.mode == GRIDPICO_APC_CHOOSER_PATCHBAY) {
        gridpico_pico_patch_panel *panel = &router->panels[GRIDPICO_PICO_APC];
        panel->pending = panel->latest_valid;
        start_led_page(panel);
        return;
    }
    for (uint8_t index = 0; index < 2; ++index) {
        const bool active = (index == 0) == (router->keys.mode == GRIDPICO_APC_CHOOSER_PATCHBAY);
        gridpico_pico_packet packet = {{0x19, 0x96, (uint8_t)(0x20u + index), 0}};
        if (open) packet.bytes[3] = active ? 0x15 : 0x01;
        else if (router->keys.have_computer_pad[index]) packet = router->keys.computer_pad[index];
        enqueue_packet(queue, &packet, GRIDPICO_PICO_QUEUE_SIZE);
    }
}

static gridpico_pico_keys_hold *find_hold(gridpico_pico_keys *keys, uint8_t stream, uint8_t status,
                                        uint8_t data, unsigned *live)
{
    gridpico_pico_keys_hold *free_slot = NULL, *match = NULL;
    *live = 0;
    for (size_t i = 0; i < GRIDPICO_PICO_KEYS_HOLDS; ++i) {
        gridpico_pico_keys_hold *hold = &keys->holds[i];
        if (hold->count == 0) {
            if (free_slot == NULL) free_slot = hold;
        } else {
            if (!hold->blocked) *live += hold->count;
            if (hold->stream == stream && hold->status == status && hold->data == data) match = hold;
        }
    }
    return match != NULL ? match : free_slot;
}

/* Returns whether this packet still belongs to the existing Patchbay input. */
static bool dispatch_keys(gridpico_pico_router *router, gridpico_pico_peer peer, const gridpico_pico_packet *packet,
                          gridpico_result *result)
{
    const uint8_t cable = packet->bytes[0] >> 4;
    const uint8_t length = voice_length(packet);
    if (length == 0 || cable > (peer == GRIDPICO_PICO_APC ? 1u : 2u)) {
        *result = GRIDPICO_INVALID;
        return false;
    }
    gridpico_pico_keys *keys = &router->keys;
    *result = GRIDPICO_OK;
    if (peer == GRIDPICO_PICO_APC) {
        const bool was_open = keys->chooser.open;
        const gridpico_connection connection = {APC_KEYS, router->generation[peer]};
        const gridpico_apc_chooser_result action = gridpico_apc_chooser_receive(
            &keys->chooser, connection, cable, &packet->bytes[1], length);
        if (was_open != keys->chooser.open) chooser_feedback(router, keys->chooser.open);
        if (action.requested_mode != GRIDPICO_APC_CHOOSER_NO_REQUEST) begin_mode_change(router, action.requested_mode);
        if (action.consumed) return false;
    }
    const bool raw = keys->mode == GRIDPICO_APC_CHOOSER_KEYS;
    const bool changing = keys->next_mode != GRIDPICO_APC_CHOOSER_NO_REQUEST;
    if (raw && peer == GRIDPICO_PICO_WEB) {
        if (cable != 1 || changing) return false;
        if (!router->attached[GRIDPICO_PICO_APC] ||
            !enqueue_packet(&keys->apc_queue, packet, GRIDPICO_PICO_QUEUE_SIZE - 2u)) {
            *result = GRIDPICO_UNAVAILABLE;
        } else if ((packet->bytes[1] == 0x96 || packet->bytes[1] == 0x90 ||
                    packet->bytes[1] == 0x86 || packet->bytes[1] == 0x80) &&
                   (packet->bytes[2] == 0x20 || packet->bytes[2] == 0x21)) {
            const size_t pad = packet->bytes[2] - 0x20u;
            keys->computer_pad[pad] = *packet;
            keys->have_computer_pad[pad] = true;
        }
        return false;
    }

    const uint8_t kind = packet->bytes[1] >> 4;
    const bool sustain = kind == 11 && packet->bytes[2] == 64;
    const bool note = kind == 8 || kind == 9;
    const bool tracked = note || sustain;
    const bool attack = sustain ? packet->bytes[3] >= 64 : kind == 9 && packet->bytes[3] != 0;
    const uint8_t stream = peer == GRIDPICO_PICO_APC ? (uint8_t)(APC_KEY_STREAM + cable)
                         : cable == 0 ? A_STREAM : cable == 2 ? B_STREAM : WEB_PANEL_STREAM;
    unsigned live;
    gridpico_pico_keys_hold *hold = tracked ? find_hold(keys, stream,
        (uint8_t)((sustain ? 0xb0u : 0x90u) | (packet->bytes[1] & 15u)), packet->bytes[2], &live) : NULL;
    if (tracked && hold == NULL) {
        *result = GRIDPICO_CAPACITY;
        return false;
    }
    if (hold != NULL && hold->count != 0 && hold->blocked) {
        if (!attack && (sustain || --hold->count == 0)) hold->count = 0;
        return false;
    }
    if (changing && !attack) return false;
    if (raw && !changing) {
        if (!router->attached[GRIDPICO_PICO_WEB]) {
            if (tracked && attack && hold->count == 0) {
                *hold = (gridpico_pico_keys_hold){
                    .stream = stream, .status = (uint8_t)((sustain ? 0xb0u : 0x90u) | (packet->bytes[1] & 15u)),
                    .data = packet->bytes[2], .count = 1, .blocked = true,
                };
            }
            return false;
        }
        if (tracked && !attack && hold->count == 0) return false;
        if (tracked && attack && (!sustain || hold->count == 0) && live >= KEYS_RELEASE_RESERVE) {
            *result = GRIDPICO_CAPACITY;
            return false;
        }
        const unsigned limit = tracked && !attack ? GRIDPICO_PICO_QUEUE_SIZE
                                                   : GRIDPICO_PICO_QUEUE_SIZE - KEYS_RELEASE_RESERVE;
        if (!enqueue_packet(&router->notes[0], packet, limit)) {
            *result = GRIDPICO_CAPACITY;
            return false;
        }
    }
    if (hold != NULL) {
        if (attack) {
            if (hold->count == 0) {
                *hold = (gridpico_pico_keys_hold){
                    .stream = stream, .status = (uint8_t)((sustain ? 0xb0u : 0x90u) | (packet->bytes[1] & 15u)),
                    .data = packet->bytes[2], .count = 1, .blocked = changing,
                };
            } else if (!sustain && (raw || stream == APC_KEY_STREAM || stream == A_STREAM || stream == B_STREAM) &&
                       hold->count < UINT8_MAX) {
                ++hold->count;
            }
        } else if (hold->count != 0) {
            if (sustain) hold->count = 0;
            else --hold->count;
        } else if (raw) return false;
    }
    return !changing && !raw;
}

void gridpico_pico_router_finish_mode(gridpico_pico_router *router, bool native_idle, bool apc_idle, bool synth_idle)
{
    if (router->keys.next_mode == GRIDPICO_APC_CHOOSER_NO_REQUEST) return;
    if (!native_idle || !apc_idle || !synth_idle || router->notes[0].count != 0 ||
        router->notes[1].count != 0 || router->keys.apc_queue.count != 0) return;

    if (router->attached[GRIDPICO_PICO_WEB]) {
        update_port(router, WEB_EXPORT, GRIDPICO_PICO_WEB);
        update_port(router, MIDI_B_OUT, GRIDPICO_PICO_WEB);
    }
    if (router->attached[GRIDPICO_PICO_SYNTH]) update_port(router, SYNTH_NOTES, GRIDPICO_PICO_SYNTH);
    if (router->notes[0].count != 0 || router->notes[1].count != 0) return;

    router->keys.mode = router->keys.next_mode;
    router->keys.next_mode = GRIDPICO_APC_CHOOSER_NO_REQUEST;
    reset_panel(router, GRIDPICO_PICO_APC);
    reset_panel(router, GRIDPICO_PICO_WEB);
    set_managed_inputs(router);
    refresh_panels(router);
}
#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
void gridpico_pico_router_cancel_audition_owner(gridpico_pico_router *router, gridpico_pico_peer owner)
{
    if (router->audition.owner == owner) cancel_audition(router);
}

void gridpico_pico_router_retire_native_output(gridpico_pico_router *router)
{
    if (!router->native_output_ready) return;
    router->native_output_ready = false;
    gridpico_pico_queue *queue = &router->notes[note_queue_index(GRIDPICO_PICO_WEB)];
    queue->head = queue->count = 0;
    update_port(router, WEB_EXPORT, GRIDPICO_PICO_WEB);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    if (router->audition.owner == GRIDPICO_PICO_WEB || router->audition.destination == WEB_EXPORT) {
        cancel_audition(router);
    }
    if (router->attached[GRIDPICO_PICO_WEB]) {
        gridpico_apc_physical_detach(&router->panels[GRIDPICO_PICO_WEB].frontend);
        reset_panel(router, GRIDPICO_PICO_WEB);
    }
#endif
}
#endif

void gridpico_pico_router_attach(gridpico_pico_router *router, gridpico_pico_peer peer, bool attached)
{
    if (peer >= GRIDPICO_PICO_PEERS) return;
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (peer >= GRIDPICO_PICO_DISCOVERED) {
        const size_t slot = peer - GRIDPICO_PICO_DISCOVERED;
        if ((router->discovered[slot].input == 0 && router->discovered[slot].output == 0) ||
            (attached && router->discovered[slot].input_generation == UINT32_MAX)) return;
    }
#endif
    if (router->attached[peer] == attached) return;
    if (attached) {
        if (router->generation[peer] == UINT32_MAX) return;
        ++router->generation[peer];
    }
    router->attached[peer] = attached;
    if (peer != GRIDPICO_PICO_APC) {
        gridpico_pico_queue *queue = &router->notes[note_queue_index(peer)];
        queue->head = queue->count = 0;
#ifdef GRIDPICO_PICO_GENERIC_MIDI
        if (peer >= GRIDPICO_PICO_DISCOVERED) {
            const size_t slot = (size_t)(peer - GRIDPICO_PICO_DISCOVERED);
            if (router->discovered[slot].output != 0) update_port(router, router->discovered[slot].output, peer);
            if (router->discovered[slot].input != 0) {
                if (attached) ++router->discovered[slot].input_generation;
                (void)update_discovered_input(router, slot, attached);
            }
        } else if (attached) {
            router->native_output_ready = true;
            update_port(router, WEB_EXPORT, peer);
        } else {
            gridpico_pico_router_retire_native_output(router);
        }
#else
        {
            update_port(router, peer == GRIDPICO_PICO_WEB ? WEB_EXPORT : SYNTH_NOTES, peer);
#ifdef GRIDPICO_RP2040_KEYS_BENCH
            if (peer == GRIDPICO_PICO_WEB) update_port(router, MIDI_B_OUT, GRIDPICO_PICO_WEB);
#endif
        }
#endif
    }
    if (peer == GRIDPICO_PICO_APC || peer == GRIDPICO_PICO_WEB) {
#ifndef GRIDPICO_RP2040_PATCHBAY_BENCH
        reset_panel(router, peer);
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
        set_managed_inputs(router);
        if (peer == GRIDPICO_PICO_APC) {
            const gridpico_connection connection = {APC_KEYS, router->generation[peer]};
            if (attached) {
                gridpico_apc_chooser_init(&router->keys.chooser, connection);
            } else {
                gridpico_apc_chooser_detach(&router->keys.chooser, connection);
                if (router->keys.next_mode != GRIDPICO_APC_CHOOSER_NO_REQUEST) {
                    router->keys.next_mode = router->keys.mode;
                }
                if (router->keys.mode == GRIDPICO_APC_CHOOSER_KEYS && router->attached[GRIDPICO_PICO_WEB]) {
                    discard_pending_attacks(&router->notes[0]);
                    retire_keys_holds(router, true);
                }
            }
        }
        if (!attached) {
            router->keys.apc_queue.count = 0;
            if (peer == GRIDPICO_PICO_WEB && router->keys.mode == GRIDPICO_APC_CHOOSER_KEYS) {
                retire_keys_holds(router, false);
                memset(router->keys.have_computer_pad, 0, sizeof(router->keys.have_computer_pad));
            }
            clear_peer_holds(router, peer);
        }
#elif defined(GRIDPICO_PICO_GENERIC_MIDI)
        if (peer == GRIDPICO_PICO_WEB) update_port(router, WEB_KEYS, peer);
#else
        update_port(router, peer == GRIDPICO_PICO_APC ? APC_KEYS : WEB_KEYS, peer);
#endif
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
        if (!attached) gridpico_apc_physical_detach(&router->panels[peer].frontend);
        reset_panel(router, peer);
#endif
    }
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    if (!attached && router->audition.destination != 0 &&
        (router->audition.owner == peer || output_peer(router, router->audition.destination) == peer)) {
        cancel_audition(router);
    }
    refresh_panels(router);
#else
    gridpico_apc_frontend_refresh(&router->panels[0]);
#endif
}

gridpico_result gridpico_pico_router_receive(
    gridpico_pico_router *router, gridpico_pico_peer peer, const gridpico_pico_packet *packet)
{
    if (peer >= GRIDPICO_PICO_PEERS || !router->attached[peer]) return GRIDPICO_UNAVAILABLE;
#ifndef GRIDPICO_PICO_GENERIC_MIDI
    if (peer == GRIDPICO_PICO_SYNTH) return GRIDPICO_UNAVAILABLE;
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    gridpico_result dispatched;
    if (!dispatch_keys(router, peer, packet, &dispatched)) return dispatched;
#endif
    const uint8_t cable = packet->bytes[0] >> 4;
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    const gridpico_port_id source = peer >= GRIDPICO_PICO_DISCOVERED ?
        router->discovered[peer - GRIDPICO_PICO_DISCOVERED].input : peer == GRIDPICO_PICO_WEB ? WEB_KEYS : 0;
    if ((peer >= GRIDPICO_PICO_DISCOVERED && cable != 0) || (cable == 0 && source == 0)) return GRIDPICO_INVALID;
#endif
    const uint8_t code = packet->bytes[0] & 0xfu;
    const uint8_t status = packet->bytes[1];
    if ((code != 8 && code != 9) || code != status >> 4 ||
#ifndef GRIDPICO_RP2040_KEYS_BENCH
        cable > 1 ||
#endif
        packet->bytes[2] > 127 || packet->bytes[3] > 127) return GRIDPICO_INVALID;
    if (cable == 1) {
        const gridpico_apc_surface_message message = {{status, packet->bytes[2], packet->bytes[3]}};
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
        const gridpico_result result = gridpico_apc_physical_receive(&router->panels[peer].frontend,
                                                                   &message, router->now_us / 1000);
        const gridpico_pico_peer other = peer == GRIDPICO_PICO_APC ? GRIDPICO_PICO_WEB : GRIDPICO_PICO_APC;
        if (feedback_generation(router, other) != 0) {
            gridpico_apc_physical_refresh(&router->panels[other].frontend, router->now_us / 1000);
        }
        return result;
#else
        return gridpico_apc_frontend_receive(&router->panels[peer], &message);
#endif
    }
    const gridpico_ingress_note note = {
#ifdef GRIDPICO_RP2040_KEYS_BENCH
        .source = {managed_inputs[peer == GRIDPICO_PICO_APC ? 0u : cable == 0 ? 1u : 2u],
                   router->keys.source_generation[peer == GRIDPICO_PICO_APC ? 0u : cable == 0 ? 1u : 2u]},
#elif defined(GRIDPICO_PICO_GENERIC_MIDI)
        .source = {source, peer >= GRIDPICO_PICO_DISCOVERED ?
            router->discovered[peer - GRIDPICO_PICO_DISCOVERED].input_generation : router->generation[peer]},
#else
        .source = {peer == GRIDPICO_PICO_APC ? APC_KEYS : WEB_KEYS, router->generation[peer]},
#endif
        .note = {code == 9 && packet->bytes[3] != 0 ? GRIDPICO_NOTE_ON : GRIDPICO_NOTE_OFF,
                 (uint8_t)(status & 0xfu), packet->bytes[2], packet->bytes[3]},
    };
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    gridpico_patch_session_observe_apc_patch_note(&router->session, &note, router->now_us);
#endif
    return gridpico_patch_graph_submit_note(&router->graph, &note);
}

#ifdef GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH
bool gridpico_pico_router_can_program_change(
    gridpico_pico_router *router, gridpico_pico_peer peer, const gridpico_pico_packet *packet)
{
    if (peer != GRIDPICO_PICO_WEB || packet->bytes[0] != 0x0c || packet->bytes[1] != 0xc3 ||
        packet->bytes[2] > 0x7f || packet->bytes[3] != 0 ||
        !router->attached[GRIDPICO_PICO_WEB] || !router->attached[GRIDPICO_PICO_SYNTH] ||
        gridpico_pico_router_note(router, GRIDPICO_PICO_SYNTH) != NULL) {
        return false;
    }
    gridpico_local_route_status status;
    return gridpico_patch_session_read_local_route(&router->session, &routes[2], &status) == GRIDPICO_OK &&
           status.state == GRIDPICO_PATCH_ROUTE_ACTIVE;
}
#endif

const gridpico_pico_packet *gridpico_pico_router_note(
    const gridpico_pico_router *router, gridpico_pico_peer peer)
{
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    const gridpico_pico_queue *queue = peer == GRIDPICO_PICO_APC ? &router->keys.apc_queue
                                                               : &router->notes[note_queue_index(peer)];
#else
    if (peer == GRIDPICO_PICO_APC) return NULL;
    const gridpico_pico_queue *queue = &router->notes[note_queue_index(peer)];
#endif
    return queue->count == 0 ? NULL : &queue->packets[queue->head];
}

void gridpico_pico_router_note_sent(gridpico_pico_router *router, gridpico_pico_peer peer)
{
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    gridpico_pico_queue *queue = peer == GRIDPICO_PICO_APC ? &router->keys.apc_queue
                                                         : &router->notes[note_queue_index(peer)];
#else
    gridpico_pico_queue *queue = &router->notes[note_queue_index(peer)];
#endif
    queue->head = (uint8_t)((queue->head + 1) % GRIDPICO_PICO_QUEUE_SIZE);
    --queue->count;
}

const gridpico_pico_packet *gridpico_pico_router_led(
    const gridpico_pico_router *router, gridpico_pico_peer peer)
{
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    if (router->keys.mode != GRIDPICO_APC_CHOOSER_PATCHBAY || router->keys.next_mode != GRIDPICO_APC_CHOOSER_NO_REQUEST ||
        (peer == GRIDPICO_PICO_APC && router->keys.chooser.open)) return NULL;
#endif
    if (peer != GRIDPICO_PICO_APC && peer != GRIDPICO_PICO_WEB) return NULL;
    if (feedback_generation(router, peer) == 0) return NULL;
    const gridpico_pico_patch_panel *panel = &router->panels[peer];
    return panel->cursor < GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES ? &panel->sending[panel->cursor] : NULL;
#else
    return peer != GRIDPICO_PICO_SYNTH && router->led_pending[peer] ? &router->led : NULL;
#endif
}

void gridpico_pico_router_led_sent(gridpico_pico_router *router, gridpico_pico_peer peer)
{
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    gridpico_pico_patch_panel *panel = &router->panels[peer];
    if (++panel->cursor == GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES) panel->awaiting_completion = true;
#else
    router->led_pending[peer] = false;
#endif
}

#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
void gridpico_pico_router_led_complete(gridpico_pico_router *router, gridpico_pico_peer peer, bool delivered)
{
    gridpico_pico_patch_panel *panel = &router->panels[peer];
    const gridpico_revision generation = feedback_generation(router, peer);
    if (generation == 0 || panel->sending_generation != generation) return;
    if (!delivered) {
        panel->cursor = GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES;
        panel->pending = panel->latest_valid;
    } else if (panel->awaiting_completion) {
        gridpico_apc_physical_feedback_complete(&panel->frontend, &panel->sending_version);
    }
    panel->awaiting_completion = false;
    start_led_page(panel);
}
#endif
