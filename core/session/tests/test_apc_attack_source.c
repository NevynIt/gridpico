#include "core/midi/patch_graph.h"
#include "core/session/patch_session.h"

#include <assert.h>

enum { EXISTING_INPUT = 7, EXISTING_OUTPUT = 9, AUDITION = 3, ADDED_INPUT = 2, ADDED_OUTPUT = 4 };

static size_t graph_reads;

static gridpico_result read_local_state(void *context, gridpico_local_graph_state *out)
{
    ++graph_reads;
    return gridpico_patch_graph_read_local_state(context, out);
}

static gridpico_result reject_note(void *context, const gridpico_note_delivery *delivery)
{
    (void)context;
    (void)delivery;
    assert(false);
    return GRIDPICO_INVALID;
}

static gridpico_result reject_raw(void *context, const gridpico_local_raw_delivery *delivery)
{
    (void)context;
    (void)delivery;
    assert(false);
    return GRIDPICO_INVALID;
}

static void set_connection(gridpico_patch_graph *graph, gridpico_port_id id, uint32_t generation,
                           gridpico_patch_connection_state state)
{
    const gridpico_patch_port_update update = {{id, generation}, state};
    assert(gridpico_patch_graph_update_port(graph, &update) == GRIDPICO_OK);
}

static void play_note(gridpico_patch_session *session, gridpico_patch_graph *graph,
                      gridpico_port_id source, uint32_t generation, uint64_t now_us)
{
    gridpico_ingress_note input = {{source, generation}, {GRIDPICO_NOTE_ON, 0, 60, 64}};
    const size_t before = graph_reads;
    gridpico_patch_session_observe_apc_patch_note(session, &input, now_us);
    assert(gridpico_patch_graph_submit_note(graph, &input) == GRIDPICO_OK);
    input.note.kind = GRIDPICO_NOTE_OFF;
    gridpico_patch_session_observe_apc_patch_note(session, &input, now_us + 1);
    assert(gridpico_patch_graph_submit_note(graph, &input) == GRIDPICO_OK);
    assert(graph_reads == before);
}

