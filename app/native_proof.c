#include "api/native_proof.h"
#include "core/midi/native_graph.h"
#include "core/processors/transpose.h"
#include "core/session/session.h"
#include "sim/platform/midi_fixture.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static const gridpico_connection source = {.endpoint = 1, .generation = 1};
static const gridpico_connection destination = {.endpoint = 2, .generation = 1};

typedef struct {
    gridpico_session session;
    gridpico_midi_fixture midi;
    size_t checked_rows;
} fixture;

static bool expect_result(const char *operation, gridpico_result actual,
                          gridpico_result expected)
{
    if (actual == expected) {
        return true;
    }
    fprintf(stderr, "%s returned %d, expected %d\n",
            operation, (int)actual, (int)expected);
    return false;
}

static bool apply_route(fixture *demo, const int8_t *offsets, size_t count,
                        gridpico_result expected)
{
    const gridpico_route_request request = {
        .source = source.endpoint,
        .destination = destination.endpoint,
        .transpose_offsets = offsets,
        .transpose_count = count,
    };
    return expect_result("Apply", gridpico_session_apply_route(&demo->session, &request),
                         expected);
}

static bool expect_route(fixture *demo, gridpico_revision revision, int8_t offset)
{
    const gridpico_route_snapshot state = gridpico_session_read_route(&demo->session);
    if (state.configured && state.active && state.applied_revision == revision &&
        state.source == source.endpoint && state.destination == destination.endpoint &&
        state.transpose_offset == offset) {
        printf("  Active route %u -> %u, transpose %+d, revision %" PRIu64 "\n",
               (unsigned)state.source, (unsigned)state.destination,
               (int)state.transpose_offset, state.applied_revision);
        return true;
    }
    fprintf(stderr,
            "Expected active route %u -> %u at transpose %+d and revision %" PRIu64
            "; got configured=%d active=%d, %u -> %u, transpose %+d, revision %" PRIu64 "\n",
            (unsigned)source.endpoint, (unsigned)destination.endpoint, (int)offset, revision,
            state.configured, state.active, (unsigned)state.source, (unsigned)state.destination,
            (int)state.transpose_offset, state.applied_revision);
    return false;
}

static bool submit_note(fixture *demo, gridpico_note_kind kind, uint8_t key)
{
    const gridpico_ingress_note input = {
        .source = source,
        .note = {
            .kind = kind,
            .channel_index = 0,
            .key = key,
            .velocity = kind == GRIDPICO_NOTE_ON ? 64 : 32,
        },
    };
    return expect_result(kind == GRIDPICO_NOTE_ON ? "Submit Note On" : "Submit Note Off",
                         gridpico_midi_fixture_submit(&demo->midi, &input), GRIDPICO_OK);
}

static bool expect_capture_count(fixture *demo, size_t expected,
                                 gridpico_note_delivery *rows)
{
    const size_t count =
        gridpico_midi_fixture_read_capture(&demo->midi, rows, GRIDPICO_PROOF_CAPTURE_ROWS);
    if (count == expected) {
        return true;
    }
    fprintf(stderr, "Capture contains %zu rows, expected %zu\n", count, expected);
    return false;
}

static bool expect_silence(fixture *demo)
{
    gridpico_note_delivery rows[GRIDPICO_PROOF_CAPTURE_ROWS];
    return expect_capture_count(demo, demo->checked_rows, rows);
}

