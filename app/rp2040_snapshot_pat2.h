#ifndef GRIDPICO_APP_RP2040_SNAPSHOT_PAT2_H
#define GRIDPICO_APP_RP2040_SNAPSHOT_PAT2_H

#include "app/rp2040_snapshot.h"

enum {
    GRIDPICO_PICO_PAT2_IDENTITIES = 8,
    GRIDPICO_PICO_PAT2_DIRECTIONS = 18,
    GRIDPICO_PICO_PAT2_SLOTS = GRIDPICO_PATCH_SOURCE_ROWS + GRIDPICO_PATCH_DESTINATION_COLUMNS,
    GRIDPICO_PICO_PAT2_PAIRS = 81,
    GRIDPICO_PICO_PAT2_DATA_BYTES = 6 * GRIDPICO_PICO_SAVE_PAGE,
    GRIDPICO_PICO_PAT2_RECORD_BYTES = GRIDPICO_PICO_PAT2_DATA_BYTES + GRIDPICO_PICO_SAVE_PAGE,
};

typedef struct {
    uint16_t vid, pid, bcd_device;
    uint8_t interface_number, input_endpoint, output_endpoint;
    uint8_t input_cables, output_cables; /* Masks in host MIDI directions. */
    uint8_t serial_length;
    uint8_t serial[32];
} gridpico_pico_pat2_identity;

typedef struct {
    uint8_t identity_ref; /* One-based. */
    uint8_t cable;
    gridpico_patch_direction direction;
    char label[GRIDPICO_PATCH_NAME_BYTES]; /* NUL-terminated and zero-padded. */
} gridpico_pico_pat2_direction;

typedef struct {
    uint8_t source_ref, destination_ref; /* One-based direction references. */
    uint8_t enabled; /* 0 Off, 1 On. */
} gridpico_pico_pat2_pair;

typedef struct {
    uint32_t generation;
    uint8_t identity_count, direction_count, pair_count;
    gridpico_pico_pat2_identity identities[GRIDPICO_PICO_PAT2_IDENTITIES];
    gridpico_pico_pat2_direction directions[GRIDPICO_PICO_PAT2_DIRECTIONS];
    uint8_t slots[GRIDPICO_PICO_PAT2_SLOTS]; /* Rows then columns, zero means empty. */
    gridpico_pico_pat2_pair pairs[GRIDPICO_PICO_PAT2_PAIRS];
} gridpico_pico_pat2_intent;

#ifndef GRIDPICO_RP2040_SNAPSHOT_READ_ONLY
/* Encode exactly RECORD_BYTES into caller RAM, with the commit page at
 * DATA_BYTES. The record borrows the nonzero generation from intent. Invalid
 * intent, a short span or overlap returns INVALID with the output unchanged.
 * On success any capacity beyond RECORD_BYTES remains untouched.
 */
gridpico_result gridpico_pico_encode_pat2(
    const gridpico_pico_pat2_intent *intent, uint8_t *record, size_t capacity);
#endif

typedef enum {
    GRIDPICO_PICO_PAT2_UNREADABLE,
    GRIDPICO_PICO_PAT2_ERASED,
    GRIDPICO_PICO_PAT2_UNKNOWN,
    GRIDPICO_PICO_PAT2_LEGACY,
    GRIDPICO_PICO_PAT2_INCOMPLETE,
    GRIDPICO_PICO_PAT2_COMMITTED,
} gridpico_pico_pat2_kind;

typedef struct {
    gridpico_pico_pat2_kind kind;
    uint32_t generation; /* Nonzero only for validated PAT2. */
    gridpico_pico_pat1_copy legacy; /* Meaningful only for a legacy copy. */
} gridpico_pico_pat2_copy;

typedef struct {
    gridpico_pico_pat2_copy copies[GRIDPICO_PICO_SAVE_SECTORS];
    bool conflict;
    int selected; /* A=0, B=1, no selection=-1. */
} gridpico_pico_pat2_status;

