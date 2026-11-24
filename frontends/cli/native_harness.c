#include "app/patch_runtime.h"

#include <assert.h>
#include <string.h>

typedef struct {
    char output[8192];
    size_t size;
} transcript;

typedef struct {
    gridpico_patch_graph graph;
    gridpico_patch_fixture sim;
    gridpico_patch_session session;
} monitor_fixture;

typedef struct {
    gridpico_patch_graph graph;
    gridpico_patch_session session;
} local_fixture;

typedef struct {
    gridpico_local_routing_snapshot snapshot;
} routing_fixture;

static void capture(void *context, const char *bytes, size_t size)
{
    transcript *out = context;
    assert(size < sizeof out->output - out->size);
    memcpy(out->output + out->size, bytes, size);
    out->size += size;
    out->output[out->size] = '\0';
}

static void clear(transcript *out)
{
    out->size = 0;
    out->output[0] = '\0';
}

static bool contains(const transcript *out, const char *expected)
{
    const size_t expected_size = strlen(expected);
    for (size_t i = 0; i + expected_size <= out->size; ++i) {
        if (memcmp(out->output + i, expected, expected_size) == 0) {
            return true;
        }
    }
    return false;
}

static bool contains_bytes(const transcript *out, const char *expected, size_t expected_size)
{
    for (size_t i = 0; i + expected_size <= out->size; ++i) {
        if (memcmp(out->output + i, expected, expected_size) == 0) {
            return true;
        }
    }
    return false;
}

static void compare_terminal_reply(gridpico_cli_frontend *cli, transcript *terminal,
                                   const char *line,
                                   gridpico_result expected_result)
{
    transcript diagnostics = {0};
    assert(gridpico_cli_execute_line(cli, line, capture, &diagnostics) == expected_result);
    clear(terminal);
    gridpico_cli_receive(cli, line, strlen(line));
    gridpico_cli_receive(cli, "\r\n", 2);
    gridpico_cli_process(cli);
    assert(contains_bytes(terminal, diagnostics.output, diagnostics.size));
}

static void initialize_monitor_inventory(gridpico_patch_inventory *inventory)
{
    *inventory = (gridpico_patch_inventory){
        .device_count = GRIDPICO_PATCH_MONITOR_DEVICES,
        .port_count = GRIDPICO_PATCH_MONITOR_PORTS,
        .devices = {
            {1, "APC Key 25 mk2", GRIDPICO_PATCH_CONNECTED},
            {2, "Simulated dual-port instrument", GRIDPICO_PATCH_CONNECTED},
            {GRIDPICO_PATCH_MONITOR_DEVICE_ID, "Web Monitor", GRIDPICO_PATCH_DISCONNECTED},
        },
        .ports = {
            {1, 1, "Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, true},
            {4, 1, "Panel", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
            {5, 1, "LED feedback", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
            {2, 2, "MIDI A", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, true},
            {3, 2, "MIDI B", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, true},
            {GRIDPICO_PATCH_MONITOR_PORT_ID, GRIDPICO_PATCH_MONITOR_DEVICE_ID,
             "Monitor", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
        },
    };
}

static void test_monitor_cli(transcript *terminal)
{
    monitor_fixture monitor = {0};
    gridpico_patch_inventory inventory;
    initialize_monitor_inventory(&inventory);
    assert(gridpico_patch_fixture_init_monitor(&monitor.sim, &inventory) == GRIDPICO_OK);
    assert(gridpico_patch_graph_init_monitor(&monitor.graph, &inventory,
                                             gridpico_patch_fixture_deliver,
                                             &monitor.sim) == GRIDPICO_OK);
    gridpico_patch_session_init(&monitor.session, &monitor.graph,
                                gridpico_patch_graph_read_graph,
                                gridpico_patch_graph_set_route);

    gridpico_cli_frontend cli;
    assert(gridpico_cli_init(&cli, &monitor.session,
                             gridpico_patch_session_read_snapshot,
                             gridpico_session_read_device_color,
                             gridpico_session_set_device_color,
                             NULL, NULL, capture, terminal) == GRIDPICO_OK);
    const gridpico_patch_route_request route = {
        .source = 1,
        .destination = GRIDPICO_PATCH_MONITOR_PORT_ID,
        .enabled = true,
    };
    assert(gridpico_patch_session_set_route(&monitor.session, &route) == GRIDPICO_OK);

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "inventory", capture, terminal) == GRIDPICO_OK);
    assert(contains(terminal, "DEVICE 3 disconnected unset \"Web Monitor\"\r\n"));
    assert(contains(terminal, "PORT 6 3 out disconnected col:1:3 \"Monitor\"\r\nOK\r\n"));

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "status", capture, terminal) == GRIDPICO_OK);
    assert(strcmp(terminal->output, "STATUS 1 3 6 0 1\r\nOK\r\n") == 0);

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "color get 3", capture, terminal) == GRIDPICO_OK);
    assert(strcmp(terminal->output, "COLOR 3 unset\r\nOK\r\n") == 0);
    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "color set 3 #12aBef", capture, terminal) == GRIDPICO_OK);
    assert(strcmp(terminal->output, "COLOR 3 #12ABEF\r\nOK\r\n") == 0);

    gridpico_device_color color;
    assert(gridpico_session_read_device_color(&monitor.session,
                                               GRIDPICO_PATCH_MONITOR_DEVICE_ID,
                                               &color) == GRIDPICO_OK);
    assert(color.revision == 1 && color.is_set && color.rgb.red == 0x12 &&
           color.rgb.green == 0xab && color.rgb.blue == 0xef);
    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "inventory", capture, terminal) == GRIDPICO_OK);
    assert(contains(terminal, "DEVICE 3 disconnected #12ABEF \"Web Monitor\""));
    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "status", capture, terminal) == GRIDPICO_OK);
    assert(strcmp(terminal->output, "STATUS 1 3 6 0 1\r\nOK\r\n") == 0);
    compare_terminal_reply(&cli, terminal, "color get 3", GRIDPICO_OK);
}

