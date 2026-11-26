#include "app/rp2040_snapshot_pat2.h"

#include <string.h>

enum {
    HEADER_BYTES = 20, COUNTS_BYTES = 4, IDENTITY_BYTES = 44, DIRECTION_BYTES = 36,
    PAIR_BYTES = 4, COMMIT_BYTES = 12,
    MAX_PAYLOAD = COUNTS_BYTES + IDENTITY_BYTES * GRIDPICO_PICO_PAT2_IDENTITIES +
        DIRECTION_BYTES * GRIDPICO_PICO_PAT2_DIRECTIONS + GRIDPICO_PICO_PAT2_SLOTS +
        PAIR_BYTES * GRIDPICO_PICO_PAT2_PAIRS,
};

_Static_assert(HEADER_BYTES + MAX_PAYLOAD == 1361, "PAT2 maximum data extent");
_Static_assert(HEADER_BYTES + MAX_PAYLOAD <= GRIDPICO_PICO_PAT2_DATA_BYTES,
               "PAT2 must fit its data pages");
_Static_assert(GRIDPICO_PICO_PAT2_RECORD_BYTES == 1792 &&
               GRIDPICO_PICO_PAT2_RECORD_BYTES <= (int)GRIDPICO_PICO_SAVE_SECTOR,
               "PAT2 commit must fit a separate page");

static uint32_t read_le(const uint8_t *bytes, unsigned width)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= (uint32_t)bytes[i] << (8u * i);
    return value;
}

static void write_le(uint8_t *bytes, uint32_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i) {
        bytes[i] = (uint8_t)value;
        value >>= 8;
    }
}

static uint32_t crc_bytes(uint32_t crc, const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (crc & 1u ? UINT32_C(0xedb88320) : 0u);
        }
    }
    return crc;
}

static uint32_t record_crc(const uint8_t *bytes, size_t payload_size)
{
    return ~crc_bytes(crc_bytes(UINT32_MAX, bytes, 16), bytes + HEADER_BYTES, payload_size);
}

static size_t payload_extent(uint8_t identities, uint8_t directions, uint8_t pairs)
{
    return COUNTS_BYTES + IDENTITY_BYTES * (size_t)identities +
           DIRECTION_BYTES * (size_t)directions + GRIDPICO_PICO_PAT2_SLOTS +
           PAIR_BYTES * (size_t)pairs;
}

static bool all_bytes(const uint8_t *bytes, size_t count, uint8_t value)
{
    for (size_t i = 0; i < count; ++i) if (bytes[i] != value) return false;
    return true;
}

static bool valid_endpoint(uint8_t endpoint, uint8_t cables, uint8_t direction_bit)
{
    if (cables == 0) return endpoint == 0;
    return (endpoint & 0xf0u) == direction_bit && (endpoint & 0x0fu) != 0;
}

static bool valid_identity(const gridpico_pico_pat2_identity *id)
{
    return (id->input_cables != 0 || id->output_cables != 0) &&
           valid_endpoint(id->input_endpoint, id->input_cables, 0x80) &&
           valid_endpoint(id->output_endpoint, id->output_cables, 0) &&
           id->serial_length < sizeof(id->serial) &&
           memchr(id->serial, 0, id->serial_length) == NULL &&
           all_bytes(id->serial + id->serial_length, sizeof(id->serial) - id->serial_length, 0);
}

static bool same_shape(const gridpico_pico_pat2_identity *a,
                       const gridpico_pico_pat2_identity *b, bool revision_known)
{
    return a->vid == b->vid && a->pid == b->pid &&
           (!revision_known || a->bcd_device == b->bcd_device) &&
           a->interface_number == b->interface_number && a->input_endpoint == b->input_endpoint &&
           a->output_endpoint == b->output_endpoint && a->input_cables == b->input_cables &&
           a->output_cables == b->output_cables;
}

static bool same_serial(const gridpico_pico_pat2_identity *a,
                        const gridpico_pico_pat2_identity *b)
{
    return a->serial_length == b->serial_length &&
           memcmp(a->serial, b->serial, a->serial_length) == 0;
}

