#include "app/rp2040_snapshot_pat2.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    SECTOR = 4096, HEADER = 20, DATA = 1536, OLD_DATA = 256,
    IDS = 24, DIRECTIONS = IDS + 2 * 44, SLOTS = DIRECTIONS + 4 * 36,
    PAIRS = SLOTS + 13, END = PAIRS + 4 * 4,
};

static void put(uint8_t *bytes, uint32_t value, unsigned size)
{
    for (unsigned i = 0; i < size; ++i) {
        bytes[i] = (uint8_t)value;
        value >>= 8;
    }
}

static uint32_t crc(uint32_t previous, const uint8_t *bytes, size_t size)
{
    static const uint32_t table[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };
    uint32_t result = ~previous;
    for (size_t i = 0; i < size; ++i) {
        result ^= bytes[i];
        result = (result >> 4) ^ table[result & 15u];
        result = (result >> 4) ^ table[result & 15u];
    }
    return ~result;
}

static void commit(uint8_t *bytes, size_t end, uint32_t generation, bool legacy)
{
    const size_t commit_offset = legacy ? OLD_DATA : DATA;
    memcpy(bytes, "GPSN", 4);
    put(bytes + 4, legacy ? 1 : 2, 2);
    put(bytes + 6, (uint32_t)(end - HEADER), 2);
    put(bytes + 8, generation, 4);
    memcpy(bytes + 12, legacy ? "PAT1" : "PAT2", 4);
    const uint32_t check = crc(crc(0, bytes, 16), bytes + HEADER, end - HEADER);
    put(bytes + 16, check, 4);
    memcpy(bytes + commit_offset, "GPCM", 4);
    put(bytes + commit_offset + 4, generation, 4);
    put(bytes + commit_offset + 8, check, 4);
}

static void identity(uint8_t *bytes, uint8_t serial)
{
    static const uint8_t shape[] = {0x35, 0x12, 0x39, 0x01, 0x12, 0x01, 1, 0x81, 1, 3, 3};
    memcpy(bytes, shape, sizeof(shape));
    bytes[11] = 1;
    bytes[12] = serial;
}

static void direction(uint8_t *bytes, uint8_t id, uint8_t cable, uint8_t axis)
{
    bytes[0] = id;
    bytes[1] = cable;
    bytes[2] = axis;
    memcpy(bytes + 4, "Equal name", sizeof("Equal name"));
}

static void small_copy(uint8_t *bytes, uint32_t generation)
{
    memset(bytes, 0xff, SECTOR);
    memset(bytes + HEADER, 0, END - HEADER);
    memcpy(bytes + HEADER, (const uint8_t[]){2, 4, 4, 0}, 4);
    identity(bytes + IDS, 'A');
    identity(bytes + IDS + 44, 'B');
    direction(bytes + DIRECTIONS + 0 * 36, 1, 1, 0);
    direction(bytes + DIRECTIONS + 1 * 36, 2, 0, 1);
    direction(bytes + DIRECTIONS + 2 * 36, 2, 1, 0);
    direction(bytes + DIRECTIONS + 3 * 36, 1, 0, 1);
    bytes[SLOTS + 0] = 1;
    bytes[SLOTS + 3] = 3;
    bytes[SLOTS + 5] = 2;
    bytes[SLOTS + 12] = 4;
    const uint8_t pairs[][4] = {{1, 2, 1, 0}, {1, 4, 0, 0}, {3, 2, 0, 0}, {3, 4, 1, 0}};
    memcpy(bytes + PAIRS, pairs, sizeof(pairs));
    commit(bytes, END, generation, false);
}

static void check_unavailable(const uint8_t *a, const uint8_t *b,
                              gridpico_pico_pat2_status *status)
{
    gridpico_pico_pat2_intent intent;
    unsigned char sentinel[sizeof(intent)];
    memset(sentinel, 0xa5, sizeof(sentinel));
    memcpy(&intent, sentinel, sizeof(intent));
    assert(gridpico_pico_read_pat2(a, b, status, &intent) == GRIDPICO_UNAVAILABLE);
    assert(status->selected == -1 && memcmp(&intent, sentinel, sizeof(intent)) == 0);
}

