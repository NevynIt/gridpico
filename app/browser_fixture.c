#include "app/runner_io.h"
#include "core/midi/native_graph.h"
#include "core/processors/transpose.h"
#include "core/session/session.h"
#include "frontends/apc/apc_frontend.h"
#include "sim/platform/midi_fixture.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    gridpico_midi_fixture midi;
    gridpico_apc_frontend apc;
    /* Disable, overflow and disconnect can release every reserved credit at once. */
    gridpico_note accepted[GRIDPICO_BROWSER_RELEASE_CREDITS];
    size_t accepted_count;
    gridpico_apc_surface_message surface_feedback;
    bool surface_feedback_pending;
} fixture;

static gridpico_result capture_delivery(void *context, const gridpico_note_delivery *delivery)
{
    fixture *runner = context;
    if (runner->accepted_count == GRIDPICO_BROWSER_RELEASE_CREDITS) {
        return GRIDPICO_CAPACITY;
    }

    const gridpico_result result = gridpico_midi_fixture_deliver(&runner->midi, delivery);
    if (result == GRIDPICO_OK) {
        runner->accepted[runner->accepted_count++] = delivery->note;
    }
    return result;
}

static bool write_capture(fixture *runner)
{
    const size_t count = runner->accepted_count;
    runner->accepted_count = 0;

    for (size_t i = 0; i < count; ++i) {
        if (!write_note(runner->accepted[i])) {
            return false;
        }
    }
    return true;
}

static void stage_surface_feedback(void *context, const gridpico_apc_surface_message *message)
{
    fixture *runner = context;
    runner->surface_feedback = *message;
    runner->surface_feedback_pending = true;
}

static bool write_surface_feedback(fixture *runner)
{
    if (!runner->surface_feedback_pending) {
        return true;
    }

    runner->surface_feedback_pending = false;
    return write_surface(runner->surface_feedback);
}

static gridpico_result dispatch_request(fixture *runner, gridpico_connection source,
                                       const char *line)
{
    if (strcmp(line, "surface-ready") == 0) {
        gridpico_apc_frontend_refresh(&runner->apc);
        return GRIDPICO_OK;
    }
    const char surface_prefix[] = "surface ";
    if (strncmp(line, surface_prefix, sizeof(surface_prefix) - 1) == 0) {
        gridpico_apc_surface_message message;
        if (!parse_surface(line + sizeof(surface_prefix) - 1, &message)) {
            return GRIDPICO_INVALID;
        }
        return gridpico_apc_frontend_receive(&runner->apc, &message);
    }

    gridpico_ingress_note input = {.source = source};
    if (!parse_note(line, &input.note)) {
        return GRIDPICO_INVALID;
    }
    return gridpico_midi_fixture_submit(&runner->midi, &input);
}

int main(void)
{
    /* A closed output pipe must leave the ingress cleanup path reachable. */
#ifdef SIGPIPE
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        fputs("Could not ignore SIGPIPE\n", stderr);
        return EXIT_FAILURE;
    }
#endif

    fixture runner = {0};
    gridpico_midi_graph graph;
    gridpico_session session;
    const gridpico_connection source = gridpico_midi_fixture_source();
    const gridpico_connection destination = gridpico_midi_fixture_destination();
    gridpico_midi_fixture_init(&runner.midi, gridpico_midi_graph_submit_note, &graph);
    if (gridpico_midi_graph_init_browser(&graph, source, destination, gridpico_transpose_note,
                                         capture_delivery, &runner) != GRIDPICO_OK) {
        fputs("Could not initialize the MIDI graph\n", stderr);
        return EXIT_FAILURE;
    }
    gridpico_session_init(&session, &graph,
                          gridpico_midi_graph_apply_route, gridpico_midi_graph_read_route,
                          gridpico_midi_graph_set_route_enabled);
    const int8_t transpose = 12;
    const gridpico_route_request route = {
        .source = source.endpoint,
        .destination = destination.endpoint,
        .transpose_offsets = &transpose,
        .transpose_count = 1,
    };
    if (gridpico_session_apply_route(&session, &route) != GRIDPICO_OK) {
        fputs("Could not apply the browser fixture route\n", stderr);
        return EXIT_FAILURE;
    }
    gridpico_apc_frontend_init(&runner.apc, &session, gridpico_session_read_route,
                              gridpico_session_set_route_enabled, &runner,
                              stage_surface_feedback);

    bool output_ok = fputs("ready\n", stdout) != EOF && fflush(stdout) == 0;
    while (output_ok) {
        char line[REQUEST_SIZE];
        const request_status status = read_request(line);
        if (status == REQUEST_EOF) {
            break;
        }

        const gridpico_result result = status == REQUEST_LINE
            ? dispatch_request(&runner, source, line)
            : GRIDPICO_INVALID;
        output_ok = write_capture(&runner) && write_surface_feedback(&runner) &&
                    fputs(result == GRIDPICO_OK ? "ok\n" : "error\n", stdout) != EOF &&
                    fflush(stdout) == 0;
        if (result != GRIDPICO_OK && output_ok) {
            /* The host retires this fixture after an error. */
            while (getchar() != EOF) {
            }
            break;
        }
    }

    const gridpico_result cleanup = gridpico_midi_graph_disconnect_source(&graph, source);
    output_ok = output_ok && write_capture(&runner) && fflush(stdout) == 0;
    if (cleanup != GRIDPICO_OK) {
        fputs("Could not release the browser fixture ingress\n", stderr);
    }
    if (ferror(stdin)) {
        fputs("Could not read the browser fixture request\n", stderr);
    }
    if (!output_ok) {
        fputs("Could not write the browser fixture response\n", stderr);
    }
    return cleanup == GRIDPICO_OK && !ferror(stdin) && output_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
