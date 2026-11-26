static gridpico_pico_pat2_capture_scratch capture_scratch;
static gridpico_pico_pat2_capture_evidence capture_evidence;
static gridpico_pico_pat2_intent captured;

static void capture_observe(void)
{
    physical_roles.graph_revision = router.graph.revision;
    assert(gridpico_pico_pat2_observe_capture(&router, physical_roles.revision, &capture_evidence) == GRIDPICO_OK);
}

static void capture_expect(gridpico_result expected)
{
    static gridpico_pico_router old_router;
    gridpico_pico_pat2_progress old_progress;
    memset(&captured, 0xa5, sizeof(captured));
    uint8_t before[sizeof(captured)];
    memcpy(before, &captured, sizeof(before));
    memcpy(&old_router, &router, sizeof(router));
    memcpy(&old_progress, &apply.progress, sizeof(old_progress));
    assert(gridpico_pico_pat2_capture(&router, physical_roles.revision, &capture_evidence,
                                     77, &capture_scratch, &captured) == expected);
    assert(memcmp(&old_router, &router, sizeof(router)) == 0);
    assert(memcmp(&old_progress, &apply.progress, sizeof(old_progress)) == 0);
    if (expected != GRIDPICO_OK) assert(memcmp(before, &captured, sizeof(before)) == 0);
}

static uint8_t serial_for(uint8_t ref)
{
    return captured.identities[captured.directions[ref - 1u].identity_ref - 1u].serial[0];
}

static void capture_roundtrip(void)
{
    uint8_t bytes[GRIDPICO_PICO_SAVE_SECTOR];
    memset(bytes, 0xff, sizeof(bytes));
    assert(gridpico_pico_encode_pat2(&captured, bytes, sizeof(bytes)) == GRIDPICO_OK);
    gridpico_pico_pat2_status status;
    gridpico_pico_pat2_intent decoded;
    assert(gridpico_pico_read_pat2(bytes, NULL, &status, &decoded) == GRIDPICO_OK);
    assert(decoded.generation == 77 && decoded.pair_count == captured.pair_count);
    assert(memcmp(decoded.slots, captured.slots, sizeof(decoded.slots)) == 0);
    assert(memcmp(decoded.pairs, captured.pairs, decoded.pair_count * sizeof(decoded.pairs[0])) == 0);
}

static void verify_prior_capture(unsigned phase)
{
    capture_observe();
    capture_expect(GRIDPICO_OK);
    assert(captured.pair_count == (phase < 2 ? 6u : 7u));
    assert(captured.pairs[0].enabled && !captured.pairs[1].enabled && captured.pairs[2].enabled);
    assert(serial_for(captured.slots[5]) == 'C');
    assert(strcmp(captured.directions[captured.slots[5] - 1u].label, "Equal name") == 0);
    assert(captured.pairs[4].enabled == (phase < 2) &&
           serial_for(captured.pairs[4].destination_ref) == 'C');
    if (phase == 0) assert(serial_for(captured.slots[0]) == 'C');
    else assert(captured.slots[0] == 0);
    capture_roundtrip();
}

static void reset_capture_fixture(const gridpico_pico_pat2_intent *prior)
{
    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK);
    gridpico_pico_auto_routes_init(&automatic_routes, &router, &routing_scratch);
    memset(discovered, 0, sizeof(discovered));
    memset(mounted, 0, sizeof(mounted));
    memset(&apply, 0, sizeof(apply));
    apply.intent = prior;
    assert(gridpico_pico_pat2_bind_capture(&router, &apply) == GRIDPICO_OK);
}

static void attach_capture_pair(void)
{
    observe_one_cable(0, 'A');
    observe_one_cable(1, 'B');
    admit_one_cable(0, 0, a_peer);
    admit_one_cable(1, 1, (gridpico_pico_peer)(a_peer + 1));
}

static void verify_detached_prior_off(void)
{
    reset_capture_fixture(&record);
    attach_capture_pair();
    advance_policy();
    assert(apply.progress.pair_outcomes[0] == GRIDPICO_PAT2_APPLIED && apply.committed_off[0] != 0);
    gridpico_apc_physical *panel = &router.panels[GRIDPICO_PICO_APC].frontend;
    assert(panel->assign(panel->session_context, router.session.apc_layout_revision,
                         GRIDPICO_PATCH_INPUT, 1, 0) == GRIDPICO_OK);
    assert(panel->assign(panel->session_context, router.session.apc_layout_revision,
                         GRIDPICO_PATCH_OUTPUT, 1, 0) == GRIDPICO_OK);
    const gridpico_pico_peer peer = (gridpico_pico_peer)(a_peer + 1);
    gridpico_pico_router_attach(&router, peer, false);
    mounted[1] = false;
    capture_observe();
    capture_expect(GRIDPICO_OK);
    assert(captured.pair_count == 1 && !captured.pairs[0].enabled &&
           serial_for(captured.pairs[0].destination_ref) == 'B');
    capture_roundtrip();

    mounted[1] = true;
    gridpico_pico_router_attach(&router, peer, true);
    const gridpico_revision graph = router.graph.revision;
    assert(cli_route(route(0, 1, false).route, false, false) == GRIDPICO_OK && router.graph.revision == graph);
    gridpico_pico_router_attach(&router, peer, false);
    mounted[1] = false;
    capture_observe();
    capture_expect(GRIDPICO_UNAVAILABLE); /* The newer live Off needs a current key. */
}

