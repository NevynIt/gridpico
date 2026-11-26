#include "core/midi/patch_graph.h"

#include <limits.h>
#include <string.h>

enum {
    NO_DEVICE = GRIDPICO_PATCH_MAX_DEVICES,
    NO_PORT = GRIDPICO_PATCH_MAX_PORTS,
    NO_BRANCH = GRIDPICO_PATCH_MAX_BRANCHES,
    NO_CREDIT = GRIDPICO_LOCAL_MAX_RELEASE_CREDITS,
    LOCAL_MANAGED_BRANCHES = 2,
    LOCAL_RAW_BRANCHES = 1,
};

_Static_assert(GRIDPICO_PATCH_MAX_BRANCHES <= sizeof(unsigned) * CHAR_BIT,
               "branch mask must cover the configured branch backing");
_Static_assert(GRIDPICO_PATCH_MAX_BRANCHES <= UINT8_MAX && GRIDPICO_LOCAL_MAX_RELEASE_CREDITS <= UINT8_MAX &&
                   GRIDPICO_LOCAL_MAX_NOTE_OCCURRENCES <= UINT8_MAX && GRIDPICO_LOCAL_MAX_FANOUT <= UINT8_MAX,
               "note contribution indices and counts must fit in uint8_t");
_Static_assert((int)GRIDPICO_PATCH_MONITOR_BRANCHES <= (int)GRIDPICO_LOCAL_MAX_FANOUT,
               "note contribution backing must cover the Monitor profile");