static bool has_direction(const gridpico_pico_pat2_identity *id, uint8_t cable,
                          gridpico_patch_direction direction)
{
    if (cable >= 8 || (direction != GRIDPICO_PATCH_INPUT && direction != GRIDPICO_PATCH_OUTPUT)) {
        return false;
    }
    const uint8_t mask = direction == GRIDPICO_PATCH_INPUT ? id->input_cables : id->output_cables;
    return (mask & (1u << cable)) != 0;
}

static bool valid_label(const char label[GRIDPICO_PATCH_NAME_BYTES])
{
    const char *end = memchr(label, '\0', GRIDPICO_PATCH_NAME_BYTES);
    return end != NULL && all_bytes((const uint8_t *)end,
                                   GRIDPICO_PATCH_NAME_BYTES - (size_t)(end - label), 0);
}

static bool valid_ref(const gridpico_pico_pat2_intent *intent, uint8_t ref,
                      gridpico_patch_direction direction)
{
    return ref != 0 && ref <= intent->direction_count &&
           intent->directions[ref - 1u].direction == direction;
}

static bool valid_intent(const gridpico_pico_pat2_intent *intent)
{
    if (intent->generation == 0 || intent->identity_count > GRIDPICO_PICO_PAT2_IDENTITIES ||
        intent->direction_count > GRIDPICO_PICO_PAT2_DIRECTIONS ||
        intent->pair_count > GRIDPICO_PICO_PAT2_PAIRS) return false;

    for (size_t i = 0; i < intent->identity_count; ++i) {
        const gridpico_pico_pat2_identity *id = &intent->identities[i];
        if (!valid_identity(id) || id->serial_length == 0) return false;
        for (size_t j = 0; j < i; ++j) {
            const gridpico_pico_pat2_identity *previous = &intent->identities[j];
            if (same_shape(id, previous, true) && same_serial(id, previous)) return false;
        }
    }

    for (size_t i = 0; i < intent->direction_count; ++i) {
        const gridpico_pico_pat2_direction *dir = &intent->directions[i];
        if (dir->identity_ref == 0 || dir->identity_ref > intent->identity_count ||
            !has_direction(&intent->identities[dir->identity_ref - 1u], dir->cable, dir->direction) ||
            !valid_label(dir->label)) return false;
        for (size_t j = 0; j < i; ++j) {
            const gridpico_pico_pat2_direction *previous = &intent->directions[j];
            if (dir->identity_ref == previous->identity_ref && dir->cable == previous->cable &&
                dir->direction == previous->direction) return false;
        }
    }

    uint32_t assigned = 0;
    for (size_t i = 0; i < GRIDPICO_PICO_PAT2_SLOTS; ++i) {
        const uint8_t ref = intent->slots[i];
        if (ref == 0) continue;
        const gridpico_patch_direction axis =
            i < GRIDPICO_PATCH_SOURCE_ROWS ? GRIDPICO_PATCH_INPUT : GRIDPICO_PATCH_OUTPUT;
        if (!valid_ref(intent, ref, axis) || (assigned & (UINT32_C(1) << ref))) return false;
        assigned |= UINT32_C(1) << ref;
    }

    uint32_t pairs_by_source[GRIDPICO_PICO_PAT2_DIRECTIONS] = {0};
    for (size_t i = 0; i < intent->pair_count; ++i) {
        const gridpico_pico_pat2_pair *pair = &intent->pairs[i];
        if (!valid_ref(intent, pair->source_ref, GRIDPICO_PATCH_INPUT) ||
            !valid_ref(intent, pair->destination_ref, GRIDPICO_PATCH_OUTPUT) ||
            pair->enabled > 1) return false;
        uint32_t *seen = &pairs_by_source[pair->source_ref - 1u];
        const uint32_t destination = UINT32_C(1) << pair->destination_ref;
        if (*seen & destination) return false;
        *seen |= destination;
    }
    return true;
}

static bool overlaps_intent(const gridpico_pico_pat2_intent *intent, const uint8_t *record)
{
    const uintptr_t input = (uintptr_t)intent, output = (uintptr_t)record;
    return output >= input ? output - input < sizeof(*intent)
                           : input - output < GRIDPICO_PICO_PAT2_RECORD_BYTES;
}