static gridpico_result accept_managed_delivery(void *context,
                                              const gridpico_note_delivery *delivery)
{
    (void)context;
    (void)delivery;
    return GRIDPICO_OK;
}

static gridpico_result accept_raw_delivery(void *context,
                                           const gridpico_local_raw_delivery *delivery)
{
    (void)context;
    (void)delivery;
    return GRIDPICO_OK;
}

static gridpico_result read_fixture_availability(
    void *context, const gridpico_local_registry *registry,
    gridpico_local_port_availability out[GRIDPICO_PATCH_MAX_PORTS])
{
    (void)context;
    for (size_t i = 0; i < registry->inventory.port_count; ++i) {
        out[i] = registry->inventory.ports[i].state == GRIDPICO_PATCH_CONNECTED
                     ? GRIDPICO_LOCAL_IN_USE : GRIDPICO_LOCAL_FREE;
    }
    return GRIDPICO_OK;
}

static void test_local_route_cli(transcript *terminal)
{
    local_fixture local = {0};
    const gridpico_local_registry registry = {
        .inventory = {
            .device_count = 4,
            .port_count = GRIDPICO_LOCAL_PORTS,
            .devices = {
                {1, "Keybed", GRIDPICO_PATCH_DISCONNECTED},
                {2, "Managed output", GRIDPICO_PATCH_DISCONNECTED},
                {3, "Raw input", GRIDPICO_PATCH_DISCONNECTED},
                {4, "Raw output", GRIDPICO_PATCH_DISCONNECTED},
            },
            .ports = {
                {1, 1, "Keys", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, true},
                {2, 2, "MIDI", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
                {3, 3, "Raw in", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, false},
                {4, 4, "Raw out", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, false},
                {5, 1, "Panel", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_DISCONNECTED, false},
                {6, 2, "MIDI 2", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, true},
            },
        },
        .port_capabilities = {
            GRIDPICO_LOCAL_PORT_MANAGED,
            GRIDPICO_LOCAL_PORT_MANAGED,
            GRIDPICO_LOCAL_PORT_RAW,
            GRIDPICO_LOCAL_PORT_RAW,
            GRIDPICO_LOCAL_PORT_PANEL,
            GRIDPICO_LOCAL_PORT_MANAGED,
        },
    };
    assert(gridpico_patch_graph_init_local(&local.graph, &registry,
                                          accept_managed_delivery,
                                          accept_raw_delivery, NULL) == GRIDPICO_OK);
    gridpico_patch_session_init(&local.session, &local.graph,
                                gridpico_patch_graph_read_graph,
                                gridpico_patch_graph_set_route);
    gridpico_patch_session_bind_local_routes(&local.session,
                                             gridpico_patch_graph_set_local_route,
                                             gridpico_patch_graph_read_local_route);
    gridpico_patch_session_bind_local_routing(&local.session,
                                              gridpico_patch_graph_read_local_state,
                                              read_fixture_availability, NULL);
    for (gridpico_port_id id = 1; id <= 6; ++id) {
        if (id == 5) continue;
        const gridpico_patch_port_update connect = {
            .connection = {.endpoint = id, .generation = 2},
            .state = GRIDPICO_PATCH_CONNECTED,
        };
        assert(gridpico_patch_graph_update_port(&local.graph, &connect) == GRIDPICO_OK);
    }

    gridpico_cli_frontend cli;
    assert(gridpico_cli_init(&cli, &local.session,
                             gridpico_patch_session_read_snapshot,
                             gridpico_session_read_device_color,
                             gridpico_session_set_device_color,
                             NULL, NULL, capture, terminal) == GRIDPICO_OK);
    cli.set_local_route = gridpico_patch_session_set_local_route;
    cli.read_local_route = gridpico_patch_session_read_local_route;
    cli.read_local_routing = gridpico_patch_session_read_local_routing;

    clear(terminal);
    gridpico_cli_receive(&cli, "help\n", sizeof("help\n") - 1);
    gridpico_cli_process(&cli);
    assert(contains(terminal, " * route\r\n\troute <src> <dst> managed|raw [on|off]"));
    assert(!contains(terminal, " * logs"));

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing", capture, terminal) == GRIDPICO_OK);
    assert(contains(terminal, "ROUTING 1 8\r\n"));
    assert(contains(terminal, "BINDING 1 1 in managed in-use \"Keys\"\r\n"));
    assert(contains(terminal, "OK\r\n"));

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "route 1 2 managed", capture, terminal) == GRIDPICO_OK);
    assert(strcmp(terminal->output, "ROUTE 1 2 MANAGED OFF\r\nOK\r\n") == 0);
    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "route 1 2 managed on", capture, terminal) == GRIDPICO_OK);
    assert(strcmp(terminal->output, "ROUTE 1 2 MANAGED ACTIVE\r\nOK\r\n") == 0);
    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing 1 8", capture, terminal) ==
           GRIDPICO_UNAVAILABLE);
    assert(strcmp(terminal->output, "ERR UNAVAILABLE\r\n") == 0);
    compare_terminal_reply(&cli, terminal, "route 1 2 managed off", GRIDPICO_OK);
    assert(contains(terminal, "ROUTE 1 2 MANAGED OFF\r\nOK\r\n"));
    compare_terminal_reply(&cli, terminal, "route 1 2 managed", GRIDPICO_OK);
    assert(contains(terminal, "ROUTE 1 2 MANAGED OFF\r\nOK\r\n"));

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "route 3 4 raw on", capture, terminal) == GRIDPICO_OK);
    assert(strcmp(terminal->output, "ROUTE 3 4 RAW ACTIVE\r\nOK\r\n") == 0);
    compare_terminal_reply(&cli, terminal, "route 3 4 raw off", GRIDPICO_OK);
    assert(contains(terminal, "ROUTE 3 4 RAW OFF\r\nOK\r\n"));
    compare_terminal_reply(&cli, terminal, "route 3 4 raw", GRIDPICO_OK);
    assert(contains(terminal, "ROUTE 3 4 RAW OFF\r\nOK\r\n"));

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "route 1 4 managed on", capture, terminal) ==
           GRIDPICO_INVALID);
    assert(strcmp(terminal->output, "ERR INVALID\r\n") == 0);
    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "route 1 2 managed enabled", capture, terminal) ==
           GRIDPICO_INVALID);
    assert(strcmp(terminal->output, "ERR INVALID\r\n") == 0);
}