static void selection(void)
{
    uint8_t a[SECTOR], b[SECTOR], copy[SECTOR];
    gridpico_pico_pat2_status status;
    gridpico_pico_pat2_intent intent;

    small_copy(a, 4);
    small_copy(b, 8);
    memcpy(copy, b, sizeof(copy));
    assert(gridpico_pico_read_pat2(a, b, &status, &intent) == GRIDPICO_OK);
    assert(status.selected == 1 && !status.conflict && intent.generation == 8);
    assert(intent.identities[0].vid == 0x1235 && intent.identities[1].pid == 0x0139 &&
           intent.identities[1].bcd_device == 0x0112 && intent.directions[0].cable == 1 &&
           intent.identities[1].serial[0] == 'B' && intent.pairs[1].enabled == 0);
    assert(memcmp(b, copy, sizeof(b)) == 0);
    assert(gridpico_pico_read_pat2(b, a, &status, &intent) == GRIDPICO_OK && status.selected == 0);

    b[DATA + 2] = 0xff; /* Torn commit. */
    assert(gridpico_pico_read_pat2(a, b, &status, &intent) == GRIDPICO_OK);
    assert(status.selected == 0 && intent.generation == 4 &&
           status.copies[1].kind == GRIDPICO_PICO_PAT2_INCOMPLETE);
    memcpy(b, copy, sizeof(b));
    b[IDS + 12] ^= 1; /* CRC mismatch with structurally valid bytes. */
    assert(gridpico_pico_read_pat2(a, b, &status, &intent) == GRIDPICO_OK && status.selected == 0);

    memcpy(b, a, sizeof(b));
    assert(gridpico_pico_read_pat2(a, b, &status, &intent) == GRIDPICO_OK &&
           status.selected == 0 && !status.conflict);
    b[PAIRS + 2] = 0;
    commit(b, END, 4, false);
    check_unavailable(a, b, &status);
    assert(status.conflict && status.copies[0].generation == 4 && status.copies[1].generation == 4);

    small_copy(b, 9);
    memcpy(b + PAIRS, a + PAIRS + 12, 4);
    memcpy(b + PAIRS + 12, a + PAIRS, 4);
    commit(b, END, 9, false);
    assert(gridpico_pico_read_pat2(a, b, &status, &intent) == GRIDPICO_OK);
    assert(intent.pairs[0].source_ref == 3 && intent.pairs[3].source_ref == 1);

    memset(b, 0xff, sizeof(b));
    memset(b + HEADER, 0, 76);
    commit(b, HEADER + 76, 55, true);
    memcpy(copy, b, sizeof(copy));
    assert(gridpico_pico_read_pat2(a, b, &status, &intent) == GRIDPICO_OK);
    assert(status.selected == 0 && status.copies[1].kind == GRIDPICO_PICO_PAT2_LEGACY &&
           status.copies[1].legacy.kind == GRIDPICO_PICO_PAT1_COMMITTED &&
           status.copies[1].legacy.generation == 55 && memcmp(b, copy, sizeof(b)) == 0);
    check_unavailable(NULL, b, &status);
    assert(status.copies[0].kind == GRIDPICO_PICO_PAT2_UNREADABLE);
    b[OLD_DATA] = 0xff;
    check_unavailable(NULL, b, &status);
    assert(status.copies[1].legacy.kind == GRIDPICO_PICO_PAT1_INCOMPLETE);

    memset(a, 0xff, sizeof(a));
    put(b + 4, 3, 2);
    memcpy(copy, b, sizeof(copy));
    check_unavailable(a, b, &status);
    assert(status.copies[0].kind == GRIDPICO_PICO_PAT2_ERASED &&
           status.copies[1].kind == GRIDPICO_PICO_PAT2_UNKNOWN && memcmp(b, copy, sizeof(b)) == 0);

    unsigned char before[sizeof(status)];
    memcpy(before, &status, sizeof(before));
    assert(gridpico_pico_read_pat2(a, b, &status, NULL) == GRIDPICO_INVALID);
    assert(memcmp(before, &status, sizeof(before)) == 0);
    assert(gridpico_pico_read_pat2(a, b, NULL, &intent) == GRIDPICO_INVALID);
    enum { ONE_DIRECTION = IDS + 44, ONE_SLOT = ONE_DIRECTION + 36, ONE_END = ONE_SLOT + 13 };
    memset(a, 0xff, sizeof(a));
    memset(a + HEADER, 0, ONE_END - HEADER);
    a[HEADER] = a[HEADER + 1] = 1;
    identity(a + IDS, 'I');
    a[IDS + 8] = a[IDS + 10] = 0;
    a[IDS + 9] = 0x80;
    direction(a + ONE_DIRECTION, 1, 7, 0);
    a[ONE_SLOT] = 1;
    commit(a, ONE_END, 1, false);
    assert(gridpico_pico_read_pat2(a, NULL, &status, &intent) == GRIDPICO_OK);
    assert(intent.identities[0].input_cables == 0x80 && intent.directions[0].cable == 7 &&
           intent.identities[0].output_endpoint == 0);

    printf("PASS selection, legacy, unknown, torn, CRC, equal generation and output guards\n");
}

