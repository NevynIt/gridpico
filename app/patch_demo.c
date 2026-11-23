#include "app/patch_inventory.h"
#include "core/midi/patch_graph.h"
#include "core/session/patch_session.h"
#include "sim/platform/patch_fixture.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    gridpico_patch_graph graph;
    gridpico_patch_session session;
    gridpico_patch_fixture sim;
    gridpico_note_delivery verified[GRIDPICO_PATCH_CAPTURE_ROWS];
    size_t verified_count;
    gridpico_revision revision;
    bool has_snapshot;
} patch_demo;

static bool expect_result(const char *operation, gridpico_result actual, gridpico_result expected)
{
    if (actual == expected) {
        return true;
    }
    fprintf(stderr, "%s returned %d, expected %d\n", operation, (int)actual, (int)expected);
    return false;
}

static bool expect_inventory(const gridpico_patch_inventory *actual, gridpico_port_id disconnected)
{
    if (actual->device_count != patch_inventory.device_count ||
        actual->port_count != patch_inventory.port_count) {
        fputs("Unexpected inventory size\n", stderr);
        return false;
    }
    for (size_t i = 0; i < actual->device_count; ++i) {
        const gridpico_patch_device *device = &actual->devices[i];
        const gridpico_patch_device *expected = &patch_inventory.devices[i];
        if (device->id != expected->id || device->state != expected->state ||
            strncmp(device->name, expected->name, sizeof(device->name)) != 0) {
            fprintf(stderr, "Unexpected device at inventory position %zu\n", i + 1);
            return false;
        }
    }
    for (size_t i = 0; i < actual->port_count; ++i) {
        const gridpico_patch_port *port = &actual->ports[i];
        const gridpico_patch_port *expected = &patch_inventory.ports[i];
        const gridpico_patch_connection_state state = expected->id == disconnected
            ? GRIDPICO_PATCH_DISCONNECTED : GRIDPICO_PATCH_CONNECTED;
        if (port->id != expected->id || port->device != expected->device ||
            port->direction != expected->direction || port->state != state ||
            port->patch_eligible != expected->patch_eligible ||
            strncmp(port->name, expected->name, sizeof(port->name)) != 0) {
            fprintf(stderr, "Unexpected port at inventory position %zu\n", i + 1);
            return false;
        }
    }
    return true;
}

static bool expect_snapshot(patch_demo *demo, gridpico_patch_route_state a,
                            gridpico_patch_route_state b, gridpico_port_id disconnected,
                            unsigned revisions)
{
    const gridpico_port_id expected_rows[GRIDPICO_PATCH_SOURCE_ROWS] = {KEYBED};
    const gridpico_port_id expected_columns[GRIDPICO_PATCH_DESTINATION_COLUMNS] = {MIDI_A, MIDI_B};
    const gridpico_patch_route_state routes[GRIDPICO_PATCH_SOURCE_ROWS]
                                           [GRIDPICO_PATCH_DESTINATION_COLUMNS] = {{a, b}};
    const char *const route_names[] = {
        [GRIDPICO_PATCH_ROUTE_OFF] = "off",
        [GRIDPICO_PATCH_ROUTE_PENDING] = "pending",
        [GRIDPICO_PATCH_ROUTE_ACTIVE] = "active",
    };
    gridpico_patch_snapshot snapshot;
    if (!expect_result("Read Patch snapshot",
                       gridpico_patch_session_read_snapshot(&demo->session, &snapshot),
                       GRIDPICO_OK) ||
        !expect_inventory(&snapshot.inventory, disconnected)) {
        return false;
    }
    if (memcmp(snapshot.source_rows, expected_rows, sizeof(expected_rows)) != 0 ||
        memcmp(snapshot.destination_columns, expected_columns, sizeof(expected_columns)) != 0) {
        fputs("Patch row or column assignment changed\n", stderr);
        return false;
    }
    for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
        for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
            if (snapshot.routes[row][column] != routes[row][column]) {
                fprintf(stderr, "Unexpected Patch route state at row %zu, column %zu\n",
                        row + 1, column + 1);
                return false;
            }
        }
    }
    if (demo->has_snapshot && snapshot.revision != demo->revision + revisions) {
        fprintf(stderr, "Expected Patch revision %" PRIu64 ", got %" PRIu64 "\n",
                demo->revision + revisions, snapshot.revision);
        return false;
    }
    if (!demo->has_snapshot) {
        for (size_t i = 0; i < snapshot.inventory.device_count; ++i) {
            const gridpico_patch_device *device = &snapshot.inventory.devices[i];
            printf("  Device %u  %s\n", (unsigned)device->id, device->name);
        }
        for (size_t i = 0; i < snapshot.inventory.port_count; ++i) {
            const gridpico_patch_port *port = &snapshot.inventory.ports[i];
            printf("  Port %u  device %u  %s  %s  %s\n",
                   (unsigned)port->id, (unsigned)port->device, port->name,
                   port->direction == GRIDPICO_PATCH_INPUT ? "input" : "output",
                   port->patch_eligible ? "Patch eligible" : "inventory only");
        }
        printf("  Source row 1 = %u, destination columns 1 and 2 = %u and %u\n",
               (unsigned)snapshot.source_rows[0], (unsigned)snapshot.destination_columns[0],
               (unsigned)snapshot.destination_columns[1]);
    }
    demo->has_snapshot = true;
    demo->revision = snapshot.revision;
    printf("  Revision %" PRIu64 "  MIDI A %s  MIDI B %s\n",
           snapshot.revision, route_names[a], route_names[b]);
    return true;
}

