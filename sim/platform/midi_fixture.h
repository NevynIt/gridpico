#ifndef GRIDPICO_MIDI_FIXTURE_H
#define GRIDPICO_MIDI_FIXTURE_H

#include "api/native_proof.h"

/* Caller-owned state for one serialized, in-process MIDI fixture. */
typedef struct {
    gridpico_submit_note_fn submit;
    void *submit_context;
    gridpico_note_delivery capture[GRIDPICO_PROOF_CAPTURE_ROWS];
    size_t capture_start;
    size_t capture_count;
} gridpico_midi_fixture;

/* Initialize empty capture and bind the synchronous ingress callback. */
void gridpico_midi_fixture_init(gridpico_midi_fixture *fixture,
                               gridpico_submit_note_fn submit,
                               void *submit_context);

gridpico_connection gridpico_midi_fixture_source(void);
gridpico_connection gridpico_midi_fixture_destination(void);

/* Pass the initialized fixture as context to each callback. */
gridpico_result gridpico_midi_fixture_submit(void *context,
                                            const gridpico_ingress_note *input);
gridpico_result gridpico_midi_fixture_deliver(void *context,
                                             const gridpico_note_delivery *delivery);

/* Copy up to capacity rows, starting at the oldest retained row. */
size_t gridpico_midi_fixture_read_capture(void *context,
                                        gridpico_note_delivery *rows,
                                        size_t capacity);

#endif