typedef struct {
    size_t devices;
    size_t ports;
    size_t branches;
    size_t occurrences;
    size_t release_credits;
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
           !graph->ports[index].quiesced && graph->ports[index].generation == generation;
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

static bool owed_to(const gridpico_patch_release_credit *credit, gridpico_port_id destination,
                    gridpico_port_id source)
{
    return credit->owed_source != 0 &&
           (destination == 0 || credit->delivery.destination.endpoint == destination) &&
           (source == 0 || credit->owed_source == source);
}

static bool has_owed_release(const gridpico_patch_graph *graph, gridpico_port_id destination,
                             gridpico_port_id source)
{
    for (size_t i = 0; i < graph->release_credit_limit; ++i) {
        if (owed_to(&graph->credits[i], destination, source)) {
            return true;
        }
    }
    return false;
}

bool gridpico_patch_graph_destination_idle(const gridpico_patch_graph *graph, gridpico_port_id destination)
{
    if (graph == NULL) {
        return false;
    }
    const size_t port = port_index(&graph->inventory, destination);
    if (port == NO_PORT || graph->inventory.ports[port].direction != GRIDPICO_PATCH_OUTPUT) {
        return false;
    }

    for (size_t i = 0; i < graph->release_credit_limit; ++i) {
        const gridpico_patch_release_credit *credit = &graph->credits[i];
        if (credit->delivery.destination.endpoint == destination &&
            (credit->contributors != 0 || credit->owed_source != 0)) {
            return false;
        }
    }
    return true;
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
        if (!gridpico_patch_graph_destination_idle(graph, output)) {
            return GRIDPICO_UNAVAILABLE;
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

static bool branch_active(const gridpico_patch_graph *graph, const gridpico_patch_graph_branch *branch)
{
    const gridpico_patch_port *output = &graph->inventory.ports[branch->destination];
    return branch->enabled && graph->inventory.ports[branch->source].state == GRIDPICO_PATCH_CONNECTED &&
           !graph->ports[branch->source].quiesced && output->state == GRIDPICO_PATCH_CONNECTED &&
           !has_owed_release(graph, output->id, 0);
}

static unsigned active_branches(const gridpico_patch_graph *graph)
{
    unsigned active = 0;
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        if (branch_active(graph, &graph->branches[i])) {
            active |= 1u << i;
        }
    }
    return active;
}

static void publish_change(gridpico_patch_graph *graph, unsigned previous_active, bool changed)
{
    if ((changed || previous_active != active_branches(graph)) && graph->revision != UINT64_MAX) {
        ++graph->revision;
    }
}

static bool remove_contributor(gridpico_patch_release_credit *credit, gridpico_port_id source)
{
    if (--credit->contributors != 0) {
        return false;
    }

    credit->owed_source = source;
    return true;
}

static gridpico_result release_note(gridpico_patch_graph *graph, gridpico_patch_release_credit *credit,
                                    uint32_t generation, uint8_t velocity)
{
    gridpico_note_delivery release = credit->delivery;
    release.destination.generation = generation;
    release.note.kind = GRIDPICO_NOTE_OFF;
    release.note.velocity = velocity;
    if (graph->deliver(graph->delivery_context, &release) != GRIDPICO_OK) {
        return GRIDPICO_UNAVAILABLE;
    }

    *credit = (gridpico_patch_release_credit){0};
    return GRIDPICO_OK;
}

static gridpico_result reconcile_releases(gridpico_patch_graph *graph, gridpico_port_id destination,
                                           gridpico_port_id source)
{
    gridpico_result result = GRIDPICO_OK;
    for (size_t i = 0; i < graph->release_credit_limit; ++i) {
        gridpico_patch_release_credit *credit = &graph->credits[i];
        if (!owed_to(credit, destination, source)) {
            continue;
        }
        const size_t port = port_index(&graph->inventory, credit->delivery.destination.endpoint);
        if (graph->inventory.ports[port].state == GRIDPICO_PATCH_CONNECTED &&
            release_note(graph, credit, graph->ports[port].generation, 0) != GRIDPICO_OK) {
            result = GRIDPICO_UNAVAILABLE;
        }
    }
    return result;
}

static void retire_branch(gridpico_patch_graph *graph, size_t index)
{
    const gridpico_port_id source = graph->inventory.ports[graph->branches[index].source].id;
    for (size_t i = 0; i < graph->occurrence_count; ++i) {
        gridpico_patch_occurrence *occurrence = &graph->occurrences[i];
        for (size_t j = 0; j < occurrence->contribution_count; ++j) {
            const gridpico_patch_note_contribution member = occurrence->contributions[j];
            if (member.branch != index) {
                continue;
            }
            remove_contributor(&graph->credits[member.credit], source);
            --occurrence->contribution_count;
            for (size_t k = j; k < occurrence->contribution_count; ++k) {
                occurrence->contributions[k] = occurrence->contributions[k + 1];
            }
            break;
        }
    }
}

static gridpico_result retire_managed_route(gridpico_patch_graph *graph, size_t branch,
                                            const gridpico_local_route *route)
{
    const unsigned previous_active = active_branches(graph);
    retire_branch(graph, branch);
    reconcile_releases(graph, route->destination, route->source);
    if (has_owed_release(graph, route->destination, route->source)) {
        publish_change(graph, previous_active, false);
        return GRIDPICO_UNAVAILABLE;
    }
    return GRIDPICO_OK;
}

static gridpico_result retire_input(gridpico_patch_graph *graph, size_t input)
{
    const gridpico_port_id source = graph->inventory.ports[input].id;
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        if (graph->branches[i].enabled && graph->branches[i].source == input) {
            retire_branch(graph, i);
        }
    }

    size_t kept = 0;
    for (size_t i = 0; i < graph->occurrence_count; ++i) {
        if (graph->occurrences[i].source.endpoint != source) {
            graph->occurrences[kept++] = graph->occurrences[i];
        }
    }
    graph->occurrence_count = kept;
    return reconcile_releases(graph, 0, source);
}

static void retire_output(gridpico_patch_graph *graph, size_t output)
{
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        if (graph->branches[i].enabled && graph->branches[i].destination == output) {
            retire_branch(graph, i);
        }
    }
}

static gridpico_result quiesce_input(gridpico_patch_graph *graph, size_t input)
{
    graph->ports[input].quiesced = true;
    retire_input(graph, input);
    return GRIDPICO_CAPACITY;
}