static bool same_connection(gridpico_connection a, gridpico_connection b)
{
    return a.endpoint == b.endpoint && a.generation == b.generation;
}

static bool same_note(gridpico_note a, gridpico_note b)
{
    return a.kind == b.kind && a.channel_index == b.channel_index &&
           a.key == b.key && a.velocity == b.velocity;
}

static bool expect_capture(patch_demo *demo, gridpico_ingress_note note,
                           const gridpico_connection *destinations, size_t destination_count)
{
    gridpico_note_delivery rows[GRIDPICO_PATCH_CAPTURE_ROWS];
    const size_t count = gridpico_patch_fixture_read_capture(&demo->sim, rows,
                                                            GRIDPICO_PATCH_CAPTURE_ROWS);
    if (count != demo->verified_count + destination_count) {
        fprintf(stderr, "Expected %zu capture rows, got %zu\n",
                demo->verified_count + destination_count, count);
        return false;
    }
    for (size_t i = 0; i < demo->verified_count; ++i) {
        const gridpico_note_delivery *before = &demo->verified[i];
        if (!same_connection(rows[i].source, before->source) ||
            !same_connection(rows[i].destination, before->destination) ||
            !same_note(rows[i].note, before->note)) {
            fprintf(stderr, "Earlier capture row %zu changed\n", i + 1);
            return false;
        }
    }
    bool matched[GRIDPICO_PATCH_BRANCHES] = {false};
    for (size_t i = demo->verified_count; i < count; ++i) {
        const gridpico_note_delivery *delivery = &rows[i];
        size_t destination = 0;
        while (destination < destination_count &&
               (matched[destination] ||
                !same_connection(delivery->destination, destinations[destination]))) {
            ++destination;
        }
        if (destination == destination_count ||
            !same_connection(delivery->source, note.source) ||
            !same_note(delivery->note, note.note)) {
            fprintf(stderr, "Unexpected delivery at capture row %zu\n", i + 1);
            return false;
        }
        matched[destination] = true;
        demo->verified[i] = *delivery;
        printf("  %s ch=%u key=%u velocity=%u  %u/%" PRIu32 " -> %u/%" PRIu32 "\n",
               delivery->note.kind == GRIDPICO_NOTE_ON ? "Note On " : "Note Off",
               (unsigned)delivery->note.channel_index + 1u, (unsigned)delivery->note.key,
               (unsigned)delivery->note.velocity, (unsigned)delivery->source.endpoint,
               delivery->source.generation, (unsigned)delivery->destination.endpoint,
               delivery->destination.generation);
    }
    demo->verified_count = count;
    return true;
}

static bool expect_silence(patch_demo *demo)
{
    return expect_capture(demo, (gridpico_ingress_note){0}, NULL, 0);
}

static gridpico_ingress_note keybed_note(uint32_t generation, gridpico_note_kind kind,
                                       uint8_t key, uint8_t velocity)
{
    return (gridpico_ingress_note){
        .source = {KEYBED, generation},
        .note = {kind, 0, key, velocity},
    };
}

static bool submit(patch_demo *demo, gridpico_ingress_note note,
                   const gridpico_connection *destinations, size_t count)
{
    return expect_result("Submit keybed note", gridpico_patch_graph_submit_note(&demo->graph, &note),
                         GRIDPICO_OK) && expect_capture(demo, note, destinations, count);
}

static bool set_route(patch_demo *demo, gridpico_port_id destination, bool enabled)
{
    const gridpico_patch_route_request request = {
        .source = KEYBED, .destination = destination, .enabled = enabled,
    };
    return expect_result("Set Patch route",
                         gridpico_patch_session_set_route(&demo->session, &request), GRIDPICO_OK);
}

static bool update_port(patch_demo *demo, gridpico_connection connection,
                        gridpico_patch_connection_state state, gridpico_result expected)
{
    const gridpico_patch_port_update update = {.connection = connection, .state = state};
    return expect_result("Update simulation port",
                         gridpico_patch_fixture_update_port(&demo->sim, &update), expected) &&
           expect_result("Update graph port",
                         gridpico_patch_graph_update_port(&demo->graph, &update), expected);
}

