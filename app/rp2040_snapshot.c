#include "app/rp2040_snapshot.h"

#include <string.h>

enum { HEADER_BYTES = 20, PAYLOAD_BYTES = 76, COMMIT_BYTES = 12, LAYOUT_IDS = 13 };
_Static_assert(GRIDPICO_PATCH_SOURCE_ROWS + GRIDPICO_PATCH_DESTINATION_COLUMNS == LAYOUT_IDS,
               "PAT1 layout must contain thirteen positions");
_Static_assert(2 * (LAYOUT_IDS + 1) + 4 * GRIDPICO_APC_SESSION_SNAPSHOT_ROUTES == PAYLOAD_BYTES,
               "PAT1 snapshot extent must remain fixed");

typedef enum { UNREADABLE, ERASED, UNKNOWN, INCOMPLETE, VALID } record_kind;

typedef struct {
    const uint8_t *bytes;
    uint32_t generation;
    uint32_t crc;
    record_kind kind;
} record;

static uint32_t read_le(const uint8_t *bytes, unsigned width)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= (uint32_t)bytes[i] << (8u * i);
    return value;
}

static void write_le(uint8_t *bytes, uint32_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i) bytes[i] = (uint8_t)(value >> (8u * i));
}

static uint32_t crc_bytes(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (crc & 1u ? UINT32_C(0xedb88320) : 0u);
        }
    }
    return crc;
}

static uint32_t record_crc(const uint8_t *data)
{
    const uint32_t header = crc_bytes(UINT32_MAX, data, 16);
    return ~crc_bytes(header, data + HEADER_BYTES, PAYLOAD_BYTES);
}

static bool erased(const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) if (bytes[i] != 0xff) return false;
    return true;
}

static bool decode_payload(const uint8_t *bytes, gridpico_apc_session_snapshot *snapshot)
{
    size_t i = 0;
    for (size_t row = 0; row < sizeof(snapshot->layout.rows) / sizeof(snapshot->layout.rows[0]); ++row) {
        snapshot->layout.rows[row] = (gridpico_port_id)read_le(bytes + 2 * i++, 2);
    }
    for (size_t col = 0; col < sizeof(snapshot->layout.columns) / sizeof(snapshot->layout.columns[0]); ++col) {
        snapshot->layout.columns[col] = (gridpico_port_id)read_le(bytes + 2 * i++, 2);
    }
    snapshot->route_count = read_le(bytes + 2 * LAYOUT_IDS, 2);
    if (snapshot->route_count > GRIDPICO_APC_SESSION_SNAPSHOT_ROUTES) return false;

    uint32_t previous_key = 0;
    bytes += 2 * (LAYOUT_IDS + 1);
    for (size_t route = 0; route < GRIDPICO_APC_SESSION_SNAPSHOT_ROUTES; ++route, bytes += 4) {
        const uint32_t source = read_le(bytes, 2), destination = read_le(bytes + 2, 2);
        const uint32_t key = (source << 16) | destination;
        if (route < snapshot->route_count) {
            if (source == 0 || destination == 0 || key <= previous_key) return false;
            previous_key = key;
        } else if (key != 0) return false;
        snapshot->routes[route] = (gridpico_apc_session_route){(gridpico_port_id)source,
                                                            (gridpico_port_id)destination};
    }
    return true;
}

static record inspect(const uint8_t *data)
{
    if (data == NULL) return (record){.kind = UNREADABLE};
    if (erased(data, GRIDPICO_PICO_SAVE_SECTOR)) return (record){.bytes = data, .kind = ERASED};
    if (memcmp(data, "GPSN\x01\x00\x4c\x00", 8) != 0 || memcmp(data + 12, "PAT1", 4) != 0 ||
        !erased(data + HEADER_BYTES + PAYLOAD_BYTES, GRIDPICO_PICO_SAVE_PAGE - HEADER_BYTES - PAYLOAD_BYTES) ||
        !erased(data + GRIDPICO_PICO_SAVE_PAGE + COMMIT_BYTES,
                GRIDPICO_PICO_SAVE_SECTOR - GRIDPICO_PICO_SAVE_PAGE - COMMIT_BYTES)) {
        return (record){.bytes = data, .kind = UNKNOWN};
    }
    const uint8_t *commit = data + GRIDPICO_PICO_SAVE_PAGE;
    const uint32_t generation = read_le(data + 8, 4), crc = read_le(data + 16, 4);
    if (generation == 0 || memcmp(commit, "GPCM", 4) != 0 || read_le(commit + 4, 4) != generation ||
        read_le(commit + 8, 4) != crc || record_crc(data) != crc) {
        return (record){.bytes = data, .kind = INCOMPLETE};
    }
    return (record){.bytes = data, .generation = generation, .crc = crc, .kind = VALID};
}