static gridpico_result unused_snapshot(void *context, gridpico_patch_snapshot *out)
{
    (void)context;
    (void)out;
    return GRIDPICO_UNAVAILABLE;
}

static gridpico_result read_routing_fixture(void *context,
                                            gridpico_local_routing_snapshot *out)
{
    routing_fixture *fixture = context;
    *out = fixture->snapshot;
    return GRIDPICO_OK;
}

static void assert_routing_record_bounds(const transcript *output)
{
    size_t record_start = 0;
    for (size_t i = 0; i + 1 < output->size; ++i) {
        if (output->output[i] == '\r' && output->output[i + 1] == '\n') {
            assert(i + 2 - record_start <= GRIDPICO_LOCAL_ROUTING_RECORD_BYTES);
            record_start = i + 2;
            ++i;
        }
    }
}

static void initialize_routing_snapshot(routing_fixture *fixture)
{
    gridpico_local_routing_snapshot *snapshot = &fixture->snapshot;
    snapshot->revision = 41;
    snapshot->graph.registry.inventory = (gridpico_patch_inventory){
        .device_count = 8,
        .port_count = 18,
        .devices = {
            {1, "Web \"APC\"", GRIDPICO_PATCH_CONNECTED},
            {2, "Diagnostics \xC3\xBC", GRIDPICO_PATCH_DISCONNECTED},
            {3, "Monitor", GRIDPICO_PATCH_CONNECTED},
            {4, "Log", GRIDPICO_PATCH_CONNECTED},
            {5, "Keys A", GRIDPICO_PATCH_CONNECTED},
            {6, "Keys B", GRIDPICO_PATCH_CONNECTED},
            {7, "Instruments A", GRIDPICO_PATCH_CONNECTED},
            {8, "Instruments B", GRIDPICO_PATCH_CONNECTED},
        },
        .ports = {
            {1, 1, "Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
            {2, 1, "Panel", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
            {3, 1, "LED feedback", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
            {4, 2, "LED Sequence", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
            {5, 1, "Monitor", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
            {6, 2, "Log", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_DISCONNECTED, false},
            {7, 5, "A1", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
            {8, 6, "B1", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
            {9, 7, "A2", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
            {10, 8, "B2", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
            {11, 8, "B3", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
            {12, 5, "Instrument A1", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
            {13, 6, "Instrument B1", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
            {14, 7, "Instrument A2", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
            {15, 8, "Instrument B2", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
            {16, 8, "Instrument B3", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
            {17, 8, "Instrument B4", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
            {18, 8, "Instrument B5", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
        },
    };
    memset(snapshot->graph.registry.inventory.ports[5].name, '\x01',
           GRIDPICO_PATCH_NAME_BYTES - 1);
    snapshot->graph.registry.inventory.ports[5].name[GRIDPICO_PATCH_NAME_BYTES - 1] = '\0';
    snapshot->graph.registry.port_capabilities[0] = GRIDPICO_LOCAL_PORT_MANAGED;
    snapshot->graph.registry.port_capabilities[1] = GRIDPICO_LOCAL_PORT_PANEL;
    snapshot->graph.registry.port_capabilities[2] = GRIDPICO_LOCAL_PORT_RAW;
    snapshot->graph.registry.port_capabilities[3] = GRIDPICO_LOCAL_PORT_RAW;
    snapshot->graph.registry.port_capabilities[4] = GRIDPICO_LOCAL_PORT_MANAGED;
    snapshot->graph.registry.port_capabilities[5] = GRIDPICO_LOCAL_PORT_MANAGED;
    for (size_t i = 6; i < 11; ++i)
        snapshot->graph.registry.port_capabilities[i] = GRIDPICO_LOCAL_PORT_MANAGED;
    for (size_t i = 11; i < 18; ++i)
        snapshot->graph.registry.port_capabilities[i] = GRIDPICO_LOCAL_PORT_MANAGED;
    for (size_t i = 0; i < 18; ++i)
        snapshot->availability[i] = (gridpico_local_port_availability)(i % 3);
    snapshot->availability[2] = GRIDPICO_LOCAL_UNAVAILABLE;
    snapshot->availability[5] = GRIDPICO_LOCAL_UNAVAILABLE;
    snapshot->colors[0] = (gridpico_device_color){
        .revision = 3, .is_set = true, .rgb = {0xab, 0x12, 0xef},
    };
    for (size_t i = 1; i < 8; ++i)
        snapshot->colors[i] = (gridpico_device_color){.revision = 3, .is_set = false};
    snapshot->graph.route_count = 6;
    snapshot->graph.routes[0] = (gridpico_local_enabled_route){
        .route = {4, 3, GRIDPICO_LOCAL_RAW}, .state = GRIDPICO_PATCH_ROUTE_ACTIVE,
    };
    snapshot->graph.routes[1] = (gridpico_local_enabled_route){
        .route = {1, 6, GRIDPICO_LOCAL_MANAGED}, .state = GRIDPICO_PATCH_ROUTE_PENDING,
    };
    snapshot->graph.routes[2] = (gridpico_local_enabled_route){
        .route = {1, 5, GRIDPICO_LOCAL_MANAGED}, .state = GRIDPICO_PATCH_ROUTE_ACTIVE,
    };
    snapshot->graph.routes[3] = (gridpico_local_enabled_route){
        .route = {7, 12, GRIDPICO_LOCAL_MANAGED}, .state = GRIDPICO_PATCH_ROUTE_ACTIVE,
    };
    snapshot->graph.routes[4] = (gridpico_local_enabled_route){
        .route = {8, 13, GRIDPICO_LOCAL_MANAGED}, .state = GRIDPICO_PATCH_ROUTE_PENDING,
    };
    snapshot->graph.routes[5] = (gridpico_local_enabled_route){
        .route = {9, 14, GRIDPICO_LOCAL_MANAGED}, .state = GRIDPICO_PATCH_ROUTE_ACTIVE,
    };
}

static void test_routing_cli(transcript *terminal)
{
    routing_fixture fixture = {0};
    initialize_routing_snapshot(&fixture);
    gridpico_cli_frontend cli;
    assert(gridpico_cli_init(&cli, &fixture, unused_snapshot, NULL, NULL,
                             NULL, NULL, capture, terminal) == GRIDPICO_OK);
    cli.read_local_routing = read_routing_fixture;

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "help", capture, terminal) == GRIDPICO_OK);
    assert(contains(terminal, "COMMAND routing | routing <revision> <offset>"));
    clear(terminal);
    gridpico_cli_receive(&cli, "help\n", sizeof("help\n") - 1);
    gridpico_cli_process(&cli);
    assert(contains(terminal,
                    " * routing\r\n\trouting | routing <revision> <offset> -- read a bounded Routing snapshot page\r\n"));

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing", capture, terminal) == GRIDPICO_OK);
    assert(contains(terminal, "ROUTING 41 8\r\n"));
    assert(contains(terminal, "DEVICE 1 connected #AB12EF \"Web \\\"APC\\\"\"\r\n"));
    assert(contains(terminal, "DEVICE 2 disconnected unset \"Diagnostics \xC3\xBC\"\r\n"));
    assert(contains(terminal, "DEVICE 8 connected unset \"Instruments B\"\r\n"));
    assert(contains(terminal, "OK\r\n"));
    assert(!contains(terminal, "ROUTE "));
    assert_routing_record_bounds(terminal);

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing 41 8", capture, terminal) == GRIDPICO_OK);
    assert(contains(terminal, "ROUTING 41 16\r\n"));
    assert(contains(terminal, "BINDING 3 1 out raw unavailable \"LED feedback\"\r\n"));
    assert(contains(terminal, "BINDING 6 2 out managed unavailable \"\\u0001\\u0001"));
    assert(!contains(terminal, "ROUTE "));
    assert_routing_record_bounds(terminal);

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing 41 16", capture, terminal) == GRIDPICO_OK);
    assert(contains(terminal, "ROUTING 41 24\r\n"));
    assert(contains(terminal, "BINDING 9 7 in managed in-use \"A2\"\r\n"));
    assert(contains(terminal, "BINDING 16 8 out managed unavailable \"Instrument B3\"\r\n"));
    assert(!contains(terminal, "ROUTE "));
    assert_routing_record_bounds(terminal);

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing 41 24", capture, terminal) == GRIDPICO_OK);
    assert(strcmp(terminal->output,
                  "ROUTING 41 -\r\n"
                  "BINDING 17 8 out managed free \"Instrument B4\"\r\n"
                  "BINDING 18 8 out managed in-use \"Instrument B5\"\r\n"
                  "ROUTE 1 5 MANAGED ACTIVE\r\n"
                  "ROUTE 1 6 MANAGED PENDING\r\n"
                  "ROUTE 4 3 RAW ACTIVE\r\n"
                  "ROUTE 7 12 MANAGED ACTIVE\r\n"
                  "ROUTE 8 13 MANAGED PENDING\r\n"
                  "ROUTE 9 14 MANAGED ACTIVE\r\n"
                  "OK\r\n") == 0);
    assert_routing_record_bounds(terminal);

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing 41 32", capture, terminal) == GRIDPICO_OK);
    assert(strcmp(terminal->output, "ROUTING 41 -\r\nOK\r\n") == 0);

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing 41 999", capture, terminal) ==
           GRIDPICO_INVALID);
    assert(strcmp(terminal->output, "ERR INVALID\r\n") == 0);
    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing 18446744073709551615 0",
                                    capture, terminal) == GRIDPICO_UNAVAILABLE);
    assert(strcmp(terminal->output, "ERR UNAVAILABLE\r\n") == 0);

    clear(terminal);
    gridpico_cli_receive(&cli, "routing\r\n", sizeof("routing\r\n") - 1);
    gridpico_cli_process(&cli);
    assert(contains(terminal, "ROUTING 41 8\r\n"));
    assert(contains(terminal, "OK\r\n"));

    fixture.snapshot.revision = 42;
    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing 41 8", capture, terminal) ==
           GRIDPICO_UNAVAILABLE);
    assert(strcmp(terminal->output, "ERR UNAVAILABLE\r\n") == 0);

    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing 0 0", capture, terminal) ==
           GRIDPICO_INVALID);
    assert(strcmp(terminal->output, "ERR INVALID\r\n") == 0);

    gridpico_cli_frontend unavailable_cli;
    assert(gridpico_cli_init(&unavailable_cli, &fixture, unused_snapshot, NULL, NULL,
                             NULL, NULL, capture, terminal) == GRIDPICO_OK);
    clear(terminal);
    assert(gridpico_cli_execute_line(&unavailable_cli, "routing", capture, terminal) ==
           GRIDPICO_INVALID);
    assert(strcmp(terminal->output, "ERR INVALID\r\n") == 0);
    clear(terminal);
    assert(gridpico_cli_execute_line(&unavailable_cli, "help", capture, terminal) ==
           GRIDPICO_OK);
    assert(!contains(terminal, "COMMAND routing"));
    clear(terminal);
    gridpico_cli_receive(&unavailable_cli, "help\n", sizeof("help\n") - 1);
    gridpico_cli_process(&unavailable_cli);
    assert(!contains(terminal, " * routing"));

    fixture.snapshot.graph.registry.inventory.device_count = GRIDPICO_PATCH_MAX_DEVICES + 1;
    clear(terminal);
    assert(gridpico_cli_execute_line(&cli, "routing", capture, terminal) == GRIDPICO_CAPACITY);
    assert(strcmp(terminal->output, "ERR CAPACITY\r\n") == 0);
}

int main(void)
{
    patch_runtime runtime;
    assert(patch_runtime_init(&runtime) == GRIDPICO_OK);

    transcript terminal = {0};
    gridpico_cli_frontend cli;
    assert(patch_runtime_bind_cli(&runtime, &cli, capture, &terminal) == GRIDPICO_OK);
    assert(cli.set_local_route == NULL && cli.read_local_route == NULL);
    assert(gridpico_cli_execute_line(&cli, "route 1 2 managed", capture, &terminal) ==
           GRIDPICO_INVALID);
    assert(strcmp(terminal.output, "ERR INVALID\r\n") == 0);
    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "help", capture, &terminal) == GRIDPICO_OK);
    assert(contains(&terminal, "COMMAND inventory -- list devices and ports\r\n"));
    assert(contains(&terminal, "COMMAND color get <device> | color set <device> #RRGGBB"));
    assert(contains(&terminal, "COMMAND logs -- read retained diagnostic records\r\n"));
    assert(contains(&terminal, "OK\r\n"));
    assert(!contains(&terminal, "COMMAND route"));

    const gridpico_patch_route_request route = {
        .source = 1,
        .destination = 2,
        .enabled = true,
    };
    assert(patch_runtime_set_route(&runtime, &route) == GRIDPICO_OK);
    const gridpico_patch_port_update disconnect = {
        .connection = {.endpoint = 2, .generation = 1},
        .state = GRIDPICO_PATCH_DISCONNECTED,
    };
    assert(patch_runtime_update_port(&runtime, &disconnect) == GRIDPICO_OK);

    assert(gridpico_cli_execute_line(&cli, "inventory", capture, &terminal) == GRIDPICO_OK);
    assert(contains(&terminal, "DEVICE 1 connected unset \"APC Key 25 mk2\"\r\n"));
    assert(contains(&terminal, "PORT 1 1 in connected row:1:1 \"Keybed\"\r\n"));
    assert(contains(&terminal, "PORT 4 1 in connected - \"Panel\"\r\n"));
    assert(contains(&terminal, "PORT 5 1 out connected - \"LED feedback\"\r\n"));
    assert(contains(&terminal, "DEVICE 2 connected unset \"Simulated dual-port instrument\"\r\n"));
    assert(contains(&terminal, "PORT 2 2 out disconnected col:1:1 \"MIDI A\"\r\n"));
    assert(contains(&terminal, "PORT 3 2 out connected col:1:2 \"MIDI B\"\r\nOK\r\n"));

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "color get 2", capture, &terminal) == GRIDPICO_OK);
    assert(strcmp(terminal.output, "COLOR 2 unset\r\nOK\r\n") == 0);

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "color set 2 #ff00a2", capture, &terminal) == GRIDPICO_OK);
    assert(strcmp(terminal.output, "COLOR 2 #FF00A2\r\nOK\r\n") == 0);
    gridpico_device_color color;
    assert(gridpico_session_read_device_color(&runtime.session, 2, &color) == GRIDPICO_OK);
    assert(color.revision == 1 && color.is_set);

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "inventory", capture, &terminal) == GRIDPICO_OK);
    assert(contains(&terminal, "DEVICE 2 connected #FF00A2"));

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "status", capture, &terminal) == GRIDPICO_OK);
    assert(strcmp(terminal.output, "STATUS 2 2 5 0 1\r\nOK\r\n") == 0);

    const gridpico_patch_route_request invalid_route = {
        .source = 99,
        .destination = 2,
        .enabled = true,
    };
    assert(patch_runtime_set_route(&runtime, &invalid_route) == GRIDPICO_INVALID);
    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "logs", capture, &terminal) == GRIDPICO_OK);
    assert(strcmp(terminal.output,
                  "LOG 1 PORT 2 disconnected\r\n"
                  "LOG 2 ROUTE_ERROR 2 INVALID\r\nOK\r\n") == 0);

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "color get 99", capture, &terminal) == GRIDPICO_INVALID);
    assert(strcmp(terminal.output, "ERR INVALID\r\n") == 0);

    compare_terminal_reply(&cli, &terminal, "inventory", GRIDPICO_OK);
    compare_terminal_reply(&cli, &terminal, "status", GRIDPICO_OK);
    compare_terminal_reply(&cli, &terminal, "logs", GRIDPICO_OK);
    compare_terminal_reply(&cli, &terminal, "color get 2", GRIDPICO_OK);
    compare_terminal_reply(&cli, &terminal, "color set 2 #FF00A2", GRIDPICO_OK);

    clear(&terminal);
    gridpico_cli_receive(&cli, "help\r\ninventory\r\n",
                         sizeof("help\r\ninventory\r\n") - 1);
    gridpico_cli_process(&cli);
    assert(contains(&terminal, " * inventory\r\n\tinventory -- list devices and ports\r\n"));
    assert(!contains(&terminal, " * route"));
    assert(contains(&terminal, "DEVICE 2 connected #FF00A2"));
    assert(contains(&terminal, "OK\r\n"));

    clear(&terminal);
    gridpico_cli_receive(&cli, "inventory\r\n", sizeof("inventory\r\n") - 1);
    gridpico_cli_process(&cli);
    assert(contains(&terminal, "OK\r\n"));
    clear(&terminal);
    gridpico_cli_receive(&cli, "inventory\n", sizeof("inventory\n") - 1);
    gridpico_cli_process(&cli);
    assert(contains(&terminal, "OK\r\n"));
    clear(&terminal);
    gridpico_cli_receive(&cli, "inventory\r", sizeof("inventory\r") - 1);
    gridpico_cli_process(&cli);
    assert(contains(&terminal, "OK\r\n"));

    clear(&terminal);
    char long_line[GRIDPICO_CLI_LINE_BYTES + 1];
    memset(long_line, 'x', sizeof long_line - 1);
    long_line[sizeof long_line - 1] = '\0';
    assert(gridpico_cli_execute_line(&cli, long_line, capture, &terminal) == GRIDPICO_CAPACITY);
    assert(strcmp(terminal.output, "ERR CAPACITY\r\n") == 0);
    clear(&terminal);
    gridpico_cli_receive(&cli, long_line, sizeof long_line - 1);
    gridpico_cli_receive(&cli, "\r", 1);
    gridpico_cli_process(&cli);
    assert(contains(&terminal, "ERR CAPACITY\r\n"));
    test_monitor_cli(&terminal);
    test_local_route_cli(&terminal);
    test_routing_cli(&terminal);
    return 0;
}