static void malformed(void)
{
    static const struct {size_t offset; uint8_t value;} mutations[] = {
        {HEADER, 9}, {HEADER + 1, 19}, {HEADER + 2, 82}, {HEADER + 3, 1},
        {IDS + 44 + 12, 'A'}, /* Duplicate identity, same shape. */
        {IDS + 11, 0}, {IDS + 11, 32}, {IDS + 12, 0}, {IDS + 13, 1},
        {IDS + 7, 1}, {IDS + 8, 0x81}, {IDS + 9, 1},
        {DIRECTIONS, 0}, {DIRECTIONS, 3}, {DIRECTIONS + 1, 8}, {DIRECTIONS + 2, 2},
        {DIRECTIONS + 3, 1}, {DIRECTIONS + 72, 1}, /* Duplicate direction. */
        {DIRECTIONS + 35, 'X'}, /* Hidden bytes after label terminator. */
        {SLOTS, 2}, {SLOTS + 1, 1}, {SLOTS + 5, 9},
        {PAIRS, 0}, {PAIRS + 1, 3}, {PAIRS + 2, 2}, {PAIRS + 3, 1},
        {PAIRS + 5, 2}, /* Conflicting duplicate pair even with opposite state. */
    };
    uint8_t bytes[SECTOR];
    gridpico_pico_pat2_status status;
    for (size_t i = 0; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
        small_copy(bytes, 1);
        bytes[mutations[i].offset] = mutations[i].value;
        commit(bytes, END, 1, false);
        check_unavailable(NULL, bytes, &status);
        assert(status.copies[1].kind == GRIDPICO_PICO_PAT2_INCOMPLETE);
    }
    small_copy(bytes, 1);
    memset(bytes + DIRECTIONS + 4, 'X', 32);
    commit(bytes, END, 1, false);
    check_unavailable(NULL, bytes, &status);

    small_copy(bytes, 1);
    bytes[END] = 0xff;
    commit(bytes, END + 1, 1, false);
    check_unavailable(NULL, bytes, &status);

    small_copy(bytes, 1);
    bytes[END] = 0;
    check_unavailable(NULL, bytes, &status);
    bytes[END] = 0xff;
    bytes[DATA + 256] = 0;
    check_unavailable(NULL, bytes, &status);
    small_copy(bytes, 0);
    check_unavailable(NULL, bytes, &status);
    small_copy(bytes, 3);
    put(bytes + 6, UINT16_MAX, 2);
    check_unavailable(NULL, bytes, &status);
    printf("PASS malformed sizes, references, keys, shapes, padding and fields\n");
}

static void encoded_roundtrip(const uint8_t *expected, const gridpico_pico_pat2_intent *intent,
                              gridpico_pico_pat2_intent *decoded)
{
    uint8_t guarded[SECTOR + 2];
    memset(guarded, 0xa5, sizeof(guarded));
    uint8_t *sector = guarded + 1;
    assert(gridpico_pico_encode_pat2(intent, sector, SECTOR) == GRIDPICO_OK);
    const size_t extent = GRIDPICO_PICO_PAT2_RECORD_BYTES;
    assert(guarded[0] == 0xa5 && guarded[SECTOR + 1] == 0xa5);
    assert(memcmp(sector, expected, extent) == 0);
    for (size_t i = extent; i < SECTOR; ++i) assert(sector[i] == 0xa5);
    memset(sector + extent, 0xff, SECTOR - extent); /* In-memory sector for the reader. */

    gridpico_pico_pat2_status status;
    assert(gridpico_pico_read_pat2(sector, NULL, &status, decoded) == GRIDPICO_OK);
    assert(status.selected == 0 && !status.conflict && decoded->generation == intent->generation);
    assert(decoded->identity_count == intent->identity_count &&
           decoded->direction_count == intent->direction_count && decoded->pair_count == intent->pair_count);
    assert(memcmp(decoded->slots, intent->slots, sizeof(decoded->slots)) == 0);
    for (size_t i = 0; i < intent->pair_count; ++i) {
        assert(decoded->pairs[i].source_ref == intent->pairs[i].source_ref &&
               decoded->pairs[i].destination_ref == intent->pairs[i].destination_ref &&
               decoded->pairs[i].enabled == intent->pairs[i].enabled);
    }
}

