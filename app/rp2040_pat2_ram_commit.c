#include "app/rp2040_pat2_ram_commit.h"

#ifdef GRIDPICO_PAT2_APPLY_HOST
#include <string.h>

static bool overlaps(const void *left, size_t left_size, const void *right, size_t right_size)
{
    if (left == NULL || right == NULL) return false;
    const uintptr_t a = (uintptr_t)left, b = (uintptr_t)right;
    return a <= b ? b - a < left_size : a - b < right_size;
}

static bool valid_sectors(const uint8_t *a, const uint8_t *b,
                          const gridpico_pico_pat2_ram_scratch *scratch)
{
    return scratch != NULL && !overlaps(a, GRIDPICO_PICO_SAVE_SECTOR, b, GRIDPICO_PICO_SAVE_SECTOR) &&
           !overlaps(a, GRIDPICO_PICO_SAVE_SECTOR, scratch, sizeof(*scratch)) &&
           !overlaps(b, GRIDPICO_PICO_SAVE_SECTOR, scratch, sizeof(*scratch));
}

static bool classify(const uint8_t *a, const uint8_t *b, gridpico_pico_pat2_ram_scratch *scratch,
                     gridpico_pico_pat2_status *status)
{
    if (a == NULL || b == NULL) return false;
    (void)gridpico_pico_read_pat2(a, b, status, &scratch->typed);
    return !status->conflict;
}

static gridpico_pico_pat2_ram_receipt selected(const gridpico_pico_pat2_status *status)
{
    if (status->selected < 0) return (gridpico_pico_pat2_ram_receipt){0};
    return (gridpico_pico_pat2_ram_receipt){
        .generation = status->copies[status->selected].generation, .slot = (unsigned)status->selected,
    };
}

static bool program_page(uint8_t *ram, const uint8_t *bytes, size_t *remaining)
{
    const size_t count = *remaining < GRIDPICO_PICO_SAVE_PAGE ? *remaining : GRIDPICO_PICO_SAVE_PAGE;
    for (size_t i = 0; i < count; ++i) ram[i] &= bytes[i];
    *remaining -= count;
    return *remaining != 0;
}

static bool simulate_write(uint8_t *ram, const uint8_t *record, size_t budget)
{
    const size_t erase = budget < GRIDPICO_PICO_SAVE_SECTOR ? budget : GRIDPICO_PICO_SAVE_SECTOR;
    memset(ram, 0xff, erase);
    if (budget <= GRIDPICO_PICO_SAVE_SECTOR) return false;
    size_t remaining = budget - GRIDPICO_PICO_SAVE_SECTOR;

    for (size_t offset = 0; offset < GRIDPICO_PICO_PAT2_DATA_BYTES; offset += GRIDPICO_PICO_SAVE_PAGE) {
        if (!program_page(ram + offset, record + offset, &remaining)) return false;
    }
    const size_t commit = GRIDPICO_PICO_PAT2_DATA_BYTES;
    return program_page(ram + commit, record + commit, &remaining);
}

static void settle(gridpico_pico_pat2_ram_scratch *scratch)
{
    scratch->reconcile_required = false;
    memset(&scratch->pending, 0, sizeof(scratch->pending));
}

static gridpico_result confirm(const uint8_t *a, const uint8_t *b,
                              gridpico_pico_pat2_ram_scratch *scratch, bool allow_noncommit)
{
    gridpico_pico_pat2_status status;
    if (!classify(a, b, scratch, &status)) return GRIDPICO_UNAVAILABLE;
    const gridpico_pico_pat2_ram_receipt observed = selected(&status);
    const gridpico_pico_pat2_ram_receipt wanted = scratch->pending.request;
    const uint8_t *target = wanted.slot == 0 ? a : b;
    const uint8_t *peer = wanted.slot == 0 ? b : a;
    const uint8_t *backing = scratch->pending.sectors[wanted.slot];
    /* An injected read observation cannot acknowledge different backing bytes. */
    if (target != backing && memcmp(target, backing, GRIDPICO_PICO_SAVE_SECTOR) != 0) return GRIDPICO_UNAVAILABLE;
    if (memcmp(peer, scratch->protected_bytes, sizeof(scratch->protected_bytes)) != 0) return GRIDPICO_UNAVAILABLE;
    if (observed.slot == wanted.slot && observed.generation == wanted.generation &&
        memcmp(target, scratch->encoded, sizeof(scratch->encoded)) == 0) {
        scratch->confirmed = wanted;
        settle(scratch);
        return GRIDPICO_OK;
    }

    const gridpico_pico_pat2_ram_receipt previous = scratch->pending.previous;
    if (allow_noncommit && observed.slot == previous.slot && observed.generation == previous.generation &&
        status.copies[wanted.slot].generation < wanted.generation) {
        /* Both reads agree that the original selection still wins. A torn
         * target remains protected by normal slot admission.
         */
        settle(scratch);
    }
    return GRIDPICO_UNAVAILABLE;
}

