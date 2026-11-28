/* Private host witness built with the Pico7 firmware extraction. */
enum { A = 0, B, C, D, E, F, G };

static size_t bitmap_slot(uint8_t index)
{
    const discovered_device *device = mounted_binding(index);
    assert(device != NULL && device->first_peer >= GRIDPICO_PICO_DISCOVERED);
    return device->first_peer - GRIDPICO_PICO_DISCOVERED;
}

static gridpico_local_route bitmap_pair(uint8_t source, uint8_t destination)
{
    return (gridpico_local_route){router.discovered[bitmap_slot(source)].input,
                                router.discovered[bitmap_slot(destination)].output, GRIDPICO_LOCAL_MANAGED};
}

static size_t bitmap_count(const gridpico_patch_graph *graph)
{
    static gridpico_patch_graph_route_metadata metadata;
    assert(gridpico_patch_graph_read_route_metadata(graph, &metadata) == GRIDPICO_OK);
    return metadata.enabled_count;
}

static void seed_bitmap24(gridpico_local_route extra)
{
    const gridpico_local_route_request exact[] = {{extra, true}, {bitmap_pair(F, E), true}};
    for (size_t i = 0; i < sizeof exact / sizeof exact[0]; ++i) {
        assert(gridpico_patch_graph_set_local_route(&router.graph, &exact[i]) == GRIDPICO_OK);
    }
    const gridpico_patch_inventory *inventory = &router.graph.inventory;
    size_t count = 2;
    for (size_t i = 0; i < inventory->port_count && count < 24; ++i) {
        const gridpico_patch_port *in = &inventory->ports[i];
        if (in->direction != GRIDPICO_PATCH_INPUT || !in->patch_eligible ||
            in->id == extra.source || in->id == router.session.apc_audition_source) continue;
        for (size_t j = 0; j < inventory->port_count && count < 24; ++j) {
            const gridpico_patch_port *out = &inventory->ports[j];
            if (out->direction != GRIDPICO_PATCH_OUTPUT || !out->patch_eligible) continue;
            const gridpico_local_route_request on = {{in->id, out->id, GRIDPICO_LOCAL_MANAGED}, true};
            if (route_state(on.route) != GRIDPICO_PATCH_ROUTE_OFF) continue;
            assert(gridpico_patch_graph_set_local_route(&router.graph, &on) == GRIDPICO_OK);
            ++count;
        }
    }
    assert(count == 24 && bitmap_count(&router.graph) == 24);
}

