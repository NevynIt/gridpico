#include "core/midi/patch_graph.h"

enum {
    NO_DEVICE = GRIDPICO_PATCH_DEVICES,
    NO_PORT = GRIDPICO_PATCH_PORTS,
    NO_BRANCH = GRIDPICO_PATCH_BRANCHES,
    NO_CREDIT = GRIDPICO_PATCH_BROWSER_RELEASE_CREDITS,
};

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
    for (size_t i = 0; i < GRIDPICO_PATCH_BRANCHES; ++i) {
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
                                    size_t destination, uint8_t velocity)
{
    gridpico_note_delivery release = credit->delivery;
    release.destination.generation = graph->ports[destination].generation;
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
            release_note(graph, credit, port, 0) != GRIDPICO_OK) {
            result = GRIDPICO_UNAVAILABLE;
        }
    }
    return result;
}

static void retire_branch(gridpico_patch_graph *graph, size_t index)
{
    const gridpico_port_id source = graph->inventory.ports[graph->branches[index].source].id;
    for (size_t i = 0; i < graph->occurrence_count; ++i) {
        size_t *credit = &graph->occurrences[i].credits[index];
        if (*credit != NO_CREDIT) {
            remove_contributor(&graph->credits[*credit], source);
            *credit = NO_CREDIT;
        }
    }
}

