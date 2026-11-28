/* Runs the actual boot borrow, projection, restore, placement and Auto turn
 * with RAM sectors and completed role/discovery observations. */
static uint8_t sectors[GRIDPICO_PICO_SAVE_SECTORS][GRIDPICO_PICO_SAVE_SECTOR];
static gridpico_pico_auto_routes automatic_routes;
static gridpico_local_routing_snapshot routing_scratch;
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

static gridpico_pico_pat2_identity saved_identity(uint8_t serial)
{
    return (gridpico_pico_pat2_identity){
        .vid = 0x09e8, .pid = 0x004e, .bcd_device = 0x0112, .interface_number = 1,
        .input_endpoint = 0x81, .output_endpoint = 2, .input_cables = 1, .output_cables = 1,
        .serial_length = 1, .serial = {serial},
    };
}

static void encode_fixture(uint8_t *bytes, bool off)
{
    const gridpico_pico_pat2_intent saved = {
        .generation = 5, .identity_count = 3, .direction_count = 3, .pair_count = 2,
        .identities = {saved_identity('A'), saved_identity('B'), saved_identity('C')},
        .directions = {
            {.identity_ref = 1, .direction = GRIDPICO_PATCH_INPUT, .label = "A"},
            {.identity_ref = 2, .direction = GRIDPICO_PATCH_OUTPUT, .label = "B"},
            {.identity_ref = 3, .direction = GRIDPICO_PATCH_INPUT, .label = "Late C"},
        },
        .slots = {3, 0, 0, 0, 1, 2},
        .pairs = {{1, 2, off ? 0 : 1}, {3, 2, 1}},
    };
    assert(gridpico_pico_encode_pat2(&saved, bytes, GRIDPICO_PICO_SAVE_SECTOR) == GRIDPICO_OK);
}

static void start(const uint8_t *a, const uint8_t *b)
{
    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK);
    memset(&pat2_boot, 0, sizeof(pat2_boot));
    memset(interfaces, 0, sizeof(interfaces));
    memset(observations, 0, sizeof(observations));
    memset(discovered, 0, sizeof(discovered));
    memset(mounted, 0, sizeof(mounted));
    memset(descriptor_present, 0, sizeof(descriptor_present));
    discovery_dirty = false;
    boot_reads = role_syncs = nominations = 0;
    physical_roles = (apc_roles){.revision = 1, .panel_explicit = true, .led_explicit = true};
    pat2_boot_read(a, b);
    assert(boot_reads == 1);
    gridpico_pico_auto_routes_init(&automatic_routes, &router, &routing_scratch);
}

static void arrive(uint8_t index, char serial)
{
    observe(index, serial);
    interfaces[index].rx_cables = interfaces[index].tx_cables = 1;
    observations[index].input_cables = observations[index].output_cables = 1;
    observations[index].identity.inputs = observations[index].identity.outputs = 1;
    const gridpico_pico_peer peer = (gridpico_pico_peer)(GRIDPICO_PICO_DISCOVERED + index);
    assert(gridpico_pico_router_register_device(&router, peer, observations[index].name, 1, 1) == GRIDPICO_OK);
    discovered[index] = (discovered_device){
        .identity = observations[index].identity, .lifetime = observations[index].lifetime,
        .first_peer = peer, .index = index,
    };
    gridpico_pico_router_attach(&router, peer, true);
    pat2_boot_discovery_changed();
}

static gridpico_local_route pair(size_t in)
{
    return (gridpico_local_route){
        router.discovered[in].input, router.discovered[1].output, GRIDPICO_LOCAL_MANAGED,
    };
}

static bool desired(gridpico_local_route route)
{
    gridpico_local_route_status status;
    assert(gridpico_patch_graph_read_local_route(&router.graph, &route, &status) == GRIDPICO_OK);
    return status.state != GRIDPICO_PATCH_ROUTE_OFF;
}

static gridpico_auto_route_summary finish_auto(void)
{
    for (unsigned i = 0; i < 2 * GRIDPICO_AUTO_ROUTE_MAX_PAIRS + 4u; ++i) {
        service_automatic_policy();
        gridpico_auto_route_summary summary;
        if (gridpico_pico_auto_routes_read(&automatic_routes, &summary) == GRIDPICO_OK) {
            assert(boot_reads == 1 && role_syncs == nominations);
            return summary;
        }
    }
    assert(false && "Auto did not finish bounded readback");
    return (gridpico_auto_route_summary){0};
}