gridpico_result gridpico_pico_encode_pat2(
    const gridpico_pico_pat2_intent *intent, uint8_t *record, size_t capacity)
{
    if (intent == NULL || record == NULL || capacity < GRIDPICO_PICO_PAT2_RECORD_BYTES ||
        overlaps_intent(intent, record) || !valid_intent(intent)) return GRIDPICO_INVALID;

    memset(record, 0xff, GRIDPICO_PICO_PAT2_RECORD_BYTES);
    const size_t payload_size = payload_extent(intent->identity_count, intent->direction_count,
                                               intent->pair_count);
    memcpy(record, "GPSN", 4);
    write_le(record + 4, 2, 2);
    write_le(record + 6, (uint32_t)payload_size, 2);
    write_le(record + 8, intent->generation, 4);
    memcpy(record + 12, "PAT2", 4);

    uint8_t *bytes = record + HEADER_BYTES;
    bytes[0] = intent->identity_count;
    bytes[1] = intent->direction_count;
    bytes[2] = intent->pair_count;
    bytes[3] = 0;
    bytes += COUNTS_BYTES;
    for (size_t i = 0; i < intent->identity_count; ++i, bytes += IDENTITY_BYTES) {
        const gridpico_pico_pat2_identity *id = &intent->identities[i];
        write_le(bytes, id->vid, 2);
        write_le(bytes + 2, id->pid, 2);
        write_le(bytes + 4, id->bcd_device, 2);
        bytes[6] = id->interface_number;
        bytes[7] = id->input_endpoint;
        bytes[8] = id->output_endpoint;
        bytes[9] = id->input_cables;
        bytes[10] = id->output_cables;
        bytes[11] = id->serial_length;
        memcpy(bytes + 12, id->serial, sizeof(id->serial));
    }
    for (size_t i = 0; i < intent->direction_count; ++i, bytes += DIRECTION_BYTES) {
        const gridpico_pico_pat2_direction *dir = &intent->directions[i];
        bytes[0] = dir->identity_ref;
        bytes[1] = dir->cable;
        bytes[2] = dir->direction == GRIDPICO_PATCH_INPUT ? 0 : 1;
        bytes[3] = 0;
        memcpy(bytes + 4, dir->label, sizeof(dir->label));
    }
    memcpy(bytes, intent->slots, sizeof(intent->slots));
    bytes += sizeof(intent->slots);
    for (size_t i = 0; i < intent->pair_count; ++i, bytes += PAIR_BYTES) {
        const gridpico_pico_pat2_pair *pair = &intent->pairs[i];
        bytes[0] = pair->source_ref;
        bytes[1] = pair->destination_ref;
        bytes[2] = pair->enabled;
        bytes[3] = 0;
    }

    const uint32_t crc = record_crc(record, payload_size);
    write_le(record + 16, crc, 4);
    uint8_t *commit = record + GRIDPICO_PICO_PAT2_DATA_BYTES;
    memcpy(commit, "GPCM", 4);
    write_le(commit + 4, intent->generation, 4);
    write_le(commit + 8, crc, 4);
    return GRIDPICO_OK;
}

static bool decode(const uint8_t *bytes, size_t size, uint32_t generation,
                   gridpico_pico_pat2_intent *intent)
{
    if (size < COUNTS_BYTES) return false;
    const uint8_t identities = bytes[0], directions = bytes[1], pairs = bytes[2];
    if (identities > GRIDPICO_PICO_PAT2_IDENTITIES || directions > GRIDPICO_PICO_PAT2_DIRECTIONS ||
        pairs > GRIDPICO_PICO_PAT2_PAIRS || bytes[3] != 0 ||
        size != payload_extent(identities, directions, pairs)) return false;

    *intent = (gridpico_pico_pat2_intent){
        .generation = generation, .identity_count = identities,
        .direction_count = directions, .pair_count = pairs,
    };
    bytes += COUNTS_BYTES;
    for (size_t i = 0; i < identities; ++i, bytes += IDENTITY_BYTES) {
        gridpico_pico_pat2_identity *id = &intent->identities[i];
        *id = (gridpico_pico_pat2_identity){
            .vid = (uint16_t)read_le(bytes, 2), .pid = (uint16_t)read_le(bytes + 2, 2),
            .bcd_device = (uint16_t)read_le(bytes + 4, 2), .interface_number = bytes[6],
            .input_endpoint = bytes[7], .output_endpoint = bytes[8],
            .input_cables = bytes[9], .output_cables = bytes[10], .serial_length = bytes[11],
        };
        memcpy(id->serial, bytes + 12, sizeof(id->serial));
    }
    for (size_t i = 0; i < directions; ++i, bytes += DIRECTION_BYTES) {
        if (bytes[2] > 1 || bytes[3] != 0) return false;
        gridpico_pico_pat2_direction *dir = &intent->directions[i];
        dir->identity_ref = bytes[0];
        dir->cable = bytes[1];
        dir->direction = bytes[2] == 0 ? GRIDPICO_PATCH_INPUT : GRIDPICO_PATCH_OUTPUT;
        memcpy(dir->label, bytes + 4, sizeof(dir->label));
    }
    memcpy(intent->slots, bytes, sizeof(intent->slots));
    bytes += sizeof(intent->slots);
    for (size_t i = 0; i < pairs; ++i, bytes += PAIR_BYTES) {
        if (bytes[3] != 0) return false;
        intent->pairs[i] = (gridpico_pico_pat2_pair){bytes[0], bytes[1], bytes[2]};
    }
    return valid_intent(intent);
}