static bool valid_note(gridpico_note note)
{
    return note.channel_index < 16 && note.key < 128 && note.velocity < 128 &&
           (note.kind == GRIDPICO_NOTE_OFF ||
            (note.kind == GRIDPICO_NOTE_ON && note.velocity != 0));
}

typedef struct {
    gridpico_patch_note_contribution contribution;
    gridpico_note note;
} selected_attack;

static bool already_selected(const selected_attack *selected, size_t count, size_t credit)
{
    for (size_t i = 0; i < count; ++i) {
        if (selected[i].contribution.credit == credit) {
            return true;
        }
    }
    return false;
}

static size_t reserve_credit(const gridpico_patch_graph *graph, gridpico_connection destination,
                             gridpico_note note, const selected_attack *selected,
                             size_t selected_count)
{
    size_t free_credit = NO_CREDIT;
    for (size_t i = 0; i < graph->release_credit_limit; ++i) {
        const gridpico_patch_release_credit *credit = &graph->credits[i];
        if (credit->contributors != 0 && credit->delivery.destination.endpoint == destination.endpoint &&
            credit->delivery.destination.generation == destination.generation &&
            credit->delivery.note.channel_index == note.channel_index &&
            credit->delivery.note.key == note.key) {
            return i;
        }
        if (free_credit == NO_CREDIT && credit->contributors == 0 && credit->owed_source == 0 &&
            !already_selected(selected, selected_count, i)) {
            free_credit = i;
        }
    }
    return free_credit;
}

static gridpico_result submit_press(gridpico_patch_graph *graph, size_t source,
                                    const gridpico_ingress_note *input, unsigned active)
{
    if (graph->occurrence_count == graph->occurrence_limit) {
        return quiesce_input(graph, source);
    }

    selected_attack selected[GRIDPICO_LOCAL_MAX_FANOUT];
    size_t selected_count = 0;
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if ((active & (1u << i)) == 0 || branch->source != source || branch->mode != GRIDPICO_LOCAL_MANAGED) {
            continue;
        }
        gridpico_note note = input->note;
        if (graph->transform_note != NULL && !graph->transform_note(&branch->transform, &input->note, &note)) {
            continue;
        }
        if (selected_count == GRIDPICO_LOCAL_MAX_FANOUT) {
            return quiesce_input(graph, source);
        }
        const size_t output = branch->destination;
        const gridpico_connection destination = {graph->inventory.ports[output].id,
                                                 graph->ports[output].generation};
        const size_t credit = reserve_credit(graph, destination, note, selected, selected_count);
        if (credit == NO_CREDIT) {
            return quiesce_input(graph, source);
        }
        selected[selected_count++] = (selected_attack){
            .contribution = {(uint8_t)i, (uint8_t)credit},
            .note = note,
        };
    }

    gridpico_patch_occurrence *occurrence = &graph->occurrences[graph->occurrence_count++];
    *occurrence = (gridpico_patch_occurrence){
        .source = input->source,
        .channel_index = input->note.channel_index,
        .key = input->note.key,
    };

    gridpico_result result = GRIDPICO_OK;
    for (size_t i = 0; i < selected_count; ++i) {
        const gridpico_patch_note_contribution member = selected[i].contribution;
        gridpico_patch_release_credit *credit = &graph->credits[member.credit];
        if (credit->contributors == 0) {
            const size_t output = graph->branches[member.branch].destination;
            const gridpico_note_delivery delivery = {
                .source = input->source,
                .destination = {graph->inventory.ports[output].id, graph->ports[output].generation},
                .note = selected[i].note,
            };
            if (graph->deliver(graph->delivery_context, &delivery) != GRIDPICO_OK) {
                result = GRIDPICO_UNAVAILABLE;
                continue;
            }
            credit->delivery = delivery;
        }
        ++credit->contributors;
        occurrence->contributions[occurrence->contribution_count++] = member;
    }
    return result;
}

