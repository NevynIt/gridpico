#include "app/rp2040_router.h"

#include <string.h>

enum {
    APC_KEYS = 1,
    WEB_KEYS,
    WEB_EXPORT,
    SYNTH_NOTES,
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    AUDITION_KEYS,
    OCCURRENCES = 26,
    RELEASE_CREDITS = 51,
#else
    OCCURRENCES = 25,
    RELEASE_CREDITS = 50,
#endif
};

static const gridpico_local_route routes[] = {
    {APC_KEYS, SYNTH_NOTES, GRIDPICO_LOCAL_MANAGED},
    {APC_KEYS, WEB_EXPORT, GRIDPICO_LOCAL_MANAGED},
    {WEB_KEYS, SYNTH_NOTES, GRIDPICO_LOCAL_MANAGED},
};

static const gridpico_local_registry registry = {
    .inventory = {
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
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
            {4, "Audition", GRIDPICO_PATCH_DISCONNECTED},
#endif
        },
        .ports = {
            {APC_KEYS, 1, "APC Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {WEB_KEYS, 2, "Web Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {WEB_EXPORT, 2, "Physical Keybed", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {SYNTH_NOTES, 3, "MicroFreak MIDI", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
            {AUDITION_KEYS, 4, "Audition", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
#endif
        },
    },
    .port_capabilities = {
        GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
        GRIDPICO_LOCAL_PORT_MANAGED,
#endif
        GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
    },
};

static size_t note_queue_index(gridpico_pico_peer peer)
{
    return peer == GRIDPICO_PICO_WEB ? 0u : 1u;
}

static gridpico_result deliver_note(void *context, const gridpico_note_delivery *delivery)
{
    gridpico_pico_router *router = context;
    const gridpico_connection destination = delivery->destination;
    const gridpico_pico_peer peer = destination.endpoint == WEB_EXPORT ? GRIDPICO_PICO_WEB : GRIDPICO_PICO_SYNTH;
    if (!router->attached[peer] || router->generation[peer] != destination.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    gridpico_pico_queue *queue = &router->notes[note_queue_index(peer)];
    const gridpico_note note = delivery->note;
    const bool attack = note.kind == GRIDPICO_NOTE_ON;
    const size_t limit = GRIDPICO_PICO_QUEUE_SIZE - (attack ? RELEASE_CREDITS : 0u);
    if (queue->count >= limit) return GRIDPICO_CAPACITY;
    const uint8_t code = attack ? 9u : 8u;
    const size_t tail = (queue->head + queue->count) % GRIDPICO_PICO_QUEUE_SIZE;
    queue->packets[tail] = (gridpico_pico_packet){{code, (uint8_t)((code << 4) | note.channel_index),
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
}

#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
static const uint8_t audition_keys[] = {48, 55, 60, 67, 72};

static gridpico_pico_peer output_peer(gridpico_port_id destination)
{
    return destination == WEB_EXPORT ? GRIDPICO_PICO_WEB : GRIDPICO_PICO_SYNTH;
}

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

static void stop_audition(gridpico_pico_router *router)
{
    gridpico_pico_audition *audition = &router->audition;
    if (audition->destination == 0) return;
    if (!audition->stopping) {
        set_audition_port(router, false);
        audition->stopping = true;
    }
    const gridpico_local_route_request request = audition_route(audition->destination, false);
    if (gridpico_patch_session_set_local_route(&router->session, &request) == GRIDPICO_OK) {
        audition->destination = 0;
    } else {
        audition->due_us = router->now_us + 50000;
    }
}

static gridpico_result audition_request(void *context, gridpico_port_id destination, bool start)
{
    gridpico_pico_patch_panel *panel = context;
    gridpico_pico_router *router = panel->router;
    gridpico_pico_audition *audition = &router->audition;
    const uint32_t generation = router->generation[panel->peer];
    if (!start) {
        if (audition->owner == panel->peer && audition->owner_generation == generation) stop_audition(router);
        return GRIDPICO_OK;
    }
    if (destination != WEB_EXPORT && destination != SYNTH_NOTES) return GRIDPICO_INVALID;
    if (audition->destination != 0 || audition->source_generation == UINT32_MAX ||
        !router->attached[panel->peer] || !router->attached[output_peer(destination)]) return GRIDPICO_UNAVAILABLE;
    *audition = (gridpico_pico_audition){
        .destination = destination, .owner = panel->peer, .owner_generation = generation,
        .source_generation = audition->source_generation + 1, .due_us = router->now_us,
    };
    const gridpico_local_route_request request = audition_route(destination, true);
    gridpico_result result = set_audition_port(router, true);
    if (result == GRIDPICO_OK) result = gridpico_patch_session_set_local_route(&router->session, &request);
    gridpico_local_route_status status;
    if (result == GRIDPICO_OK &&
        (gridpico_patch_session_read_local_route(&router->session, &request.route, &status) != GRIDPICO_OK ||
         status.state != GRIDPICO_PATCH_ROUTE_ACTIVE)) result = GRIDPICO_UNAVAILABLE;
    if (result != GRIDPICO_OK) stop_audition(router);
    return result;
}

static void advance_audition(gridpico_pico_router *router)
{
    gridpico_pico_audition *audition = &router->audition;
    if (audition->destination == 0 || router->now_us < audition->due_us) return;
    if (audition->stopping || !router->attached[audition->owner] ||
        router->generation[audition->owner] != audition->owner_generation ||
        !router->attached[output_peer(audition->destination)]) {
        stop_audition(router);
        return;
    }
    const gridpico_ingress_note note = {
        .source = {AUDITION_KEYS, audition->source_generation},
        .note = {audition->sounding ? GRIDPICO_NOTE_OFF : GRIDPICO_NOTE_ON,
                 0, audition_keys[audition->step], audition->sounding ? 0 : 96},
    };
    if (!audition->sounding) {
        const gridpico_local_route_request request = audition_route(audition->destination, true);
        gridpico_local_route_status status;
        if (gridpico_patch_session_read_local_route(&router->session, &request.route, &status) != GRIDPICO_OK ||
            status.state != GRIDPICO_PATCH_ROUTE_ACTIVE) {
            stop_audition(router);
            return;
        }
    }
    if (gridpico_patch_graph_submit_note(&router->graph, &note) != GRIDPICO_OK) {
        stop_audition(router);
        return;
    }
    audition->sounding = !audition->sounding;
    if (!audition->sounding && ++audition->step == sizeof(audition_keys)) {
        stop_audition(router);
    } else {
        audition->due_us = router->now_us + (audition->sounding ? 150000u : 50000u);
    }
}

static void start_led_page(gridpico_pico_patch_panel *panel)
{
    if (panel->pending && !panel->awaiting_completion && panel->cursor == GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES) {
        memcpy(panel->sending, panel->latest, sizeof(panel->sending));
        panel->sending_version = panel->latest_version;
        panel->sending_generation = panel->router->generation[panel->peer];
        panel->cursor = 0;
        panel->pending = false;
    }
}

void gridpico_pico_router_offer_led_page(gridpico_pico_router *router, gridpico_pico_peer peer)
{
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
    if (!panel->router->attached[panel->peer]) return GRIDPICO_UNAVAILABLE;

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
    for (gridpico_pico_peer peer = GRIDPICO_PICO_APC; peer <= GRIDPICO_PICO_WEB; ++peer) {
        if (router->attached[peer]) {
            gridpico_apc_physical_refresh(&router->panels[peer].frontend, router->now_us / 1000);
        }
    }
}

void gridpico_pico_router_tick(gridpico_pico_router *router, uint64_t now_us)
{
    router->now_us = now_us;
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

gridpico_result gridpico_pico_router_init(gridpico_pico_router *router)
{
    static const gridpico_local_profile profile = {
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
        .midi_peer_limit = 5, .managed_branch_limit = 4, .managed_fanout_limit = 2,
#else
        .midi_peer_limit = 4, .managed_branch_limit = 3, .managed_fanout_limit = 2,
#endif
        .occurrence_limit = OCCURRENCES, .release_credit_limit = RELEASE_CREDITS,
    };
    memset(router, 0, sizeof(*router));
    for (size_t i = 0; i < GRIDPICO_PICO_PEERS; ++i) router->generation[i] = 1;
    const gridpico_result result = gridpico_patch_graph_init_local_profile(
        &router->graph, &registry, &profile, deliver_note, reject_raw, router);
    if (result != GRIDPICO_OK) return result;
    gridpico_patch_session_init(&router->session, &router->graph, gridpico_patch_graph_read_graph,
                              gridpico_patch_graph_set_route);
    gridpico_patch_session_bind_local_routes(&router->session, gridpico_patch_graph_set_local_route,
                                            gridpico_patch_graph_read_local_route);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    const gridpico_apc_patch_layout layout = {.rows = {APC_KEYS, WEB_KEYS},
                                            .columns = {SYNTH_NOTES, WEB_EXPORT}};
    router->audition.source_generation = 1;
    gridpico_patch_session_bind_local_routing(&router->session, gridpico_patch_graph_read_local_state, NULL, NULL);
    const gridpico_result layout_result = gridpico_patch_session_bind_apc_patch(&router->session, &layout,
                                                                              AUDITION_KEYS);
    if (layout_result != GRIDPICO_OK) return layout_result;
#endif
    reset_panel(router, GRIDPICO_PICO_APC);
    reset_panel(router, GRIDPICO_PICO_WEB);
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        const gridpico_local_route_request request = {.route = routes[i], .enabled = true};
        const gridpico_result enabled = gridpico_patch_session_set_local_route(&router->session, &request);
        if (enabled != GRIDPICO_OK) return enabled;
    }
    return GRIDPICO_OK;
}

static void update_port(gridpico_pico_router *router, gridpico_port_id port, gridpico_pico_peer peer)
{
    const gridpico_patch_port_update update = {
        .connection = {port, router->generation[peer]},
        .state = router->attached[peer] ? GRIDPICO_PATCH_CONNECTED : GRIDPICO_PATCH_DISCONNECTED,
    };
    gridpico_patch_graph_update_port(&router->graph, &update);
}

void gridpico_pico_router_attach(gridpico_pico_router *router, gridpico_pico_peer peer, bool attached)
{
    if (router->attached[peer] == attached) return;
    if (attached) {
        if (router->generation[peer] == UINT32_MAX) return;
        ++router->generation[peer];
    }
    router->attached[peer] = attached;
    if (peer != GRIDPICO_PICO_APC) {
        gridpico_pico_queue *queue = &router->notes[note_queue_index(peer)];
        queue->head = queue->count = 0;
        update_port(router, peer == GRIDPICO_PICO_WEB ? WEB_EXPORT : SYNTH_NOTES, peer);
    }
    if (peer != GRIDPICO_PICO_SYNTH) {
#ifndef GRIDPICO_RP2040_PATCHBAY_BENCH
        reset_panel(router, peer);
#endif
        update_port(router, peer == GRIDPICO_PICO_APC ? APC_KEYS : WEB_KEYS, peer);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
        if (!attached) gridpico_apc_physical_detach(&router->panels[peer].frontend);
        reset_panel(router, peer);
#endif
    }
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    if (!attached && router->audition.destination != 0 &&
        (router->audition.owner == peer || output_peer(router->audition.destination) == peer)) {
        stop_audition(router);
    }
    refresh_panels(router);
#else
    gridpico_apc_frontend_refresh(&router->panels[0]);
#endif
}

gridpico_result gridpico_pico_router_receive(
    gridpico_pico_router *router, gridpico_pico_peer peer, const gridpico_pico_packet *packet)
{
    if (peer == GRIDPICO_PICO_SYNTH || !router->attached[peer]) return GRIDPICO_UNAVAILABLE;
    const uint8_t cable = packet->bytes[0] >> 4;
    const uint8_t code = packet->bytes[0] & 0xfu;
    const uint8_t status = packet->bytes[1];
    if ((code != 8 && code != 9) || code != status >> 4 || cable > 1 ||
        packet->bytes[2] > 127 || packet->bytes[3] > 127) return GRIDPICO_INVALID;
    if (cable == 1) {
        const gridpico_apc_surface_message message = {{status, packet->bytes[2], packet->bytes[3]}};
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
        const gridpico_result result = gridpico_apc_physical_receive(&router->panels[peer].frontend,
                                                                   &message, router->now_us / 1000);
        const gridpico_pico_peer other = peer == GRIDPICO_PICO_APC ? GRIDPICO_PICO_WEB : GRIDPICO_PICO_APC;
        if (router->attached[other]) {
            gridpico_apc_physical_refresh(&router->panels[other].frontend, router->now_us / 1000);
        }
        return result;
#else
        return gridpico_apc_frontend_receive(&router->panels[peer], &message);
#endif
    }
    const gridpico_ingress_note note = {
        .source = {peer == GRIDPICO_PICO_APC ? APC_KEYS : WEB_KEYS, router->generation[peer]},
        .note = {code == 9 && packet->bytes[3] != 0 ? GRIDPICO_NOTE_ON : GRIDPICO_NOTE_OFF,
                 (uint8_t)(status & 0xfu), packet->bytes[2], packet->bytes[3]},
    };
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    gridpico_patch_session_observe_apc_patch_note(&router->session, &note, router->now_us);
#endif
    const gridpico_result delivered = gridpico_patch_graph_submit_note(&router->graph, &note);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    if (delivered == GRIDPICO_CAPACITY) refresh_panels(router);
#else
    if (delivered == GRIDPICO_CAPACITY) gridpico_apc_frontend_refresh(&router->panels[0]);
#endif
    return delivered;
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
           status.state == GRIDPICO_PATCH_ROUTE_ACTIVE &&
           gridpico_patch_graph_destination_idle(&router->graph, SYNTH_NOTES);
}
#endif

const gridpico_pico_packet *gridpico_pico_router_note(
    const gridpico_pico_router *router, gridpico_pico_peer peer)
{
    if (peer == GRIDPICO_PICO_APC) return NULL;
    const gridpico_pico_queue *queue = &router->notes[note_queue_index(peer)];
    return queue->count == 0 ? NULL : &queue->packets[queue->head];
}

void gridpico_pico_router_note_sent(gridpico_pico_router *router, gridpico_pico_peer peer)
{
    gridpico_pico_queue *queue = &router->notes[note_queue_index(peer)];
    queue->head = (uint8_t)((queue->head + 1) % GRIDPICO_PICO_QUEUE_SIZE);
    --queue->count;
}

const gridpico_pico_packet *gridpico_pico_router_led(
    const gridpico_pico_router *router, gridpico_pico_peer peer)
{
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    if (peer == GRIDPICO_PICO_SYNTH) return NULL;
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
    if (!router->attached[peer] || panel->sending_generation != router->generation[peer]) return;
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
