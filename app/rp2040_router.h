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

#if defined(GRIDPICO_RP2040_MATRIX_CLI) && !defined(GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH)
#define GRIDPICO_PICO_GENERIC_MIDI 1
enum { GRIDPICO_PICO_USB_CABLES = 2, GRIDPICO_PICO_USB_DEVICES = 4, GRIDPICO_PICO_USB_CABLE_GROUPS = 5 };
#endif

typedef enum {
    GRIDPICO_PICO_APC,
    GRIDPICO_PICO_WEB,
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    GRIDPICO_PICO_DISCOVERED,
    GRIDPICO_PICO_PEERS = GRIDPICO_PICO_DISCOVERED + GRIDPICO_PICO_USB_CABLE_GROUPS,
#else
    GRIDPICO_PICO_SYNTH,
    GRIDPICO_PICO_PEERS,
#endif
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
    gridpico_revision sending_generation;
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
    uint32_t destination_generation;
    uint64_t due_us;
    gridpico_note authored_end;
    uint8_t step;
    bool end_pending;
    bool stopping;
} gridpico_pico_audition;
#endif

struct gridpico_pico_router {
    gridpico_patch_graph graph;
    gridpico_patch_session session;
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    gridpico_local_routing_snapshot projection_scratch;
#endif
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    gridpico_revision physical_feedback_generation;
    bool native_output_ready;
    bool keys_mode, keys_cutover;
    uint8_t apc_placement_first, apc_placement_end;
    gridpico_patch_manual_routes manual_routes;
    struct {
        gridpico_port_id input, output;
        uint32_t input_generation;
    } discovered[GRIDPICO_PICO_PEERS - GRIDPICO_PICO_DISCOVERED];
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
    gridpico_pico_queue notes[GRIDPICO_PICO_PEERS - 1];
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
#ifdef GRIDPICO_PICO_GENERIC_MIDI
/* Append one validated device with per-cable direction masks. No attachment or placement. */
gridpico_result gridpico_pico_router_register_device(gridpico_pico_router *router,
                                                    gridpico_pico_peer first_peer, const char *name,
                                                    uint8_t inputs, uint8_t outputs);
/* Consume first admission after role nomination. Masks index discovered cable groups. */
gridpico_result gridpico_pico_router_place_discovered(
    gridpico_pico_router *router, gridpico_revision graph_revision, gridpico_revision layout_revision,
    uint32_t blocked_inputs, uint32_t blocked_outputs);
/* Commit an admitted route change, silently cancelling Audition on its edited OUT. */
gridpico_result gridpico_pico_router_commit_local_route(
    gridpico_pico_router *router, const gridpico_local_route_request *request);
/* An independent physical LED owner supplies a nonzero target epoch; zero stops feedback. */
void gridpico_pico_router_commit_keys_mode(gridpico_pico_router *router, bool keys_mode);
bool gridpico_pico_router_queue_keys_packet(gridpico_pico_router *router, const gridpico_pico_packet *packet);
void gridpico_pico_router_set_physical_feedback(gridpico_pico_router *router, gridpico_revision generation);
/* Freeze native MIDI OUT for this configuration. Old release debt stays owned.
 * Other current input and output bindings remain active.
 */
void gridpico_pico_router_cancel_audition_owner(gridpico_pico_router *router, gridpico_pico_peer owner);
void gridpico_pico_router_retire_native_output(gridpico_pico_router *router);
/* The caller has fenced the input, checked revision space, and released its Panel role. */
gridpico_result gridpico_pico_router_renew_managed_input(gridpico_pico_router *router, gridpico_pico_peer peer);
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
