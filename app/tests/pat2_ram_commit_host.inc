/* Borrowed byte arrays only. The intent comes from the real capture fixture. */
enum {
    RAM_SECTOR = GRIDPICO_PICO_SAVE_SECTOR,
    RAM_DATA_END = RAM_SECTOR + GRIDPICO_PICO_PAT2_DATA_BYTES,
    RAM_WRITE_END = RAM_SECTOR + GRIDPICO_PICO_PAT2_RECORD_BYTES,
};

static uint8_t ram[2][RAM_SECTOR], ram_before[2][RAM_SECTOR], alternate[RAM_SECTOR];
static gridpico_pico_pat2_ram_scratch ram_work;

static void ram_fresh(void)
{
    memset(ram, 0xff, sizeof(ram));
    memset(&ram_work, 0, sizeof(ram_work));
}

static void ram_seed(uint8_t *bytes, const gridpico_pico_pat2_intent *intent, uint32_t generation, bool different)
{
    gridpico_pico_pat2_intent seed = *intent;
    seed.generation = generation;
    if (different) seed.pairs[0].enabled ^= 1u;
    memset(bytes, 0xff, RAM_SECTOR);
    assert(gridpico_pico_encode_pat2(&seed, bytes, RAM_SECTOR) == GRIDPICO_OK);
}

static gridpico_result ram_request(const gridpico_pico_pat2_intent *intent, const gridpico_pico_pat2_ram_fault *fault)
{
    return gridpico_pico_pat2_ram_commit(ram[0], ram[1], intent, fault, &ram_work);
}

static void ram_selected(int slot, uint32_t generation, gridpico_pico_pat2_intent *intent)
{
    gridpico_pico_pat2_status status;
    assert(gridpico_pico_read_pat2(ram[0], ram[1], &status, intent) ==
           (slot < 0 ? GRIDPICO_UNAVAILABLE : GRIDPICO_OK));
    assert(status.selected == slot);
    if (slot >= 0) assert(intent->generation == generation);
}

static void ram_request_unchanged(const gridpico_pico_pat2_intent *intent, gridpico_result expected)
{
    memcpy(ram_before, ram, sizeof(ram));
    assert(ram_request(intent, NULL) == expected);
    assert(memcmp(ram_before, ram, sizeof(ram)) == 0);
}

static void ram_reconcile_unchanged(gridpico_result expected)
{
    memcpy(ram_before, ram, sizeof(ram));
    assert(gridpico_pico_pat2_ram_reconcile(ram[0], ram[1], &ram_work) == expected);
    assert(memcmp(ram_before, ram, sizeof(ram)) == 0);
}

static void ram_previous(const gridpico_pico_pat2_intent *intent)
{
    ram_fresh();
    assert(ram_request(intent, NULL) == GRIDPICO_OK && ram_work.confirmed.generation == 1 &&
           ram_work.confirmed.slot == 0);
    assert(ram_request(intent, NULL) == GRIDPICO_OK && ram_work.confirmed.generation == 2 &&
           ram_work.confirmed.slot == 1);
}

static void ram_protected_bytes(uint8_t *slot, unsigned kind, const gridpico_pico_pat2_intent *intent)
{
    memset(slot, 0xff, RAM_SECTOR);
    if (kind == 0) {
        /* Valid all-zero PAT1 payload, CRC 7e8c9342 and maximum legacy generation. */
        const uint8_t header[] = {0x47,0x50,0x53,0x4e,1,0,76,0,0xff,0xff,0xff,0xff,
                                  0x50,0x41,0x54,0x31,0x42,0x93,0x8c,0x7e};
        const uint8_t commit[] = {0x47,0x50,0x43,0x4d,0xff,0xff,0xff,0xff,0x42,0x93,0x8c,0x7e};
        memcpy(slot, header, sizeof(header));
        memset(slot + 20, 0, 76);
        memcpy(slot + 256, commit, sizeof(commit));
    } else if (kind == 1) {
        memcpy(slot, "unknown", 7);
    } else {
        ram_seed(slot, intent, 33, false);
        memset(slot + GRIDPICO_PICO_PAT2_DATA_BYTES, 0xff, GRIDPICO_PICO_SAVE_PAGE);
    }
    const gridpico_pico_pat2_kind kinds[] = {
        GRIDPICO_PICO_PAT2_LEGACY, GRIDPICO_PICO_PAT2_UNKNOWN, GRIDPICO_PICO_PAT2_INCOMPLETE,
    };
    gridpico_pico_pat2_status status;
    gridpico_pico_pat2_intent decoded;
    assert(gridpico_pico_read_pat2(slot, NULL, &status, &decoded) == GRIDPICO_UNAVAILABLE);
    assert(status.copies[0].kind == kinds[kind]);
    if (kind == 0) assert(status.copies[0].legacy.kind == GRIDPICO_PICO_PAT1_COMMITTED);
}