/* Verify Machine bytes against the raw registry and exact Graph pairs. */
static void capture_bitmap_pages(const char *path, size_t raw_routes, bool expect_pending)
{
    FILE *capture_file = path[0] == '\0' ? NULL : fopen(path, "wb");
    assert(path[0] == '\0' || capture_file != NULL);
    static gridpico_patch_routing_metadata full;
    assert(gridpico_patch_session_read_routing_metadata(&router.session, &full) == GRIDPICO_OK);
    assert(full.graph.enabled_count == raw_routes);
    const gridpico_local_registry *registry = &full.graph.registry;
    const gridpico_patch_inventory *inventory = &registry->inventory;
    assert(inventory->device_count == 9 && inventory->port_count == 19);
    const size_t total = 8 + 18 + 25;
    size_t offset = 0, device = 0, port = 0, routes = 0, pending = 0;
    unsigned previous_source = 0, previous_destination = 0;
    do {
        char command[96];
        assert(snprintf(command, sizeof command, "routing %" PRIu64 " %zu", full.revision, offset) > 0);
        assert(run_command(offset == 0 ? "routing" : command) == GRIDPICO_OK);
        assert(strstr(reply, "Audition") == NULL);
        if (capture_file != NULL) assert(fwrite(reply, 1, reply_size, capture_file) == reply_size);
        char *line = strtok(reply, "\r\n");
        uint64_t revision;
        char cursor[24];
        assert(line && sscanf(line, "ROUTING %" SCNu64 " %23s", &revision, cursor) == 2 &&
               revision == full.revision);
        const size_t limit = total - offset < 8 ? total - offset : 8;
        for (size_t row = 0; row < limit; ++row) {
            line = strtok(NULL, "\r\n");
            assert(line != NULL);
            unsigned id, parent;
            int name_offset = 0;
            char state[16], role[16], availability[16], color[16];
            if (offset + row < 8) {
                while (strcmp(inventory->devices[device].name, "Audition") == 0) ++device;
                const gridpico_patch_device *expected = &inventory->devices[device++];
                assert(sscanf(line, "DEVICE %u %15s %15s %n", &id, state, color, &name_offset) == 3);
                assert(id == expected->id && strcmp(color, "unset") == 0 && name_offset > 0);
                assert(strcmp(state, expected->state == GRIDPICO_PATCH_CONNECTED ? "connected" : "disconnected") == 0);
                expect_name(line + name_offset, expected->name);
            } else if (offset + row < 26) {
                while (inventory->ports[port].id == router.session.apc_audition_source) ++port;
                const gridpico_patch_port *expected = &inventory->ports[port];
                assert(sscanf(line, "BINDING %u %u %15s %15s %15s %n", &id, &parent,
                              state, role, availability, &name_offset) == 5);
                assert(id == expected->id && parent == expected->device && name_offset > 0);
                assert(strcmp(state, expected->direction == GRIDPICO_PATCH_INPUT ? "in" : "out") == 0);
                assert(strcmp(role, registry->port_capabilities[port] == GRIDPICO_LOCAL_PORT_PANEL ?
                                   "panel" : "managed") == 0);
                assert(strcmp(availability, full.availability[port] == GRIDPICO_LOCAL_IN_USE ?
                                           "in-use" : "unavailable") == 0);
                expect_name(line + name_offset, expected->name);
                ++port;
            } else {
                assert(sscanf(line, "ROUTE %u %u MANAGED %15s%n", &id, &parent, state, &name_offset) == 3);
                assert(name_offset > 0 && line[name_offset] == '\0');
                assert(id != router.session.apc_audition_source && parent != router.session.apc_audition_source);
                assert(id > previous_source || (id == previous_source && parent > previous_destination));
                previous_source = id;
                previous_destination = parent;
                const gridpico_patch_route_state exact = route_state(
                    (gridpico_local_route){id, parent, GRIDPICO_LOCAL_MANAGED});
                assert(exact != GRIDPICO_PATCH_ROUTE_OFF);
                assert(strcmp(state, exact == GRIDPICO_PATCH_ROUTE_ACTIVE ? "ACTIVE" : "PENDING") == 0);
                pending += exact == GRIDPICO_PATCH_ROUTE_PENDING;
                ++routes;
            }
        }
        line = strtok(NULL, "\r\n");
        assert(line && strcmp(line, "OK") == 0 && strtok(NULL, "\r\n") == NULL);
        offset += limit;
        assert(offset == cli_routing_page.next_offset);
        if (offset == total) assert(strcmp(cursor, "-") == 0);
        else assert(strtoul(cursor, NULL, 10) == offset);
    } while (offset < total);
    assert(routes == 25 && (pending != 0) == expect_pending);
    if (capture_file != NULL) assert(fclose(capture_file) == 0);
    printf("Machine pages: 8 devices, 18 bindings, 25 routes (%zu Pending), %zu raw desired\n", pending, raw_routes);
}

static void assert_fixed_read_refusal(void)
{
    static gridpico_local_graph_state local, before_local;
    static gridpico_patch_graph_state legacy, before_legacy;
    memset(&local, 0xa5, sizeof local);
    memset(&legacy, 0xa5, sizeof legacy);
    memcpy(&before_local, &local, sizeof local);
    memcpy(&before_legacy, &legacy, sizeof legacy);
    assert(gridpico_patch_graph_read_local_state(&router.graph, &local) == GRIDPICO_CAPACITY);
    assert(gridpico_patch_graph_read_graph(&router.graph, &legacy) == GRIDPICO_CAPACITY);
    assert(memcmp(&local, &before_local, sizeof local) == 0 &&
           memcmp(&legacy, &before_legacy, sizeof legacy) == 0);
}

