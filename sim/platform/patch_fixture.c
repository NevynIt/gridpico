#include "patch_fixture.h"

enum { PATCH_FIXTURE_OUTPUTS = 2, PATCH_MONITOR_FIXTURE_OUTPUTS = 3 };

static bool valid_state(gridpico_patch_connection_state state)
{
    return state == GRIDPICO_PATCH_DISCONNECTED || state == GRIDPICO_PATCH_CONNECTED ||
           state == GRIDPICO_PATCH_AMBIGUOUS;
}

static bool valid_note(gridpico_note note)
{
    return note.channel_index <= 15 && note.key <= 127 && note.velocity <= 127 &&
           (note.kind == GRIDPICO_NOTE_OFF ||
            (note.kind == GRIDPICO_NOTE_ON && note.velocity > 0));
}

static size_t find_port(const gridpico_patch_fixture *fixture, gridpico_port_id id)
{
    for (size_t i = 0; i < fixture->port_count; i++) {
        if (fixture->ports[i].status.connection.endpoint == id) {
            return i;
        }
    }
    return fixture->port_count;
}

static gridpico_result init_fixture(gridpico_patch_fixture *fixture,
                                    const gridpico_patch_inventory *inventory,
                                    size_t device_limit, size_t port_limit,
                                    size_t destination_limit)
{
    if (fixture == NULL || inventory == NULL) {
        return GRIDPICO_INVALID;
    }
    if (inventory->device_count > device_limit || inventory->port_count > port_limit) {
        return GRIDPICO_CAPACITY;
    }

    gridpico_patch_fixture initial = {0};
    size_t destinations = 0;
    for (size_t i = 0; i < inventory->port_count; i++) {
        const gridpico_patch_port *port = &inventory->ports[i];
        if (port->id == 0 || find_port(&initial, port->id) != initial.port_count ||
            !valid_state(port->state) ||
            (port->direction != GRIDPICO_PATCH_INPUT && port->direction != GRIDPICO_PATCH_OUTPUT)) {
            return GRIDPICO_INVALID;
        }
        const bool destination = port->direction == GRIDPICO_PATCH_OUTPUT && port->patch_eligible;
        destinations += destination;
        if (destinations > destination_limit) {
            return GRIDPICO_CAPACITY;
        }
        initial.ports[initial.port_count++] = (gridpico_patch_fixture_port){
            .status = {.connection = {.endpoint = port->id, .generation = 1}, .state = port->state},
            .destination = destination,
        };
    }
    *fixture = initial;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_fixture_init(gridpico_patch_fixture *fixture,
                                          const gridpico_patch_inventory *inventory)
{
    return init_fixture(fixture, inventory, GRIDPICO_PATCH_DEVICES, GRIDPICO_PATCH_PORTS,
                        PATCH_FIXTURE_OUTPUTS);
}

gridpico_result gridpico_patch_fixture_init_monitor(gridpico_patch_fixture *fixture,
                                                  const gridpico_patch_inventory *inventory)
{
    return init_fixture(fixture, inventory, GRIDPICO_PATCH_MONITOR_DEVICES,
                        GRIDPICO_PATCH_MONITOR_PORTS, PATCH_MONITOR_FIXTURE_OUTPUTS);
}

gridpico_result gridpico_patch_fixture_update_port(void *context,
                                                 const gridpico_patch_port_update *update)
{
    gridpico_patch_fixture *fixture = context;
    if (fixture == NULL || update == NULL || update->connection.generation == 0 ||
        !valid_state(update->state)) {
        return GRIDPICO_INVALID;
    }
    const size_t index = find_port(fixture, update->connection.endpoint);
    if (index == fixture->port_count) {
        return GRIDPICO_INVALID;
    }

    gridpico_patch_port_update *current = &fixture->ports[index].status;
    const uint32_t generation = update->connection.generation;
    const uint32_t prior = current->connection.generation;
    if (update->state == GRIDPICO_PATCH_CONNECTED && current->state != GRIDPICO_PATCH_CONNECTED) {
        if (generation <= prior) {
            return GRIDPICO_UNAVAILABLE;
        }
    } else if (generation != prior) {
        return GRIDPICO_UNAVAILABLE;
    }

    *current = *update;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_fixture_deliver(void *context,
                                             const gridpico_note_delivery *delivery)
{
    gridpico_patch_fixture *fixture = context;
    if (fixture == NULL || delivery == NULL || !valid_note(delivery->note)) {
        return GRIDPICO_INVALID;
    }

    const size_t port_index = find_port(fixture, delivery->destination.endpoint);
    if (port_index == fixture->port_count) {
        return GRIDPICO_UNAVAILABLE;
    }
    const gridpico_patch_fixture_port *port = &fixture->ports[port_index];
    if (!port->destination || port->status.state != GRIDPICO_PATCH_CONNECTED ||
        port->status.connection.generation != delivery->destination.generation) {
        return GRIDPICO_UNAVAILABLE;
    }

    const size_t index = (fixture->capture_start + fixture->capture_count) % GRIDPICO_PATCH_CAPTURE_ROWS;
    fixture->capture[index] = *delivery;
    if (fixture->capture_count == GRIDPICO_PATCH_CAPTURE_ROWS) {
        fixture->capture_start = (fixture->capture_start + 1) % GRIDPICO_PATCH_CAPTURE_ROWS;
    } else {
        fixture->capture_count++;
    }
    return GRIDPICO_OK;
}

size_t gridpico_patch_fixture_read_capture(void *context,
                                          gridpico_note_delivery *rows,
                                          size_t capacity)
{
    const gridpico_patch_fixture *fixture = context;
    if (fixture == NULL || rows == NULL) {
        return 0;
    }

    const size_t count = capacity < fixture->capture_count ? capacity : fixture->capture_count;
    for (size_t i = 0; i < count; i++) {
        rows[i] = fixture->capture[(fixture->capture_start + i) % GRIDPICO_PATCH_CAPTURE_ROWS];
    }
    return count;
}