static bool expect_delivery(fixture *demo, gridpico_note_kind kind, uint8_t key, uint8_t velocity)
{
    gridpico_note_delivery rows[GRIDPICO_PROOF_CAPTURE_ROWS];
    if (!expect_capture_count(demo, demo->checked_rows + 1, rows)) {
        return false;
    }

    const gridpico_note_delivery *row = &rows[demo->checked_rows];
    if (row->source.endpoint != source.endpoint || row->source.generation != source.generation ||
        row->destination.endpoint != destination.endpoint ||
        row->destination.generation != destination.generation ||
        row->note.kind != kind || row->note.channel_index != 0 ||
        row->note.key != key || row->note.velocity != velocity) {
        fprintf(stderr,
                "Capture row %zu: expected kind=%d ch=1 key=%u velocity=%u, %u/%u -> %u/%u"
                "; got kind=%d ch=%u key=%u velocity=%u, %u/%u -> %u/%u\n",
                demo->checked_rows + 1, (int)kind, (unsigned)key, (unsigned)velocity,
                (unsigned)source.endpoint, (unsigned)source.generation,
                (unsigned)destination.endpoint, (unsigned)destination.generation,
                (int)row->note.kind, (unsigned)row->note.channel_index + 1u,
                (unsigned)row->note.key, (unsigned)row->note.velocity,
                (unsigned)row->source.endpoint, (unsigned)row->source.generation,
                (unsigned)row->destination.endpoint, (unsigned)row->destination.generation);
        return false;
    }

    printf("  %s ch=%u key=%u velocity=%u\n",
           row->note.kind == GRIDPICO_NOTE_ON ? "Note On " : "Note Off",
           (unsigned)row->note.channel_index + 1u,
           (unsigned)row->note.key, (unsigned)row->note.velocity);
    demo->checked_rows++;
    return true;
}

static bool expect_note_pair(fixture *demo, uint8_t output_key)
{
    return submit_note(demo, GRIDPICO_NOTE_ON, 60) &&
           expect_delivery(demo, GRIDPICO_NOTE_ON, output_key, 64) &&
           submit_note(demo, GRIDPICO_NOTE_OFF, 60) &&
           expect_delivery(demo, GRIDPICO_NOTE_OFF, output_key, 32);
}

static bool run_cases(fixture *demo)
{
    const int8_t octave_up = 12;
    const int8_t fifth_up = 7;
    const int8_t over_capacity[] = {7, 12};

    puts("1. Apply +12 and play key 60");
    if (!expect_silence(demo) ||
        !apply_route(demo, &octave_up, 1, GRIDPICO_OK) ||
        !expect_route(demo, 1, octave_up) ||
        !expect_silence(demo) ||
        !expect_note_pair(demo, 72)) {
        return false;
    }

    puts("2. Play and release out-of-range key 120 (+12)");
    if (!submit_note(demo, GRIDPICO_NOTE_ON, 120) || !expect_silence(demo) ||
        !submit_note(demo, GRIDPICO_NOTE_OFF, 120) || !expect_silence(demo)) {
        return false;
    }
    puts("  No deliveries");

    puts("3. Apply +7 with key 60 held");
    if (!submit_note(demo, GRIDPICO_NOTE_ON, 60) ||
        !expect_delivery(demo, GRIDPICO_NOTE_ON, 72, 64) ||
        !apply_route(demo, &fifth_up, 1, GRIDPICO_OK) ||
        !expect_delivery(demo, GRIDPICO_NOTE_OFF, 72, 0) ||
        !expect_route(demo, 2, fifth_up) ||
        !submit_note(demo, GRIDPICO_NOTE_OFF, 60) ||
        !expect_silence(demo)) {
        return false;
    }
    puts("  No extra delivery on source release");
    if (!expect_note_pair(demo, 67)) {
        return false;
    }

    puts("4. Propose two transpose instances (+7, +12)");
    if (!apply_route(demo, over_capacity, 2, GRIDPICO_CAPACITY)) {
        return false;
    }
    puts("  Apply rejected (GRIDPICO_CAPACITY)");
    if (!expect_route(demo, 2, fifth_up) || !expect_silence(demo) ||
        !expect_note_pair(demo, 67)) {
        return false;
    }

    puts("PASS (4 cases, 8 captured deliveries)");
    return true;
}

int main(void)
{
    fixture demo = {0};
    gridpico_midi_graph graph;
    gridpico_midi_fixture_init(&demo.midi, gridpico_midi_graph_submit_note, &graph);
    const gridpico_result initialized = gridpico_midi_graph_init(
        &graph, gridpico_midi_fixture_source(), gridpico_midi_fixture_destination(),
        gridpico_transpose_note, gridpico_midi_fixture_deliver, &demo.midi);
    if (!expect_result("Initialize MIDI graph", initialized, GRIDPICO_OK)) {
        return EXIT_FAILURE;
    }

    gridpico_session_init(&demo.session, &graph,
                          gridpico_midi_graph_apply_route, gridpico_midi_graph_read_route);

    return run_cases(&demo) ? EXIT_SUCCESS : EXIT_FAILURE;
}