static bool same_payload(const record *left, const record *right)
{
    return memcmp(left->bytes + HEADER_BYTES, right->bytes + HEADER_BYTES, PAYLOAD_BYTES) == 0;
}

static gridpico_result restore(gridpico_pico_router *router, const record *item)
{
    gridpico_apc_session_snapshot snapshot;
    if (!decode_payload(item->bytes + HEADER_BYTES, &snapshot)) return GRIDPICO_INVALID;
    return gridpico_pico_router_init_snapshot(router, &snapshot);
}

static void reject(gridpico_pico_save_state *state, const record *item)
{
    state->storage_issue = true;
    state->rejected_generation = item->generation;
    state->rejected_crc = item->crc;
}

gridpico_result gridpico_pico_boot_saved(gridpico_pico_router *router, const gridpico_pico_save_store *store,
                                      gridpico_pico_save_state *state)
{
    if (router == NULL || store == NULL || store->read == NULL || state == NULL) return GRIDPICO_INVALID;
    const record records[] = {inspect(store->read(store->context, 0)), inspect(store->read(store->context, 1))};
    *state = (gridpico_pico_save_state){
        .reconcile_required = records[0].kind == UNREADABLE || records[1].kind == UNREADABLE,
    };
    const unsigned first = records[1].generation > records[0].generation ? 1u : 0u;
    for (unsigned i = 0; i < GRIDPICO_PICO_SAVE_SECTORS; ++i) {
        const unsigned sector = first ^ i;
        const record *item = &records[sector], *other = &records[sector ^ 1u];
        if (item->kind == ERASED) continue;
        if (item->kind != VALID || restore(router, item) != GRIDPICO_OK) {
            reject(state, item);
            continue;
        }
        if (i == 0 && other->kind == VALID && item->generation == other->generation && !same_payload(item, other)) {
            state->storage_issue = true;
            if (restore(router, other) == GRIDPICO_OK) break; /* Different valid snapshots at the same generation. */
            reject(state, other);
            /* The semantic check used the same unpublished candidate. Restore it from scratch. */
            const gridpico_result result = restore(router, item);
            if (result != GRIDPICO_OK) return result;
        }
        state->generation = item->generation;
        state->record_crc = item->crc;
        state->sector = sector;
        state->storage_issue |= other->kind != VALID && other->kind != ERASED;
        return GRIDPICO_OK;
    }
    return gridpico_pico_router_init(router);
}

static void encode_plan(gridpico_pico_save_plan *plan, uint32_t generation,
                        const gridpico_apc_session_snapshot *snapshot)
{
    memset(plan->data, 0xff, sizeof(plan->data));
    memcpy(plan->data, "GPSN\x01\x00\x4c\x00", 8);
    write_le(plan->data + 8, generation, 4);
    memcpy(plan->data + 12, "PAT1", 4);
    uint8_t *payload = plan->data + HEADER_BYTES;
    memset(payload, 0, PAYLOAD_BYTES);
    size_t i = 0;
    for (size_t row = 0; row < sizeof(snapshot->layout.rows) / sizeof(snapshot->layout.rows[0]); ++row) {
        write_le(payload + 2 * i++, snapshot->layout.rows[row], 2);
    }
    for (size_t col = 0; col < sizeof(snapshot->layout.columns) / sizeof(snapshot->layout.columns[0]); ++col) {
        write_le(payload + 2 * i++, snapshot->layout.columns[col], 2);
    }
    write_le(payload + 2 * i, (uint32_t)snapshot->route_count, 2);
    payload += 2 * (i + 1);
    for (size_t route = 0; route < snapshot->route_count; ++route, payload += 4) {
        write_le(payload, snapshot->routes[route].source, 2);
        write_le(payload + 2, snapshot->routes[route].destination, 2);
    }
    const uint32_t crc = record_crc(plan->data);
    write_le(plan->data + 16, crc, 4);
    memset(plan->commit, 0xff, sizeof(plan->commit));
    memcpy(plan->commit, "GPCM", 4);
    write_le(plan->commit + 4, generation, 4);
    write_le(plan->commit + 8, crc, 4);
}