int main(void)
{
    const gridpico_local_registry registry = {
        .inventory = {
            .device_count = 3,
            .port_count = 3,
            .devices = {
                {1, "Existing keys", GRIDPICO_PATCH_DISCONNECTED},
                {2, "Existing destination", GRIDPICO_PATCH_DISCONNECTED},
                {3, "Internal audition", GRIDPICO_PATCH_DISCONNECTED},
            },
            .ports = {
                {EXISTING_INPUT, 1, "Existing input", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
                {EXISTING_OUTPUT, 2, "Existing output", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
                {AUDITION, 3, "Audition input", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            },
        },
        .port_capabilities = {GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
                              GRIDPICO_LOCAL_PORT_MANAGED},
    };
    const gridpico_local_profile profile = {
        .midi_peer_limit = 3,
        .managed_branch_limit = 1,
        .managed_fanout_limit = 1,
        .occurrence_limit = 2,
        .release_credit_limit = 2,
        .dynamic_device_limit = 4,
        .dynamic_port_limit = 5,
    };
    gridpico_patch_graph graph;
    assert(gridpico_patch_graph_init_local_profile(&graph, &registry, &profile, reject_note, reject_raw,
                                                   NULL) == GRIDPICO_OK);
    gridpico_patch_session session;
    gridpico_patch_session_init(&session, &graph, gridpico_patch_graph_read_graph,
                                gridpico_patch_graph_set_route);
    gridpico_patch_session_bind_local_routing(&session, read_local_state, NULL, NULL);
    assert(gridpico_patch_session_add_apc_attack_source(&session, 0, EXISTING_INPUT) == GRIDPICO_UNAVAILABLE);
    const gridpico_apc_patch_layout layout = {.rows = {EXISTING_INPUT}, .columns = {EXISTING_OUTPUT}};
    assert(gridpico_patch_session_bind_apc_patch(&session, &layout, AUDITION) == GRIDPICO_OK);
    set_connection(&graph, EXISTING_INPUT, 2, GRIDPICO_PATCH_CONNECTED);
    play_note(&session, &graph, EXISTING_INPUT, 2, 100);

    const gridpico_patch_device added_device = {4, "Added instrument", GRIDPICO_PATCH_DISCONNECTED};
    const gridpico_patch_port added_ports[] = {
        {ADDED_INPUT, 4, "Added input", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
        {ADDED_OUTPUT, 4, "Added output", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
    };
    const size_t old_port_count = graph.inventory.port_count;
    assert(gridpico_patch_graph_append_managed_device(&graph, &added_device, added_ports, 2) == GRIDPICO_OK);
    const gridpico_revision appended_revision = graph.revision;
    const size_t reads_before_hook = graph_reads;
    assert(gridpico_patch_session_add_apc_attack_source(&session, old_port_count, ADDED_INPUT) == GRIDPICO_OK);
    assert(gridpico_patch_session_add_apc_attack_source(NULL, old_port_count, ADDED_INPUT) == GRIDPICO_INVALID);
    assert(gridpico_patch_session_add_apc_attack_source(&session, GRIDPICO_PATCH_MAX_PORTS, ADDED_INPUT) ==
           GRIDPICO_INVALID);
    assert(gridpico_patch_session_add_apc_attack_source(&session, 1, 0) == GRIDPICO_INVALID);
    assert(gridpico_patch_session_add_apc_attack_source(&session, 2, AUDITION) == GRIDPICO_INVALID);
    assert(gridpico_patch_session_add_apc_attack_source(&session, old_port_count, EXISTING_INPUT) ==
           GRIDPICO_INVALID);
    assert(gridpico_patch_session_add_apc_attack_source(&session, old_port_count + 1, ADDED_INPUT) ==
           GRIDPICO_INVALID);
    assert(graph_reads == reads_before_hook && graph.revision == appended_revision);

    gridpico_apc_patch_snapshot snapshot;
    assert(gridpico_patch_session_read_apc_patch(&session, &snapshot) == GRIDPICO_OK);
    assert(snapshot.layout_revision == 1 && snapshot.graph_revision == appended_revision);
    assert(snapshot.layout.rows[0] == EXISTING_INPUT && snapshot.layout.rows[1] == 0);
    assert(snapshot.layout.columns[0] == EXISTING_OUTPUT && snapshot.layout.columns[1] == 0);
    assert(snapshot.port_count == 4 && snapshot.ports[2].id == ADDED_INPUT && !snapshot.ports[2].connected);
    assert(snapshot.ports[0].last_attack_sequence == 1 && snapshot.ports[0].last_attack_us == 100);
    assert(snapshot.ports[2].last_attack_sequence == 0 && snapshot.ports[3].id == ADDED_OUTPUT);

    set_connection(&graph, ADDED_INPUT, 2, GRIDPICO_PATCH_CONNECTED);
    play_note(&session, &graph, ADDED_INPUT, 2, 200);
    assert(gridpico_patch_session_add_apc_attack_source(&session, old_port_count, ADDED_INPUT) == GRIDPICO_OK);
    assert(gridpico_patch_session_add_apc_attack_source(&session, 1, EXISTING_INPUT) == GRIDPICO_INVALID);
    assert(gridpico_patch_session_read_apc_patch(&session, &snapshot) == GRIDPICO_OK);
    assert(snapshot.layout_revision == 1 && snapshot.attack_sequence == 2);
    assert(snapshot.ports[0].last_attack_sequence == 1 && snapshot.ports[0].last_attack_us == 100);
    assert(snapshot.ports[2].last_attack_sequence == 2 && snapshot.ports[2].last_attack_us == 200);
    assert(snapshot.ports[3].last_attack_sequence == 0);

    set_connection(&graph, ADDED_INPUT, 2, GRIDPICO_PATCH_DISCONNECTED);
    set_connection(&graph, ADDED_INPUT, 3, GRIDPICO_PATCH_CONNECTED);
    play_note(&session, &graph, ADDED_INPUT, 3, 300);
    assert(gridpico_patch_session_read_apc_patch(&session, &snapshot) == GRIDPICO_OK);
    assert(snapshot.attack_sequence == 3 && snapshot.ports[2].last_attack_sequence == 3);
    assert(snapshot.ports[2].id == ADDED_INPUT && snapshot.ports[2].last_attack_us == 300);
    return 0;
}
