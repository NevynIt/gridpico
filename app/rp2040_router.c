#include "app/rp2040_router.h"

#include <string.h>

enum {
    APC_KEYS = 1,
    WEB_KEYS,
    WEB_EXPORT,
    SYNTH_NOTES,
    OCCURRENCES = 25,
    RELEASE_CREDITS = 50,
};

static const gridpico_local_route routes[] = {
    {APC_KEYS, SYNTH_NOTES, GRIDPICO_LOCAL_MANAGED},
    {APC_KEYS, WEB_EXPORT, GRIDPICO_LOCAL_MANAGED},
    {WEB_KEYS, SYNTH_NOTES, GRIDPICO_LOCAL_MANAGED},
};

static const gridpico_local_registry registry = {
    .inventory = {
        .device_count = 3,
        .port_count = 4,
        .devices = {
            {1, "APC Key 25 mk2", GRIDPICO_PATCH_DISCONNECTED},
            {2, "Computer", GRIDPICO_PATCH_DISCONNECTED},
            {3, "MicroFreak", GRIDPICO_PATCH_DISCONNECTED},
        },
        .ports = {
            {APC_KEYS, 1, "APC Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {WEB_KEYS, 2, "Web Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {WEB_EXPORT, 2, "Physical Keybed", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {SYNTH_NOTES, 3, "MicroFreak MIDI", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
        },
    },
    .port_capabilities = {
        GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
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

static gridpico_result set_route(void *context, bool enabled)
{
    gridpico_pico_router *router = context;
    const gridpico_local_route_request request = {.route = routes[0], .enabled = enabled};
    const gridpico_result result = gridpico_patch_session_set_local_route(&router->session, &request);
    if (result != GRIDPICO_OK) gridpico_apc_frontend_refresh(&router->panels[0]);
    return result;
}

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

gridpico_result gridpico_pico_router_init(gridpico_pico_router *router)
{
    static const gridpico_local_profile profile = {
        .midi_peer_limit = 4, .managed_branch_limit = 3, .managed_fanout_limit = 2,
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
        reset_panel(router, peer);
        update_port(router, peer == GRIDPICO_PICO_APC ? APC_KEYS : WEB_KEYS, peer);
    }
    gridpico_apc_frontend_refresh(&router->panels[0]);
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
        return gridpico_apc_frontend_receive(&router->panels[peer], &message);
    }
    const gridpico_ingress_note note = {
        .source = {peer == GRIDPICO_PICO_APC ? APC_KEYS : WEB_KEYS, router->generation[peer]},
        .note = {code == 9 && packet->bytes[3] != 0 ? GRIDPICO_NOTE_ON : GRIDPICO_NOTE_OFF,
                 (uint8_t)(status & 0xfu), packet->bytes[2], packet->bytes[3]},
    };
    const gridpico_result delivered = gridpico_patch_graph_submit_note(&router->graph, &note);
    if (delivered == GRIDPICO_CAPACITY) gridpico_apc_frontend_refresh(&router->panels[0]);
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
    return peer != GRIDPICO_PICO_SYNTH && router->led_pending[peer] ? &router->led : NULL;
}

void gridpico_pico_router_led_sent(gridpico_pico_router *router, gridpico_pico_peer peer)
{
    router->led_pending[peer] = false;
}