gridpico_result gridpico_pico_pat2_ram_commit(
    uint8_t *a, uint8_t *b, const gridpico_pico_pat2_intent *intent,
    const gridpico_pico_pat2_ram_fault *fault, gridpico_pico_pat2_ram_scratch *scratch)
{
    if (!valid_sectors(a, b, scratch) || intent == NULL ||
        overlaps(a, GRIDPICO_PICO_SAVE_SECTOR, intent, sizeof(*intent)) ||
        overlaps(b, GRIDPICO_PICO_SAVE_SECTOR, intent, sizeof(*intent)) ||
        overlaps(scratch, sizeof(*scratch), intent, sizeof(*intent)) ||
        intent->generation == 0) return GRIDPICO_INVALID;

    const gridpico_pico_pat2_ram_fault f = fault == NULL
        ? (gridpico_pico_pat2_ram_fault){.cut_after = SIZE_MAX} : *fault;
    if ((f.hide_target && f.alternate_target != NULL) ||
        overlaps(f.alternate_target, GRIDPICO_PICO_SAVE_SECTOR, scratch, sizeof(*scratch)) ||
        overlaps(f.alternate_target, GRIDPICO_PICO_SAVE_SECTOR, a, GRIDPICO_PICO_SAVE_SECTOR) ||
        overlaps(f.alternate_target, GRIDPICO_PICO_SAVE_SECTOR, b, GRIDPICO_PICO_SAVE_SECTOR)) {
        return GRIDPICO_INVALID;
    }
    if (scratch->reconcile_required) return GRIDPICO_UNAVAILABLE;

    gridpico_pico_pat2_status status;
    if (!classify(a, b, scratch, &status)) return GRIDPICO_UNAVAILABLE;
    const gridpico_pico_pat2_ram_receipt previous = selected(&status);
    if (previous.generation == UINT32_MAX) return GRIDPICO_CAPACITY;

    unsigned slot = previous.generation ? 1u - previous.slot : 0u;
    if (!previous.generation && status.copies[0].kind != GRIDPICO_PICO_PAT2_ERASED) slot = 1;
    if (status.copies[slot].kind != GRIDPICO_PICO_PAT2_ERASED &&
        !(previous.generation && status.copies[slot].kind == GRIDPICO_PICO_PAT2_COMMITTED)) {
        return GRIDPICO_UNAVAILABLE;
    }

    scratch->typed = *intent;
    scratch->typed.generation = previous.generation + 1u;
    const gridpico_result result = gridpico_pico_encode_pat2(&scratch->typed, scratch->encoded,
                                                              sizeof(scratch->encoded));
    if (result != GRIDPICO_OK) return result;

    scratch->pending.sectors[0] = a;
    scratch->pending.sectors[1] = b;
    scratch->pending.request = (gridpico_pico_pat2_ram_receipt){scratch->typed.generation, slot};
    scratch->pending.previous = previous;
    memcpy(scratch->protected_bytes, slot == 0 ? b : a, sizeof(scratch->protected_bytes));
    scratch->reconcile_required = true;
    uint8_t *target = slot == 0 ? a : b;
    const bool acknowledged = simulate_write(target, scratch->encoded, f.cut_after);

    const uint8_t *read_target = f.hide_target ? NULL : target;
    if (f.alternate_target != NULL) read_target = f.alternate_target;
    return confirm(slot == 0 ? read_target : a, slot == 1 ? read_target : b, scratch, !acknowledged);
}

gridpico_result gridpico_pico_pat2_ram_reconcile(
    const uint8_t *a, const uint8_t *b, gridpico_pico_pat2_ram_scratch *scratch)
{
    if (!valid_sectors(a, b, scratch)) return GRIDPICO_INVALID;
    if (!scratch->reconcile_required || scratch->pending.sectors[0] != a ||
        scratch->pending.sectors[1] != b) return GRIDPICO_UNAVAILABLE;
    return confirm(a, b, scratch, true);
}
#endif
