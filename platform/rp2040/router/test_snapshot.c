#include "app/rp2040_snapshot.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum interruption { COMPLETE, BUSY, AFTER_ERASE, MID_DATA, AFTER_DATA, MID_COMMIT, AFTER_COMMIT, HIDE_VERIFY };

static struct {
    unsigned calls;
    enum interruption interruption;
    unsigned unreadable_sector_plus_one;
    uint8_t sectors[GRIDPICO_PICO_SAVE_SECTORS][GRIDPICO_PICO_SAVE_SECTOR];
} memory;
static gridpico_pico_router live, rebooted;
static gridpico_pico_save_plan plan;

static const uint8_t *read_sector(void *context, unsigned sector)
{
    assert(context == &memory && sector < GRIDPICO_PICO_SAVE_SECTORS);
    if (memory.unreadable_sector_plus_one == sector + 1) {
        memory.unreadable_sector_plus_one = 0;
        return NULL;
    }
    return memory.sectors[sector];
}

static void program(uint8_t *output, const uint8_t *input, size_t size)
{
    for (size_t i = 0; i < size; ++i) output[i] &= input[i];
}

static gridpico_result apply(void *context, const gridpico_pico_save_plan *request)
{
    assert(context == &memory && request->sector < GRIDPICO_PICO_SAVE_SECTORS);
    ++memory.calls;
    const enum interruption cut = memory.interruption;
    if (cut == BUSY) return GRIDPICO_UNAVAILABLE;
    uint8_t *sector = memory.sectors[request->sector];
    memset(sector, 0xff, GRIDPICO_PICO_SAVE_SECTOR);
    if (cut == AFTER_ERASE) return GRIDPICO_UNAVAILABLE;
    program(sector, request->data, cut == MID_DATA ? 32u : GRIDPICO_PICO_SAVE_PAGE);
    if (cut == MID_DATA || cut == AFTER_DATA) return GRIDPICO_UNAVAILABLE;
    program(sector + GRIDPICO_PICO_SAVE_PAGE, request->commit, cut == MID_COMMIT ? 8u : GRIDPICO_PICO_SAVE_PAGE);
    if (cut == HIDE_VERIFY) memory.unreadable_sector_plus_one = request->sector + 1;
    return cut == AFTER_COMMIT ? GRIDPICO_UNAVAILABLE : GRIDPICO_OK;
}

static const gridpico_pico_save_store store = {&memory, read_sector, apply};

static gridpico_apc_session_snapshot snapshot(const gridpico_pico_router *router)
{
    gridpico_apc_session_snapshot state;
    assert(gridpico_patch_session_export_apc_snapshot(&router->session, &state) == GRIDPICO_OK);
    return state;
}

static void edit_live(void)
{
    gridpico_apc_patch_snapshot page;
    assert(gridpico_patch_session_read_apc_patch(&live.session, &page) == GRIDPICO_OK);
    assert(gridpico_patch_session_move_apc_patch(&live.session, page.layout_revision, GRIDPICO_PATCH_INPUT,
                                                0, 1) == GRIDPICO_OK);
    const gridpico_local_route_request request = {{1, 4, GRIDPICO_LOCAL_MANAGED}, false};
    assert(gridpico_patch_session_set_local_route(&live.session, &request) == GRIDPICO_OK);
}

/* Build a committed but different record while independently computing the CRC over its byte stream. */
static void edit_record(uint8_t *record, uint32_t generation, uint8_t first_row)
{
    record[20] = first_row;
    for (unsigned i = 0; i < 4; ++i) record[8 + i] = record[260 + i] = (uint8_t)(generation >> (8u * i));
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < 96; ++i) {
        if (i >= 16 && i < 20) continue;
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t feedback = (crc ^ ((uint32_t)record[i] >> bit)) & 1u;
            crc = (crc >> 1) ^ (feedback * UINT32_C(0xedb88320));
        }
    }
    for (unsigned i = 0; i < 4; ++i) record[16 + i] = record[264 + i] = (uint8_t)(~crc >> (8u * i));
}