static void reject_encoding(const gridpico_pico_pat2_intent *intent, size_t capacity)
{
    uint8_t output[GRIDPICO_PICO_PAT2_RECORD_BYTES];
    uint8_t before[sizeof(output)];
    memset(output, 0xa5, sizeof(output));
    memcpy(before, output, sizeof(before));
    assert(gridpico_pico_encode_pat2(intent, output, capacity) == GRIDPICO_INVALID);
    assert(memcmp(before, output, sizeof(output)) == 0);
}

static void encoding(void)
{
    uint8_t expected[SECTOR];
    const uint32_t generation = UINT32_C(0x12345678);
    small_copy(expected, generation);
    expected[IDS + 12] = 'Z';
    expected[IDS + 44 + 12] = 'A';
    const uint8_t ordered[][4] = {{3, 2, 0, 0}, {1, 4, 0, 0}, {3, 4, 1, 0}, {1, 2, 1, 0}};
    memcpy(expected + PAIRS, ordered, sizeof(ordered));
    commit(expected, END, generation, false);

    gridpico_pico_pat2_status status;
    gridpico_pico_pat2_intent intent, decoded;
    assert(gridpico_pico_read_pat2(expected, NULL, &status, &intent) == GRIDPICO_OK);
    encoded_roundtrip(expected, &intent, &decoded);
    assert(decoded.identities[0].serial[0] == 'Z' && decoded.identities[1].serial[0] == 'A' &&
           decoded.directions[1].identity_ref == 2 && decoded.directions[3].identity_ref == 1 &&
           decoded.pairs[0].source_ref == 3 && decoded.pairs[0].enabled == 0 &&
           decoded.pairs[3].source_ref == 1 && decoded.pairs[3].enabled == 1);

    const size_t extent = GRIDPICO_PICO_PAT2_RECORD_BYTES;
    uint8_t exact[GRIDPICO_PICO_PAT2_RECORD_BYTES];
    assert(gridpico_pico_encode_pat2(&intent, exact, sizeof(exact)) == GRIDPICO_OK);
    assert(memcmp(exact, expected, sizeof(exact)) == 0);
    assert(gridpico_pico_encode_pat2(&intent, NULL, extent) == GRIDPICO_INVALID);
    reject_encoding(&intent, extent - 1);
    reject_encoding(NULL, extent);
    intent.generation = 0;
    reject_encoding(&intent, extent);
    intent.generation = generation;
    intent.identity_count = GRIDPICO_PICO_PAT2_IDENTITIES + 1;
    reject_encoding(&intent, extent);
    intent.identity_count = 2;
    intent.slots[0] = 2; /* OUT in a row. */
    reject_encoding(&intent, extent);
    intent.slots[0] = 1;
    intent.directions[0].label[31] = 'X';
    reject_encoding(&intent, extent);
    intent.directions[0].label[31] = '\0';
    intent.pairs[1] = intent.pairs[0];
    reject_encoding(&intent, extent);

    union {
        gridpico_pico_pat2_intent intent;
        uint8_t bytes[GRIDPICO_PICO_PAT2_RECORD_BYTES];
    } overlap;
    memset(&overlap, 0xa5, sizeof(overlap));
    overlap.intent = decoded;
    memcpy(exact, overlap.bytes, sizeof(exact));
    assert(gridpico_pico_encode_pat2(&overlap.intent, overlap.bytes, sizeof(overlap.bytes)) == GRIDPICO_INVALID);
    assert(memcmp(exact, overlap.bytes, sizeof(exact)) == 0);
    puts("PASS PAT2 encode/decode exact On/Off and stored order, padding, generation, bounds and untouched output");
}