static gridpico_pico_pat2_copy inspect(const uint8_t *bytes,
                                      gridpico_pico_pat2_intent *scratch)
{
    if (bytes == NULL) return (gridpico_pico_pat2_copy){.kind = GRIDPICO_PICO_PAT2_UNREADABLE};
    if (all_bytes(bytes, GRIDPICO_PICO_SAVE_SECTOR, 0xff)) {
        return (gridpico_pico_pat2_copy){.kind = GRIDPICO_PICO_PAT2_ERASED};
    }
    if (memcmp(bytes, "GPSN", 4) != 0) {
        return (gridpico_pico_pat2_copy){.kind = GRIDPICO_PICO_PAT2_UNKNOWN};
    }
    const uint32_t version = read_le(bytes + 4, 2);
    if (version == 1 && memcmp(bytes + 12, "PAT1", 4) == 0) {
        const uint8_t *const sectors[] = {bytes, NULL};
        gridpico_pico_pat1_status legacy;
        (void)gridpico_pico_inspect_pat1(sectors, &legacy);
        return (gridpico_pico_pat2_copy){.kind = GRIDPICO_PICO_PAT2_LEGACY,
                                        .legacy = legacy.copies[0]};
    }
    if (version != 2 || memcmp(bytes + 12, "PAT2", 4) != 0) {
        return (gridpico_pico_pat2_copy){.kind = GRIDPICO_PICO_PAT2_UNKNOWN};
    }

    const size_t payload_size = read_le(bytes + 6, 2);
    const uint8_t *commit = bytes + GRIDPICO_PICO_PAT2_DATA_BYTES;
    const uint32_t generation = read_le(bytes + 8, 4), crc = read_le(bytes + 16, 4);
    if (payload_size <= MAX_PAYLOAD && generation != 0 &&
        all_bytes(bytes + HEADER_BYTES + payload_size,
                  GRIDPICO_PICO_PAT2_DATA_BYTES - HEADER_BYTES - payload_size, 0xff) &&
        memcmp(commit, "GPCM", 4) == 0 && read_le(commit + 4, 4) == generation &&
        read_le(commit + 8, 4) == crc &&
        all_bytes(commit + COMMIT_BYTES,
                  GRIDPICO_PICO_SAVE_SECTOR - GRIDPICO_PICO_PAT2_DATA_BYTES - COMMIT_BYTES, 0xff) &&
        record_crc(bytes, payload_size) == crc &&
        decode(bytes + HEADER_BYTES, payload_size, generation, scratch)) {
        return (gridpico_pico_pat2_copy){.kind = GRIDPICO_PICO_PAT2_COMMITTED, .generation = generation};
    }
    return (gridpico_pico_pat2_copy){.kind = GRIDPICO_PICO_PAT2_INCOMPLETE};
}