static void ordinary_boot(gridpico_pico_pat2_kind a_kind, bool conflict)
{
    start(sectors[0], sectors[1]);
    assert(!router.pat2_apply && pat2_boot.status.selected == -1 &&
        pat2_boot.status.copies[0].kind == a_kind && pat2_boot.status.conflict == conflict);
    arrive(0, 'A');
    arrive(1, 'B');
    (void)finish_auto();
    assert(router.session.apc_layout.rows[0] == router.discovered[0].input && desired(pair(0)));
}

int main(void)
{
    memset(sectors, 0xff, sizeof(sectors));
    encode_fixture(sectors[1], true);
    start(NULL, sectors[1]);
    assert(pat2_boot.status.selected == 1 && pat2_boot.status.copies[0].kind == GRIDPICO_PICO_PAT2_UNREADABLE);
    assert(router.pat2_apply == &pat2_boot.apply && pat2_boot.intent.generation == 5);
    memset(sectors[1], 0xff, sizeof(sectors[1]));
    pat2_boot_read(sectors[0], sectors[1]);
    assert(boot_reads == 1 && pat2_boot.intent.generation == 5);

    arrive(0, 'A');
    arrive(1, 'B');
    const gridpico_local_route_request prior = {pair(0), true};
    assert(gridpico_patch_graph_set_local_route(&router.graph, &prior) == GRIDPICO_OK);
    service_automatic_policy();
    assert(router.session.apc_layout.rows[0] == 0 &&
           router.session.apc_layout.rows[4] == router.discovered[0].input &&
           router.session.apc_layout.columns[0] == router.discovered[1].output);
    assert(pat2_boot.pending && pat2_boot.apply.progress.reservations[0] == 3);
    assert(!desired(pair(0)));
    assert(finish_auto().user_off >= 1 && !desired(pair(0)));
    puts("PASS one borrowed selection, named placement and explicit Off before Auto, unreadable peer retained");

    mounted[3] = true;
    pat2_boot_discovery_changed();
    const gridpico_revision graph = router.graph.revision, layout = router.session.apc_layout_revision;
    service_automatic_policy();
    assert(!pat2_boot.coherent && pat2_boot.apply.projection.unknown_shape_interfaces == (1u << 3));
    assert(router.graph.revision == graph && router.session.apc_layout_revision == layout);
    mounted[3] = false;

    arrive(2, 'C');
    service_automatic_policy();
    assert(router.session.apc_layout.rows[0] == router.discovered[2].input && desired(pair(2)));
    assert(!pat2_boot.pending && !pat2_boot.apply.progress.reservations[0] && !desired(pair(0)));
    const gridpico_revision last_attempt = pat2_boot.apply.plan.graph_revision;
    pat2_boot_discovery_changed();
    (void)finish_auto();
    assert(pat2_boot.apply.plan.graph_revision == last_attempt);
    puts("PASS unadmitted census gates policy, later named arrival resolves once with fresh binding");

    ordinary_boot(GRIDPICO_PICO_PAT2_ERASED, false);
    static const uint8_t legacy_header[] = {
        0x47, 0x50, 0x53, 0x4e, 0x01, 0x00, 0x4c, 0x00, 0x37, 0x00, 0x00, 0x00,
        0x50, 0x41, 0x54, 0x31, 0xff, 0xc5, 0x02, 0x93,
    };
    static const uint8_t legacy_commit[] = {0x47, 0x50, 0x43, 0x4d, 0x37, 0x00, 0x00, 0x00, 0xff, 0xc5, 0x02, 0x93};
    memcpy(sectors[0], legacy_header, sizeof(legacy_header));
    memset(sectors[0] + sizeof(legacy_header), 0, 76);
    memcpy(sectors[0] + GRIDPICO_PICO_SAVE_PAGE, legacy_commit, sizeof(legacy_commit));
    ordinary_boot(GRIDPICO_PICO_PAT2_LEGACY, false);
    assert(pat2_boot.status.copies[0].legacy.kind == GRIDPICO_PICO_PAT1_COMMITTED);
    memset(sectors[0], 0xff, sizeof(sectors[0]));
    encode_fixture(sectors[0], true);
    encode_fixture(sectors[1], false);
    ordinary_boot(GRIDPICO_PICO_PAT2_COMMITTED, true);
    puts("PASS erased, legacy-only and conflicting sectors retain ordinary placement and Auto");
    return 0;
}
