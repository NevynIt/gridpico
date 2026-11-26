#ifndef GRIDPICO_APP_RP2040_PAT2_PROJECTION_H
#define GRIDPICO_APP_RP2040_PAT2_PROJECTION_H

#include "app/rp2040_snapshot_pat2.h"

#ifdef GRIDPICO_PICO_GENERIC_MIDI
typedef struct {
    gridpico_pico_pat2_verified_view view;
    gridpico_pico_pat2_observation observations[GRIDPICO_PICO_PAT2_IDENTITIES];
    gridpico_pico_pat2_binding bindings[GRIDPICO_PICO_PAT2_DIRECTIONS];
    gridpico_local_graph_state graph;
    uint32_t unknown_shape_interfaces; /* Diagnostic bits in USB host interface order. */
} gridpico_pico_pat2_projection;

/* Read in the serialized foreground after role sync and nomination, before
 * placement or Auto. Caller owns the storage and must keep it in place while
 * using the view. Use the view only on OK. No restore or transport work runs.
 */
gridpico_result gridpico_pico_project_pat2_view(gridpico_pico_pat2_projection *out);
#endif

#endif