/* Borrow two full sectors. NULL means unreadable. On UNAVAILABLE only status
 * changes. On INVALID both outputs remain intact. Views and outputs are disjoint.
 */
gridpico_result gridpico_pico_read_pat2(
    const uint8_t *sector_a, const uint8_t *sector_b,
    gridpico_pico_pat2_status *status, gridpico_pico_pat2_intent *out_intent);

/* The provider includes all current USB observations, including unadmitted
 * interfaces. A probe with no usable serial has serial_length zero (and may
 * lack bcdDevice). Such an observation can block a match.
 */
typedef struct {
    gridpico_pico_pat2_identity identity;
    bool revision_known;
} gridpico_pico_pat2_observation;

typedef struct {
    size_t observation_index; /* Zero-based index into observations. */
    uint8_t cable;
    gridpico_patch_direction direction;
    gridpico_connection binding;
    bool eligible; /* Verified current Managed direction after role resolution. */
} gridpico_pico_pat2_binding;

typedef struct {
    gridpico_revision graph_revision, layout_revision;
    const gridpico_pico_pat2_observation *observations;
    size_t observation_count;
    const gridpico_pico_pat2_binding *bindings;
    size_t binding_count;
    /* Total enabled routes, including Pending and other modes. */
    size_t branch_count;
#ifndef GRIDPICO_RP2040_SNAPSHOT_READ_ONLY
    /* Used by the historical host planner with a complete sparse snapshot. */
    const gridpico_local_enabled_route *branches;
    size_t branch_limit;
#endif
} gridpico_pico_pat2_verified_view;

typedef struct {
    uint8_t direction_ref; /* Nonzero reserves this slot even without a binding. */
    gridpico_connection binding; /* Zero if empty or unresolved. */
    char label[GRIDPICO_PATCH_NAME_BYTES];
} gridpico_pico_pat2_slot;

typedef enum {
    GRIDPICO_PICO_PAT2_PAIR_UNRESOLVED,
    GRIDPICO_PICO_PAT2_PAIR_OFF,
    GRIDPICO_PICO_PAT2_PAIR_ON,
    GRIDPICO_PICO_PAT2_PAIR_CAPACITY,
} gridpico_pico_pat2_pair_decision;

typedef struct {
    gridpico_pico_pat2_pair saved;
    gridpico_connection source, destination;
    gridpico_pico_pat2_pair_decision decision;
} gridpico_pico_pat2_planned_pair;

typedef struct {
    uint32_t generation;
    gridpico_revision graph_revision, layout_revision;
    uint8_t direction_count, pair_count;
    /* One entry per saved direction. Zero marks an unresolved reference. */
    gridpico_connection directions[GRIDPICO_PICO_PAT2_DIRECTIONS];
    gridpico_pico_pat2_slot slots[GRIDPICO_PICO_PAT2_SLOTS];
    /* Apply resolved Off first, then accepted On in saved pair order. */
    gridpico_pico_pat2_planned_pair pairs[GRIDPICO_PICO_PAT2_PAIRS];
} gridpico_pico_pat2_plan;

/* Resolve the saved intent against the verified census only. Admission is
 * left to the staged Graph writer. Output is unchanged on failure.
 */
gridpico_result gridpico_pico_resolve_saved_intent(
    const gridpico_pico_pat2_intent *intent, const gridpico_pico_pat2_verified_view *view,
    gridpico_pico_pat2_plan *out_plan);

#ifndef GRIDPICO_RP2040_SNAPSHOT_READ_ONLY
/* Historical host planner with a fixed sparse snapshot and projected capacity. */
gridpico_result gridpico_pico_plan_saved_intent(
    const gridpico_pico_pat2_intent *intent, const gridpico_pico_pat2_verified_view *view,
    gridpico_pico_pat2_plan *out_plan);
#endif

#endif