static void finish_bitmap_auto(size_t expected_public, unsigned user_off)
{
    /* Withhold ordinary new discovery routes so the staged selections stay
     * isolated. Existing Active/Pending and explicit Off still get read. */
    gridpico_auto_route_summary summary;
    unsigned turns = 0;
    do {
        assert(++turns <= 2 * GRIDPICO_AUTO_ROUTE_MAX_PAIRS + 2);
        gridpico_pico_auto_routes_service(&automatic_routes, true, UINT32_MAX, UINT32_MAX);
    } while (gridpico_pico_auto_routes_read(&automatic_routes, &summary) != GRIDPICO_OK);
    assert(summary.active + summary.pending == expected_public && summary.user_off >= user_off);
    const gridpico_revision graph = router.graph.revision;
    assert(automatic_routes.routing_revision == routing_revision() && router.graph.revision == graph);
    printf("Auto ready: %u Active, %u Pending, %u explicit Off, exact Routing guard\n",
           summary.active, summary.pending, summary.user_off);
}

static void receive_bitmap_packet(uint8_t target, uint8_t status, uint8_t note, uint8_t velocity)
{
    const unsigned sent = wire.count;
    assert(service_musical_out(target, mounted_binding(target)));
    const uint8_t expected[] = {(uint8_t)(status >> 4), status, note, velocity};
    assert(wire.count == sent + 1 && wire.address == target + 1 &&
           memcmp(wire.packet.bytes, expected, sizeof expected) == 0);
    complete(target);
}

static void bitmap_note_pair(uint8_t first, uint8_t second, uint8_t key)
{
    input(A, 0, 0x90, key, 0x60);
    receive_bitmap_packet(first, 0x90, key, 0x60);
    receive_bitmap_packet(second, 0x90, key, 0x60);
    input(A, 0, 0x80, key, 0x20);
    receive_bitmap_packet(first, 0x80, key, 0x20);
    receive_bitmap_packet(second, 0x80, key, 0x20);
    assert(queues_empty());
}

static void bitmap_queue_case(void)
{
    const gridpico_pico_peer target = mounted_binding(B)->first_peer;
    const gridpico_pico_peer sibling = mounted_binding(C)->first_peer;
    const uint32_t generation = router.generation[target];
    const uint32_t sibling_generation = router.generation[sibling];
    gridpico_pico_queue *full = &router.notes[target - GRIDPICO_PICO_WEB];
    for (unsigned i = 0; i < GRIDPICO_PICO_QUEUE_SIZE; ++i) {
        input(A, 0, 0x90, (uint8_t)i, 1);
        assert(gridpico_pico_router_note(&router, sibling) != NULL);
        gridpico_pico_router_note_sent(&router, sibling);
        assert(full->count == i + 1);
    }
    const gridpico_pico_queue before = *full;
    input(A, 0, 0x90, 69, 0x60);
    receive_bitmap_packet(C, 0x90, 69, 0x60);
    input(A, 0, 0x80, 69, 0x20);
    receive_bitmap_packet(C, 0x80, 69, 0x20);
    assert(memcmp(full, &before, sizeof before) == 0);

    submit_to(B);
    const unsigned sent = wire.count, old_aborts = aborts;
    const gridpico_local_route route = bitmap_pair(A, B);
    unmount_midi(B);
    assert(aborts == old_aborts + 1 && queues_empty() && !current_musical_flight(B));
    route_command(route.source, route.destination, 0, "PENDING");
    input(A, 0, 0x80, 69, 0x20);
    receive_bitmap_packet(C, 0x80, 69, 0x20);
    assert(router.generation[sibling] == sibling_generation && queues_empty());
    mount_midi(B, names[B], 'B');
    assert(router.generation[target] > generation && bitmap_pair(A, B).destination == route.destination);
    assert(wire.count == sent + 1 && queues_empty());
    stale_event = true;
    input(A, 0, 0x90, 70, 0x60);
    assert(service_musical_out(B, mounted_binding(B)) && wire.count == sent + 1 && !current_musical_flight(B));
    const unsigned completed = completions;
    assert(!__wrap_midih_xfer_cb(B + 1, 2, XFER_RESULT_SUCCESS, GRIDPICO_PICO_PACKET_BYTES));
    assert(!stale_event && completions == completed && full->count == 1);
    receive_bitmap_packet(B, 0x90, 70, 0x60);
    receive_bitmap_packet(C, 0x90, 70, 0x60);
    input(A, 0, 0x80, 70, 0x20);
    receive_bitmap_packet(B, 0x80, 70, 0x20);
    receive_bitmap_packet(C, 0x80, 70, 0x20);
    assert(queues_empty());
    puts("Queue: actual 128-packet refusal preserves waiting bytes and sibling On/Off; exact detach/rebind and late completion fence");
}

