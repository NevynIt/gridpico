/* Uses the observation stand-ins and exact projection from pat2_projection_host.c. */
static gridpico_pico_auto_routes automatic_routes;
static gridpico_local_routing_snapshot routing_scratch;
static gridpico_pico_pat2_apply apply;
static gridpico_pico_pat2_intent record;
static unsigned role_syncs, nominations;

static gridpico_result sync_apc_roles(apc_roles *roles)
{
    ++role_syncs;
    roles->graph_revision = router.graph.revision;
    return GRIDPICO_OK;
}

static void nominate_controller(apc_roles *roles)
{
    assert(roles->graph_revision == router.graph.revision && role_syncs > nominations);
    ++nominations;
}

static void add_trace(int event, uint8_t interface, const uint8_t *data, size_t size)
{
    (void)event; (void)interface; (void)data; (void)size;
}

#include "policy.inc"

enum { A0 = 0, A1 = 1, B0 = 2, B1 = 3, C0 = 4 };
static const gridpico_pico_peer a_peer = GRIDPICO_PICO_DISCOVERED;

static gridpico_local_route_request route(size_t source, size_t destination, bool on)
{
    return (gridpico_local_route_request){
        {router.discovered[source].input, router.discovered[destination].output, GRIDPICO_LOCAL_MANAGED}, on,
    };
}

static bool desired(gridpico_local_route route)
{
    gridpico_local_route_status status;
    assert(gridpico_patch_graph_read_local_route(&router.graph, &route, &status) == GRIDPICO_OK);
    return status.state != GRIDPICO_PATCH_ROUTE_OFF;
}

static void observe_one_cable(uint8_t index, char serial)
{
    observe(index, serial);
    interfaces[index].rx_cables = interfaces[index].tx_cables = 1;
    observations[index].input_cables = observations[index].output_cables = 1;
    observations[index].identity.inputs = observations[index].identity.outputs = 1;
}

static void admit_one_cable(uint8_t index, size_t device, gridpico_pico_peer peer)
{
    assert(gridpico_pico_router_register_device(&router, peer, observations[index].name, 1, 1) == GRIDPICO_OK);
    discovered[device] = (discovered_device){
        .identity = observations[index].identity, .lifetime = observations[index].lifetime,
        .index = index, .first_peer = peer,
    };
    gridpico_pico_router_attach(&router, peer, true);
}

static void seed_pending_route(gridpico_port_id source, gridpico_port_id destination)
{
    const gridpico_local_route_request request = {{source, destination, GRIDPICO_LOCAL_MANAGED}, true};
    gridpico_local_route_status status;
    assert(gridpico_patch_graph_set_local_route(&router.graph, &request) == GRIDPICO_OK);
    assert(gridpico_patch_graph_read_local_route(&router.graph, &request.route, &status) == GRIDPICO_OK);
    assert(status.state == GRIDPICO_PATCH_ROUTE_PENDING);
}

static void seed_matrix24_pending_routes(void)
{
    const gridpico_patch_device device = {
        .id = 100, .name = "Fixture only", .state = GRIDPICO_PATCH_DISCONNECTED,
    };
    const gridpico_patch_port output = {
        .id = 100, .device = device.id, .name = "Fixture only Out", .direction = GRIDPICO_PATCH_OUTPUT,
        .state = GRIDPICO_PATCH_DISCONNECTED, .patch_eligible = true,
    };
    /* A registry-only OUT keeps the last discovered cable group available for C. */
    assert(router.graph.local_profile.dynamic_port_limit < GRIDPICO_PATCH_MAX_PORTS);
    ++router.graph.local_profile.dynamic_port_limit;
    assert(gridpico_patch_graph_append_managed_device(&router.graph, &device, &output, 1) == GRIDPICO_OK);

    const gridpico_local_native_virtual_cable native = router.graph.native_virtual_cable;
    for (size_t cable = A0; cable <= B1; ++cable) {
        seed_pending_route(native.source, router.discovered[cable].output);
        seed_pending_route(router.discovered[cable].input, output.id);
        /* The original A1 to native Pending route is seeded last in both fixtures. */
        if (cable != A1) seed_pending_route(router.discovered[cable].input, native.destination);
    }
}