gridpico_result gridpico_pico_read_pat2(
    const uint8_t *sector_a, const uint8_t *sector_b,
    gridpico_pico_pat2_status *status, gridpico_pico_pat2_intent *out_intent)
{
    if (status == NULL || out_intent == NULL) return GRIDPICO_INVALID;

    gridpico_pico_pat2_intent scratch;
    gridpico_pico_pat2_status report = {.selected = -1};
    report.copies[0] = inspect(sector_a, &scratch);
    report.copies[1] = inspect(sector_b, &scratch);
    const uint32_t a = report.copies[0].generation, b = report.copies[1].generation;
    if (a != 0 && a == b) {
        const size_t size_a = read_le(sector_a + 6, 2), size_b = read_le(sector_b + 6, 2);
        report.conflict = size_a != size_b ||
            memcmp(sector_a + HEADER_BYTES, sector_b + HEADER_BYTES, size_a) != 0;
    }
    if ((!a && !b) || report.conflict) {
        *status = report;
        return GRIDPICO_UNAVAILABLE;
    }

    report.selected = b > a ? 1 : 0;
    const uint8_t *selected = report.selected == 0 ? sector_a : sector_b;
    (void)decode(selected + HEADER_BYTES, read_le(selected + 6, 2),
                 report.copies[report.selected].generation, out_intent);
    *status = report;
    return GRIDPICO_OK;
}

static bool valid_view(const gridpico_pico_pat2_verified_view *view)
{
    if (view->graph_revision == 0 || view->layout_revision == 0 ||
        view->observation_count > GRIDPICO_PICO_PAT2_IDENTITIES ||
        (view->observation_count != 0 && view->observations == NULL) ||
        view->binding_count > GRIDPICO_PICO_PAT2_DIRECTIONS ||
        (view->binding_count != 0 && view->bindings == NULL) ||
        view->branch_limit > GRIDPICO_PATCH_MAX_BRANCHES || view->branch_count > view->branch_limit ||
        (view->branch_count != 0 && view->branches == NULL)) return false;

    for (size_t i = 0; i < view->observation_count; ++i) {
        const gridpico_pico_pat2_observation *item = &view->observations[i];
        if (!valid_identity(&item->identity) ||
            (!item->revision_known && item->identity.serial_length != 0)) return false;
    }

    for (size_t i = 0; i < view->binding_count; ++i) {
        const gridpico_pico_pat2_binding *item = &view->bindings[i];
        if (item->observation_index >= view->observation_count ||
            item->binding.endpoint == 0 || item->binding.generation == 0 ||
            !has_direction(&view->observations[item->observation_index].identity,
                           item->cable, item->direction)) return false;
        for (size_t j = 0; j < i; ++j) {
            const gridpico_pico_pat2_binding *previous = &view->bindings[j];
            if (item->binding.endpoint == previous->binding.endpoint ||
                (item->observation_index == previous->observation_index &&
                 item->cable == previous->cable && item->direction == previous->direction)) {
                return false;
            }
        }
    }

    for (size_t i = 0; i < view->branch_count; ++i) {
        const gridpico_local_enabled_route *branch = &view->branches[i];
        const gridpico_local_route *route = &branch->route;
        if (route->source == 0 || route->destination == 0 || route->source == route->destination ||
            (route->mode != GRIDPICO_LOCAL_MANAGED && route->mode != GRIDPICO_LOCAL_RAW) ||
            (branch->state != GRIDPICO_PATCH_ROUTE_ACTIVE &&
             branch->state != GRIDPICO_PATCH_ROUTE_PENDING)) return false;
        for (size_t j = 0; j < i; ++j) {
            const gridpico_local_route *previous = &view->branches[j].route;
            if (route->source == previous->source && route->destination == previous->destination &&
                route->mode == previous->mode) return false;
        }
        for (size_t j = 0; j < view->binding_count; ++j) {
            const gridpico_pico_pat2_binding *item = &view->bindings[j];
            if ((route->source == item->binding.endpoint && item->direction != GRIDPICO_PATCH_INPUT) ||
                (route->destination == item->binding.endpoint && item->direction != GRIDPICO_PATCH_OUTPUT)) {
                return false;
            }
        }
    }
    return true;
}

static size_t match_identity(const gridpico_pico_pat2_verified_view *view,
                             const gridpico_pico_pat2_identity *id)
{
    size_t matched = view->observation_count;
    for (size_t i = 0; i < view->observation_count; ++i) {
        const gridpico_pico_pat2_observation *item = &view->observations[i];
        if (!same_shape(id, &item->identity, item->revision_known)) continue;
        if (item->identity.serial_length == 0) return view->observation_count;
        if (same_serial(id, &item->identity)) {
            if (matched != view->observation_count) return view->observation_count;
            matched = i;
        }
    }
    return matched;
}