static void stash_audition_route(void)
{
    const gridpico_port_id audition = router.session.apc_audition_source;
    const gridpico_patch_port_update online = {
        .connection = {audition, ++router.audition.source_generation}, .state = GRIDPICO_PATCH_CONNECTED,
    };
    const gridpico_local_route_request hidden = {{audition, bitmap_pair(A, B).destination, GRIDPICO_LOCAL_MANAGED}, true};
    assert(gridpico_patch_graph_update_port(&router.graph, &online) == GRIDPICO_OK);
    assert(gridpico_patch_graph_set_local_route(&router.graph, &hidden) == GRIDPICO_OK);
    char command[96];
    assert(snprintf(command, sizeof command, "route %u %u managed off if %" PRIu64,
                    (unsigned)hidden.route.source, (unsigned)hidden.route.destination, routing_revision()) > 0);
    assert(run_command(command) == GRIDPICO_INVALID && strcmp(reply, "ERR INVALID\r\n") == 0);
    assert(route_state(hidden.route) == GRIDPICO_PATCH_ROUTE_ACTIVE);
}

static void machine_bitmap_case(const char *path)
{
    const gridpico_local_route selected = bitmap_pair(A, B);
    seed_bitmap24(bitmap_pair(A, C));
    unmount_midi(F);
    const gridpico_revision old = routing_revision();
    assert(cli_routing_page.metadata.graph.enabled_count == 24);
    gridpico_apc_patch_snapshot *panel = &router.panel_scratch;
    assert(gridpico_patch_session_read_apc_patch(&router.session, panel) == GRIDPICO_OK);
    const gridpico_revision old_layout = panel->layout_revision, old_graph = panel->graph_revision;
    route_command(selected.source, selected.destination, 0, "OFF");
    route_command(selected.source, selected.destination, old, "ACTIVE");
    assert(bitmap_count(&router.graph) == 25);
    route_command(selected.source, selected.destination, 0, "ACTIVE");
    assert_fixed_read_refusal();
    char command[96];
    assert(snprintf(command, sizeof command, "routing %" PRIu64 " 8", old) > 0);
    assert(run_command(command) == GRIDPICO_UNAVAILABLE && strcmp(reply, "ERR UNAVAILABLE\r\n") == 0);
    route_command(selected.source, selected.destination, old, NULL);
    assert(gridpico_patch_session_set_apc_patch_cell(&router.session, old_layout, old_graph, 0, 0, false)
           == GRIDPICO_UNAVAILABLE);
    puts("Guarded Machine On: exact 24 to 25, Off/Active readback, stale CLI/APC with no prefix or edit");

    stash_audition_route();
    mount_midi(G, names[G], 'G');
    place_after_roles();
    finish_bitmap_auto(25, 0);
    assert(run_command("status") == GRIDPICO_OK && strncmp(reply, "STATUS ", 7) == 0);
    assert(router.session.apc_layout_revision != 0 && physical_roles.panel == router.discovered[1].input &&
           physical_roles.led == router.discovered[1].output);
    capture_bitmap_pages(path, 26, true);
    assert(wire.count == 0 && queues_empty());
    bitmap_note_pair(B, C, 67);
    bitmap_queue_case();
}