static void verify_ram_admission(const gridpico_pico_pat2_intent *intent)
{
    static uint8_t peer[RAM_SECTOR];
    gridpico_pico_pat2_intent decoded;
    ram_previous(intent);
    memcpy(peer, ram[1], RAM_SECTOR);
    assert(ram_request(intent, NULL) == GRIDPICO_OK); /* Inactive older copy. */
    assert(ram_work.confirmed.slot == 0 && ram_work.confirmed.generation == 3 &&
           memcmp(peer, ram[1], RAM_SECTOR) == 0);
    ram_selected(0, 3, &decoded);
    assert(memcmp(decoded.slots, intent->slots, sizeof(decoded.slots)) == 0 &&
           decoded.pair_count == intent->pair_count &&
           memcmp(decoded.pairs, intent->pairs, intent->pair_count * sizeof(decoded.pairs[0])) == 0);

    memcpy(ram[1], ram[0], RAM_SECTOR);
    memcpy(peer, ram[0], RAM_SECTOR);
    assert(ram_request(intent, NULL) == GRIDPICO_OK); /* Identical tie preserves A. */
    assert(ram_work.confirmed.slot == 1 && ram_work.confirmed.generation == 4 &&
           memcmp(peer, ram[0], RAM_SECTOR) == 0);

    for (unsigned kind = 0; kind < 3; ++kind) {
        ram_fresh();
        ram_protected_bytes(ram[0], kind, intent);
        memcpy(peer, ram[0], RAM_SECTOR);
        assert(ram_request(intent, NULL) == GRIDPICO_OK);
        assert(ram_work.confirmed.slot == 1 && ram_work.confirmed.generation == 1 &&
               memcmp(peer, ram[0], RAM_SECTOR) == 0);
        ram_request_unchanged(intent, GRIDPICO_UNAVAILABLE);
    }

    ram_fresh();
    ram_seed(ram[0], intent, UINT32_MAX, false);
    ram_request_unchanged(intent, GRIDPICO_CAPACITY);
    memcpy(ram_before, ram, sizeof(ram));
    assert(gridpico_pico_pat2_ram_commit(ram[0], NULL, intent, NULL, &ram_work) == GRIDPICO_UNAVAILABLE);
    assert(memcmp(ram_before, ram, sizeof(ram)) == 0);
    ram_seed(ram[0], intent, 8, false);
    ram_seed(ram[1], intent, 8, true);
    ram_request_unchanged(intent, GRIDPICO_UNAVAILABLE);

    ram_fresh();
    gridpico_pico_pat2_intent bad = *intent;
    bad.pair_count = GRIDPICO_PICO_PAT2_PAIRS + 1;
    ram_request_unchanged(&bad, GRIDPICO_INVALID);
    assert(gridpico_pico_pat2_ram_commit(ram[0], ram[0] + 1, intent, NULL, &ram_work) == GRIDPICO_INVALID);
    assert(memcmp(ram_before, ram, sizeof(ram)) == 0);
}

static void verify_ram_cuts(const gridpico_pico_pat2_intent *intent)
{
    const size_t stops[] = {0, 64, RAM_SECTOR, RAM_SECTOR + 128, RAM_DATA_END, RAM_DATA_END + 3, RAM_WRITE_END};
    for (size_t i = 0; i < sizeof(stops) / sizeof(stops[0]); ++i) {
        ram_previous(intent);
        memcpy(ram_before, ram, sizeof(ram));
        const bool complete = stops[i] == RAM_WRITE_END;
        const gridpico_pico_pat2_ram_fault fault = {.cut_after = stops[i]};
        assert(ram_request(intent, &fault) == (complete ? GRIDPICO_OK : GRIDPICO_UNAVAILABLE));
        assert(memcmp(ram_before[1], ram[1], RAM_SECTOR) == 0);
        if (stops[i] == 0) assert(memcmp(ram_before[0], ram[0], RAM_SECTOR) == 0);
        gridpico_pico_pat2_intent decoded;
        ram_selected(complete ? 0 : 1, complete ? 3 : 2, &decoded);
        assert(!ram_work.reconcile_required && ram_work.confirmed.generation == (complete ? 3u : 2u));
        if (stops[i] > RAM_SECTOR && !complete) ram_request_unchanged(intent, GRIDPICO_UNAVAILABLE);
    }
    ram_fresh();
    const gridpico_pico_pat2_ram_fault tear = {.cut_after = RAM_DATA_END + 3};
    assert(ram_request(intent, &tear) == GRIDPICO_UNAVAILABLE);
    gridpico_pico_pat2_intent unused;
    ram_selected(-1, 0, &unused);
    assert(ram_work.confirmed.generation == 0 && !ram_work.reconcile_required);
}