static void verify_first_capture(void)
{
    reset_capture_fixture(NULL);
    attach_capture_pair();
    const gridpico_local_route off = route(0, 1, false).route, on = route(1, 0, true).route;
    const gridpico_revision before_off = router.graph.revision;
    capture_observe();
    assert(cli_route(off, false, false) == GRIDPICO_OK && router.graph.revision == before_off);
    capture_expect(GRIDPICO_UNAVAILABLE);
    advance_policy();
    assert(!desired(off) && desired(on));
    gridpico_apc_physical *panel = &router.panels[GRIDPICO_PICO_APC].frontend;
    assert(panel->assign(panel->session_context, router.session.apc_layout_revision,
                         GRIDPICO_PATCH_OUTPUT, 1, 0) == GRIDPICO_OK);

    capture_observe();
    capture_expect(GRIDPICO_OK);
    assert(captured.pair_count == 1 && !captured.pairs[0].enabled && captured.slots[6] == 0);
    const gridpico_revision before_on = router.graph.revision;
    assert(panel->set_cell(panel->session_context, router.session.apc_layout_revision,
                           before_on, 1, 0, true) == GRIDPICO_OK && router.graph.revision == before_on);
    capture_expect(GRIDPICO_UNAVAILABLE); /* Successful no-op On changed the captured evidence. */
    capture_observe();
    capture_expect(GRIDPICO_OK);
    assert(captured.pair_count == 2 && captured.pairs[1].enabled);
    const uint8_t a = captured.slots[0], b = captured.slots[1];
    assert(a != b && serial_for(a) == 'A' && serial_for(b) == 'B');
    assert(strcmp(captured.directions[a - 1u].label, captured.directions[b - 1u].label) == 0);
    capture_roundtrip();
    verify_ram_commit(&captured);
    record = captured;
    record.pair_count = 1;
    memset(record.slots, 0, sizeof(record.slots));

    observe_one_cable(3, 'A'); /* No Graph revision change, no admitted challenger. */
    capture_expect(GRIDPICO_UNAVAILABLE);
    observations[3].verified = false;
    capture_expect(GRIDPICO_UNAVAILABLE);
    mounted[3] = false;
    ++physical_roles.revision;
    capture_expect(GRIDPICO_UNAVAILABLE);
    capture_observe();

    assert(gridpico_pico_router_renew_managed_input(&router, a_peer) == GRIDPICO_OK);
    capture_expect(GRIDPICO_UNAVAILABLE);
    capture_observe();
    gridpico_pico_router_attach(&router, a_peer, false);
    capture_observe();
    capture_expect(GRIDPICO_UNAVAILABLE); /* An actual nonzero saved position is detached. */
    gridpico_pico_router_attach(&router, a_peer, true);

    /* Corrupt the test evidence by removing a touched On with no Off ledger. */
    const gridpico_local_route_request removed = {on, false};
    assert(gridpico_patch_graph_set_local_route(&router.graph, &removed) == GRIDPICO_OK);
    capture_observe();
    capture_expect(GRIDPICO_UNAVAILABLE);
    apply.capture_lifetime = false;
    capture_expect(GRIDPICO_UNAVAILABLE);
    verify_detached_prior_off();
}

static void verify_capture_overflow(void)
{
    record = (gridpico_pico_pat2_intent){
        .generation = 11, .identity_count = 8, .direction_count = 8, .pair_count = 4,
    };
    for (uint8_t i = 0; i < 8; ++i) {
        record.identities[i] = (gridpico_pico_pat2_identity){
            .vid = 0x09e8, .pid = 0x004e, .bcd_device = 0x0112, .interface_number = 1,
            .input_endpoint = 0x81, .output_endpoint = 2, .input_cables = 1, .output_cables = 1,
            .serial_length = 1, .serial = {(uint8_t)('0' + i)},
        };
        record.directions[i] = (gridpico_pico_pat2_direction){
            .identity_ref = (uint8_t)(i + 1), .direction = i < 4 ? GRIDPICO_PATCH_INPUT : GRIDPICO_PATCH_OUTPUT,
            .label = "Missing",
        };
    }
    for (uint8_t i = 0; i < 4; ++i) {
        record.pairs[i] = (gridpico_pico_pat2_pair){(uint8_t)(i + 1), (uint8_t)(i + 5), 1};
    }
    reset_capture_fixture(&record);
    attach_capture_pair();
    advance_policy();
    capture_observe();
    capture_expect(GRIDPICO_CAPACITY); /* Eight retained keys plus live layout keys. */
    puts("PASS PAT2 capture: first intent, exact operator choices, retained prior, census guards, overflow and RAM round-trip");
}