static gridpico_result submit_release(gridpico_patch_graph *graph, const gridpico_ingress_note *input)
{
    for (size_t i = 0; i < graph->occurrence_count; ++i) {
        const gridpico_patch_occurrence occurrence = graph->occurrences[i];
        if (occurrence.source.endpoint != input->source.endpoint ||
            occurrence.source.generation != input->source.generation ||
            occurrence.channel_index != input->note.channel_index || occurrence.key != input->note.key) {
            continue;
        }

        --graph->occurrence_count;
        for (size_t j = i; j < graph->occurrence_count; ++j) {
            graph->occurrences[j] = graph->occurrences[j + 1];
        }

        gridpico_result result = GRIDPICO_OK;
        for (size_t j = 0; j < occurrence.contribution_count; ++j) {
            const gridpico_patch_note_contribution member = occurrence.contributions[j];
            gridpico_patch_release_credit *credit = &graph->credits[member.credit];
            if (remove_contributor(credit, input->source.endpoint) &&
                release_note(graph, credit, credit->delivery.destination.generation,
                             input->note.velocity) != GRIDPICO_OK) {
                result = GRIDPICO_UNAVAILABLE;
            }
        }
        return result;
    }
    return GRIDPICO_OK;
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
        .occurrence_limit = limits.occurrences,
        .release_credit_limit = limits.release_credits,
    };
    for (size_t i = 0; i < graph->inventory.port_count; ++i) {
        graph->ports[i].generation = 1;
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
        .occurrences = GRIDPICO_PATCH_NOTE_OCCURRENCES,
        .release_credits = GRIDPICO_PATCH_RELEASE_CREDITS,
    };
    return init_graph(graph, inventory, deliver, delivery_context, limits);
}

gridpico_result gridpico_patch_graph_init_browser(gridpico_patch_graph *graph,
                                                 const gridpico_patch_inventory *inventory,
                                                 gridpico_deliver_note_fn deliver,
                                                 void *delivery_context)
{
    const graph_limits limits = {
        .devices = GRIDPICO_PATCH_DEVICES,
        .ports = GRIDPICO_PATCH_PORTS,
        .branches = GRIDPICO_PATCH_BRANCHES,
        .occurrences = GRIDPICO_PATCH_BROWSER_NOTE_OCCURRENCES,
        .release_credits = GRIDPICO_PATCH_BROWSER_RELEASE_CREDITS,
    };
    return init_graph(graph, inventory, deliver, delivery_context, limits);
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
        .occurrences = GRIDPICO_PATCH_MONITOR_NOTE_OCCURRENCES,
        .release_credits = GRIDPICO_PATCH_MONITOR_RELEASE_CREDITS,
    };
    return init_graph(graph, inventory, deliver, delivery_context, limits);
}

static gridpico_result init_local_graph(gridpico_patch_graph *graph, const gridpico_local_registry *registry,
                                        gridpico_local_profile profile, gridpico_deliver_note_fn deliver_note,
                                        gridpico_local_deliver_raw_fn deliver_raw, void *delivery_context,
                                        bool configured)
{
    if (graph == NULL || registry == NULL || deliver_note == NULL || deliver_raw == NULL) {
        return GRIDPICO_INVALID;
    }
    graph_limits limits = {
        .devices = configured ? GRIDPICO_PATCH_MAX_DEVICES : GRIDPICO_LOCAL_DEVICES,
        .ports = configured ? GRIDPICO_PATCH_MAX_PORTS : GRIDPICO_LOCAL_PORTS,
        .occurrences = profile.occurrence_limit,
        .release_credits = profile.release_credit_limit,
    };
    if (configured && profile.midi_peer_limit == 0) {
        return GRIDPICO_INVALID;
    }
    const bool dynamic = profile.dynamic_device_limit != 0;
    if (dynamic != (profile.dynamic_port_limit != 0)) {
        return GRIDPICO_INVALID;
    }
    if (registry->inventory.device_count > limits.devices || registry->inventory.port_count > limits.ports ||
        (configured && profile.midi_peer_limit > registry->inventory.port_count) ||
        profile.managed_branch_limit > GRIDPICO_LOCAL_MAX_MANAGED_BRANCHES ||
        profile.raw_branch_limit > GRIDPICO_LOCAL_MAX_RAW_BRANCHES ||
        profile.managed_fanout_limit > GRIDPICO_LOCAL_MAX_FANOUT ||
        profile.occurrence_limit > GRIDPICO_LOCAL_MAX_NOTE_OCCURRENCES ||
        profile.release_credit_limit > GRIDPICO_LOCAL_MAX_RELEASE_CREDITS) {
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
        .occurrence_limit = GRIDPICO_LOCAL_NOTE_OCCURRENCES,
        .release_credit_limit = GRIDPICO_LOCAL_RELEASE_CREDITS,
    };
    return init_local_graph(graph, registry, profile, deliver_note, deliver_raw, delivery_context, false);
}

