#ifndef GRIDPICO_APP_RP2040_PAT2_CAPTURE_H
#define GRIDPICO_APP_RP2040_PAT2_CAPTURE_H

#include "app/rp2040_pat2_apply.h"

#ifdef GRIDPICO_PAT2_APPLY_HOST
typedef struct {
    const gridpico_pico_pat2_apply *owner;
    const gridpico_pico_pat2_intent *prior;
    uint32_t prior_generation;
    gridpico_revision graph_revision, layout_revision, role_revision;
    gridpico_apc_patch_layout layout;
    gridpico_patch_manual_routes off;
    gridpico_pico_pat2_progress progress;
    uint16_t committed_off[GRIDPICO_PICO_PAT2_PAIRS];
} gridpico_pico_pat2_capture_evidence;

typedef struct {
    gridpico_pico_pat2_projection projection;
    gridpico_pico_pat2_plan plan;
    gridpico_pico_pat2_intent intent;
    gridpico_connection live[GRIDPICO_PICO_PAT2_DIRECTIONS];
    uint32_t reserved_refs;
    gridpico_patch_manual_routes prior_off;
    uint8_t choices[GRIDPICO_PATCH_MAX_PORTS][GRIDPICO_PATCH_MAX_PORTS];
} gridpico_pico_pat2_capture_scratch;

/* Bind a zero-initialized witness before edit or discovery. The apply
 * workspace may also borrow an immutable prior record.
 */
gridpico_result gridpico_pico_pat2_bind_capture(gridpico_pico_router *router, gridpico_pico_pat2_apply *owner);
gridpico_result gridpico_pico_pat2_observe_capture(const gridpico_pico_router *router,
    gridpico_revision roles, gridpico_pico_pat2_capture_evidence *out);

/* Foreground after role nomination. Read a fresh mounted census, stage in
 * disjoint caller scratch and publish only a validated RAM intent.
 */
gridpico_result gridpico_pico_pat2_capture(const gridpico_pico_router *router, gridpico_revision roles,
    const gridpico_pico_pat2_capture_evidence *evidence, uint32_t generation,
    gridpico_pico_pat2_capture_scratch *scratch, gridpico_pico_pat2_intent *out);
#endif

#endif