static gridpico_connection match_direction(const gridpico_pico_pat2_verified_view *view,
                                          size_t observation_index,
                                          const gridpico_pico_pat2_direction *dir)
{
    for (size_t i = 0; i < view->binding_count; ++i) {
        const gridpico_pico_pat2_binding *item = &view->bindings[i];
        if (item->eligible && item->observation_index == observation_index &&
            item->cable == dir->cable && item->direction == dir->direction) return item->binding;
    }
    return (gridpico_connection){0};
}

static bool branch_enabled(const gridpico_pico_pat2_verified_view *view,
                           const gridpico_pico_pat2_planned_pair *pair)
{
    for (size_t i = 0; i < view->branch_count; ++i) {
        const gridpico_local_route *route = &view->branches[i].route;
        if (route->mode == GRIDPICO_LOCAL_MANAGED && route->source == pair->source.endpoint &&
            route->destination == pair->destination.endpoint) return true;
    }
    return false;
}

gridpico_result gridpico_pico_plan_saved_intent(
    const gridpico_pico_pat2_intent *intent, const gridpico_pico_pat2_verified_view *view,
    gridpico_pico_pat2_plan *out_plan)
{
    if (intent == NULL || view == NULL || out_plan == NULL ||
        !valid_intent(intent) || !valid_view(view)) return GRIDPICO_INVALID;

    *out_plan = (gridpico_pico_pat2_plan){
        .generation = intent->generation,
        .graph_revision = view->graph_revision, .layout_revision = view->layout_revision,
        .direction_count = intent->direction_count, .pair_count = intent->pair_count,
    };
    size_t matches[GRIDPICO_PICO_PAT2_IDENTITIES];
    for (size_t i = 0; i < intent->identity_count; ++i) {
        matches[i] = match_identity(view, &intent->identities[i]);
    }
    for (size_t i = 0; i < intent->direction_count; ++i) {
        const gridpico_pico_pat2_direction *dir = &intent->directions[i];
        out_plan->directions[i] = match_direction(view, matches[dir->identity_ref - 1u], dir);
    }
    for (size_t i = 0; i < GRIDPICO_PICO_PAT2_SLOTS; ++i) {
        const uint8_t ref = intent->slots[i];
        if (ref == 0) continue;
        gridpico_pico_pat2_slot *slot = &out_plan->slots[i];
        slot->direction_ref = ref;
        slot->binding = out_plan->directions[ref - 1u];
        memcpy(slot->label, intent->directions[ref - 1u].label, sizeof(slot->label));
    }

    /* Account for resolved Off before assigning space to On, even if the
     * corresponding Off appears later in the record.
     */
    size_t occupied = view->branch_count;
    for (size_t i = 0; i < intent->pair_count; ++i) {
        gridpico_pico_pat2_planned_pair *pair = &out_plan->pairs[i];
        pair->saved = intent->pairs[i];
        pair->source = out_plan->directions[pair->saved.source_ref - 1u];
        pair->destination = out_plan->directions[pair->saved.destination_ref - 1u];
        if (pair->source.endpoint && pair->destination.endpoint && !pair->saved.enabled) {
            pair->decision = GRIDPICO_PICO_PAT2_PAIR_OFF;
            if (branch_enabled(view, pair)) --occupied;
        }
    }
    for (size_t i = 0; i < intent->pair_count; ++i) {
        gridpico_pico_pat2_planned_pair *pair = &out_plan->pairs[i];
        if (!pair->source.endpoint || !pair->destination.endpoint || !pair->saved.enabled) continue;
        if (branch_enabled(view, pair)) {
            pair->decision = GRIDPICO_PICO_PAT2_PAIR_ON;
        } else if (occupied < view->branch_limit) {
            pair->decision = GRIDPICO_PICO_PAT2_PAIR_ON;
            ++occupied;
        } else {
            pair->decision = GRIDPICO_PICO_PAT2_PAIR_CAPACITY;
        }
    }
    return GRIDPICO_OK;
}