static void maximum(void)
{
    enum { MAX_END = 1361 };
    uint8_t bytes[SECTOR];
    memset(bytes, 0xff, sizeof(bytes));
    memset(bytes + HEADER, 0, MAX_END - HEADER);
    memcpy(bytes + HEADER, (const uint8_t[]){8, 18, 81, 0}, 4);
    uint8_t *p = bytes + IDS;
    for (uint8_t i = 0; i < 8; ++i, p += 44) identity(p, (uint8_t)('A' + i));
    for (uint8_t axis = 0; axis < 2; ++axis) {
        for (uint8_t id = 1; id <= 8; ++id, p += 36) direction(p, id, 0, axis);
        direction(p, 1, 1, axis);
        p += 36;
    }
    /* Longest identity serial and label. */
    bytes[IDS + 11] = 31;
    memset(bytes + IDS + 12, 'Q', 31);
    memset(bytes + IDS + 8 * 44 + 4, 'N', 31);
    for (uint8_t i = 1; i <= 5; ++i) *p++ = i;
    for (uint8_t i = 10; i < 18; ++i) *p++ = i;
    for (uint8_t source = 1; source <= 9; ++source) {
        for (uint8_t destination = 10; destination <= 18; ++destination, p += 4) {
            p[0] = source;
            p[1] = destination;
            p[2] = (uint8_t)((source + destination) % 2);
        }
    }
    assert(p == bytes + MAX_END);
    commit(bytes, MAX_END, UINT32_MAX, false);

    gridpico_pico_pat2_status status;
    gridpico_pico_pat2_intent intent;
    assert(gridpico_pico_read_pat2(bytes, NULL, &status, &intent) == GRIDPICO_OK);
    gridpico_pico_pat2_intent decoded;
    encoded_roundtrip(bytes, &intent, &decoded);
    puts("PASS PAT2 encoder maximal 1361-byte record in 1792-byte data/commit envelope");
    assert(decoded.generation == UINT32_MAX && decoded.identity_count == 8 &&
           decoded.direction_count == 18 && decoded.pair_count == 81 &&
           decoded.identities[0].serial_length == 31 && strlen(decoded.directions[0].label) == 31 &&
           intent.pairs[80].source_ref == 9 && intent.pairs[80].destination_ref == 18);

    const gridpico_pico_pat2_verified_view empty = {.graph_revision = 65, .layout_revision = 77};
    gridpico_pico_pat2_plan plan;
    assert(gridpico_pico_plan_saved_intent(&intent, &empty, &plan) == GRIDPICO_OK);
    for (size_t i = 0; i < 81; ++i) {
        assert(plan.pairs[i].decision == GRIDPICO_PICO_PAT2_PAIR_UNRESOLVED);
        assert(plan.pairs[i].saved.enabled == intent.pairs[i].enabled);
    }
    for (size_t i = 0; i < 13; ++i) {
        assert(plan.slots[i].direction_ref != 0 && plan.slots[i].binding.endpoint == 0);
    }
    printf("PASS maximum 1361-byte record, 1792-byte envelope, 81 unresolved pairs\n");
}

static void check_bad_plan(const gridpico_pico_pat2_intent *intent,
                           const gridpico_pico_pat2_verified_view *view)
{
    gridpico_pico_pat2_plan plan;
    unsigned char bytes[sizeof(plan)];
    memset(bytes, 0xa5, sizeof(bytes));
    memcpy(&plan, bytes, sizeof(plan));
    assert(gridpico_pico_plan_saved_intent(intent, view, &plan) == GRIDPICO_INVALID);
    assert(memcmp(bytes, &plan, sizeof(plan)) == 0);
}