static void setup(void)
{
    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK);
    gridpico_pico_auto_routes_init(&automatic_routes, &router, &routing_scratch);
    physical_roles.panel_explicit = physical_roles.led_explicit = true;
    observe(0, 'A');
    observe(1, 'B');
    observe_one_cable(4, 'C');
    mounted[4] = false;

    record = (gridpico_pico_pat2_intent){
        .generation = 5, .identity_count = 3, .direction_count = 10, .pair_count = 6,
        .slots = {9, 0, 0, 0, 1, 10, 0, 0, 0, 0, 0, 0, 8},
        .pairs = {{1, 2, 1}, {1, 8, 0}, {1, 4, 1}, {1, 6, 0}, {1, 10, 1}, {9, 2, 1}},
    };
    const uint8_t indices[] = {0, 1, 4};
    for (uint8_t i = 0; i < 3; ++i) {
        const midi_identity *identity = &observations[indices[i]].identity;
        record.identities[i] = (gridpico_pico_pat2_identity){
            .vid = identity->vid, .pid = identity->pid, .bcd_device = identity->revision,
            .interface_number = identity->interface, .input_endpoint = identity->input_endpoint,
            .output_endpoint = identity->output_endpoint, .input_cables = identity->inputs,
            .output_cables = identity->outputs, .serial_length = 1, .serial = {(uint8_t)identity->serial[0]},
        };
    }
    for (uint8_t i = 0; i < 10; ++i) {
        record.directions[i] = (gridpico_pico_pat2_direction){
            .identity_ref = (uint8_t)(1 + i / 4), .cable = (uint8_t)(i / 2 % 2),
            .direction = i % 2 ? GRIDPICO_PATCH_OUTPUT : GRIDPICO_PATCH_INPUT, .label = "Equal name",
        };
    }
    apply.intent = &record;
    assert(gridpico_pico_pat2_bind_capture(&router, &apply) == GRIDPICO_OK);

    admit_fixture(0, 0, a_peer);
    admit_fixture(1, 1, (gridpico_pico_peer)(a_peer + 2));
    for (size_t source = 0; source < 4; ++source) {
        for (size_t destination = 0; destination < 4; ++destination) {
            if ((source == A0 && destination != B1) || (source == B1 && destination == A1)) continue;
            const gridpico_local_route_request request = route(source, destination, true);
            assert(gridpico_patch_graph_set_local_route(&router.graph, &request) == GRIDPICO_OK);
        }
    }
    if (GRIDPICO_PATCH_MAX_BRANCHES == 24) seed_matrix24_pending_routes();
    const gridpico_local_route_request pending = {
        {router.discovered[A1].input, router.graph.native_virtual_cable.destination, GRIDPICO_LOCAL_MANAGED}, true,
    };
    assert(gridpico_patch_graph_set_local_route(&router.graph, &pending) == GRIDPICO_OK);
    assert(gridpico_patch_graph_read_local_state(&router.graph, &apply.projection.graph) == GRIDPICO_OK);
    assert(apply.projection.graph.route_count == GRIDPICO_PATCH_MAX_BRANCHES);
    assert(apply.projection.graph.routes[GRIDPICO_PATCH_MAX_BRANCHES - 1].state == GRIDPICO_PATCH_ROUTE_PENDING);
    gridpico_pico_router_set_physical_feedback(&router, 1);
}

static void advance_policy(void)
{
    gridpico_auto_route_summary summary;
    for (unsigned i = 0; i < 2 * GRIDPICO_AUTO_ROUTE_MAX_PAIRS + 4u; ++i) {
        service_automatic_policy();
        if (gridpico_pico_auto_routes_read(&automatic_routes, &summary) == GRIDPICO_OK) {
            assert(role_syncs == nominations);
            return;
        }
    }
    assert(false && "Auto did not finish bounded readback");
}

static void project(void)
{
    physical_roles.graph_revision = router.graph.revision;
    assert(gridpico_pico_project_pat2_view(&apply.projection) == GRIDPICO_OK);
}

static char cli_reply[256];
static void cli_output(void *context, const char *bytes, size_t count)
{
    (void)context;
    const size_t offset = strlen(cli_reply);
    assert(count < sizeof(cli_reply) - offset);
    memcpy(cli_reply + offset, bytes, count);
    cli_reply[offset + count] = '\0';
}