static void start_bitmap_boot(void)
{
    const gridpico_pico_pat2_intent saved = {
        .generation = 5, .identity_count = 5, .direction_count = 6, .pair_count = 4,
        .identities = {saved_identity('A'), saved_identity('B'), saved_identity('C'),
                       saved_identity('D'), saved_identity('G')},
        .directions = {
            {.identity_ref = 1, .direction = GRIDPICO_PATCH_INPUT, .label = "A"},
            {.identity_ref = 2, .direction = GRIDPICO_PATCH_OUTPUT, .label = "B"},
            {.identity_ref = 3, .direction = GRIDPICO_PATCH_OUTPUT, .label = "C"},
            {.identity_ref = 4, .direction = GRIDPICO_PATCH_OUTPUT, .label = "D"},
            {.identity_ref = 5, .direction = GRIDPICO_PATCH_INPUT, .label = "Late G IN"},
            {.identity_ref = 5, .direction = GRIDPICO_PATCH_OUTPUT, .label = "Late G OUT"},
        },
        .slots = {[0] = 5, [GRIDPICO_PATCH_SOURCE_ROWS] = 6},
        .pairs = {{1, 2, 1}, {1, 3, 1}, {1, 4, 0}, {5, 2, 1}},
    };
    memset(boot_sectors, 0xff, sizeof boot_sectors);
    assert(gridpico_pico_encode_pat2(&saved, boot_sectors[1], sizeof boot_sectors[1]) == GRIDPICO_OK);
    memcpy(original_boot_sectors, boot_sectors, sizeof boot_sectors);
    pat2_boot_read(boot_sectors[0], boot_sectors[1]);
    assert(pat2_boot.read && pat2_boot.pending && pat2_boot.status.selected == 1);
    pat2_boot_read(NULL, NULL);
    assert(pat2_boot.status.selected == 1 && pat2_boot.intent.generation == 5);
}