static void restore_plan(void)
{
    uint8_t bytes[SECTOR];
    small_copy(bytes, 18);
    gridpico_pico_pat2_status status;
    gridpico_pico_pat2_intent intent;
    assert(gridpico_pico_read_pat2(NULL, bytes, &status, &intent) == GRIDPICO_OK);
    const gridpico_pico_pat2_observation a = {intent.identities[0], true};
    const gridpico_pico_pat2_observation b = {intent.identities[1], true};
    gridpico_pico_pat2_observation lookalike = b;
    lookalike.identity.serial[0] = 'C';
    gridpico_pico_pat2_observation observations[] = {b, a, lookalike};
    gridpico_pico_pat2_binding bindings[] = {
        {1, 0, GRIDPICO_PATCH_OUTPUT, {6, 17}, true},
        {0, 1, GRIDPICO_PATCH_INPUT, {59, 82}, true},
        {1, 1, GRIDPICO_PATCH_INPUT, {47, 15}, true},
        {0, 0, GRIDPICO_PATCH_OUTPUT, {12, 80}, true},
        {2, 0, GRIDPICO_PATCH_OUTPUT, {7, 3}, true},
    };
    const gridpico_local_enabled_route branches[] = {
        {{47, 6, GRIDPICO_LOCAL_MANAGED}, GRIDPICO_PATCH_ROUTE_ACTIVE},
        {{2, 3, GRIDPICO_LOCAL_MANAGED}, GRIDPICO_PATCH_ROUTE_PENDING},
    };
    gridpico_pico_pat2_verified_view view = {
        .graph_revision = 0x100000001, .layout_revision = 99,
        .observations = observations, .observation_count = 3,
        .bindings = bindings, .binding_count = 5,
        .branches = branches, .branch_count = 2, .branch_limit = 2,
    };

    gridpico_pico_pat2_plan plan;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(plan.generation == 18 && plan.graph_revision == 0x100000001 &&
           plan.layout_revision == 99 && plan.pair_count == 4);
    assert(plan.slots[0].binding.endpoint == 47 && plan.slots[0].binding.generation == 15 &&
           plan.slots[3].binding.endpoint == 59 && plan.slots[5].binding.endpoint == 12 &&
           plan.slots[12].binding.endpoint == 6 && plan.slots[1].direction_ref == 0);
    assert(strcmp(plan.slots[0].label, plan.slots[3].label) == 0);
    assert(plan.pairs[0].decision == GRIDPICO_PICO_PAT2_PAIR_ON &&
           plan.pairs[0].source.endpoint == 47 && plan.pairs[0].destination.endpoint == 12 &&
           plan.pairs[0].destination.generation == 80);
    assert(plan.pairs[1].decision == GRIDPICO_PICO_PAT2_PAIR_OFF &&
           plan.pairs[1].destination.endpoint == 6 &&
           plan.pairs[2].decision == GRIDPICO_PICO_PAT2_PAIR_OFF &&
           plan.pairs[2].source.endpoint == 59 &&
           plan.pairs[3].decision == GRIDPICO_PICO_PAT2_PAIR_CAPACITY);

    const gridpico_pico_pat2_pair first_on = intent.pairs[0];
    intent.pairs[0] = intent.pairs[3];
    intent.pairs[3] = first_on;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(plan.pairs[0].source.endpoint == 59 && plan.pairs[0].decision == GRIDPICO_PICO_PAT2_PAIR_ON &&
           plan.pairs[3].source.endpoint == 47 && plan.pairs[3].decision == GRIDPICO_PICO_PAT2_PAIR_CAPACITY);
    intent.pairs[3] = intent.pairs[0];
    intent.pairs[0] = first_on;

    /* An unadmitted equal serial blocks both directions on the admitted unit. */
    observations[2] = b;
    view.binding_count = 4;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(plan.slots[5].direction_ref == 2 && plan.slots[5].binding.endpoint == 0 &&
           strcmp(plan.slots[5].label, "Equal name") == 0);
    assert(plan.slots[0].binding.endpoint == 47 && plan.slots[1].direction_ref == 0 &&
           plan.pairs[0].decision == GRIDPICO_PICO_PAT2_PAIR_UNRESOLVED &&
           plan.pairs[1].decision == GRIDPICO_PICO_PAT2_PAIR_OFF &&
           plan.pairs[2].decision == GRIDPICO_PICO_PAT2_PAIR_UNRESOLVED &&
           plan.pairs[2].saved.enabled == 0);
    /* A fixture fill respects an unresolved reservation and can still use the
     * next empty column for unrelated eligible inventory.
     */
    gridpico_port_id columns[GRIDPICO_PATCH_DESTINATION_COLUMNS] = {0};
    for (size_t col = 0; col < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++col) {
        const gridpico_pico_pat2_slot *slot = &plan.slots[GRIDPICO_PATCH_SOURCE_ROWS + col];
        if (slot->direction_ref == 0) {
            columns[col] = 7;
            break;
        }
    }
    assert(columns[0] == 0 && columns[1] == 7);
    /* Same-shape incomplete observation, even with bcdDevice missing. */
    observations[2].identity.bcd_device = 0;
    observations[2].identity.serial_length = 0;
    memset(observations[2].identity.serial, 0, sizeof(observations[2].identity.serial));
    observations[2].revision_known = false;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(plan.directions[0].endpoint == 0 && plan.directions[1].endpoint == 0);
    /* Numeric IDs can stay unchanged while an interface becomes the wrong serial. */
    observations[0] = observations[2] = lookalike;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(plan.slots[5].binding.endpoint == 0 && plan.slots[3].binding.endpoint == 0);
    observations[0] = b;
    ++observations[0].identity.interface_number;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(plan.slots[5].binding.endpoint == 0 && plan.slots[3].binding.endpoint == 0);
    observations[0] = b;
    bindings[3].eligible = false;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(plan.slots[5].binding.endpoint == 0 && plan.slots[3].binding.endpoint == 59);

    /* A later coherent projection resolves the reserved slot at the new owner. */
    bindings[3].eligible = true;
    bindings[3].binding = (gridpico_connection){120, 91};
    const gridpico_revision old_layout = plan.layout_revision, old_graph = plan.graph_revision;
    ++view.layout_revision;
    ++view.graph_revision;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(old_layout != view.layout_revision && old_graph != view.graph_revision);
    assert(plan.layout_revision == 100 && plan.graph_revision == 0x100000002 &&
           plan.slots[5].binding.endpoint == 120 && plan.slots[5].binding.generation == 91 &&
           plan.pairs[0].destination.endpoint == 120 && plan.pairs[2].destination.generation == 91);

    const gridpico_local_enabled_route existing = {
        {47, 120, GRIDPICO_LOCAL_MANAGED}, GRIDPICO_PATCH_ROUTE_ACTIVE,
    };
    view.branches = &existing;
    view.branch_count = view.branch_limit = 1;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(plan.pairs[0].decision == GRIDPICO_PICO_PAT2_PAIR_ON &&
           plan.pairs[3].decision == GRIDPICO_PICO_PAT2_PAIR_CAPACITY);

    gridpico_local_enabled_route full[GRIDPICO_PATCH_MAX_BRANCHES];
    full[0] = existing;
    for (size_t i = 1; i < GRIDPICO_PATCH_MAX_BRANCHES; ++i) {
        full[i] = (gridpico_local_enabled_route){
            {(gridpico_port_id)(130 + i), 6, GRIDPICO_LOCAL_MANAGED}, GRIDPICO_PATCH_ROUTE_PENDING,
        };
    }
    view.branches = full;
    view.branch_count = view.branch_limit = GRIDPICO_PATCH_MAX_BRANCHES;
    assert(gridpico_pico_plan_saved_intent(&intent, &view, &plan) == GRIDPICO_OK);
    assert(plan.pairs[0].decision == GRIDPICO_PICO_PAT2_PAIR_ON &&
           plan.pairs[3].decision == GRIDPICO_PICO_PAT2_PAIR_CAPACITY);

    view.branch_count = GRIDPICO_PATCH_MAX_BRANCHES + 1;
    check_bad_plan(&intent, &view);
    view.branch_count = view.branch_limit = 1;
    view.branches = &existing;
    view.graph_revision = 0;
    check_bad_plan(&intent, &view);
    view.graph_revision = 1;
    view.layout_revision = 0;
    check_bad_plan(&intent, &view);
    view.layout_revision = 1;
    bindings[3].binding.generation = 0;
    check_bad_plan(&intent, &view);
    bindings[3].binding = bindings[2].binding;
    check_bad_plan(&intent, &view);
    bindings[3].binding = (gridpico_connection){120, 91};
    intent.directions[0].label[30] = 'X';
    check_bad_plan(&intent, &view);
    intent.directions[0].label[30] = '\0';
    intent.pairs[0].destination_ref = intent.pairs[1].destination_ref;
    check_bad_plan(&intent, &view);
    check_bad_plan(NULL, &view);
    check_bad_plan(&intent, NULL);
    printf("PASS identity ambiguity, reservations, generations, Off-before-On and capacity\n");
}

int main(void)
{
    assert(crc(0, (const uint8_t *)"123456789", 9) == UINT32_C(0xcbf43926));
    selection();
    malformed();
    encoding();
    maximum();
    restore_plan();
    return 0;
}
