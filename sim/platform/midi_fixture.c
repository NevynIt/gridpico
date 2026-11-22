#include "midi_fixture.h"

static bool same_connection(gridpico_connection actual, gridpico_connection expected)
{
    return actual.endpoint == expected.endpoint && actual.generation == expected.generation;
}

static bool valid_note(gridpico_note note)
{
    return note.channel_index <= 15 && note.key <= 127 && note.velocity <= 127 &&
           (note.kind == GRIDPICO_NOTE_OFF ||
            (note.kind == GRIDPICO_NOTE_ON && note.velocity > 0));
}

void gridpico_midi_fixture_init(gridpico_midi_fixture *fixture,
                               gridpico_submit_note_fn submit,
                               void *submit_context)
{
    *fixture = (gridpico_midi_fixture){.submit = submit, .submit_context = submit_context};
}

gridpico_connection gridpico_midi_fixture_source(void)
{
    return (gridpico_connection){.endpoint = 1, .generation = 1};
}

gridpico_connection gridpico_midi_fixture_destination(void)
{
    return (gridpico_connection){.endpoint = 2, .generation = 1};
}

gridpico_result gridpico_midi_fixture_submit(void *context,
                                            const gridpico_ingress_note *input)
{
    const gridpico_midi_fixture *fixture = context;
    if (fixture == NULL || input == NULL || !valid_note(input->note)) {
        return GRIDPICO_INVALID;
    }
    if (fixture->submit == NULL ||
        !same_connection(input->source, gridpico_midi_fixture_source())) {
        return GRIDPICO_UNAVAILABLE;
    }
    return fixture->submit(fixture->submit_context, input);
}

gridpico_result gridpico_midi_fixture_deliver(void *context,
                                             const gridpico_note_delivery *delivery)
{
    gridpico_midi_fixture *fixture = context;
    if (fixture == NULL || delivery == NULL || !valid_note(delivery->note)) {
        return GRIDPICO_INVALID;
    }
    if (!same_connection(delivery->source, gridpico_midi_fixture_source()) ||
        !same_connection(delivery->destination, gridpico_midi_fixture_destination())) {
        return GRIDPICO_UNAVAILABLE;
    }

    const size_t index = (fixture->capture_start + fixture->capture_count) %
                         GRIDPICO_PROOF_CAPTURE_ROWS;
    fixture->capture[index] = *delivery;
    if (fixture->capture_count == GRIDPICO_PROOF_CAPTURE_ROWS) {
        fixture->capture_start = (fixture->capture_start + 1) % GRIDPICO_PROOF_CAPTURE_ROWS;
    } else {
        fixture->capture_count++;
    }
    return GRIDPICO_OK;
}

size_t gridpico_midi_fixture_read_capture(void *context,
                                        gridpico_note_delivery *rows,
                                        size_t capacity)
{
    const gridpico_midi_fixture *fixture = context;
    if (fixture == NULL || rows == NULL) {
        return 0;
    }

    const size_t count = capacity < fixture->capture_count ? capacity : fixture->capture_count;
    for (size_t i = 0; i < count; i++) {
        rows[i] = fixture->capture[(fixture->capture_start + i) % GRIDPICO_PROOF_CAPTURE_ROWS];
    }
    return count;
}
