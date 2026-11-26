#ifndef GRIDPICO_APP_RP2040_PAT2_RAM_COMMIT_H
#define GRIDPICO_APP_RP2040_PAT2_RAM_COMMIT_H

#include "app/rp2040_snapshot_pat2.h"

#ifdef GRIDPICO_PAT2_APPLY_HOST
typedef struct {
    uint32_t generation; /* Zero means no confirmed receipt. */
    unsigned slot;
} gridpico_pico_pat2_ram_receipt;

typedef struct {
    gridpico_pico_pat2_ram_receipt confirmed;
    bool reconcile_required;
    struct {
        const uint8_t *sectors[GRIDPICO_PICO_SAVE_SECTORS];
        gridpico_pico_pat2_ram_receipt request, previous;
    } pending;
    gridpico_pico_pat2_intent typed;
    uint8_t encoded[GRIDPICO_PICO_PAT2_RECORD_BYTES];
    uint8_t protected_bytes[GRIDPICO_PICO_SAVE_SECTOR];
} gridpico_pico_pat2_ram_scratch;

/* Optional fixture fault. Count processed erase bytes, then data bytes, then
 * commit bytes. A cut on the final byte loses the programming result.
 * SIZE_MAX completes normally. An alternate readback is a full RAM view.
 */
typedef struct {
    size_t cut_after;
    bool hide_target;
    const uint8_t *alternate_target;
} gridpico_pico_pat2_ram_fault;

/* Start with zeroed caller scratch. Each nonnull RAM sector is exactly 4 KiB
 * and must be disjoint from the other sector, scratch and immutable intent.
 * NULL represents an unreadable sector. All request decisions are serialized.
 * Keep the same scratch and RAM pair alive while reconciliation is required.
 */
gridpico_result gridpico_pico_pat2_ram_commit(
    uint8_t *a, uint8_t *b, const gridpico_pico_pat2_intent *intent,
    const gridpico_pico_pat2_ram_fault *fault, gridpico_pico_pat2_ram_scratch *scratch);

/* Read-only, against the same two sectors. Only the exact staged record can
 * advance the receipt. Definitive noncommit retains the previous receipt.
 */
gridpico_result gridpico_pico_pat2_ram_reconcile(
    const uint8_t *a, const uint8_t *b, gridpico_pico_pat2_ram_scratch *scratch);
#endif

#endif
