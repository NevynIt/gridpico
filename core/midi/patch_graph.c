#include "core/midi/patch_graph.h"

#include <string.h>

enum {
    NO_DEVICE = GRIDPICO_PATCH_MAX_DEVICES,
    NO_PORT = GRIDPICO_PATCH_MAX_PORTS,
    NO_BRANCH = GRIDPICO_PATCH_MAX_BRANCHES,
    DORMANT_GENERATION = 1,
    LOCAL_MANAGED_BRANCHES = 2,
    LOCAL_RAW_BRANCHES = 1,
};

typedef struct {
    size_t devices;
    size_t ports;
    size_t branches;
} graph_limits;

static bool valid_state(gridpico_patch_connection_state state)
{
    return state == GRIDPICO_PATCH_DISCONNECTED || state == GRIDPICO_PATCH_CONNECTED ||
           state == GRIDPICO_PATCH_AMBIGUOUS;
}

static bool valid_name(const char name[GRIDPICO_PATCH_NAME_BYTES])
{
    size_t i = 0;
    while (i < GRIDPICO_PATCH_NAME_BYTES) {
        const unsigned char first = (unsigned char)name[i++];
        if (first == 0) {
            return true;
        }
        if (first < 0x80) {
            continue;
        }
        if (first < 0xc2 || first > 0xf4) {
            return false;
        }

        const size_t remaining = first < 0xe0 ? 1 : first < 0xf0 ? 2 : 3;
        if (i + remaining >= GRIDPICO_PATCH_NAME_BYTES) {
            return false;
        }
        const unsigned char second = (unsigned char)name[i];
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) {
            return false;
        }
        for (size_t j = 0; j < remaining; ++j) {
            const unsigned char byte = (unsigned char)name[i++];
            if (byte < 0x80 || byte >= 0xc0) {
                return false;
            }
        }
    }
    return false;
}

static size_t device_index(const gridpico_patch_inventory *inventory, gridpico_device_id id)
{
    for (size_t i = 0; i < inventory->device_count; ++i) {
        if (inventory->devices[i].id == id) {
            return i;
        }
    }
    return NO_DEVICE;
}

static size_t port_index(const gridpico_patch_inventory *inventory, gridpico_port_id id)
{
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (inventory->ports[i].id == id) {
            return i;
        }
    }
    return NO_PORT;
}

static bool port_accepts(const gridpico_patch_graph *graph, size_t index,
                         gridpico_patch_direction direction, gridpico_local_route_mode mode)
{
    if (index == NO_PORT || graph->inventory.ports[index].direction != direction) {
        return false;
    }
    if (mode == GRIDPICO_LOCAL_MANAGED) {
        return graph->inventory.ports[index].patch_eligible &&
               (graph->deliver_raw == NULL || graph->port_capabilities[index] == GRIDPICO_LOCAL_PORT_MANAGED);
    }
    return mode == GRIDPICO_LOCAL_RAW && graph->deliver_raw != NULL &&
           graph->port_capabilities[index] == GRIDPICO_LOCAL_PORT_RAW;
}

static bool source_available(const gridpico_patch_graph *graph, size_t index, uint32_t generation)
{
    return graph->inventory.ports[index].state == GRIDPICO_PATCH_CONNECTED &&
           graph->ports[index].generation == generation;
}

static bool valid_inventory(const gridpico_patch_inventory *inventory)
{
    for (size_t i = 0; i < inventory->device_count; ++i) {
        const gridpico_patch_device *device = &inventory->devices[i];
        if (device->id == 0 || device_index(inventory, device->id) != i ||
            !valid_name(device->name) || !valid_state(device->state)) {
            return false;
        }
    }
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const gridpico_patch_port *port = &inventory->ports[i];
        if (port->id == 0 || port_index(inventory, port->id) != i ||
            device_index(inventory, port->device) == NO_DEVICE || !valid_name(port->name) ||
            !valid_state(port->state) ||
            (port->direction != GRIDPICO_PATCH_INPUT && port->direction != GRIDPICO_PATCH_OUTPUT)) {
            return false;
        }
    }
    return true;
}

static bool valid_local_registry(const gridpico_local_registry *registry)
{
    for (size_t i = 0; i < registry->inventory.device_count; ++i) {
        if (registry->inventory.devices[i].state != GRIDPICO_PATCH_DISCONNECTED) {
            return false;
        }
    }
    for (size_t i = 0; i < registry->inventory.port_count; ++i) {
        const gridpico_patch_port *port = &registry->inventory.ports[i];
        const gridpico_local_port_capability capability = registry->port_capabilities[i];
        if (port->state != GRIDPICO_PATCH_DISCONNECTED ||
            port->patch_eligible != (capability == GRIDPICO_LOCAL_PORT_MANAGED) ||
            (capability != GRIDPICO_LOCAL_PORT_MANAGED && capability != GRIDPICO_LOCAL_PORT_RAW &&
             !(capability == GRIDPICO_LOCAL_PORT_PANEL && port->direction == GRIDPICO_PATCH_INPUT))) {
            return false;
        }
    }
    const gridpico_local_native_virtual_cable *pair = &registry->native_virtual_cable;
    if (pair->source == 0 && pair->destination == 0) {
        return true;
    }
    const size_t source = port_index(&registry->inventory, pair->source);
    const size_t destination = port_index(&registry->inventory, pair->destination);
    return gridpico_local_native_virtual_cable_matches(pair, pair->source, pair->destination) &&
           source != NO_PORT && destination != NO_PORT &&
           registry->port_capabilities[source] == GRIDPICO_LOCAL_PORT_MANAGED &&
           registry->port_capabilities[destination] == GRIDPICO_LOCAL_PORT_MANAGED &&
           registry->inventory.ports[source].direction == GRIDPICO_PATCH_INPUT &&
           registry->inventory.ports[destination].direction == GRIDPICO_PATCH_OUTPUT &&
           registry->inventory.ports[source].device == registry->inventory.ports[destination].device;
}

static void aggregate_devices(gridpico_patch_inventory *inventory)
{
    for (size_t i = 0; i < inventory->device_count; ++i) {
        inventory->devices[i].state = GRIDPICO_PATCH_DISCONNECTED;
    }
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const gridpico_patch_port *port = &inventory->ports[i];
        gridpico_patch_device *device = &inventory->devices[device_index(inventory, port->device)];
        if (port->state == GRIDPICO_PATCH_CONNECTED ||
            (port->state == GRIDPICO_PATCH_AMBIGUOUS && device->state == GRIDPICO_PATCH_DISCONNECTED)) {
            device->state = port->state;
        }
    }
}

