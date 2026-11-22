#include "core/midi/native_graph.h"

enum { NO_RELEASE_CREDIT = GRIDPICO_PROOF_RELEASE_CREDITS };

static bool valid_connection(gridpico_connection connection)
{
    return connection.endpoint != 0 && connection.generation != 0;
}

static bool same_connection(gridpico_connection left, gridpico_connection right)
{
    return left.endpoint == right.endpoint && left.generation == right.generation;
}

static bool valid_note(gridpico_note note)
{
    return note.channel_index < 16 && note.key < 128 && note.velocity < 128 &&
           (note.kind == GRIDPICO_NOTE_OFF ||
            (note.kind == GRIDPICO_NOTE_ON && note.velocity != 0));
}

static bool output_blocked(const gridpico_midi_graph *graph)
{
    for (size_t i = 0; i < GRIDPICO_PROOF_RELEASE_CREDITS; ++i) {
        if (graph->release_credits[i].release_pending) {
            return true;
        }
    }
    return false;
}

static gridpico_result release_output(gridpico_midi_graph *graph, size_t index,
                                      uint8_t velocity)
{
    gridpico_midi_release_credit *credit = &graph->release_credits[index];
    gridpico_note_delivery release = credit->delivery;
    release.note.kind = GRIDPICO_NOTE_OFF;
    release.note.velocity = velocity;

    const gridpico_result result = graph->deliver(graph->delivery_context, &release);
    if (result == GRIDPICO_OK) {
        *credit = (gridpico_midi_release_credit){0};
    } else {
        credit->release_pending = true;
    }
    return result;
}

static gridpico_result cleanup_outputs(gridpico_midi_graph *graph, bool retire_sounding)
{
    gridpico_result result = GRIDPICO_OK;
    /* Pending releases have no remaining input references. Attempt each once. */
    for (size_t i = 0; i < GRIDPICO_PROOF_RELEASE_CREDITS; ++i) {
        if (graph->release_credits[i].release_pending) {
            if (release_output(graph, i, 0) != GRIDPICO_OK) {
                result = GRIDPICO_UNAVAILABLE;
            }
        }
    }

    if (!retire_sounding) {
        return result;
    }

    for (size_t i = 0; i < graph->occurrence_count; ++i) {
        const size_t index = graph->occurrences[i].release_credit;
        if (index == NO_RELEASE_CREDIT) {
            continue;
        }

        /* Retire all input references to this sounding output, even if its
         * release has to be retried. Keep their places in the input FIFO. */
        for (size_t j = i; j < graph->occurrence_count; ++j) {
            if (graph->occurrences[j].release_credit == index) {
                graph->occurrences[j].release_credit = NO_RELEASE_CREDIT;
            }
        }
        graph->release_credits[index].contributors = 0;

        if (release_output(graph, index, 0) != GRIDPICO_OK) {
            result = GRIDPICO_UNAVAILABLE;
        }
    }
    return result;
}

static gridpico_result close_ingress(gridpico_midi_graph *graph)
{
    graph->source_available = false;
    const gridpico_result result = cleanup_outputs(graph, true);
    graph->occurrence_count = 0;
    return result;
}

static gridpico_result reject_overflow(gridpico_midi_graph *graph)
{
    close_ingress(graph);
    return GRIDPICO_CAPACITY;
}

static gridpico_result submit_press(gridpico_midi_graph *graph,
                                    const gridpico_ingress_note *input)
{
    if (graph->occurrence_count == GRIDPICO_PROOF_NOTE_OCCURRENCES) {
        return reject_overflow(graph);
    }

    gridpico_midi_occurrence *occurrence = &graph->occurrences[graph->occurrence_count++];
    *occurrence = (gridpico_midi_occurrence){
        .channel_index = input->note.channel_index,
        .key = input->note.key,
        .release_credit = NO_RELEASE_CREDIT,
    };

    if (!graph->enabled) {
        return GRIDPICO_OK;
    }
    if (output_blocked(graph)) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_note output;
    if (!graph->transpose(graph->transpose_offset, &input->note, &output)) {
        return GRIDPICO_OK;
    }

    size_t free_credit = NO_RELEASE_CREDIT;
    for (size_t i = 0; i < GRIDPICO_PROOF_RELEASE_CREDITS; ++i) {
        gridpico_midi_release_credit *credit = &graph->release_credits[i];
        if (credit->contributors == 0) {
            free_credit = i;
        } else if (credit->delivery.note.channel_index == output.channel_index &&
                   credit->delivery.note.key == output.key) {
            ++credit->contributors;
            occurrence->release_credit = i;
            return GRIDPICO_OK;
        }
    }
    if (free_credit == NO_RELEASE_CREDIT) {
        return reject_overflow(graph);
    }

    gridpico_midi_release_credit *credit = &graph->release_credits[free_credit];
    *credit = (gridpico_midi_release_credit){
        .delivery = {.source = input->source, .destination = graph->destination, .note = output},
        .contributors = 1,
    };
    const gridpico_result result = graph->deliver(graph->delivery_context, &credit->delivery);
    if (result == GRIDPICO_OK) {
        occurrence->release_credit = free_credit;
    } else {
        /* A rejected delivery leaves only the silent input occurrence. */
        *credit = (gridpico_midi_release_credit){0};
    }
    return result;
}