static gridpico_result retire_input(gridpico_patch_graph *graph, size_t input)
{
    const gridpico_port_id source = graph->inventory.ports[input].id;
    for (size_t i = 0; i < GRIDPICO_PATCH_BRANCHES; ++i) {
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
    for (size_t i = 0; i < GRIDPICO_PATCH_BRANCHES; ++i) {
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

static bool already_selected(const size_t *selected, size_t count, size_t credit)
{
    for (size_t i = 0; i < count; ++i) {
        if (selected[i] == credit) {
            return true;
        }
    }
    return false;
}

static size_t reserve_credit(const gridpico_patch_graph *graph, gridpico_connection destination,
                             gridpico_note note, const size_t *selected, size_t selected_count)
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

    size_t selected[GRIDPICO_PATCH_BRANCHES];
    for (size_t i = 0; i < GRIDPICO_PATCH_BRANCHES; ++i) {
        selected[i] = NO_CREDIT;
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if ((active & (1u << i)) != 0 && branch->source == source) {
            const size_t output = branch->destination;
            const gridpico_connection destination = {graph->inventory.ports[output].id,
                                                     graph->ports[output].generation};
            selected[i] = reserve_credit(graph, destination, input->note, selected, i);
            if (selected[i] == NO_CREDIT) {
                return quiesce_input(graph, source);
            }
        }
    }

    gridpico_patch_occurrence *occurrence = &graph->occurrences[graph->occurrence_count++];
    occurrence->source = input->source;
    occurrence->channel_index = input->note.channel_index;
    occurrence->key = input->note.key;

    gridpico_result result = GRIDPICO_OK;
    for (size_t i = 0; i < GRIDPICO_PATCH_BRANCHES; ++i) {
        occurrence->credits[i] = NO_CREDIT;
        if (selected[i] == NO_CREDIT) {
            continue;
        }

        gridpico_patch_release_credit *credit = &graph->credits[selected[i]];
        if (credit->contributors == 0) {
            const size_t output = graph->branches[i].destination;
            const gridpico_note_delivery delivery = {
                .source = input->source,
                .destination = {graph->inventory.ports[output].id, graph->ports[output].generation},
                .note = input->note,
            };
            if (graph->deliver(graph->delivery_context, &delivery) != GRIDPICO_OK) {
                result = GRIDPICO_UNAVAILABLE;
                continue;
            }
            credit->delivery = delivery;
        }
        ++credit->contributors;
        occurrence->credits[i] = selected[i];
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
        for (size_t j = 0; j < GRIDPICO_PATCH_BRANCHES; ++j) {
            if (occurrence.credits[j] == NO_CREDIT) {
                continue;
            }
            gridpico_patch_release_credit *credit = &graph->credits[occurrence.credits[j]];
            if (remove_contributor(credit, input->source.endpoint) &&
                release_note(graph, credit, graph->branches[j].destination, input->note.velocity) != GRIDPICO_OK) {
                result = GRIDPICO_UNAVAILABLE;
            }
        }
        return result;
    }
    return GRIDPICO_OK;
}

static gridpico_result init_graph(gridpico_patch_graph *graph, const gridpico_patch_inventory *inventory,
                                  gridpico_deliver_note_fn deliver, void *delivery_context,
                                  size_t occurrence_limit, size_t release_credit_limit)
{
    if (graph == NULL || inventory == NULL || deliver == NULL) {
        return GRIDPICO_INVALID;
    }
    if (inventory->device_count > GRIDPICO_PATCH_DEVICES || inventory->port_count > GRIDPICO_PATCH_PORTS) {
        return GRIDPICO_CAPACITY;
    }
    if (!valid_inventory(inventory)) {
        return GRIDPICO_INVALID;
    }

    *graph = (gridpico_patch_graph){
        .inventory = *inventory,
        .deliver = deliver,
        .delivery_context = delivery_context,
        .occurrence_limit = occurrence_limit,
        .release_credit_limit = release_credit_limit,
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
    return init_graph(graph, inventory, deliver, delivery_context,
                      GRIDPICO_PATCH_NOTE_OCCURRENCES, GRIDPICO_PATCH_RELEASE_CREDITS);
}

gridpico_result gridpico_patch_graph_init_browser(gridpico_patch_graph *graph,
                                                 const gridpico_patch_inventory *inventory,
                                                 gridpico_deliver_note_fn deliver,
                                                 void *delivery_context)
{
    return init_graph(graph, inventory, deliver, delivery_context,
                      GRIDPICO_PATCH_BROWSER_NOTE_OCCURRENCES, GRIDPICO_PATCH_BROWSER_RELEASE_CREDITS);
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

gridpico_result gridpico_patch_graph_set_route(void *context,
                                               const gridpico_patch_route_request *request)
{
    gridpico_patch_graph *graph = context;
    if (graph == NULL || request == NULL) {
        return GRIDPICO_INVALID;
    }
    const size_t source = port_index(&graph->inventory, request->source);
    const size_t destination = port_index(&graph->inventory, request->destination);
    if (source == NO_PORT || destination == NO_PORT ||
        graph->inventory.ports[source].direction != GRIDPICO_PATCH_INPUT ||
        graph->inventory.ports[destination].direction != GRIDPICO_PATCH_OUTPUT ||
        !graph->inventory.ports[source].patch_eligible || !graph->inventory.ports[destination].patch_eligible) {
        return GRIDPICO_INVALID;
    }

    size_t existing = NO_BRANCH;
    size_t free_branch = NO_BRANCH;
    for (size_t i = 0; i < GRIDPICO_PATCH_BRANCHES; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (branch->enabled && branch->source == source && branch->destination == destination) {
            existing = i;
            break;
        }
        if (!branch->enabled && free_branch == NO_BRANCH) {
            free_branch = i;
        }
    }
    if (request->enabled == (existing != NO_BRANCH)) {
        return GRIDPICO_OK;
    }
    if (graph->revision == UINT64_MAX || (request->enabled && free_branch == NO_BRANCH)) {
        return GRIDPICO_CAPACITY;
    }
    if (request->enabled) {
        graph->branches[free_branch] = (gridpico_patch_graph_branch){source, destination, true};
        ++graph->revision;
        return GRIDPICO_OK;
    }

    const unsigned previous_active = active_branches(graph);
    retire_branch(graph, existing);
    reconcile_releases(graph, request->destination, request->source);
    if (has_owed_release(graph, request->destination, request->source)) {
        publish_change(graph, previous_active, false);
        return GRIDPICO_UNAVAILABLE;
    }
    graph->branches[existing].enabled = false;
    ++graph->revision;
    return GRIDPICO_OK;
}

gridpico_result gridpico_patch_graph_read_graph(void *context, gridpico_patch_graph_state *out)
{
    const gridpico_patch_graph *graph = context;
    if (graph == NULL || out == NULL) {
        return GRIDPICO_INVALID;
    }
    *out = (gridpico_patch_graph_state){.revision = graph->revision, .inventory = graph->inventory};
    for (size_t i = 0; i < GRIDPICO_PATCH_BRANCHES; ++i) {
        const gridpico_patch_graph_branch *branch = &graph->branches[i];
        if (branch->enabled) {
            out->branches[out->branch_count++] = (gridpico_patch_branch){
                .source = graph->inventory.ports[branch->source].id,
                .destination = graph->inventory.ports[branch->destination].id,
                .state = branch_active(graph, branch) ? GRIDPICO_PATCH_ROUTE_ACTIVE : GRIDPICO_PATCH_ROUTE_PENDING,
            };
        }
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
    if (source == NO_PORT || graph->inventory.ports[source].direction != GRIDPICO_PATCH_INPUT ||
        !graph->inventory.ports[source].patch_eligible) {
        return GRIDPICO_INVALID;
    }
    if (graph->inventory.ports[source].state != GRIDPICO_PATCH_CONNECTED || graph->ports[source].quiesced ||
        graph->ports[source].generation != input->source.generation) {
        return GRIDPICO_UNAVAILABLE;
    }

    const unsigned previous_active = active_branches(graph);
    const gridpico_result result = input->note.kind == GRIDPICO_NOTE_ON
        ? submit_press(graph, source, input, previous_active) : submit_release(graph, input);
    publish_change(graph, previous_active, false);
    return result;
}