static void boot_bitmap_case(const char *path)
{
    const gridpico_local_route b = bitmap_pair(A, B), c = bitmap_pair(A, C), off = bitmap_pair(A, D);
    seed_bitmap24(off);
    assert(sync_apc_roles(&physical_roles) == GRIDPICO_OK);
    nominate_controller(&physical_roles);
    gridpico_pico_pat2_apply *apply = &pat2_boot.apply;
    static gridpico_pico_router before;
    const gridpico_pico_pat2_progress progress = apply->progress;
    memcpy(&before, &router, sizeof before);
    assert(gridpico_pico_project_pat2_view(&apply->projection) == GRIDPICO_OK);
    assert(gridpico_pico_pat2_prepare(&router, physical_roles.revision) == GRIDPICO_OK);
    assert(bitmap_count(&router.graph) == 24 && bitmap_count(&apply->shadow.graph) == 25);
    assert(apply->before.membership.present[2] &&
           apply->next.pair_outcomes[0] == GRIDPICO_PAT2_APPLIED &&
           apply->next.pair_outcomes[1] == GRIDPICO_PAT2_APPLIED &&
           apply->next.pair_outcomes[2] == GRIDPICO_PAT2_APPLIED);
    assert(route_state(b) == GRIDPICO_PATCH_ROUTE_OFF && route_state(c) == GRIDPICO_PATCH_ROUTE_OFF &&
           route_state(off) == GRIDPICO_PATCH_ROUTE_ACTIVE && wire.count == 0 && queues_empty());
    assert(gridpico_pico_project_pat2_view(&apply->projection) == GRIDPICO_OK);
    assert(gridpico_pico_pat2_commit(&router, physical_roles.revision - 1) == GRIDPICO_UNAVAILABLE);
    assert(!apply->prepared && apply->shadow.owner_address == 0 && memcmp(&before, &router, sizeof before) == 0 &&
           memcmp(&apply->progress, &progress, sizeof progress) == 0);
    assert(memcmp(original_boot_sectors, boot_sectors, sizeof boot_sectors) == 0);

    service_automatic_policy();
    assert(bitmap_count(&router.graph) == 25 && pat2_boot.coherent && pat2_boot.pending);
    assert(apply->progress.reservations[0] == 5 &&
           apply->progress.reservations[GRIDPICO_PATCH_SOURCE_ROWS] == 6);
    assert(route_state(b) == GRIDPICO_PATCH_ROUTE_ACTIVE && route_state(c) == GRIDPICO_PATCH_ROUTE_ACTIVE &&
           route_state(off) == GRIDPICO_PATCH_ROUTE_OFF);
    assert_fixed_read_refusal();

    mount_midi(G, names[G], 'G');
    const gridpico_local_route withheld = bitmap_pair(G, B);
    const gridpico_revision guard = routing_revision(), graph = router.graph.revision;
    char command[96];
    assert(snprintf(command, sizeof command, "route %u %u managed off if %" PRIu64,
                    (unsigned)withheld.source, (unsigned)withheld.destination, guard) > 0);
    assert(run_command(command) == GRIDPICO_OK && router.graph.revision == graph);
    service_automatic_policy();
    const size_t slot = bitmap_slot(G);
    assert(pat2_boot.coherent && !pat2_boot.pending && apply->progress.pair_outcomes[3] == GRIDPICO_PAT2_OPERATOR);
    assert(apply->projection.view.observation_count == 7 && apply->projection.view.binding_count == 16);
    assert(router.session.apc_layout.rows[0] == router.discovered[slot].input &&
           router.session.apc_layout.columns[0] == router.discovered[slot].output);
    assert(route_state(withheld) == GRIDPICO_PATCH_ROUTE_OFF && bitmap_count(&router.graph) == 25);
    finish_bitmap_auto(25, 2);
    capture_bitmap_pages(path, 25, false);
    assert(wire.count == 0 && queues_empty());
    bitmap_note_pair(B, C, 68);
    assert(memcmp(original_boot_sectors, boot_sectors, sizeof boot_sectors) == 0);
    puts("Boot PAT2: borrowed 24, staged Off + two On at 25, stale discard, late slots and guarded operator Off, sectors identical");
}

static void bitmap_case(bool boot, const char *path)
{
    assert(router.graph.pico25_fixture && router.graph.revision == 0);
    static gridpico_patch_graph_route_metadata metadata;
    assert(gridpico_patch_graph_read_route_metadata(&router.graph, &metadata) == GRIDPICO_UNAVAILABLE);
    if (boot) start_bitmap_boot();
    else pat2_boot_read(NULL, NULL);
    assert(gridpico_cli_init(&cli, &router.session, gridpico_patch_session_read_snapshot, NULL, NULL,
                             NULL, NULL, capture, NULL) == GRIDPICO_OK);
    bind_fixture_pages();
    gridpico_cli_bind_apc_patch(&cli, gridpico_patch_session_read_apc_patch, &router.panel_scratch);
    gridpico_pico_auto_routes_init(&automatic_routes, &router, &routing_scratch);
    mount_hub(ROOT_HUB, 0, 0);
    mount_hub(DOWNSTREAM_HUB, ROOT_HUB, 5);
    mount_midi(A, names[A], 'A'); /* The registrar must work before any native attachment. */
    assert(mounted_binding(A) != NULL && router.graph.revision != 0);
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, true);
    for (uint8_t i = B; i <= F; ++i) {
        mount_midi(i, names[i], (char)('A' + i));
        assert(mounted_binding(i) != NULL);
    }
    place_after_roles();
    router.session.read_local_graph = refuse_full_vector;
    if (boot) boot_bitmap_case(path);
    else machine_bitmap_case(path);
    printf("Host sizeof Graph %zu shadow %zu router %zu boot workspace %zu\n",
           sizeof router.graph, sizeof pat2_boot.apply.shadow, sizeof router, sizeof pat2_boot);
}
