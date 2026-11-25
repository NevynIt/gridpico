#ifndef GRIDPICO_APP_RP2040_ROUTER_H
#define GRIDPICO_APP_RP2040_ROUTER_H

#include "core/midi/patch_graph.h"
#include "core/session/patch_session.h"
#include "frontends/apc/apc_frontend.h"
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
#include "frontends/apc/apc_physical.h"
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
#include "frontends/apc/apc_chooser.h"
#endif

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

typedef struct gridpico_pico_router gridpico_pico_router;

#ifdef GRIDPICO_RP2040_KEYS_BENCH
enum { GRIDPICO_PICO_KEYS_HOLDS = 96 };
typedef struct {
    uint8_t stream, status, data, count;
    bool blocked;
} gridpico_pico_keys_hold;

typedef struct {
    gridpico_apc_chooser chooser;
    gridpico_apc_chooser_mode mode, next_mode;
    gridpico_pico_queue apc_queue;
    gridpico_pico_keys_hold holds[GRIDPICO_PICO_KEYS_HOLDS];
    uint32_t source_generation[3];
    bool source_connected[3];
    gridpico_pico_packet computer_pad[2];
    bool have_computer_pad[2];
} gridpico_pico_keys;
#endif

#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
typedef struct {
    gridpico_pico_router *router;
    gridpico_pico_peer peer;
    gridpico_apc_physical frontend;
    gridpico_pico_packet latest[GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES];
    gridpico_pico_packet sending[GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES];
    gridpico_apc_physical_page_version latest_version;
    gridpico_apc_physical_page_version sending_version;
    uint32_t sending_generation;
    uint8_t cursor;
    bool latest_valid;
    bool pending;
    bool awaiting_completion;
} gridpico_pico_patch_panel;

typedef struct {
    gridpico_port_id destination;
    gridpico_pico_peer owner;
    uint32_t owner_generation;
    uint32_t source_generation;
    uint64_t due_us;
    uint8_t step;
    bool sounding;
    bool stopping;
} gridpico_pico_audition;
#endif

struct gridpico_pico_router {
    gridpico_patch_graph graph;
    gridpico_patch_session session;
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    gridpico_local_routing_snapshot projection_scratch;
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    gridpico_pico_keys keys;
#endif
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    gridpico_pico_patch_panel panels[2];
    gridpico_apc_patch_snapshot panel_scratch;
    gridpico_pico_audition audition;
    uint64_t now_us;
    uint64_t next_refresh_us;
#else
    gridpico_apc_frontend panels[2];
#endif

    bool attached[GRIDPICO_PICO_PEERS];
    uint32_t generation[GRIDPICO_PICO_PEERS];
    gridpico_pico_queue notes[2];
#ifndef GRIDPICO_RP2040_PATCHBAY_BENCH
    gridpico_pico_packet led;
    bool led_pending[2];
#endif
};

#ifdef GRIDPICO_RP2040_KEYS_BENCH
void gridpico_pico_router_finish_mode(gridpico_pico_router *router,
                                     bool native_idle, bool apc_idle, bool synth_idle);
#endif

gridpico_result gridpico_pico_router_init(gridpico_pico_router *router);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
/* Startup only. Discard the candidate on a rejected snapshot. */
gridpico_result gridpico_pico_router_init_snapshot(gridpico_pico_router *router,
                                                  const gridpico_apc_session_snapshot *snapshot);
#endif
#ifdef GRIDPICO_RP2040_MATRIX_CLI
gridpico_result gridpico_pico_router_read_routing(void *context, gridpico_local_routing_snapshot *out);
gridpico_result gridpico_pico_router_read_managed_route(
    void *context, const gridpico_local_route *route, gridpico_local_route_status *out);
gridpico_result gridpico_pico_router_set_managed_route(
    void *context, const gridpico_local_route_request *request);
#endif
gridpico_route_snapshot gridpico_pico_router_pad_status(gridpico_pico_router *router);
void gridpico_pico_router_attach(gridpico_pico_router *router, gridpico_pico_peer peer, bool attached);
gridpico_result gridpico_pico_router_receive(
    gridpico_pico_router *router, gridpico_pico_peer peer, const gridpico_pico_packet *packet);
#ifdef GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH
bool gridpico_pico_router_can_program_change(
    gridpico_pico_router *router, gridpico_pico_peer peer, const gridpico_pico_packet *packet);
#endif
const gridpico_pico_packet *gridpico_pico_router_note(
    const gridpico_pico_router *router, gridpico_pico_peer peer);
void gridpico_pico_router_note_sent(gridpico_pico_router *router, gridpico_pico_peer peer);
const gridpico_pico_packet *gridpico_pico_router_led(
    const gridpico_pico_router *router, gridpico_pico_peer peer);
void gridpico_pico_router_led_sent(gridpico_pico_router *router, gridpico_pico_peer peer);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
void gridpico_pico_router_tick(gridpico_pico_router *router, uint64_t now_us);
void gridpico_pico_router_offer_led_page(gridpico_pico_router *router, gridpico_pico_peer peer);
void gridpico_pico_router_led_complete(gridpico_pico_router *router, gridpico_pico_peer peer, bool delivered);
#endif

#endif