gridpico_result gridpico_patch_graph_set_apc_led_reserved(
    gridpico_patch_graph *graph, gridpico_port_id output, bool reserved)
{
    if (graph == NULL) {
        return GRIDPICO_INVALID;
    }
    const size_t index = port_index(&graph->inventory, output);
    if (index == NO_PORT || graph->inventory.ports[index].direction != GRIDPICO_PATCH_OUTPUT) {
        return GRIDPICO_INVALID;
    }

    gridpico_patch_port *port = &graph->inventory.ports[index];
    gridpico_patch_port_runtime *runtime = &graph->ports[index];
    const gridpico_local_port_capability capability = graph->port_capabilities[index];
    const bool ordinary = capability == GRIDPICO_LOCAL_PORT_MANAGED && port->patch_eligible;
    const bool tagged = runtime->apc_led_reserved && capability == GRIDPICO_LOCAL_PORT_PANEL &&
                        !port->patch_eligible;
    if (!ordinary && !tagged) {
        return GRIDPICO_INVALID;
    }
    if (reserved == tagged) {
        return GRIDPICO_OK;
    }

    if (reserved) {
        for (size_t i = 0; i < graph->branch_limit; ++i) {
            if (graph->branches[i].enabled && graph->branches[i].destination == index) {
                return GRIDPICO_UNAVAILABLE;
            }
        }
    }
    if (graph->revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }

    runtime->apc_led_reserved = reserved;
    port->patch_eligible = !reserved;
    graph->port_capabilities[index] = reserved ? GRIDPICO_LOCAL_PORT_PANEL : GRIDPICO_LOCAL_PORT_MANAGED;
    ++graph->revision;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_set_apc_panel_reserved(
    gridpico_patch_graph *graph, gridpico_port_id input, bool reserved)
{
    if (graph == NULL) {
        return GRIDPICO_INVALID;
    }
    const size_t index = port_index(&graph->inventory, input);
    if (index == NO_PORT || graph->inventory.ports[index].direction != GRIDPICO_PATCH_INPUT) {
        return GRIDPICO_INVALID;
    }

    gridpico_patch_port *port = &graph->inventory.ports[index];
    gridpico_patch_port_runtime *runtime = &graph->ports[index];
    const gridpico_local_port_capability capability = graph->port_capabilities[index];
    const bool ordinary = capability == GRIDPICO_LOCAL_PORT_MANAGED && port->patch_eligible;
    const bool tagged = runtime->apc_panel_reserved && capability == GRIDPICO_LOCAL_PORT_PANEL &&
                        !port->patch_eligible;
    if (!ordinary && !tagged) {
        return GRIDPICO_INVALID;
    }
    if (reserved == tagged) {
        return GRIDPICO_OK;
    }

    if (reserved) {
        for (size_t i = 0; i < graph->branch_limit; ++i) {
            if (graph->branches[i].enabled && graph->branches[i].source == index) {
                return GRIDPICO_UNAVAILABLE;
            }
        }
    }
    if (graph->revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }

    runtime->apc_panel_reserved = reserved;
    port->patch_eligible = !reserved;
    graph->port_capabilities[index] = reserved ? GRIDPICO_LOCAL_PORT_PANEL : GRIDPICO_LOCAL_PORT_MANAGED;
    ++graph->revision;
    return GRIDPICO_OK;
}

static bool branch_active(const gridpico_patch_graph *graph, const gridpico_patch_graph_branch *branch)
{
    return branch->enabled &&
           port_accepts(graph, branch->source, GRIDPICO_PATCH_INPUT, branch->mode) &&
           port_accepts(graph, branch->destination, GRIDPICO_PATCH_OUTPUT, branch->mode) &&
           graph->inventory.ports[branch->source].state == GRIDPICO_PATCH_CONNECTED &&
           graph->inventory.ports[branch->destination].state == GRIDPICO_PATCH_CONNECTED;
}

static bool valid_note(gridpico_note note)
{
    return note.channel_index < 16 && note.key < 128 && note.velocity < 128 &&
           (note.kind == GRIDPICO_NOTE_OFF ||
            (note.kind == GRIDPICO_NOTE_ON && note.velocity != 0));
}

static gridpico_result init_graph(gridpico_patch_graph *graph, const gridpico_patch_inventory *inventory,
                                  gridpico_deliver_note_fn deliver, void *delivery_context,
                                  graph_limits limits)
{
    if (graph == NULL || inventory == NULL || deliver == NULL) {
        return GRIDPICO_INVALID;
    }
    if (inventory->device_count > limits.devices || inventory->port_count > limits.ports) {
        return GRIDPICO_CAPACITY;
    }
    if (!valid_inventory(inventory)) {
        return GRIDPICO_INVALID;
    }

    *graph = (gridpico_patch_graph){
        .inventory = *inventory,
        .deliver = deliver,
        .delivery_context = delivery_context,
        .branch_limit = limits.branches,
    };
    for (size_t i = 0; i < graph->inventory.port_count; ++i) {
        graph->ports[i].generation = DORMANT_GENERATION;
    }
    aggregate_devices(&graph->inventory);
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_init(gridpico_patch_graph *graph,
                                         const gridpico_patch_inventory *inventory,
                                         gridpico_deliver_note_fn deliver,
                                         void *delivery_context)
{
    const graph_limits limits = {
        .devices = GRIDPICO_PATCH_DEVICES,
        .ports = GRIDPICO_PATCH_PORTS,
        .branches = GRIDPICO_PATCH_BRANCHES,
    };
    return init_graph(graph, inventory, deliver, delivery_context, limits);
}

gridpico_result gridpico_patch_graph_init_browser(gridpico_patch_graph *graph,
                                                 const gridpico_patch_inventory *inventory,
                                                 gridpico_deliver_note_fn deliver,
                                                 void *delivery_context)
{
    return gridpico_patch_graph_init(graph, inventory, deliver, delivery_context);
}

gridpico_result gridpico_patch_graph_init_monitor(gridpico_patch_graph *graph,
                                                 const gridpico_patch_inventory *inventory,
                                                 gridpico_deliver_note_fn deliver,
                                                 void *delivery_context)
{
    const graph_limits limits = {
        .devices = GRIDPICO_PATCH_MONITOR_DEVICES,
        .ports = GRIDPICO_PATCH_MONITOR_PORTS,
        .branches = GRIDPICO_PATCH_MONITOR_BRANCHES,
    };
    return init_graph(graph, inventory, deliver, delivery_context, limits);
}

typedef enum {
    LOCAL_LEGACY_PROFILE,
    LOCAL_CONFIGURED_PROFILE,
    LOCAL_MANAGED_BRANCH_STORAGE,
} local_profile_kind;

static gridpico_result init_local_graph(gridpico_patch_graph *graph, const gridpico_local_registry *registry,
                                        gridpico_local_profile profile, gridpico_deliver_note_fn deliver_note,
                                        gridpico_local_deliver_raw_fn deliver_raw, void *delivery_context,
                                        local_profile_kind kind)
{
    if (graph == NULL || registry == NULL || deliver_note == NULL || deliver_raw == NULL) {
        return GRIDPICO_INVALID;
    }
    const bool configured = kind != LOCAL_LEGACY_PROFILE;
    const bool managed_storage = kind == LOCAL_MANAGED_BRANCH_STORAGE;
    if (managed_storage) {
        if (profile.raw_branch_limit != 0) {
            return GRIDPICO_INVALID;
        }
        profile.managed_branch_limit = GRIDPICO_PATCH_MAX_BRANCHES;
        profile.managed_fanout_limit = GRIDPICO_PATCH_MAX_BRANCHES;
    }
    graph_limits limits = {
        .devices = configured ? GRIDPICO_PATCH_MAX_DEVICES : GRIDPICO_LOCAL_DEVICES,
        .ports = configured ? GRIDPICO_PATCH_MAX_PORTS : GRIDPICO_LOCAL_PORTS,
    };
    if (configured && profile.midi_peer_limit == 0) {
        return GRIDPICO_INVALID;
    }
    const bool dynamic = profile.dynamic_device_limit != 0;
    if (dynamic != (profile.dynamic_port_limit != 0)) {
        return GRIDPICO_INVALID;
    }
    if (!managed_storage &&
        (profile.managed_branch_limit > GRIDPICO_LOCAL_MAX_MANAGED_BRANCHES ||
         profile.raw_branch_limit > GRIDPICO_LOCAL_MAX_RAW_BRANCHES ||
         profile.managed_fanout_limit > GRIDPICO_LOCAL_MAX_FANOUT)) {
        return GRIDPICO_CAPACITY;
    }
    if (registry->inventory.device_count > limits.devices || registry->inventory.port_count > limits.ports ||
        (configured && profile.midi_peer_limit > registry->inventory.port_count)) {
        return GRIDPICO_CAPACITY;
    }
    if (dynamic && (profile.dynamic_device_limit > limits.devices || profile.dynamic_port_limit > limits.ports ||
                    registry->inventory.device_count > profile.dynamic_device_limit ||
                    registry->inventory.port_count > profile.dynamic_port_limit)) {
        return GRIDPICO_CAPACITY;
    }
    if (!valid_local_registry(registry)) {
        return GRIDPICO_INVALID;
    }

    limits.branches = profile.managed_branch_limit + profile.raw_branch_limit;
    const gridpico_result result = init_graph(graph, &registry->inventory, deliver_note, delivery_context, limits);
    if (result == GRIDPICO_OK) {
        graph->deliver_raw = deliver_raw;
        graph->local_profile = profile;
        graph->native_virtual_cable = registry->native_virtual_cable;
        memcpy(graph->port_capabilities, registry->port_capabilities,
               graph->inventory.port_count * sizeof graph->port_capabilities[0]);
    }
    return result;
}

gridpico_result gridpico_patch_graph_init_local(gridpico_patch_graph *graph,
                                               const gridpico_local_registry *registry,
                                               gridpico_deliver_note_fn deliver_note,
                                               gridpico_local_deliver_raw_fn deliver_raw,
                                               void *delivery_context)
{
    const gridpico_local_profile profile = {
        .midi_peer_limit = GRIDPICO_LOCAL_PORTS,
        .managed_branch_limit = LOCAL_MANAGED_BRANCHES,
        .raw_branch_limit = LOCAL_RAW_BRANCHES,
        .managed_fanout_limit = LOCAL_MANAGED_BRANCHES,
    };
    return init_local_graph(graph, registry, profile, deliver_note, deliver_raw, delivery_context,
                            LOCAL_LEGACY_PROFILE);
}

gridpico_result gridpico_patch_graph_init_local_profile(
    gridpico_patch_graph *graph, const gridpico_local_registry *registry,
    const gridpico_local_profile *profile, gridpico_deliver_note_fn deliver_note,
    gridpico_local_deliver_raw_fn deliver_raw, void *delivery_context)
{
    if (profile == NULL) {
        return GRIDPICO_INVALID;
    }
    return init_local_graph(graph, registry, *profile, deliver_note, deliver_raw, delivery_context,
                            LOCAL_CONFIGURED_PROFILE);
}

gridpico_result gridpico_patch_graph_init_local_managed_storage(
    gridpico_patch_graph *graph, const gridpico_local_registry *registry,
    const gridpico_local_profile *profile, gridpico_deliver_note_fn deliver_note,
    gridpico_local_deliver_raw_fn deliver_raw, void *delivery_context)
{
    if (profile == NULL) {
        return GRIDPICO_INVALID;
    }
    return init_local_graph(graph, registry, *profile, deliver_note, deliver_raw, delivery_context,
                            LOCAL_MANAGED_BRANCH_STORAGE);
}

static gridpico_result append_managed_device(
    gridpico_patch_graph *graph, const gridpico_patch_device *device,
    const gridpico_patch_port *ports, size_t port_count, uint32_t generation)
{
    if (graph == NULL) {
        return GRIDPICO_INVALID;
    }
    const gridpico_local_profile *profile = &graph->local_profile;
    if (profile->dynamic_device_limit == 0) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (device == NULL || ports == NULL || port_count == 0) {
        return GRIDPICO_INVALID;
    }

    gridpico_patch_inventory *inventory = &graph->inventory;
    const size_t first_port = inventory->port_count;
    const bool ephemeral = generation != DORMANT_GENERATION;
    if (!ephemeral && first_port != 0 && graph->ports[first_port - 1].ephemeral_managed_output) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (inventory->device_count >= profile->dynamic_device_limit ||
        port_count > profile->dynamic_port_limit - first_port || graph->revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }
    if (device->id == 0 || device->state != GRIDPICO_PATCH_DISCONNECTED || !valid_name(device->name) ||
        device_index(inventory, device->id) != NO_DEVICE) {
        return GRIDPICO_INVALID;
    }
    for (size_t i = 0; i < port_count; ++i) {
        const gridpico_patch_port *port = &ports[i];
        if (port->id == 0 || port->device != device->id || port->state != GRIDPICO_PATCH_DISCONNECTED ||
            !port->patch_eligible || !valid_name(port->name) || port_index(inventory, port->id) != NO_PORT ||
            (port->direction != GRIDPICO_PATCH_INPUT && port->direction != GRIDPICO_PATCH_OUTPUT)) {
            return GRIDPICO_INVALID;
        }
        for (size_t j = 0; j < i; ++j) {
            if (ports[j].id == port->id) {
                return GRIDPICO_INVALID;
            }
        }
    }

    const gridpico_patch_connection_state state =
        ephemeral ? GRIDPICO_PATCH_CONNECTED : GRIDPICO_PATCH_DISCONNECTED;
    inventory->devices[inventory->device_count] = *device;
    inventory->devices[inventory->device_count++].state = state;
    memmove(&inventory->ports[first_port], ports, port_count * sizeof ports[0]);
    for (size_t i = first_port; i < first_port + port_count; ++i) {
        inventory->ports[i].state = state;
        graph->port_capabilities[i] = GRIDPICO_LOCAL_PORT_MANAGED;
        graph->ports[i] = (gridpico_patch_port_runtime){
            .generation = generation, .ephemeral_managed_output = ephemeral,
        };
    }
    inventory->port_count += port_count;
    ++graph->revision;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_append_managed_device(
    gridpico_patch_graph *graph, const gridpico_patch_device *device,
    const gridpico_patch_port *ports, size_t port_count)
{
    return append_managed_device(graph, device, ports, port_count, DORMANT_GENERATION);
}

gridpico_result gridpico_patch_graph_publish_ephemeral_managed_output(
    gridpico_patch_graph *graph, const gridpico_patch_device *device,
    const gridpico_patch_port *port, gridpico_connection owner)
{
    if (port == NULL || port->direction != GRIDPICO_PATCH_OUTPUT ||
        owner.endpoint != port->id || owner.generation <= DORMANT_GENERATION) {
        return GRIDPICO_INVALID;
    }
    return append_managed_device(graph, device, port, 1, owner.generation);
}

static void remove_port_branches(gridpico_patch_graph *graph, size_t port)
{
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (branch->source == port || branch->destination == port) {
            *branch = (gridpico_patch_graph_branch){0};
            continue;
        }
        if (branch->source > port) {
            --branch->source;
        }
        if (branch->destination > port) {
            --branch->destination;
        }
    }
}

gridpico_result gridpico_patch_graph_retire_ephemeral_managed_output(
    gridpico_patch_graph *graph, gridpico_connection owner)
{
    if (graph == NULL || owner.endpoint == 0 || owner.generation <= DORMANT_GENERATION) {
        return GRIDPICO_INVALID;
    }
    gridpico_patch_inventory *inventory = &graph->inventory;
    const size_t port = port_index(inventory, owner.endpoint);
    if (port == NO_PORT || !graph->ports[port].ephemeral_managed_output ||
        graph->ports[port].generation != owner.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (graph->revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }

    const size_t device = device_index(inventory, inventory->ports[port].device);
    remove_port_branches(graph, port);

    const size_t last_device = --inventory->device_count;
    memmove(&inventory->devices[device], &inventory->devices[device + 1],
            (last_device - device) * sizeof inventory->devices[0]);
    inventory->devices[last_device] = (gridpico_patch_device){0};

    const size_t last_port = --inventory->port_count;
    const size_t moved_ports = last_port - port;
    memmove(&inventory->ports[port], &inventory->ports[port + 1],
            moved_ports * sizeof inventory->ports[0]);
    memmove(&graph->ports[port], &graph->ports[port + 1], moved_ports * sizeof graph->ports[0]);
    memmove(&graph->port_capabilities[port], &graph->port_capabilities[port + 1],
            moved_ports * sizeof graph->port_capabilities[0]);
    inventory->ports[last_port] = (gridpico_patch_port){0};
    graph->ports[last_port] = (gridpico_patch_port_runtime){0};
    graph->port_capabilities[last_port] = GRIDPICO_LOCAL_PORT_MANAGED;

    ++graph->revision;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_update_port(void *context,
                                                const gridpico_patch_port_update *update)
{
    gridpico_patch_graph *graph = context;
    if (graph == NULL || update == NULL || update->connection.endpoint == 0 ||
        update->connection.generation == 0 || !valid_state(update->state)) {
        return GRIDPICO_INVALID;
    }
    const size_t index = port_index(&graph->inventory, update->connection.endpoint);
    if (index == NO_PORT) {
        return GRIDPICO_INVALID;
    }
    if (graph->deliver_raw != NULL && update->state == GRIDPICO_PATCH_AMBIGUOUS) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_patch_port *port = &graph->inventory.ports[index];
    gridpico_patch_port_runtime *runtime = &graph->ports[index];
    const uint32_t generation = update->connection.generation;
    if (runtime->ephemeral_managed_output &&
        (port->state != update->state || runtime->generation != generation)) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (update->state == GRIDPICO_PATCH_CONNECTED && port->state != GRIDPICO_PATCH_CONNECTED) {
        if (generation <= runtime->generation) {
            return GRIDPICO_UNAVAILABLE;
        }
    } else if (generation != runtime->generation) {
        return GRIDPICO_UNAVAILABLE;
    }

    if (port->state != update->state || runtime->generation != generation) {
        port->state = update->state;
        runtime->generation = generation;
        aggregate_devices(&graph->inventory);
        if (graph->revision != UINT64_MAX) {
            ++graph->revision;
        }
    }
    return GRIDPICO_OK;
}

static bool route_ports(const gridpico_patch_graph *graph, const gridpico_local_route *route,
                        size_t *source, size_t *destination)
{
    *source = port_index(&graph->inventory, route->source);
    *destination = port_index(&graph->inventory, route->destination);
    return port_accepts(graph, *source, GRIDPICO_PATCH_INPUT, route->mode) &&
           port_accepts(graph, *destination, GRIDPICO_PATCH_OUTPUT, route->mode);
}

static size_t branch_index(const gridpico_patch_graph *graph, size_t source, size_t destination,
                           gridpico_local_route_mode mode)
{
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (branch->enabled && branch->source == source && branch->destination == destination && branch->mode == mode) {
            return i;
        }
    }
    return NO_BRANCH;
}

static gridpico_patch_managed_enable_decision check_new_branch(
    const gridpico_patch_graph *graph, size_t source, gridpico_local_route_mode mode, size_t *slot)
{
    if (graph->revision == UINT64_MAX) {
        return GRIDPICO_PATCH_MANAGED_ENABLE_REVISION_EXHAUSTED;
    }
    size_t free_branch = NO_BRANCH;
    size_t mode_count = 0;
    size_t source_count = 0;
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (!branch->enabled && free_branch == NO_BRANCH) {
            free_branch = i;
        }
        if (branch->enabled && branch->mode == mode) {
            ++mode_count;
            if (branch->source == source) {
                ++source_count;
            }
        }
    }
    if (free_branch == NO_BRANCH) {
        return GRIDPICO_PATCH_MANAGED_ENABLE_BRANCH_LIMIT;
    }
    if (graph->deliver_raw != NULL) {
        const gridpico_local_profile *profile = &graph->local_profile;
        const bool managed = mode == GRIDPICO_LOCAL_MANAGED;
        const size_t mode_limit = managed ? profile->managed_branch_limit : profile->raw_branch_limit;
        if (mode_count >= mode_limit) {
            return GRIDPICO_PATCH_MANAGED_ENABLE_BRANCH_LIMIT;
        }
        if (managed && source_count >= profile->managed_fanout_limit) {
            return GRIDPICO_PATCH_MANAGED_ENABLE_FANOUT_LIMIT;
        }
    }
    *slot = free_branch;
    return GRIDPICO_PATCH_MANAGED_ENABLE_ALLOWED;
}

static gridpico_result enable_branch(gridpico_patch_graph *graph, size_t source, size_t destination,
                                     gridpico_local_route_mode mode)
{
    size_t slot;
    const gridpico_patch_managed_enable_decision decision = check_new_branch(graph, source, mode, &slot);
    if (decision != GRIDPICO_PATCH_MANAGED_ENABLE_ALLOWED) {
        return GRIDPICO_CAPACITY;
    }
    const gridpico_patch_graph_branch branch = {
        .source = source, .destination = destination, .mode = mode, .enabled = true,
    };
    if (mode == GRIDPICO_LOCAL_RAW && !branch_active(graph, &branch)) {
        return GRIDPICO_UNAVAILABLE;
    }
    graph->branches[slot] = branch;
    ++graph->revision;
    return GRIDPICO_OK;
}

static gridpico_result set_route(gridpico_patch_graph *graph, const gridpico_local_route *route,
                                 bool enabled)
{
    size_t source, destination;
    if (!route_ports(graph, route, &source, &destination)) {
        return GRIDPICO_INVALID;
    }
    const size_t existing = branch_index(graph, source, destination, route->mode);
    if (enabled == (existing != NO_BRANCH)) {
        return GRIDPICO_OK;
    }
    if (enabled) {
        return enable_branch(graph, source, destination, route->mode);
    }
    if (graph->revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }

    graph->branches[existing].enabled = false;
    ++graph->revision;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_set_route(void *context,
                                               const gridpico_patch_route_request *request)
{
    gridpico_patch_graph *graph = context;
    if (graph == NULL || request == NULL) {
        return GRIDPICO_INVALID;
    }
    const gridpico_local_route route = {request->source, request->destination, GRIDPICO_LOCAL_MANAGED};
    return set_route(graph, &route, request->enabled);
}

gridpico_result gridpico_patch_graph_set_local_route(void *context,
                                                    const gridpico_local_route_request *request)
{
    gridpico_patch_graph *graph = context;
    if (graph == NULL || graph->deliver_raw == NULL || request == NULL) {
        return GRIDPICO_INVALID;
    }
    return set_route(graph, &request->route, request->enabled);
}

void gridpico_patch_graph_bind_note_transform(
    gridpico_patch_graph *graph, gridpico_managed_note_transform_fn transform)
{
    if (graph != NULL && graph->deliver_raw != NULL) {
        graph->transform_note = transform;
    }
}

static gridpico_result managed_transform_branch(const gridpico_patch_graph *graph,
                                                const gridpico_local_route *route, size_t *index)
{
    size_t source, destination;
    if (graph == NULL || graph->deliver_raw == NULL || route == NULL ||
        route->mode != GRIDPICO_LOCAL_MANAGED || !route_ports(graph, route, &source, &destination)) {
        return GRIDPICO_INVALID;
    }
    *index = branch_index(graph, source, destination, GRIDPICO_LOCAL_MANAGED);
    return *index == NO_BRANCH ? GRIDPICO_UNAVAILABLE : GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_read_managed_note_transform(
    const gridpico_patch_graph *graph, const gridpico_local_route *route,
    gridpico_managed_note_transform *out)
{
    if (out == NULL) {
        return GRIDPICO_INVALID;
    }
    size_t index;
    const gridpico_result result = managed_transform_branch(graph, route, &index);
    if (result == GRIDPICO_OK) {
        *out = graph->branches[index].transform;
    }
    return result;
}

gridpico_result gridpico_patch_graph_set_managed_note_transform(
    gridpico_patch_graph *graph, const gridpico_local_route *route,
    const gridpico_managed_note_transform *transform)
{
    if (transform == NULL || transform->transpose_semitones < -127 || transform->output_channel > 16) {
        return GRIDPICO_INVALID;
    }
    size_t index;
    const gridpico_result branch = managed_transform_branch(graph, route, &index);
    if (branch != GRIDPICO_OK) {
        return branch;
    }
    gridpico_managed_note_transform *current = &graph->branches[index].transform;
    if (current->transpose_semitones == transform->transpose_semitones &&
        current->output_channel == transform->output_channel) {
        return GRIDPICO_OK;
    }
    if (graph->transform_note == NULL) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (graph->revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }
    *current = *transform;
    ++graph->revision;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_check_managed_enable(
    const gridpico_patch_graph *graph, const gridpico_local_route *route,
    gridpico_patch_managed_enable_decision *out)
{
    size_t source, destination;
    if (graph == NULL || graph->deliver_raw == NULL || route == NULL || out == NULL ||
        route->mode != GRIDPICO_LOCAL_MANAGED || !route_ports(graph, route, &source, &destination)) {
        return GRIDPICO_INVALID;
    }
    size_t slot;
    *out = branch_index(graph, source, destination, route->mode) == NO_BRANCH
               ? check_new_branch(graph, source, route->mode, &slot)
               : GRIDPICO_PATCH_MANAGED_ENABLE_ALLOWED;
    return GRIDPICO_OK;
}

static size_t enabled_branch_count(const gridpico_patch_graph *graph)
{
    size_t count = 0;
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        count += graph->branches[i].enabled;
    }
    return count;
}

gridpico_result gridpico_patch_graph_managed_storage_full(
    const gridpico_patch_graph *graph, const gridpico_local_route *route, bool *out)
{
    if (out == NULL) {
        return GRIDPICO_INVALID;
    }
    gridpico_patch_managed_enable_decision decision;
    const gridpico_result result = gridpico_patch_graph_check_managed_enable(graph, route, &decision);
    if (result == GRIDPICO_OK) {
        *out = decision == GRIDPICO_PATCH_MANAGED_ENABLE_BRANCH_LIMIT && graph->branch_limit != 0 &&
               enabled_branch_count(graph) == graph->branch_limit;
    }
    return result;
}

gridpico_result gridpico_patch_graph_read_local_route(void *context, const gridpico_local_route *route,
                                                     gridpico_local_route_status *out)
{
    const gridpico_patch_graph *graph = context;
    size_t source, destination;
    if (graph == NULL || graph->deliver_raw == NULL || route == NULL || out == NULL ||
        !route_ports(graph, route, &source, &destination)) {
        return GRIDPICO_INVALID;
    }
    const size_t index = branch_index(graph, source, destination, route->mode);
    gridpico_patch_route_state state = GRIDPICO_PATCH_ROUTE_OFF;
    if (index != NO_BRANCH) {
        state = branch_active(graph, &graph->branches[index]) ? GRIDPICO_PATCH_ROUTE_ACTIVE : GRIDPICO_PATCH_ROUTE_PENDING;
    }
    *out = (gridpico_local_route_status){.revision = graph->revision, .state = state};
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_read_graph(void *context, gridpico_patch_graph_state *out)
{
    const gridpico_patch_graph *graph = context;
    if (graph == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }
    *out = (gridpico_patch_graph_state){.revision = graph->revision, .inventory = graph->inventory};
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (branch->enabled && branch->mode == GRIDPICO_LOCAL_MANAGED) {
            out->branches[out->branch_count++] = (gridpico_patch_branch){
                .source = graph->inventory.ports[branch->source].id,
                .destination = graph->inventory.ports[branch->destination].id,
                .state = branch_active(graph, branch) ? GRIDPICO_PATCH_ROUTE_ACTIVE : GRIDPICO_PATCH_ROUTE_PENDING,
            };
        }
    }
    return GRIDPICO_OK;
}

static void read_local_registry(const gridpico_patch_graph *graph, gridpico_local_registry *registry)
{
    *registry = (gridpico_local_registry){
        .inventory = graph->inventory, .native_virtual_cable = graph->native_virtual_cable,
    };
    const size_t native_input = port_index(&graph->inventory, graph->native_virtual_cable.source);
    const size_t native_output = port_index(&graph->inventory, graph->native_virtual_cable.destination);
    if ((native_input != NO_PORT && graph->ports[native_input].apc_panel_reserved) ||
        (native_output != NO_PORT && graph->ports[native_output].apc_led_reserved)) {
        registry->native_virtual_cable = (gridpico_local_native_virtual_cable){0};
    }
    memcpy(registry->port_capabilities, graph->port_capabilities,
           graph->inventory.port_count * sizeof graph->port_capabilities[0]);
}

static gridpico_local_enabled_route read_enabled_route(
    const gridpico_patch_graph *graph, const gridpico_patch_graph_branch *branch)
{
    return (gridpico_local_enabled_route){
        .route = {
            .source = graph->inventory.ports[branch->source].id,
            .destination = graph->inventory.ports[branch->destination].id,
            .mode = branch->mode,
        },
        .state = branch_active(graph, branch) ? GRIDPICO_PATCH_ROUTE_ACTIVE : GRIDPICO_PATCH_ROUTE_PENDING,
    };
}

gridpico_result gridpico_patch_graph_read_local_state(void *context, gridpico_local_graph_state *out)
{
    const gridpico_patch_graph *graph = context;
    if (graph == NULL || graph->deliver_raw == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }

    *out = (gridpico_local_graph_state){.revision = graph->revision};
    read_local_registry(graph, &out->registry);
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (branch->enabled) {
            out->routes[out->route_count++] = read_enabled_route(graph, branch);
        }
    }
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_read_route_metadata(
    const gridpico_patch_graph *graph, gridpico_patch_graph_route_metadata *out)
{
    if (graph == NULL || graph->deliver_raw == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }
    /* Port updates still change bindings after the revision saturates. */
    if (graph->revision == 0 || graph->revision == UINT64_MAX) {
        return GRIDPICO_UNAVAILABLE;
    }

    read_local_registry(graph, &out->registry);
    out->revision = graph->revision;
    out->enabled_count = enabled_branch_count(graph);
    return GRIDPICO_OK;
}

static bool route_precedes(gridpico_local_route a, gridpico_local_route b)
{
    if (a.source != b.source) {
        return a.source < b.source;
    }
    if (a.destination != b.destination) {
        return a.destination < b.destination;
    }
    return a.mode < b.mode;
}

static bool next_enabled_route(const gridpico_patch_graph *graph,
                               const gridpico_local_enabled_route *previous,
                               gridpico_local_enabled_route *out)
{
    bool found = false;
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (!branch->enabled) {
            continue;
        }
        const gridpico_local_enabled_route candidate = read_enabled_route(graph, branch);
        if ((previous == NULL || route_precedes(previous->route, candidate.route)) &&
            (!found || route_precedes(candidate.route, out->route))) {
            *out = candidate;
            found = true;
        }
    }
    return found;
}

gridpico_result gridpico_patch_graph_read_route_page(
    const gridpico_patch_graph *graph, gridpico_revision expected_revision, size_t offset,
    gridpico_patch_graph_route_page *out)
{
    if (graph == NULL || graph->deliver_raw == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }
    if (expected_revision == 0 || expected_revision == UINT64_MAX || graph->revision != expected_revision) {
        return GRIDPICO_UNAVAILABLE;
    }
    const size_t total = enabled_branch_count(graph);
    if (offset > total) {
        return GRIDPICO_INVALID;
    }

    gridpico_patch_graph_route_page page = {.revision = expected_revision, .next_offset = offset};
    const size_t remaining = total - offset;
    const size_t count = remaining < GRIDPICO_PATCH_GRAPH_ROUTE_PAGE_RECORDS
        ? remaining : GRIDPICO_PATCH_GRAPH_ROUTE_PAGE_RECORDS;
    if (count != 0) {
        gridpico_local_enabled_route previous;
        for (size_t i = 0; i < offset + count; ++i) {
            gridpico_local_enabled_route next;
            if (!next_enabled_route(graph, i == 0 ? NULL : &previous, &next)) {
                return GRIDPICO_INVALID;
            }
            if (i >= offset) {
                page.records[page.record_count++] = next;
            }
            previous = next;
        }
        page.next_offset += page.record_count;
    }
    *out = page;
    return GRIDPICO_OK;
}

static gridpico_result reject_shadow_note(void *context, const gridpico_note_delivery *delivery)
{
    (void)context;
    (void)delivery;
    return GRIDPICO_UNAVAILABLE;
}

static gridpico_result reject_shadow_raw(void *context, const gridpico_local_raw_delivery *delivery)
{
    (void)context;
    (void)delivery;
    return GRIDPICO_UNAVAILABLE;
}

gridpico_result gridpico_patch_graph_prepare_route_shadow(
    gridpico_patch_graph *live, gridpico_revision expected_revision, gridpico_patch_graph_route_shadow *shadow)
{
    if (live == NULL || shadow == NULL || live == &shadow->graph || live->deliver == NULL ||
        live->deliver == reject_shadow_note) {
        return GRIDPICO_INVALID;
    }
    if (expected_revision == 0 || live->revision != expected_revision ||
        live->route_shadow_peer != 0 || shadow->owner_address != 0) {
        return GRIDPICO_UNAVAILABLE;
    }

    shadow->graph = *live;
    shadow->base_revision = expected_revision;
    shadow->owner_address = (uintptr_t)live;
    shadow->shadow_address = (uintptr_t)shadow;
    shadow->graph.deliver = reject_shadow_note;
    if (live->deliver_raw != NULL) {
        shadow->graph.deliver_raw = reject_shadow_raw;
    }
    shadow->graph.delivery_context = NULL;
    shadow->graph.route_shadow_peer = (uintptr_t)live;
    live->route_shadow_peer = (uintptr_t)&shadow->graph;
    return GRIDPICO_OK;
}

static bool owns_route_shadow(const gridpico_patch_graph *live, const gridpico_patch_graph_route_shadow *shadow)
{
    return live != NULL && shadow != NULL && live != &shadow->graph &&
           shadow->owner_address == (uintptr_t)live && shadow->shadow_address == (uintptr_t)shadow;
}

void gridpico_patch_graph_discard_route_shadow(
    gridpico_patch_graph *live, gridpico_patch_graph_route_shadow *shadow)
{
    if (!owns_route_shadow(live, shadow)) {
        return;
    }
    if (live->route_shadow_peer == (uintptr_t)&shadow->graph) {
        live->route_shadow_peer = 0;
    }
    if (shadow->graph.route_shadow_peer == (uintptr_t)live) {
        shadow->graph.route_shadow_peer = 0;
    }
    shadow->owner_address = 0;
    shadow->shadow_address = 0;
    shadow->base_revision = 0;
}

static bool shadow_bindings_match(const gridpico_patch_graph *live, const gridpico_patch_graph *staged)
{
    const gridpico_patch_inventory *current = &live->inventory, *saved = &staged->inventory;
    if (current->device_count != saved->device_count || current->port_count != saved->port_count ||
        live->native_virtual_cable.source != staged->native_virtual_cable.source ||
        live->native_virtual_cable.destination != staged->native_virtual_cable.destination) {
        return false;
    }
    for (size_t i = 0; i < current->device_count; ++i) {
        const gridpico_patch_device *a = &current->devices[i], *b = &saved->devices[i];
        if (a->id != b->id || a->state != b->state || strncmp(a->name, b->name, sizeof a->name) != 0) {
            return false;
        }
    }
    for (size_t i = 0; i < current->port_count; ++i) {
        const gridpico_patch_port *a = &current->ports[i], *b = &saved->ports[i];
        const gridpico_patch_port_runtime *p = &live->ports[i], *q = &staged->ports[i];
        if (a->id != b->id || a->device != b->device || a->direction != b->direction ||
            a->state != b->state || a->patch_eligible != b->patch_eligible ||
            strncmp(a->name, b->name, sizeof a->name) != 0 ||
            live->port_capabilities[i] != staged->port_capabilities[i] || p->generation != q->generation ||
            p->apc_led_reserved != q->apc_led_reserved || p->apc_panel_reserved != q->apc_panel_reserved ||
            p->ephemeral_managed_output != q->ephemeral_managed_output) {
            return false;
        }
    }
    return true;
}

static bool shadow_profiles_match(const gridpico_local_profile *live, const gridpico_local_profile *staged)
{
    return live->midi_peer_limit == staged->midi_peer_limit &&
           live->managed_branch_limit == staged->managed_branch_limit &&
           live->raw_branch_limit == staged->raw_branch_limit &&
           live->managed_fanout_limit == staged->managed_fanout_limit &&
           live->occurrence_limit == staged->occurrence_limit &&
           live->release_credit_limit == staged->release_credit_limit &&
           live->dynamic_device_limit == staged->dynamic_device_limit &&
           live->dynamic_port_limit == staged->dynamic_port_limit;
}

static bool branch_equal(const gridpico_patch_graph_branch *a, const gridpico_patch_graph_branch *b)
{
    return a->enabled == b->enabled && a->source == b->source && a->destination == b->destination &&
           a->mode == b->mode && a->transform.transpose_semitones == b->transform.transpose_semitones &&
           a->transform.output_channel == b->transform.output_channel;
}

static bool valid_shadow_routes(const gridpico_patch_graph *live, const gridpico_patch_graph *staged)
{
    size_t managed = 0, raw = 0;
    size_t managed_fanout[GRIDPICO_PATCH_MAX_PORTS] = {0};
    for (size_t i = 0; i < staged->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &staged->branches[i];
        if (staged->revision == live->revision && !branch_equal(branch, &live->branches[i])) {
            return false;
        }
        if (!branch->enabled) {
            continue;
        }
        if (branch->source >= live->inventory.port_count || branch->destination >= live->inventory.port_count ||
            !port_accepts(live, branch->source, GRIDPICO_PATCH_INPUT, branch->mode) ||
            !port_accepts(live, branch->destination, GRIDPICO_PATCH_OUTPUT, branch->mode) ||
            branch_index(staged, branch->source, branch->destination, branch->mode) != i) {
            return false;
        }
        if (branch->mode == GRIDPICO_LOCAL_MANAGED) {
            if (branch->transform.transpose_semitones < -127 || branch->transform.output_channel > 16) {
                return false;
            }
            if (live->deliver_raw != NULL && (++managed > live->local_profile.managed_branch_limit ||
                ++managed_fanout[branch->source] > live->local_profile.managed_fanout_limit)) {
                return false;
            }
        } else if (++raw > live->local_profile.raw_branch_limit ||
                   branch->transform.transpose_semitones != 0 || branch->transform.output_channel != 0) {
            return false;
        }
    }
    return true;
}

gridpico_result gridpico_patch_graph_publish_route_shadow(
    gridpico_patch_graph *live, gridpico_revision expected_revision, gridpico_patch_graph_route_shadow *shadow)
{
    if (live == NULL || shadow == NULL || live == &shadow->graph || live->deliver == NULL ||
        live->deliver == reject_shadow_note) {
        return GRIDPICO_INVALID;
    }
    const gridpico_patch_graph *staged = &shadow->graph;
    if (expected_revision == 0 || live->revision != expected_revision ||
        shadow->base_revision != expected_revision || !owns_route_shadow(live, shadow) ||
        live->route_shadow_peer != (uintptr_t)staged || staged->route_shadow_peer != (uintptr_t)live ||
        !shadow_bindings_match(live, staged)) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (staged->revision < expected_revision || staged->branch_limit != live->branch_limit ||
        staged->deliver != reject_shadow_note ||
        staged->deliver_raw != (live->deliver_raw == NULL ? NULL : reject_shadow_raw) ||
        staged->delivery_context != NULL || staged->transform_note != live->transform_note ||
        !shadow_profiles_match(&live->local_profile, &staged->local_profile) ||
        !valid_shadow_routes(live, staged)) {
        return GRIDPICO_INVALID;
    }

    memcpy(live->branches, staged->branches, sizeof live->branches);
    live->revision = staged->revision;
    gridpico_patch_graph_discard_route_shadow(live, shadow);
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_submit_note(void *context, const gridpico_ingress_note *input)
{
    gridpico_patch_graph *graph = context;
    if (graph == NULL || input == NULL || input->source.endpoint == 0 || input->source.generation == 0 ||
        !valid_note(input->note)) {
        return GRIDPICO_INVALID;
    }
    const size_t source = port_index(&graph->inventory, input->source.endpoint);
    if (!port_accepts(graph, source, GRIDPICO_PATCH_INPUT, GRIDPICO_LOCAL_MANAGED)) {
        return GRIDPICO_INVALID;
    }
    if (!source_available(graph, source, input->source.generation)) {
        return GRIDPICO_UNAVAILABLE;
    }

    gridpico_result result = GRIDPICO_OK;
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (branch->mode != GRIDPICO_LOCAL_MANAGED || branch->source != source || !branch_active(graph, branch)) {
            continue;
        }
        const size_t output = branch->destination;
        gridpico_note_delivery delivery = {
            .source = input->source,
            .destination = {graph->inventory.ports[output].id, graph->ports[output].generation},
            .note = input->note,
        };
        if (graph->transform_note != NULL &&
            !graph->transform_note(&branch->transform, &input->note, &delivery.note)) {
            continue;
        }
        if (graph->deliver(graph->delivery_context, &delivery) != GRIDPICO_OK) {
            result = GRIDPICO_UNAVAILABLE;
        }
    }
    return result;
}

static bool valid_raw(const uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES])
{
    const unsigned kind = (unsigned)bytes[0] >> 4;
    return ((kind >= 0x8 && kind <= 0xb) || kind == 0xe) && bytes[1] < 0x80 && bytes[2] < 0x80;
}

gridpico_result gridpico_patch_graph_submit_raw(void *context, const gridpico_local_raw_ingress *input)
{
    gridpico_patch_graph *graph = context;
    if (graph == NULL || graph->deliver_raw == NULL || input == NULL || input->source.generation == 0 ||
        !valid_raw(input->bytes)) {
        return GRIDPICO_INVALID;
    }
    const size_t source = port_index(&graph->inventory, input->source.endpoint);
    if (!port_accepts(graph, source, GRIDPICO_PATCH_INPUT, GRIDPICO_LOCAL_RAW)) {
        return GRIDPICO_INVALID;
    }
    if (!source_available(graph, source, input->source.generation)) {
        return GRIDPICO_UNAVAILABLE;
    }

    for (size_t i = 0; i < graph->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (branch->mode == GRIDPICO_LOCAL_RAW && branch->source == source && branch_active(graph, branch)) {
            const size_t output = branch->destination;
            gridpico_local_raw_delivery delivery = {
                .source = input->source,
                .destination = {graph->inventory.ports[output].id, graph->ports[output].generation},
            };
            memcpy(delivery.bytes, input->bytes, sizeof delivery.bytes);
            return graph->deliver_raw(graph->delivery_context, &delivery);
        }
    }
    return GRIDPICO_OK;
}