gridpico_result gridpico_patch_graph_init_local_profile(
    gridpico_patch_graph *graph, const gridpico_local_registry *registry,
    const gridpico_local_profile *profile, gridpico_deliver_note_fn deliver_note,
    gridpico_local_deliver_raw_fn deliver_raw, void *delivery_context)
{
    if (profile == NULL) {
        return GRIDPICO_INVALID;
    }
    return init_local_graph(graph, registry, *profile, deliver_note, deliver_raw, delivery_context, true);
}

gridpico_result gridpico_patch_graph_append_managed_device(
    gridpico_patch_graph *graph, const gridpico_patch_device *device,
    const gridpico_patch_port *ports, size_t port_count)
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

    inventory->devices[inventory->device_count++] = *device;
    memmove(&inventory->ports[first_port], ports, port_count * sizeof ports[0]);
    for (size_t i = first_port; i < first_port + port_count; ++i) {
        graph->port_capabilities[i] = GRIDPICO_LOCAL_PORT_MANAGED;
        graph->ports[i] = (gridpico_patch_port_runtime){.generation = 1};
    }
    inventory->port_count += port_count;
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
    if (update->state == GRIDPICO_PATCH_CONNECTED && port->state != GRIDPICO_PATCH_CONNECTED) {
        if (generation <= runtime->generation) {
            return GRIDPICO_UNAVAILABLE;
        }
    } else if (generation != runtime->generation) {
        return GRIDPICO_UNAVAILABLE;
    }

    const unsigned previous_active = active_branches(graph);
    const bool changed = port->state != update->state || generation != runtime->generation;
    port->state = update->state;
    runtime->generation = generation;
    gridpico_result result = GRIDPICO_OK;
    if (port->state == GRIDPICO_PATCH_CONNECTED) {
        if (changed) {
            runtime->quiesced = false;
        }
        if (port->direction == GRIDPICO_PATCH_OUTPUT) {
            result = reconcile_releases(graph, port->id, 0);
        }
    } else if (changed) {
        if (port->direction == GRIDPICO_PATCH_INPUT) {
            result = retire_input(graph, index);
        } else {
            retire_output(graph, index);
        }
    }
    if (changed) {
        aggregate_devices(&graph->inventory);
    }
    publish_change(graph, previous_active, changed);
    return result;
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

static size_t parent_device(const gridpico_patch_graph *graph, size_t port)
{
    return device_index(&graph->inventory, graph->inventory.ports[port].device);
}

static bool creates_device_cycle(const gridpico_patch_graph *graph, size_t source, size_t destination)
{
    bool reachable[GRIDPICO_PATCH_MAX_DEVICES] = {false};
    reachable[parent_device(graph, destination)] = true;

    /* Walk desired edges so an unavailable endpoint cannot hide a cycle. */
    for (size_t pass = 0; pass < graph->inventory.device_count; ++pass) {
        for (size_t i = 0; i < graph->branch_limit; ++i) {
            const gridpico_patch_graph_branch *branch = &graph->branches[i];
            if (branch->enabled && reachable[parent_device(graph, branch->source)]) {
                reachable[parent_device(graph, branch->destination)] = true;
            }
        }
    }
    return reachable[parent_device(graph, source)];
}