static bool run_demo(patch_demo *demo)
{
    const gridpico_connection initial_outputs[] = {{MIDI_A, 1}, {MIDI_B, 1}};
    const gridpico_connection reconnected_outputs[] = {{MIDI_A, 1}, {MIDI_B, 2}};
    const gridpico_patch_route_state off = GRIDPICO_PATCH_ROUTE_OFF;
    const gridpico_patch_route_state active = GRIDPICO_PATCH_ROUTE_ACTIVE;
    const gridpico_patch_route_state pending = GRIDPICO_PATCH_ROUTE_PENDING;

    puts("1. Named inventory and two branches from one keybed");
    if (!expect_snapshot(demo, off, off, 0, 0) || !expect_silence(demo) ||
        !set_route(demo, MIDI_A, true) || !set_route(demo, MIDI_B, true) ||
        !expect_snapshot(demo, active, active, 0, 2) || !expect_silence(demo) ||
        !submit(demo, keybed_note(1, GRIDPICO_NOTE_ON, 60, 96), initial_outputs, 2) ||
        !submit(demo, keybed_note(1, GRIDPICO_NOTE_OFF, 60, 32), initial_outputs, 2)) {
        return false;
    }

    puts("2. Disable MIDI A while both outputs sound");
    if (!submit(demo, keybed_note(1, GRIDPICO_NOTE_ON, 62, 96), initial_outputs, 2) ||
        !set_route(demo, MIDI_A, false) ||
        !expect_capture(demo, keybed_note(1, GRIDPICO_NOTE_OFF, 62, 0), &initial_outputs[0], 1) ||
        !expect_snapshot(demo, off, active, 0, 1) ||
        !submit(demo, keybed_note(1, GRIDPICO_NOTE_OFF, 62, 32), &initial_outputs[1], 1)) {
        return false;
    }

    puts("3. Disconnect MIDI B with a held note and restore its desired route");
    if (!set_route(demo, MIDI_A, true) || !expect_snapshot(demo, active, active, 0, 1) ||
        !expect_silence(demo) ||
        !submit(demo, keybed_note(1, GRIDPICO_NOTE_ON, 64, 96), initial_outputs, 2) ||
        !update_port(demo, initial_outputs[1], GRIDPICO_PATCH_DISCONNECTED, GRIDPICO_OK) ||
        !expect_snapshot(demo, active, pending, MIDI_B, 1) || !expect_silence(demo) ||
        !submit(demo, keybed_note(1, GRIDPICO_NOTE_OFF, 64, 32), &initial_outputs[0], 1) ||
        !update_port(demo, reconnected_outputs[1], GRIDPICO_PATCH_CONNECTED, GRIDPICO_OK) ||
        !expect_capture(demo, keybed_note(1, GRIDPICO_NOTE_OFF, 64, 0), &reconnected_outputs[1], 1) ||
        !expect_snapshot(demo, active, active, 0, 1)) {
        return false;
    }

    puts("4. Reject stale generations and play from the reconnected keybed");
    const gridpico_ingress_note stale = keybed_note(1, GRIDPICO_NOTE_ON, 65, 96);
    if (!update_port(demo, initial_outputs[1], GRIDPICO_PATCH_DISCONNECTED, GRIDPICO_UNAVAILABLE) ||
        !expect_snapshot(demo, active, active, 0, 0) || !expect_silence(demo) ||
        !update_port(demo, (gridpico_connection){KEYBED, 1}, GRIDPICO_PATCH_DISCONNECTED, GRIDPICO_OK) ||
        !expect_snapshot(demo, pending, pending, KEYBED, 1) ||
        !update_port(demo, (gridpico_connection){KEYBED, 2}, GRIDPICO_PATCH_CONNECTED, GRIDPICO_OK) ||
        !expect_snapshot(demo, active, active, 0, 1) ||
        !expect_result("Submit stale source generation",
                       gridpico_patch_graph_submit_note(&demo->graph, &stale), GRIDPICO_UNAVAILABLE) ||
        !expect_silence(demo) ||
        !submit(demo, keybed_note(2, GRIDPICO_NOTE_ON, 65, 96), reconnected_outputs, 2) ||
        !submit(demo, keybed_note(2, GRIDPICO_NOTE_OFF, 65, 32), reconnected_outputs, 2)) {
        return false;
    }
    printf("PASS (4 cases, %zu captured deliveries)\n", demo->verified_count);
    return true;
}

int main(void)
{
    patch_demo demo = {0};
    if (!expect_result("Initialize Patch simulation",
                       gridpico_patch_fixture_init(&demo.sim, &patch_inventory), GRIDPICO_OK) ||
        !expect_result("Initialize Patch graph",
                       gridpico_patch_graph_init(&demo.graph, &patch_inventory,
                                                gridpico_patch_fixture_deliver, &demo.sim),
                       GRIDPICO_OK)) {
        return EXIT_FAILURE;
    }
    gridpico_patch_session_init(&demo.session, &demo.graph,
                                gridpico_patch_graph_read_graph, gridpico_patch_graph_set_route);
    return run_demo(&demo) ? EXIT_SUCCESS : EXIT_FAILURE;
}