static gridpico_result submit_release(gridpico_midi_graph *graph, const gridpico_note *note)
{
    for (size_t i = 0; i < graph->occurrence_count; ++i) {
        const gridpico_midi_occurrence occurrence = graph->occurrences[i];
        if (occurrence.channel_index != note->channel_index || occurrence.key != note->key) {
            continue;
        }

        --graph->occurrence_count;
        for (size_t j = i; j < graph->occurrence_count; ++j) {
            graph->occurrences[j] = graph->occurrences[j + 1];
        }

        if (occurrence.release_credit != NO_RELEASE_CREDIT) {
            gridpico_midi_release_credit *credit = &graph->release_credits[occurrence.release_credit];
            if (--credit->contributors == 0) {
                return release_output(graph, occurrence.release_credit, note->velocity);
            }
        }
        break;
    }
    return GRIDPICO_OK;
}

gridpico_result gridpico_midi_graph_init(gridpico_midi_graph *graph,
                                       gridpico_connection source,
                                       gridpico_connection destination,
                                       gridpico_transpose_note_fn transpose,
                                       gridpico_deliver_note_fn deliver,
                                       void *delivery_context)
{
    if (graph == NULL || !valid_connection(source) || !valid_connection(destination) ||
        source.endpoint == destination.endpoint || transpose == NULL || deliver == NULL) {
        return GRIDPICO_INVALID;
    }

    *graph = (gridpico_midi_graph){
        .source = source,
        .destination = destination,
        .transpose = transpose,
        .deliver = deliver,
        .delivery_context = delivery_context,
        .source_available = true,
    };
    return GRIDPICO_OK;
}

gridpico_result gridpico_midi_graph_submit_note(void *context,
                                              const gridpico_ingress_note *input)
{
    gridpico_midi_graph *graph = context;
    if (graph == NULL || input == NULL || !valid_connection(input->source) ||
        !valid_note(input->note)) {
        return GRIDPICO_INVALID;
    }
    if (!graph->source_available || !same_connection(graph->source, input->source)) {
        return GRIDPICO_UNAVAILABLE;
    }

    return input->note.kind == GRIDPICO_NOTE_ON ? submit_press(graph, input)
                                               : submit_release(graph, &input->note);
}

gridpico_result gridpico_midi_graph_apply_route(void *context,
                                              const gridpico_route_request *request)
{
    gridpico_midi_graph *graph = context;
    if (graph == NULL || request == NULL || request->source != graph->source.endpoint ||
        request->destination != graph->destination.endpoint || request->transpose_count == 0 ||
        request->transpose_offsets == NULL) {
        return GRIDPICO_INVALID;
    }
    if (request->transpose_count > GRIDPICO_PROOF_TRANSPOSE_INSTANCES) {
        return GRIDPICO_CAPACITY;
    }

    const int8_t offset = request->transpose_offsets[0];
    if (offset == INT8_MIN) {
        return GRIDPICO_INVALID;
    }

    const bool retire_sounding = graph->configured && offset != graph->transpose_offset;
    const gridpico_result result = cleanup_outputs(graph, retire_sounding);
    if (result == GRIDPICO_OK) {
        if (!graph->configured) {
            graph->enabled = true;
        }
        graph->configured = true;
        graph->transpose_offset = offset;
    }
    return result;
}

gridpico_result gridpico_midi_graph_set_route_enabled(void *context, bool enabled)
{
    gridpico_midi_graph *graph = context;
    if (graph == NULL || !graph->configured) {
        return GRIDPICO_INVALID;
    }

    const bool retire_sounding = graph->enabled && !enabled;
    const gridpico_result result = cleanup_outputs(graph, retire_sounding);
    if (result == GRIDPICO_OK) {
        graph->enabled = enabled;
    }
    return result;
}

gridpico_route_snapshot gridpico_midi_graph_read_route(void *context)
{
    const gridpico_midi_graph *graph = context;
    if (graph == NULL || !graph->configured) {
        return (gridpico_route_snapshot){0};
    }

    return (gridpico_route_snapshot){
        .configured = true,
        .active = graph->enabled && graph->source_available && !output_blocked(graph),
        .source = graph->source.endpoint,
        .destination = graph->destination.endpoint,
        .transpose_offset = graph->transpose_offset,
        .enabled = graph->enabled,
    };
}

gridpico_result gridpico_midi_graph_disconnect_source(gridpico_midi_graph *graph,
                                                    gridpico_connection source)
{
    if (graph == NULL || !valid_connection(source)) {
        return GRIDPICO_INVALID;
    }
    if (!same_connection(graph->source, source)) {
        return GRIDPICO_UNAVAILABLE;
    }
    return close_ingress(graph);
}

gridpico_result gridpico_midi_graph_attach_source(gridpico_midi_graph *graph,
                                                gridpico_connection source)
{
    if (graph == NULL || !valid_connection(source) || source.endpoint != graph->source.endpoint) {
        return GRIDPICO_INVALID;
    }
    if (graph->source_available || source.generation <= graph->source.generation) {
        return GRIDPICO_UNAVAILABLE;
    }

    graph->source = source;
    graph->source_available = true;
    return GRIDPICO_OK;
}