static gridpico_result cli_route(gridpico_local_route route, bool on, bool stale)
{
    gridpico_cli_frontend cli = {
        .session_context = &router.session, .read_snapshot = gridpico_patch_session_read_snapshot,
    };
    gridpico_cli_bind_routing(&cli, &router, gridpico_pico_router_read_routing,
                             gridpico_pico_router_read_managed_route, gridpico_pico_router_set_managed_route,
                             &routing_scratch);
    assert(gridpico_pico_router_read_routing(&router, &routing_scratch) == GRIDPICO_OK);
    const gridpico_revision revision = routing_scratch.revision;
    assert(revision != 0);
    char line[96];
    snprintf(line, sizeof(line), "route %u %u managed %s if %" PRIu64, (unsigned)route.source,
             (unsigned)route.destination, on ? "on" : "off", revision + (stale ? 1u : 0u));
    cli_reply[0] = '\0';
    return gridpico_cli_execute_line(&cli, line, cli_output, NULL);
}

static void verify_no_midi(void)
{
    for (size_t i = 0; i < sizeof(router.notes) / sizeof(router.notes[0]); ++i) {
        assert(router.notes[i].count == 0);
    }
}

static void verify_clear_with_two_arrivals(void)
{
    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK);
    gridpico_pico_auto_routes_init(&automatic_routes, &router, &routing_scratch);
    memset(mounted, 0, sizeof(mounted));
    memset(discovered, 0, sizeof(discovered));
    memset(&apply, 0, sizeof(apply));
    record = (gridpico_pico_pat2_intent){
        .generation = 6, .identity_count = 1, .direction_count = 1, .slots = {1},
        .identities = {{.vid = 0x09e8, .pid = 0x004e, .bcd_device = 0x0112,
                        .interface_number = 1, .input_endpoint = 0x81, .output_endpoint = 2,
                        .input_cables = 1, .output_cables = 1, .serial_length = 1, .serial = {'C'}}},
        .directions = {{.identity_ref = 1, .direction = GRIDPICO_PATCH_INPUT, .label = "Equal name"}},
    };
    apply.intent = &record;
    router.pat2_apply = &apply;
    observe_one_cable(2, 'A');
    admit_one_cable(2, 0, a_peer);
    advance_policy();
    assert(apply.progress.reservations[0] == 1);
    gridpico_apc_physical *panel = &router.panels[GRIDPICO_PICO_APC].frontend;
    assert(panel->assign(panel->session_context, router.session.apc_layout_revision,
                         GRIDPICO_PATCH_INPUT, 0, 0) == GRIDPICO_OK);
    /* Register the unrelated direction first so it would take the old zero
     * even if the saved direction alone were suppressed.
     */
    observe_one_cable(0, 'U');
    observe_one_cable(1, 'C');
    admit_one_cable(0, 1, (gridpico_pico_peer)(a_peer + 1));
    admit_one_cable(1, 2, (gridpico_pico_peer)(a_peer + 2));
    advance_policy();
    assert(router.session.apc_layout.rows[0] == 0 && apply.progress.cancelled[0] == 1);
    assert(router.session.apc_layout.rows[1] == router.discovered[0].input &&
           router.session.apc_layout.rows[2] == router.discovered[1].input &&
           router.session.apc_layout.rows[3] == router.discovered[2].input);
    verify_no_midi();
}

#include "pat2_ram_commit_host.inc"
#include "pat2_capture_host.inc"

