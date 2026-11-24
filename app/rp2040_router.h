#ifndef GRIDPICO_APP_RP2040_ROUTER_H
#define GRIDPICO_APP_RP2040_ROUTER_H

#include "core/midi/patch_graph.h"
#include "core/session/patch_session.h"
#include "frontends/apc/apc_frontend.h"

typedef enum {
    GRIDPICO_PICO_APC,
    GRIDPICO_PICO_WEB,
    GRIDPICO_PICO_SYNTH,
    GRIDPICO_PICO_PEERS,
} gridpico_pico_peer;

enum { GRIDPICO_PICO_PACKET_BYTES = 4, GRIDPICO_PICO_QUEUE_SIZE = 128 };

typedef struct {
    uint8_t bytes[GRIDPICO_PICO_PACKET_BYTES];
} gridpico_pico_packet;

typedef struct {
    gridpico_pico_packet packets[GRIDPICO_PICO_QUEUE_SIZE];
    uint8_t head;
    uint8_t count;
} gridpico_pico_queue;

typedef struct {
    gridpico_patch_graph graph;
    gridpico_patch_session session;
    gridpico_apc_frontend panels[2];
    bool attached[GRIDPICO_PICO_PEERS];
    uint32_t generation[GRIDPICO_PICO_PEERS];
    gridpico_pico_queue notes[2];
    gridpico_pico_packet led;
    bool led_pending[2];
} gridpico_pico_router;

gridpico_result gridpico_pico_router_init(gridpico_pico_router *router);
gridpico_route_snapshot gridpico_pico_router_pad_status(gridpico_pico_router *router);
void gridpico_pico_router_attach(gridpico_pico_router *router, gridpico_pico_peer peer, bool attached);
gridpico_result gridpico_pico_router_receive(
    gridpico_pico_router *router, gridpico_pico_peer peer, const gridpico_pico_packet *packet);
const gridpico_pico_packet *gridpico_pico_router_note(
    const gridpico_pico_router *router, gridpico_pico_peer peer);
void gridpico_pico_router_note_sent(gridpico_pico_router *router, gridpico_pico_peer peer);
const gridpico_pico_packet *gridpico_pico_router_led(
    const gridpico_pico_router *router, gridpico_pico_peer peer);
void gridpico_pico_router_led_sent(gridpico_pico_router *router, gridpico_pico_peer peer);

#endif
