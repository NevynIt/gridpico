#include "core/midi/patch_graph.h"
#include "core/session/patch_session.h"

#include <assert.h>
#include <string.h>

enum { KEYS_A = 1, KEYS_B, AUDITION, OUT_B, OUT_C, OUT_A, RAW_IN, RAW_OUT };

typedef struct {
    gridpico_patch_graph graph;
    gridpico_patch_session session;
    size_t deliveries;
} fixture;

static const gridpico_apc_patch_layout factory_layout = {
    .rows = {KEYS_A, KEYS_B},
    .columns = {OUT_B, OUT_C, OUT_A},
};

static const gridpico_local_registry registry = {
    .inventory = {
        .device_count = 4,
        .port_count = 8,
        .devices = {
            {1, "Instrument A", GRIDPICO_PATCH_DISCONNECTED},
            {2, "Instrument B", GRIDPICO_PATCH_DISCONNECTED},
            {3, "Instrument C", GRIDPICO_PATCH_DISCONNECTED},
            {4, "Internal", GRIDPICO_PATCH_DISCONNECTED},
        },
        .ports = {
            {KEYS_A, 1, "Keys A", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {KEYS_B, 2, "Keys B", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {AUDITION, 4, "Audition", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {OUT_B, 2, "Out B", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {OUT_C, 3, "Out C", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {OUT_A, 1, "Out A", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            {RAW_IN, 4, "Raw in", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, false},
            {RAW_OUT, 3, "Raw out", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, false},
        },
    },
    .port_capabilities = {
        GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
        GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_LOCAL_PORT_MANAGED,
        GRIDPICO_LOCAL_PORT_RAW, GRIDPICO_LOCAL_PORT_RAW,
    },
};

static gridpico_result deliver_note(void *context, const gridpico_note_delivery *delivery)
{
    fixture *runtime = context;
    assert(delivery->destination.endpoint == OUT_C);
    ++runtime->deliveries;
    return GRIDPICO_OK;
}

static gridpico_result reject_raw(void *context, const gridpico_local_raw_delivery *delivery)
{
    (void)context;
    (void)delivery;
    return GRIDPICO_INVALID;
}

static void init_fixture_with_registry(fixture *runtime, size_t managed_limit,
                                        const gridpico_local_registry *configured_registry)
{
    const gridpico_local_profile profile = {
        .midi_peer_limit = 8,
        .managed_branch_limit = managed_limit,
        .raw_branch_limit = 1,
        .managed_fanout_limit = 2,
        .occurrence_limit = 2,
        .release_credit_limit = 4,
    };
    runtime->deliveries = 0;
    assert(gridpico_patch_graph_init_local_profile(&runtime->graph, configured_registry, &profile,
                                                  deliver_note, reject_raw, runtime) == GRIDPICO_OK);
    gridpico_patch_session_init(&runtime->session, &runtime->graph, gridpico_patch_graph_read_graph,
                                gridpico_patch_graph_set_route);
    gridpico_patch_session_bind_local_routes(&runtime->session, gridpico_patch_graph_set_local_route,
                                             gridpico_patch_graph_read_local_route);
    gridpico_patch_session_bind_local_routing(&runtime->session, gridpico_patch_graph_read_local_state,
                                              NULL, NULL);
    assert(gridpico_patch_session_bind_apc_patch(&runtime->session, &factory_layout, AUDITION) ==
           GRIDPICO_OK);
}

static void init_fixture(fixture *runtime, size_t managed_limit)
{
    init_fixture_with_registry(runtime, managed_limit, &registry);
}

static void update_port(fixture *runtime, gridpico_port_id id, gridpico_patch_connection_state state)
{
    const gridpico_patch_port_update update = {{id, 2}, state};
    assert(gridpico_patch_graph_update_port(&runtime->graph, &update) == GRIDPICO_OK);
}

static void enable(fixture *runtime, gridpico_port_id source, gridpico_port_id destination,
                   gridpico_local_route_mode mode)
{
    const gridpico_local_route_request request = {{source, destination, mode}, true};
    assert(gridpico_patch_session_set_local_route(&runtime->session, &request) == GRIDPICO_OK);
}

static void expect_layout(const gridpico_apc_patch_layout *actual, const gridpico_apc_patch_layout *expected)
{
    assert(memcmp(actual->rows, expected->rows, sizeof(actual->rows)) == 0);
    assert(memcmp(actual->columns, expected->columns, sizeof(actual->columns)) == 0);
}

static void expect_routes(const gridpico_apc_session_snapshot *actual,
                          const gridpico_apc_session_snapshot *expected)
{
    assert(actual->route_count == expected->route_count);
    for (size_t i = 0; i < actual->route_count; ++i) {
        assert(actual->routes[i].source == expected->routes[i].source);
        assert(actual->routes[i].destination == expected->routes[i].destination);
    }
    for (size_t i = actual->route_count; i < GRIDPICO_APC_SESSION_SNAPSHOT_ROUTES; ++i) {
        assert(actual->routes[i].source == 0 && actual->routes[i].destination == 0);
    }
}

static void expect_graph(fixture *runtime, size_t route_count)
{
    gridpico_local_graph_state graph;
    assert(gridpico_patch_graph_read_local_state(&runtime->graph, &graph) == GRIDPICO_OK);
    assert(graph.revision == route_count && graph.route_count == route_count);
    for (size_t i = 0; i < graph.route_count; ++i) {
        assert(graph.routes[i].state == GRIDPICO_PATCH_ROUTE_PENDING);
    }
    assert(runtime->deliveries == 0);
}

static gridpico_result set_apc_cell(fixture *runtime, const gridpico_apc_patch_snapshot *snapshot,
                                  uint8_t row, uint8_t column, bool enabled)
{
    return gridpico_patch_session_set_apc_patch_cell(&runtime->session, snapshot->layout_revision,
                                                    snapshot->graph_revision, row, column, enabled);
}

static void verify_native_virtual_cable(void)
{
    const gridpico_apc_session_snapshot saved = {
        .layout = factory_layout,
        .route_count = 1,
        .routes = {{KEYS_A, OUT_A}},
    };
    fixture runtime;
    init_fixture(&runtime, 4);
    gridpico_apc_patch_snapshot patch;
    assert(gridpico_patch_session_read_apc_patch(&runtime.session, &patch) == GRIDPICO_OK);
    assert(!patch.cells[0][2].compatible);
    assert(set_apc_cell(&runtime, &patch, 0, 2, true) == GRIDPICO_INVALID);
    assert(gridpico_patch_session_restore_apc_snapshot(&runtime.session, &saved) == GRIDPICO_INVALID);
    expect_graph(&runtime, 0);

    gridpico_local_registry configured = registry;
    configured.native_virtual_cable = (gridpico_local_native_virtual_cable){KEYS_A, OUT_A};
    init_fixture_with_registry(&runtime, 4, &configured);
    assert(gridpico_patch_session_read_apc_patch(&runtime.session, &patch) == GRIDPICO_OK);
    assert(patch.cells[0][2].compatible && !patch.cells[0][2].available);
    assert(patch.cells[0][2].state == GRIDPICO_PATCH_ROUTE_OFF);
    assert(!patch.cells[1][0].compatible);
    assert(set_apc_cell(&runtime, &patch, 1, 0, true) == GRIDPICO_INVALID);
    assert(set_apc_cell(&runtime, &patch, 0, 2, true) == GRIDPICO_OK);
    assert(gridpico_patch_session_read_apc_patch(&runtime.session, &patch) == GRIDPICO_OK);
    assert(patch.cells[0][2].state == GRIDPICO_PATCH_ROUTE_PENDING);

    update_port(&runtime, KEYS_A, GRIDPICO_PATCH_CONNECTED);
    update_port(&runtime, OUT_A, GRIDPICO_PATCH_CONNECTED);
    assert(gridpico_patch_session_read_apc_patch(&runtime.session, &patch) == GRIDPICO_OK);
    assert(patch.cells[0][2].available && patch.cells[0][2].state == GRIDPICO_PATCH_ROUTE_ACTIVE);
    assert(set_apc_cell(&runtime, &patch, 0, 2, false) == GRIDPICO_OK);
    assert(gridpico_patch_session_read_apc_patch(&runtime.session, &patch) == GRIDPICO_OK);
    assert(patch.cells[0][2].available && patch.cells[0][2].state == GRIDPICO_PATCH_ROUTE_OFF);

    assert(patch.cells[0][0].compatible && patch.cells[1][2].compatible);
    assert(set_apc_cell(&runtime, &patch, 0, 0, true) == GRIDPICO_OK);
    assert(gridpico_patch_session_read_apc_patch(&runtime.session, &patch) == GRIDPICO_OK);
    assert(set_apc_cell(&runtime, &patch, 1, 2, true) == GRIDPICO_INVALID);

    init_fixture_with_registry(&runtime, 4, &configured);
    assert(gridpico_patch_session_restore_apc_snapshot(&runtime.session, &saved) == GRIDPICO_OK);
    expect_graph(&runtime, 1);
    gridpico_apc_session_snapshot exported;
    assert(gridpico_patch_session_export_apc_snapshot(&runtime.session, &exported) == GRIDPICO_OK);
    expect_layout(&exported.layout, &saved.layout);
    expect_routes(&exported, &saved);
}

int main(void)
{
    verify_native_virtual_cable();

    fixture live;
    init_fixture(&live, 4);
    enable(&live, KEYS_B, OUT_C, GRIDPICO_LOCAL_MANAGED);
    enable(&live, KEYS_A, OUT_C, GRIDPICO_LOCAL_MANAGED);
    enable(&live, AUDITION, OUT_B, GRIDPICO_LOCAL_MANAGED);
    enable(&live, KEYS_A, OUT_B, GRIDPICO_LOCAL_MANAGED);
    update_port(&live, KEYS_A, GRIDPICO_PATCH_CONNECTED);
    update_port(&live, OUT_C, GRIDPICO_PATCH_CONNECTED);
    update_port(&live, RAW_IN, GRIDPICO_PATCH_CONNECTED);
    update_port(&live, RAW_OUT, GRIDPICO_PATCH_CONNECTED);
    enable(&live, RAW_IN, RAW_OUT, GRIDPICO_LOCAL_RAW);

    gridpico_ingress_note note = {{KEYS_A, 2}, {GRIDPICO_NOTE_ON, 0, 60, 64}};
    assert(gridpico_patch_graph_submit_note(&live.graph, &note) == GRIDPICO_OK);
    assert(live.deliveries == 1);
    assert(gridpico_patch_session_move_apc_patch(&live.session, 1, GRIDPICO_PATCH_INPUT, 0, 4) ==
           GRIDPICO_OK);
    unsigned char live_graph[sizeof(live.graph)];
    memcpy(live_graph, &live.graph, sizeof(live_graph));

    const gridpico_apc_session_snapshot expected = {
        .layout = {.rows = {0, KEYS_B, 0, 0, KEYS_A}, .columns = {OUT_B, OUT_C, OUT_A}},
        .route_count = 3,
        .routes = {{KEYS_A, OUT_B}, {KEYS_A, OUT_C}, {KEYS_B, OUT_C}},
    };
    gridpico_apc_session_snapshot exported;
    assert(gridpico_patch_session_export_apc_snapshot(&live.session, &exported) == GRIDPICO_OK);
    expect_layout(&exported.layout, &expected.layout);
    expect_routes(&exported, &expected);
    assert(gridpico_patch_session_restore_apc_snapshot(&live.session, &exported) == GRIDPICO_UNAVAILABLE);

    fixture staged;
    init_fixture(&staged, 4);
    gridpico_apc_session_snapshot invalid = exported;
    invalid.route_count = GRIDPICO_APC_SESSION_SNAPSHOT_ROUTES + 1;
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &invalid) == GRIDPICO_CAPACITY);
    invalid = exported;
    invalid.layout.rows[0] = AUDITION;
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &invalid) == GRIDPICO_INVALID);
    invalid.layout.rows[0] = KEYS_B;
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &invalid) == GRIDPICO_INVALID);
    invalid = exported;
    invalid.layout.columns[7] = KEYS_A;
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &invalid) == GRIDPICO_INVALID);
    invalid.layout = exported.layout;
    invalid.routes[2] = invalid.routes[0];
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &invalid) == GRIDPICO_INVALID);
    invalid.routes[2].source = 0;
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &invalid) == GRIDPICO_INVALID);
    invalid.routes[2].source = RAW_IN;
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &invalid) == GRIDPICO_INVALID);
    invalid.routes[2].source = 99;
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &invalid) == GRIDPICO_INVALID);
    expect_graph(&staged, 0);

    invalid.routes[2] = (gridpico_apc_session_route){KEYS_B, OUT_A};
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &invalid) == GRIDPICO_INVALID);
    gridpico_apc_session_snapshot stage_state;
    assert(gridpico_patch_session_export_apc_snapshot(&staged.session, &stage_state) == GRIDPICO_OK);
    expect_layout(&stage_state.layout, &factory_layout);

    init_fixture(&staged, 1);
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &exported) == GRIDPICO_CAPACITY);
    assert(gridpico_patch_session_export_apc_snapshot(&staged.session, &stage_state) == GRIDPICO_OK);
    expect_layout(&stage_state.layout, &factory_layout);

    init_fixture(&staged, 4);
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &exported) == GRIDPICO_OK);
    expect_graph(&staged, 3);
    assert(gridpico_patch_session_export_apc_snapshot(&staged.session, &stage_state) == GRIDPICO_OK);
    expect_layout(&stage_state.layout, &expected.layout);
    expect_routes(&stage_state, &expected);

    init_fixture(&staged, 4);
    const gridpico_apc_session_snapshot empty = {.layout = expected.layout};
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &empty) == GRIDPICO_OK);
    expect_graph(&staged, 0);
    assert(gridpico_patch_session_export_apc_snapshot(&staged.session, &stage_state) == GRIDPICO_OK);
    expect_layout(&stage_state.layout, &expected.layout);
    expect_routes(&stage_state, &empty);

    init_fixture(&staged, 4);
    update_port(&staged, KEYS_A, GRIDPICO_PATCH_CONNECTED);
    update_port(&staged, KEYS_A, GRIDPICO_PATCH_DISCONNECTED);
    assert(gridpico_patch_session_restore_apc_snapshot(&staged.session, &exported) == GRIDPICO_UNAVAILABLE);

    assert(memcmp(live_graph, &live.graph, sizeof(live_graph)) == 0);
    assert(live.deliveries == 1);
    note.note.kind = GRIDPICO_NOTE_OFF;
    note.note.velocity = 32;
    assert(gridpico_patch_graph_submit_note(&live.graph, &note) == GRIDPICO_OK);
    assert(live.deliveries == 2);
    return 0;
}