int main(void)
{
    memset(memory.sectors, 0xff, sizeof(memory.sectors));
    gridpico_pico_save_state active, loaded;
    assert(gridpico_pico_boot_saved(&live, &store, &active) == GRIDPICO_OK);
    assert(active.generation == 0 && !active.storage_issue);
    const gridpico_apc_session_snapshot factory = snapshot(&live);
    assert(factory.layout.rows[0] == 1 && factory.layout.rows[1] == 2 && factory.route_count == 3);
    assert(gridpico_pico_save_patch(&live, &store, &active, &plan) == GRIDPICO_OK);
    assert(active.sector == 0 && active.generation == 1);
    static uint8_t previous[GRIDPICO_PICO_SAVE_SECTOR];
    memcpy(previous, memory.sectors[0], sizeof(previous));
    assert(memcmp(previous, "GPSN\x01\x00\x4c\x00\x01\x00\x00\x00PAT1", 16) == 0);
    edit_live();
    const gridpico_apc_session_snapshot edited = snapshot(&live);
    assert(edited.layout.rows[0] == 2 && edited.route_count == 2);
    for (enum interruption cut = BUSY; cut <= AFTER_COMMIT; ++cut) {
        memory.interruption = cut;
        const bool committed = cut == AFTER_COMMIT;
        assert(gridpico_pico_save_patch(&live, &store, &active, &plan) ==
               (committed ? GRIDPICO_OK : GRIDPICO_UNAVAILABLE));
        assert(memcmp(previous, memory.sectors[0], sizeof(previous)) == 0);
        assert(gridpico_pico_boot_saved(&rebooted, &store, &loaded) == GRIDPICO_OK);
        assert(active.generation == loaded.generation && loaded.generation == (committed ? 2u : 1u));
        assert(snapshot(&rebooted).route_count == (committed ? 2u : 3u));
        assert(snapshot(&rebooted).layout.rows[0] == (committed ? 2u : 1u));
        assert(snapshot(&live).layout.rows[0] == 2);
    }
    memory.interruption = COMPLETE;

    edit_record(memory.sectors[1], 2, 99);
    assert(gridpico_pico_boot_saved(&rebooted, &store, &loaded) == GRIDPICO_OK);
    assert(loaded.generation == 1 && loaded.storage_issue && snapshot(&rebooted).route_count == 3);
    assert(gridpico_pico_save_patch(&rebooted, &store, &loaded, &plan) == GRIDPICO_OK);
    assert(loaded.generation == 3 && loaded.sector == 1);

    edit_record(memory.sectors[1], 1, 99);
    assert(gridpico_pico_boot_saved(&rebooted, &store, &loaded) == GRIDPICO_OK);
    assert(loaded.generation == 1 && loaded.sector == 0 && loaded.storage_issue);
    assert(gridpico_pico_save_patch(&rebooted, &store, &loaded, &plan) == GRIDPICO_OK);
    assert(loaded.generation == 2 && loaded.sector == 1);

    /* Both layouts are legal. Equal generation with different payloads is a real conflict. */
    edit_record(memory.sectors[1], 1, 0);
    const unsigned prior_calls = memory.calls;
    assert(gridpico_pico_boot_saved(&rebooted, &store, &loaded) == GRIDPICO_OK);
    assert(loaded.generation == 0 && loaded.storage_issue);
    assert(gridpico_pico_save_patch(&rebooted, &store, &loaded, &plan) == GRIDPICO_UNAVAILABLE);
    assert(memory.calls == prior_calls);

    memory.sectors[1][4] = 2;
    assert(gridpico_pico_boot_saved(&rebooted, &store, &loaded) == GRIDPICO_OK);
    assert(loaded.generation == 1 && loaded.storage_issue);
    assert(gridpico_pico_save_patch(&rebooted, &store, &loaded, &plan) == GRIDPICO_UNAVAILABLE);
    assert(memory.calls == prior_calls && memory.sectors[1][4] == 2);

    memset(memory.sectors[1], 0xff, GRIDPICO_PICO_SAVE_SECTOR);
    gridpico_pico_router_attach(&rebooted, GRIDPICO_PICO_APC, true);
    gridpico_pico_router_attach(&rebooted, GRIDPICO_PICO_SYNTH, true);
    const gridpico_pico_packet attack = {{9, 0x90, 60, 96}};
    assert(gridpico_pico_router_receive(&rebooted, GRIDPICO_PICO_APC, &attack) == GRIDPICO_OK);
    assert(gridpico_pico_save_patch(&rebooted, &store, &loaded, &plan) == GRIDPICO_UNAVAILABLE);
    assert(memory.calls == prior_calls);

    gridpico_apc_session_snapshot empty = factory;
    empty.route_count = 0;
    assert(gridpico_pico_router_init_snapshot(&live, &empty) == GRIDPICO_OK);
    assert(gridpico_pico_save_patch(&live, &store, &loaded, &plan) == GRIDPICO_OK);
    assert(gridpico_pico_boot_saved(&rebooted, &store, &loaded) == GRIDPICO_OK);
    assert(loaded.generation == 2 && snapshot(&rebooted).route_count == 0);

    static uint8_t last_complete[GRIDPICO_PICO_SAVE_SECTOR];
    memcpy(last_complete, memory.sectors[1], sizeof(last_complete));
    assert(gridpico_pico_save_patch(&rebooted, &store, &loaded, &plan) == GRIDPICO_OK);
    assert(loaded.generation == 3 && loaded.sector == 0);
    assert(memcmp(last_complete, memory.sectors[1], sizeof(last_complete)) == 0);

    edit_record(memory.sectors[1], UINT32_MAX, 1);
    assert(gridpico_pico_boot_saved(&rebooted, &store, &loaded) == GRIDPICO_OK);
    assert(loaded.generation == UINT32_MAX);
    assert(gridpico_pico_save_patch(&rebooted, &store, &loaded, &plan) == GRIDPICO_CAPACITY);

    /* A hidden immediate read after a real commit must never permit erasing that newer copy. */
    memcpy(memory.sectors[0], previous, sizeof(previous));
    memset(memory.sectors[1], 0xff, GRIDPICO_PICO_SAVE_SECTOR);
    assert(gridpico_pico_boot_saved(&live, &store, &active) == GRIDPICO_OK && active.generation == 1);
    edit_live();
    memory.interruption = HIDE_VERIFY;
    assert(gridpico_pico_save_patch(&live, &store, &active, &plan) == GRIDPICO_UNAVAILABLE);
    assert(active.generation == 1 && active.reconcile_required);
    memory.interruption = AFTER_ERASE;
    const unsigned hidden_calls = memory.calls;
    assert(gridpico_pico_save_patch(&live, &store, &active, &plan) == GRIDPICO_UNAVAILABLE);
    assert(memory.calls == hidden_calls);
    memory.unreadable_sector_plus_one = 2;
    assert(gridpico_pico_boot_saved(&live, &store, &active) == GRIDPICO_OK);
    assert(active.generation == 1 && active.reconcile_required);
    assert(gridpico_pico_save_patch(&live, &store, &active, &plan) == GRIDPICO_UNAVAILABLE);
    assert(memory.calls == hidden_calls);
    assert(gridpico_pico_boot_saved(&rebooted, &store, &loaded) == GRIDPICO_OK);
    assert(loaded.generation == 2 && !loaded.reconcile_required && snapshot(&rebooted).layout.rows[0] == 2);
    memcpy(last_complete, memory.sectors[1], sizeof(last_complete));
    assert(gridpico_pico_save_patch(&rebooted, &store, &loaded, &plan) == GRIDPICO_UNAVAILABLE);
    assert(memcmp(last_complete, memory.sectors[1], sizeof(last_complete)) == 0);
    assert(gridpico_pico_boot_saved(&live, &store, &active) == GRIDPICO_OK && active.generation == 2);
    puts("PASS explicit Save, torn writes, readback recovery, older fallback, conflict/unknown isolation, quiet-point and no wrap");
}