static bool musical_idle(const gridpico_pico_router *router)
{
    if (router->graph.occurrence_count != 0 || router->audition.destination != 0 ||
        router->notes[0].count != 0 || router->notes[1].count != 0) return false;
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    if (router->keys.mode != GRIDPICO_APC_CHOOSER_PATCHBAY || router->keys.apc_queue.count != 0 ||
        router->keys.next_mode != GRIDPICO_APC_CHOOSER_NO_REQUEST || router->keys.chooser.open) return false;
    for (size_t i = 0; i < GRIDPICO_PICO_KEYS_HOLDS; ++i) {
        if (router->keys.holds[i].count != 0) return false;
    }
#endif
    const gridpico_patch_inventory *inventory = &router->graph.inventory;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const gridpico_patch_port *port = &inventory->ports[i];
        if (port->direction == GRIDPICO_PATCH_OUTPUT &&
            !gridpico_patch_graph_destination_idle(&router->graph, port->id)) return false;
    }
    return true;
}

gridpico_result gridpico_pico_save_patch(const gridpico_pico_router *router, const gridpico_pico_save_store *store,
                                      gridpico_pico_save_state *state, gridpico_pico_save_plan *scratch)
{
    if (router == NULL || store == NULL || store->read == NULL || store->apply == NULL || state == NULL ||
        scratch == NULL || state->sector >= GRIDPICO_PICO_SAVE_SECTORS) return GRIDPICO_INVALID;
    if (state->reconcile_required || !musical_idle(router)) return GRIDPICO_UNAVAILABLE;
    const record records[] = {inspect(store->read(store->context, 0)), inspect(store->read(store->context, 1))};
    if (records[0].kind == UNREADABLE || records[1].kind == UNREADABLE) {
        state->storage_issue = state->reconcile_required = true;
        return GRIDPICO_UNAVAILABLE;
    }
    const uint32_t highest = records[0].generation > records[1].generation ? records[0].generation : records[1].generation;
    if (highest == UINT32_MAX) return GRIDPICO_CAPACITY;

    unsigned target = 0;
    if (state->generation != 0) {
        const record *current = &records[state->sector];
        if (current->kind != VALID || current->generation != state->generation || current->crc != state->record_crc) {
            return GRIDPICO_UNAVAILABLE;
        }
        target = state->sector ^ 1u;
        const record *alternate = &records[target];
        /* Only erase a same/newer different copy if boot verified that exact record was unusable. */
        if (alternate->kind == VALID && alternate->generation >= current->generation &&
            (alternate->generation != current->generation || !same_payload(current, alternate)) &&
            (alternate->generation != state->rejected_generation || alternate->crc != state->rejected_crc)) {
            state->storage_issue = state->reconcile_required = true;
            return GRIDPICO_UNAVAILABLE;
        }
    } else {
        if (highest != 0) return GRIDPICO_UNAVAILABLE;
        if (records[0].kind == UNKNOWN) target = 1;
    }
    if (records[target].kind == UNKNOWN) return GRIDPICO_UNAVAILABLE;

    gridpico_apc_session_snapshot snapshot;
    const gridpico_result exported = gridpico_patch_session_export_apc_snapshot(&router->session, &snapshot);
    if (exported != GRIDPICO_OK) return exported;
    encode_plan(scratch, highest + 1, &snapshot);
    scratch->sector = target;
    const gridpico_result applied = store->apply(store->context, scratch);
    const record durable = inspect(store->read(store->context, target));
    if (durable.kind == UNREADABLE) {
        state->storage_issue = state->reconcile_required = true;
        return GRIDPICO_UNAVAILABLE;
    }
    if (durable.kind != VALID || durable.generation != highest + 1 ||
        memcmp(durable.bytes, scratch->data, sizeof(scratch->data)) != 0 ||
        memcmp(durable.bytes + GRIDPICO_PICO_SAVE_PAGE, scratch->commit, sizeof(scratch->commit)) != 0) {
        state->storage_issue |= applied == GRIDPICO_OK || durable.kind != records[target].kind;
        return GRIDPICO_UNAVAILABLE;
    }
    *state = (gridpico_pico_save_state){.generation = durable.generation, .sector = target,
                                       .record_crc = durable.crc,
                                       .storage_issue = records[target ^ 1u].kind == UNKNOWN};
    return GRIDPICO_OK;
}