static gridpico_result enable_branch(gridpico_patch_graph *graph, size_t source, size_t destination,
                                     gridpico_local_route_mode mode)
{
    const bool terminal_cable = mode == GRIDPICO_LOCAL_MANAGED && gridpico_local_native_virtual_cable_matches(
        &graph->native_virtual_cable, graph->inventory.ports[source].id, graph->inventory.ports[destination].id);
    if (graph->deliver_raw != NULL && !terminal_cable && creates_device_cycle(graph, source, destination)) {
        return GRIDPICO_INVALID;
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
        return GRIDPICO_CAPACITY;
    }
    if (graph->deliver_raw != NULL) {
        const gridpico_local_profile *profile = &graph->local_profile;
        const bool managed = mode == GRIDPICO_LOCAL_MANAGED;
        const size_t mode_limit = managed ? profile->managed_branch_limit : profile->raw_branch_limit;
        if (mode_count >= mode_limit || (managed && source_count >= profile->managed_fanout_limit)) {
            return GRIDPICO_CAPACITY;
        }
    }
    const gridpico_patch_graph_branch branch = {
        .source = source, .destination = destination, .mode = mode, .enabled = true,
    };
    if (mode == GRIDPICO_LOCAL_RAW && !branch_active(graph, &branch)) {
        return GRIDPICO_UNAVAILABLE;
    }
    graph->branches[free_branch] = branch;
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
    if (graph->revision == UINT64_MAX) {
        return GRIDPICO_CAPACITY;
    }
    if (enabled) {
        return enable_branch(graph, source, destination, route->mode);
    }

    if (route->mode == GRIDPICO_LOCAL_MANAGED) {
        const gridpico_result result = retire_managed_route(graph, existing, route);
        if (result != GRIDPICO_OK) {
            return result;
        }
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
    const gridpico_result cleanup = retire_managed_route(graph, index, route);
    if (cleanup == GRIDPICO_OK) {
        *current = *transform;
        ++graph->revision;
    }
    return cleanup;
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

gridpico_result gridpico_patch_graph_read_local_state(void *context, gridpico_local_graph_state *out)
{
    const gridpico_patch_graph *graph = context;
    if (graph == NULL || graph->deliver_raw == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }

    *out = (gridpico_local_graph_state){
        .revision = graph->revision,
        .registry = {.inventory = graph->inventory, .native_virtual_cable = graph->native_virtual_cable},
    };
    const size_t native_output = port_index(&graph->inventory, graph->native_virtual_cable.destination);
    if (native_output != NO_PORT && graph->ports[native_output].apc_led_reserved) {
        out->registry.native_virtual_cable = (gridpico_local_native_virtual_cable){0};
    }
    memcpy(out->registry.port_capabilities, graph->port_capabilities,
           graph->inventory.port_count * sizeof graph->port_capabilities[0]);
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (!branch->enabled) {
            continue;
        }
        out->routes[out->route_count++] = (gridpico_local_enabled_route){
            .route = {
                .source = graph->inventory.ports[branch->source].id,
                .destination = graph->inventory.ports[branch->destination].id,
                .mode = branch->mode,
            },
            .state = branch_active(graph, branch) ? GRIDPICO_PATCH_ROUTE_ACTIVE : GRIDPICO_PATCH_ROUTE_PENDING,
        };
    }
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

    const unsigned previous_active = active_branches(graph);
    const gridpico_result result = input->note.kind == GRIDPICO_NOTE_ON
        ? submit_press(graph, source, input, previous_active) : submit_release(graph, input);
    publish_change(graph, previous_active, false);
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