int main(void)
{
    setup();
    const gridpico_revision before_revision = router.graph.revision;
    static gridpico_pico_router before_router;
    gridpico_pico_pat2_progress before_progress;

    /* A valid removing Off exhausts the shadow revision. */
    router.graph.revision = UINT64_MAX - 1;
    memcpy(&before_router, &router, sizeof(router));
    memcpy(&before_progress, &apply.progress, sizeof(apply.progress));
    service_automatic_policy();
    assert(apply.shadow.graph.revision == UINT64_MAX &&
           apply.next.pair_outcomes[1] == GRIDPICO_PAT2_APPLIED && !automatic_routes.coherent);
    assert(memcmp(&before_router, &router, sizeof(router)) == 0);
    assert(memcmp(&before_progress, &apply.progress, sizeof(apply.progress)) == 0);
    router.graph.revision = before_revision;

    memcpy(&before_router, &router, sizeof(router));
    project();
    assert(gridpico_pico_pat2_prepare(&router, physical_roles.revision) == GRIDPICO_OK);
    observe(3, 'A'); /* Exact competitor with no Graph or layout change. */
    project();
    assert(gridpico_pico_pat2_commit(&router, physical_roles.revision) == GRIDPICO_UNAVAILABLE);
    assert(memcmp(&before_router, &router, sizeof(router)) == 0);
    mounted[3] = false;

    project();
    assert(gridpico_pico_pat2_prepare(&router, physical_roles.revision) == GRIDPICO_OK);
    const gridpico_revision layout_rev = router.session.apc_layout_revision;
    ++router.session.apc_layout_revision;
    project();
    assert(gridpico_pico_pat2_commit(&router, physical_roles.revision) == GRIDPICO_UNAVAILABLE);
    assert(router.session.apc_layout_revision == layout_rev + 1 && !desired(route(A0, A0, true).route));

    project();
    assert(gridpico_pico_pat2_prepare(&router, physical_roles.revision) == GRIDPICO_OK);
    assert(gridpico_pico_router_renew_managed_input(&router, a_peer) == GRIDPICO_OK);
    project();
    assert(gridpico_pico_pat2_commit(&router, physical_roles.revision) == GRIDPICO_UNAVAILABLE);
    assert(!desired(route(A0, A0, true).route));

    project();
    assert(gridpico_pico_pat2_prepare(&router, physical_roles.revision) == GRIDPICO_OK);
    physical_roles.led_explicit = false; /* Same graph and generations, different withholding. */
    project();
    assert(gridpico_pico_pat2_commit(&router, physical_roles.revision) == GRIDPICO_UNAVAILABLE);
    physical_roles.led_explicit = true;

    /* A no-op On protects the saved removing Off. On must see the still-full table. */
    memcpy(&before_progress, &apply.progress, sizeof(apply.progress));
    const gridpico_revision no_op = router.graph.revision;
    assert(cli_route(route(A0, B1, true).route, true, false) == GRIDPICO_OK && router.graph.revision == no_op);
    project();
    assert(gridpico_pico_pat2_prepare(&router, physical_roles.revision) == GRIDPICO_OK);
    assert(apply.next.pair_outcomes[0] == GRIDPICO_PAT2_FULL &&
           apply.next.pair_outcomes[1] == GRIDPICO_PAT2_OPERATOR && desired(route(A0, B1, true).route));
    gridpico_pico_pat2_discard(&router);
    memcpy(&apply.progress, &before_progress, sizeof(apply.progress)); /* Reset test probe. */

    advance_policy();
    const gridpico_apc_patch_layout *layout = &router.session.apc_layout;
    assert(layout->rows[0] == 0 && layout->columns[0] == 0);
    assert(layout->rows[4] == router.discovered[A0].input && layout->columns[7] == router.discovered[B1].output);
    assert(layout->rows[1] != 0 && layout->columns[1] != 0);
    assert(apply.progress.reservations[0] == 9 && apply.progress.reservations[5] == 10 &&
           strcmp(record.directions[apply.progress.reservations[0] - 1].label, "Equal name") == 0);
    assert(apply.progress.pair_outcomes[0] == GRIDPICO_PAT2_APPLIED &&
           apply.progress.pair_outcomes[1] == GRIDPICO_PAT2_APPLIED &&
           apply.progress.pair_outcomes[2] == GRIDPICO_PAT2_FULL &&
           apply.progress.pair_outcomes[3] == GRIDPICO_PAT2_APPLIED);
    assert(desired(route(A0, A0, true).route) && !desired(route(A0, A1, true).route) &&
           !desired(route(A0, B1, false).route) && !desired(route(A0, B0, false).route));
    gridpico_auto_route_summary summary;
    assert(gridpico_pico_auto_routes_read(&automatic_routes, &summary) == GRIDPICO_OK);
    assert(summary.user_off >= 2);
    verify_prior_capture(0);
    assert(router.panels[GRIDPICO_PICO_APC].latest_valid &&
           router.panels[GRIDPICO_PICO_APC].latest_version.layout_revision == router.session.apc_layout_revision);
    verify_no_midi();

    gridpico_apc_physical *panel = &router.panels[GRIDPICO_PICO_APC].frontend;
    const gridpico_revision guarded_layout = router.session.apc_layout_revision;
    assert(panel->assign(panel->session_context, guarded_layout - 1, GRIDPICO_PATCH_INPUT, 0, 0) == GRIDPICO_UNAVAILABLE);
    assert(apply.progress.reservations[0] == 9);
    assert(panel->assign(panel->session_context, guarded_layout, GRIDPICO_PATCH_INPUT, 0, 0) == GRIDPICO_OK);
    assert(router.session.apc_layout_revision == guarded_layout + 1 && apply.progress.reservations[0] == 0 &&
           apply.progress.cancelled[0] == 9);
    verify_prior_capture(1);

    mounted[4] = true;
    const gridpico_pico_peer c_peer = (gridpico_pico_peer)(a_peer + C0);
    admit_one_cable(4, 2, c_peer);
    assert(router.graph.inventory.port_count == router.graph.local_profile.dynamic_port_limit);

    const gridpico_local_route_request leave_off = route(A0, C0, false);
    assert(gridpico_patch_session_set_local_route(&router.session, &leave_off) == GRIDPICO_OK);
    const gridpico_local_route_request spare = route(B1, B0, false);
    assert(gridpico_patch_session_set_local_route(&router.session, &spare) == GRIDPICO_OK);
    project();
    assert(gridpico_pico_pat2_prepare(&router, physical_roles.revision) == GRIDPICO_OK);
    memcpy(&before_progress, &apply.progress, sizeof(apply.progress));
    assert(cli_route(leave_off.route, false, true) != GRIDPICO_OK && strstr(cli_reply, "ERR UNAVAILABLE") != NULL);
    assert(memcmp(&before_progress, &apply.progress, sizeof(apply.progress)) == 0);
    const gridpico_revision cli_noop_graph = router.graph.revision;
    assert(cli_route(leave_off.route, false, false) == GRIDPICO_OK && router.graph.revision == cli_noop_graph);
    assert(memcmp(&before_progress, &apply.progress, sizeof(apply.progress)) != 0);
    project();
    assert(gridpico_pico_pat2_commit(&router, physical_roles.revision) == GRIDPICO_UNAVAILABLE);
    assert(!desired(leave_off.route));
    verify_prior_capture(2);

    advance_policy();
    assert(layout->rows[0] == 0 && layout->columns[0] == router.discovered[C0].output);
    assert(apply.progress.pair_outcomes[4] == GRIDPICO_PAT2_OPERATOR &&
           apply.progress.pair_outcomes[5] == GRIDPICO_PAT2_APPLIED &&
           apply.progress.pair_outcomes[2] == GRIDPICO_PAT2_FULL);
    assert(!desired(leave_off.route) && desired(route(C0, A0, true).route));
    assert(!desired(route(A0, A1, true).route));
    verify_no_midi();

    const gridpico_pico_packet on = {{0x09, 0x90, 0x3c, 0x62}}, off = {{0x08, 0x80, 0x3c, 0x24}};
    assert(gridpico_pico_router_receive(&router, a_peer, &on) == GRIDPICO_OK);
    assert(gridpico_pico_router_receive(&router, a_peer, &off) == GRIDPICO_OK);
    const gridpico_pico_packet *packet = gridpico_pico_router_note(&router, a_peer);
    assert(packet != NULL && memcmp(packet, &on, sizeof(on)) == 0);
    gridpico_pico_router_note_sent(&router, a_peer);
    packet = gridpico_pico_router_note(&router, a_peer);
    assert(packet != NULL && memcmp(packet, &off, sizeof(off)) == 0);
    gridpico_pico_router_note_sent(&router, a_peer);

    verify_clear_with_two_arrivals();

    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK);
    gridpico_pico_auto_routes_init(&automatic_routes, &router, &routing_scratch);
    memset(discovered, 0, sizeof(discovered));
    memset(mounted, 0, sizeof(mounted));
    observe_one_cable(0, 'U');
    admit_one_cable(0, 0, a_peer);
    service_automatic_policy();
    assert(router.pat2_apply == NULL && automatic_routes.coherent &&
           router.session.apc_layout.rows[0] == router.discovered[0].input &&
           router.session.apc_layout.columns[0] == router.discovered[0].output);
    verify_no_midi();
    verify_first_capture();
    verify_capture_overflow();
    puts("PASS guarded PAT2 transaction, placement and Auto ordering, exact late ownership and fresh MIDI");
    return 0;
}
