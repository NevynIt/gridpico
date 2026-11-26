#ifndef GRIDPICO_APP_RP2040_AUTO_ROUTES_H
#define GRIDPICO_APP_RP2040_AUTO_ROUTES_H

#include "app/rp2040_router.h"

#ifdef GRIDPICO_PICO_GENERIC_MIDI

typedef struct {
    gridpico_pico_router *router;
    gridpico_local_routing_snapshot *routing_scratch;
    gridpico_revision graph_revision, color_revision, routing_revision;
    uint64_t attachment_epoch;
    gridpico_patch_manual_routes manual_routes;
    gridpico_auto_route_summary summary;
    gridpico_port_id ids[GRIDPICO_PATCH_MAX_PORTS];
    uint8_t sources[GRIDPICO_PATCH_MAX_PORTS], destinations[GRIDPICO_PATCH_MAX_PORTS];
    uint8_t decisions[GRIDPICO_AUTO_ROUTE_MAX_PAIRS];
    uint8_t source_count, destination_count, sweep;
    uint16_t next;
    uint32_t blocked_inputs, blocked_outputs;
    bool dirty, ready, coherent, enable;
} gridpico_pico_auto_routes;

void gridpico_pico_auto_routes_init(gridpico_pico_auto_routes *plan, gridpico_pico_router *router,
                                   gridpico_local_routing_snapshot *routing_scratch);
void gridpico_pico_auto_routes_invalidate(gridpico_pico_auto_routes *plan);
/* Masks index the existing generic transport slots. Process at most one pair
 * per service call, after verified admission and controller-role resolution.
 */
void gridpico_pico_auto_routes_service(gridpico_pico_auto_routes *plan, bool coherent,
                                      uint32_t blocked_inputs, uint32_t blocked_outputs);
gridpico_result gridpico_pico_auto_routes_read(void *context, gridpico_auto_route_summary *out);
gridpico_result gridpico_pico_auto_routes_explain(void *context, gridpico_revision expected_revision,
                                                   uint16_t offset, gridpico_auto_route_explain_page *out);
#endif
#endif