static void verify_ram_reconcile(const gridpico_pico_pat2_intent *intent)
{
    ram_previous(intent);
    memcpy(alternate, ram[1], RAM_SECTOR);
    const gridpico_pico_pat2_ram_fault hidden = {.cut_after = SIZE_MAX, .hide_target = true};
    assert(ram_request(intent, &hidden) == GRIDPICO_UNAVAILABLE && ram_work.reconcile_required &&
           ram_work.confirmed.generation == 2 && memcmp(alternate, ram[1], RAM_SECTOR) == 0);
    ram_request_unchanged(intent, GRIDPICO_UNAVAILABLE);
    assert(gridpico_pico_pat2_ram_reconcile(NULL, ram[1], &ram_work) == GRIDPICO_UNAVAILABLE);
    assert(memcmp(ram_before, ram, sizeof(ram)) == 0 && ram_work.reconcile_required);
    ram_reconcile_unchanged(GRIDPICO_OK);
    assert(!ram_work.reconcile_required && ram_work.confirmed.generation == 3);

    ram_previous(intent);
    ram_seed(alternate, intent, 3, true); /* Complete but different at the attempted generation. */
    const gridpico_pico_pat2_ram_fault mismatch = {.cut_after = SIZE_MAX, .alternate_target = alternate};
    memcpy(ram_before, ram, sizeof(ram));
    assert(ram_request(intent, &mismatch) == GRIDPICO_UNAVAILABLE && ram_work.reconcile_required &&
           ram_work.confirmed.generation == 2 && memcmp(ram_before[1], ram[1], RAM_SECTOR) == 0);
    uint8_t exact[RAM_SECTOR];
    memcpy(exact, ram[0], sizeof(exact));
    memcpy(ram[0], alternate, sizeof(alternate)); /* Fixture injects different backing for the read. */
    gridpico_pico_pat2_intent decoded;
    ram_selected(0, 3, &decoded);
    ram_reconcile_unchanged(GRIDPICO_UNAVAILABLE);
    assert(ram_work.reconcile_required && ram_work.confirmed.generation == 2);
    memcpy(ram[0], exact, sizeof(exact));
    ram_reconcile_unchanged(GRIDPICO_OK);

    ram_previous(intent);
    const gridpico_pico_pat2_ram_fault hidden_torn = {.cut_after = RAM_DATA_END + 3, .hide_target = true};
    assert(ram_request(intent, &hidden_torn) == GRIDPICO_UNAVAILABLE && ram_work.reconcile_required);
    memcpy(alternate, ram[1], RAM_SECTOR);
    ram_seed(ram[1], intent, 2, true); /* Same original generation alone is insufficient. */
    ram_reconcile_unchanged(GRIDPICO_UNAVAILABLE);
    assert(ram_work.reconcile_required);
    memcpy(ram[1], alternate, sizeof(alternate));
    ram_reconcile_unchanged(GRIDPICO_UNAVAILABLE);
    assert(!ram_work.reconcile_required && ram_work.confirmed.generation == 2);
    ram_request_unchanged(intent, GRIDPICO_UNAVAILABLE);
}

static void verify_ram_commit(const gridpico_pico_pat2_intent *intent)
{
    uint8_t before[sizeof(*intent)];
    memcpy(before, intent, sizeof(before));
    verify_ram_admission(intent);
    verify_ram_cuts(intent);
    verify_ram_reconcile(intent);
    assert(memcmp(before, intent, sizeof(before)) == 0);
    puts("PASS PAT2 RAM commit-last, protected slots, interrupted writes and read-only reconciliation");
}
