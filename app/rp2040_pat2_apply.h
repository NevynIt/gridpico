#ifndef GRIDPICO_APP_RP2040_PAT2_APPLY_H
#define GRIDPICO_APP_RP2040_PAT2_APPLY_H

#include "app/rp2040_pat2_projection.h"

#ifdef GRIDPICO_PAT2_APPLY_HOST
enum {
    GRIDPICO_PAT2_DUE,
    GRIDPICO_PAT2_APPLIED,
    GRIDPICO_PAT2_FULL,
    GRIDPICO_PAT2_OPERATOR,
};

typedef struct {
    uint16_t touched_slots, consumed_slots;
    uint32_t owned_directions;
    gridpico_patch_manual_routes owned_pairs;
    uint8_t reservations[GRIDPICO_PICO_PAT2_SLOTS]; /* Direction ref and label in intent. */
    uint8_t cancelled[GRIDPICO_PICO_PAT2_SLOTS];
    uint8_t pair_outcomes[GRIDPICO_PICO_PAT2_PAIRS];
} gridpico_pico_pat2_progress;

typedef struct gridpico_pico_pat2_apply {
    /* The fixture loans one immutable record before first admission. */
    const gridpico_pico_pat2_intent *intent;
    gridpico_pico_pat2_progress progress;
    gridpico_pico_pat2_projection projection;
    gridpico_pico_pat2_plan plan, current_plan;
    gridpico_patch_graph shadow_graph;
    gridpico_patch_session shadow_session;
    gridpico_local_routing_snapshot shadow_read;
    struct {
        const gridpico_pico_pat2_intent *intent;
        gridpico_revision role_revision;
        gridpico_port_id audition_destination;
        gridpico_pico_pat2_progress progress;
        gridpico_patch_manual_routes off;
    } before;
    gridpico_pico_pat2_progress next;
    gridpico_patch_manual_routes off;
    gridpico_apc_patch_layout layout;
    gridpico_revision layout_revision;
    bool prepared;
} gridpico_pico_pat2_apply;

/* Project into apply->projection before prepare and again before commit.
 * Both calls run in the foreground. Commit consumes any prepared candidate.
 */
gridpico_result gridpico_pico_pat2_prepare(gridpico_pico_router *router, gridpico_revision roles);
gridpico_result gridpico_pico_pat2_commit(gridpico_pico_router *router, gridpico_revision roles);
void gridpico_pico_pat2_refresh_panels(gridpico_pico_router *router);

size_t gridpico_pico_pat2_slot_index(gridpico_patch_direction axis, uint8_t slot);
bool gridpico_pico_pat2_placement_blocked(const gridpico_pico_router *router,
                                          size_t slot);
bool gridpico_pico_pat2_has_reservation(const gridpico_pico_router *router, size_t slot);
void gridpico_pico_pat2_own_slot(gridpico_pico_router *router, size_t slot,
                                gridpico_port_id previous, gridpico_port_id current);
void gridpico_pico_pat2_own_pair(gridpico_pico_router *router, const gridpico_local_route *route);
#endif

#endif
