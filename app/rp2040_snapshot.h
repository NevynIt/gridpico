#ifndef GRIDPICO_APP_RP2040_SNAPSHOT_H
#define GRIDPICO_APP_RP2040_SNAPSHOT_H

#include "app/rp2040_router.h"

enum {
    GRIDPICO_PICO_SAVE_PAGE = 256,
    GRIDPICO_PICO_SAVE_SECTOR = 4096,
    GRIDPICO_PICO_SAVE_SECTORS = 2,
};

typedef struct {
    uint8_t data[GRIDPICO_PICO_SAVE_PAGE];
    uint8_t commit[GRIDPICO_PICO_SAVE_PAGE];
    unsigned sector;
} gridpico_pico_save_plan;

typedef struct {
    void *context;
    /* Borrow the mapped sector for foreground reads only. */
    const uint8_t *(*read)(void *context, unsigned sector);
    /* While quiesced, erase the designated sector, then program data and commit.
       This call returns after mapped reads and service are safe again. */
    gridpico_result (*apply)(void *context, const gridpico_pico_save_plan *plan);
} gridpico_pico_save_store;

typedef struct {
    uint32_t generation; /* Zero denotes factory state. */
    uint32_t record_crc;
    uint32_t rejected_generation;
    uint32_t rejected_crc;
    unsigned sector;
    bool storage_issue;
    bool reconcile_required;
} gridpico_pico_save_state;

/* Boot before any attachment or ingress. Boot reads but never writes storage. */
gridpico_result gridpico_pico_boot_saved(gridpico_pico_router *router, const gridpico_pico_save_store *store,
                                      gridpico_pico_save_state *state);
/* Explicit request in the serialized foreground. A failed save leaves live edits intact. */
gridpico_result gridpico_pico_save_patch(const gridpico_pico_router *router, const gridpico_pico_save_store *store,
                                      gridpico_pico_save_state *state, gridpico_pico_save_plan *scratch);

#endif
