#ifndef GRIDPICO_MIDI_PATCH_GRAPH_H
#define GRIDPICO_MIDI_PATCH_GRAPH_H

#include "api/patchbay.h"

enum {
    GRIDPICO_PATCH_BROWSER_NOTE_OCCURRENCES = 25,
    GRIDPICO_PATCH_BROWSER_RELEASE_CREDITS = GRIDPICO_PATCH_BROWSER_NOTE_OCCURRENCES * GRIDPICO_PATCH_BRANCHES,
};

typedef struct {
    uint32_t generation;
    bool quiesced;
} gridpico_patch_port_runtime;

typedef struct {
    size_t source;
    size_t destination;
    bool enabled;
} gridpico_patch_graph_branch;

typedef struct {
    gridpico_connection source;
    uint8_t channel_index;
    uint8_t key;
    size_t credits[GRIDPICO_PATCH_MAX_BRANCHES];
} gridpico_patch_occurrence;

typedef struct {
    gridpico_note_delivery delivery;
    size_t contributors;
    gridpico_port_id owed_source; /* Last contributing branch's source, or 0. */
} gridpico_patch_release_credit;

typedef struct {
    gridpico_patch_inventory inventory;
    gridpico_revision revision;
    gridpico_deliver_note_fn deliver;
    void *delivery_context;
    gridpico_patch_port_runtime ports[GRIDPICO_PATCH_MAX_PORTS];
    gridpico_patch_graph_branch branches[GRIDPICO_PATCH_MAX_BRANCHES];
    size_t branch_limit;
    size_t occurrence_limit;
    size_t release_credit_limit;
    size_t occurrence_count;
    gridpico_patch_occurrence occurrences[GRIDPICO_PATCH_MONITOR_NOTE_OCCURRENCES];
    gridpico_patch_release_credit credits[GRIDPICO_PATCH_MONITOR_RELEASE_CREDITS];
} gridpico_patch_graph;

/* Assembly owns storage and serializes calls, including the delivery callback.
 * The default uses the stage-one native limits. The browser and Monitor
 * initializers budget one occurrence per key and one credit per branch for each.
 * Only the Monitor initializer allows the expanded inventory and third branch. */
gridpico_result gridpico_patch_graph_init(gridpico_patch_graph *graph,
                                         const gridpico_patch_inventory *inventory,
                                         gridpico_deliver_note_fn deliver,
                                         void *delivery_context);
gridpico_result gridpico_patch_graph_init_browser(gridpico_patch_graph *graph,
                                                 const gridpico_patch_inventory *inventory,
                                                 gridpico_deliver_note_fn deliver,
                                                 void *delivery_context);
gridpico_result gridpico_patch_graph_init_monitor(gridpico_patch_graph *graph,
                                                 const gridpico_patch_inventory *inventory,
                                                 gridpico_deliver_note_fn deliver,
                                                 void *delivery_context);
gridpico_result gridpico_patch_graph_update_port(void *context,
                                                const gridpico_patch_port_update *update);
gridpico_result gridpico_patch_graph_set_route(void *context,
                                               const gridpico_patch_route_request *request);
gridpico_result gridpico_patch_graph_read_graph(void *context, gridpico_patch_graph_state *out);
gridpico_result gridpico_patch_graph_submit_note(void *context, const gridpico_ingress_note *input);

#endif
