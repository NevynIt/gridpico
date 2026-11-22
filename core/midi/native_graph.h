#ifndef GRIDPICO_MIDI_NATIVE_GRAPH_H
#define GRIDPICO_MIDI_NATIVE_GRAPH_H

#include "api/native_proof.h"

typedef struct {
    uint8_t channel_index;
    uint8_t key;
    size_t release_credit;
} gridpico_midi_occurrence;

typedef struct {
    gridpico_note_delivery delivery;
    size_t contributors;
    bool release_pending;
} gridpico_midi_release_credit;

typedef struct {
    gridpico_connection source;
    gridpico_connection destination;
    gridpico_transpose_note_fn transpose;
    gridpico_deliver_note_fn deliver;
    void *delivery_context;
    bool source_available;
    bool configured;
    bool enabled;
    int8_t transpose_offset;
    size_t occurrence_count;
    gridpico_midi_occurrence occurrences[GRIDPICO_PROOF_NOTE_OCCURRENCES];
    gridpico_midi_release_credit release_credits[GRIDPICO_PROOF_RELEASE_CREDITS];
} gridpico_midi_graph;

/* The caller owns storage. Calls and synchronous delivery are serialized. */
gridpico_result gridpico_midi_graph_init(gridpico_midi_graph *graph,
                                        gridpico_connection source,
                                        gridpico_connection destination,
                                        gridpico_transpose_note_fn transpose,
                                        gridpico_deliver_note_fn deliver,
                                        void *delivery_context);

gridpico_result gridpico_midi_graph_submit_note(void *context,
                                               const gridpico_ingress_note *input);
gridpico_result gridpico_midi_graph_apply_route(void *context,
                                               const gridpico_route_request *request);
gridpico_result gridpico_midi_graph_set_route_enabled(void *context, bool enabled);
/* Session/control supplies the applied revision. */
gridpico_route_snapshot gridpico_midi_graph_read_route(void *context);

/* Disconnect cleans up the current ingress. A newer generation can attach
 * after disconnect or ingress quiescence, using the same logical endpoint. */
gridpico_result gridpico_midi_graph_disconnect_source(gridpico_midi_graph *graph,
                                                     gridpico_connection source);
gridpico_result gridpico_midi_graph_attach_source(gridpico_midi_graph *graph,
                                                 gridpico_connection source);

#endif
