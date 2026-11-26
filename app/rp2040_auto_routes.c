#include "app/rp2040_auto_routes.h"

#ifdef GRIDPICO_PICO_GENERIC_MIDI

#include <string.h>

_Static_assert(GRIDPICO_PICO_USB_CABLE_GROUPS <= 32, "lane mask fits one word");

static uint64_t attachment_epoch(const gridpico_pico_router *router)
{
    uint64_t epoch = 0;
    for (unsigned peer = GRIDPICO_PICO_WEB; peer < GRIDPICO_PICO_PEERS; ++peer) {
        epoch += router->generation[peer];
    }
    return epoch;
}

static bool state_current(const gridpico_pico_auto_routes *plan)
{
    const gridpico_pico_router *router = plan->router;
    return !plan->dirty && plan->graph_revision == router->graph.revision &&
           plan->color_revision == router->session.color_revision &&
           plan->attachment_epoch == attachment_epoch(router) &&
           memcmp(&plan->manual_routes, &router->manual_routes, sizeof(plan->manual_routes)) == 0;
}

void gridpico_pico_auto_routes_init(gridpico_pico_auto_routes *plan, gridpico_pico_router *router,
                                   gridpico_local_routing_snapshot *routing_scratch)
{
    *plan = (gridpico_pico_auto_routes){.router = router, .routing_scratch = routing_scratch, .dirty = true};
}

void gridpico_pico_auto_routes_invalidate(gridpico_pico_auto_routes *plan)
{
    plan->dirty = true;
    plan->ready = false;
}

static void append_ordered(const gridpico_pico_auto_routes *plan, uint8_t *slots, uint8_t *count, uint8_t slot)
{
    size_t i = *count;
    while (i > 0 && plan->ids[slots[i - 1]] > plan->ids[slot]) {
        slots[i] = slots[i - 1];
        --i;
    }
    slots[i] = slot;
    ++*count;
}

static void start_plan(gridpico_pico_auto_routes *plan, const gridpico_local_registry *registry,
                       uint32_t blocked_inputs, uint32_t blocked_outputs)
{
    const gridpico_pico_router *router = plan->router;
    const uint64_t epoch = attachment_epoch(router);
    /* Restarting a stale projection must preserve its unfinished attach grant. */
    plan->enable = plan->enable || plan->attachment_epoch != epoch;
    plan->attachment_epoch = epoch;
    plan->graph_revision = router->graph.revision;
    plan->color_revision = router->session.color_revision;
    plan->manual_routes = router->manual_routes;
    plan->blocked_inputs = blocked_inputs;
    plan->blocked_outputs = blocked_outputs;
    plan->source_count = plan->destination_count = plan->sweep = 0;
    plan->next = 0;
    plan->ready = plan->dirty = false;
    plan->summary = (gridpico_auto_route_summary){.revision = plan->summary.revision};
    const gridpico_patch_inventory *inventory = &registry->inventory;
    for (uint8_t slot = 0; slot < inventory->port_count; ++slot) {
        const gridpico_patch_port *port = &inventory->ports[slot];
        if (!port->patch_eligible || port->id == router->session.apc_audition_source ||
            registry->port_capabilities[slot] != GRIDPICO_LOCAL_PORT_MANAGED) continue;
        plan->ids[slot] = port->id;
        if (port->direction == GRIDPICO_PATCH_INPUT) {
            append_ordered(plan, plan->sources, &plan->source_count, slot);
        } else {
            append_ordered(plan, plan->destinations, &plan->destination_count, slot);
        }
    }
    const uint16_t pairs = (uint16_t)((unsigned)plan->source_count * plan->destination_count);
    plan->summary.candidates = pairs;
    memset(plan->decisions, 0, pairs);
}

static bool lane_blocked(const gridpico_pico_router *router, gridpico_port_id id, bool output, uint32_t mask)
{
    for (unsigned slot = 0; mask != 0 && slot < GRIDPICO_PICO_USB_CABLE_GROUPS; ++slot) {
        if ((mask & (UINT32_C(1) << slot)) &&
            id == (output ? router->discovered[slot].output : router->discovered[slot].input)) return true;
    }
    return false;
}

static gridpico_auto_route_skip_reason admission_reason(const gridpico_pico_router *router,
                                                        const gridpico_local_graph_state *state,
                                                        const gridpico_local_route *route)
{
    gridpico_patch_managed_enable_decision decision;
    if (gridpico_patch_graph_check_managed_enable(&router->graph, route, &decision) != GRIDPICO_OK) {
        return GRIDPICO_AUTO_SKIP_UNAVAILABLE;
    }
    switch (decision) {
    case GRIDPICO_PATCH_MANAGED_ENABLE_CYCLE: return GRIDPICO_AUTO_SKIP_CYCLE;
    case GRIDPICO_PATCH_MANAGED_ENABLE_BRANCH_LIMIT: return GRIDPICO_AUTO_SKIP_BRANCH_LIMIT;
    case GRIDPICO_PATCH_MANAGED_ENABLE_FANOUT_LIMIT: return GRIDPICO_AUTO_SKIP_FANOUT_LIMIT;
    case GRIDPICO_PATCH_MANAGED_ENABLE_REVISION_EXHAUSTED: return GRIDPICO_AUTO_SKIP_UNAVAILABLE;
    case GRIDPICO_PATCH_MANAGED_ENABLE_ALLOWED: break;
    }
    size_t public_branches = 0;
    for (size_t i = 0; i < state->route_count; ++i) {
        const gridpico_local_route *existing = &state->routes[i].route;
        if (existing->mode == GRIDPICO_LOCAL_MANAGED && existing->source != router->session.apc_audition_source) {
            ++public_branches;
        }
    }
    return public_branches + 1u >= router->graph.local_profile.managed_branch_limit ?
        GRIDPICO_AUTO_SKIP_AUDITION_RESERVED : 0;
}

static gridpico_auto_route_skip_reason evaluate_pair(gridpico_pico_auto_routes *plan,
                                                      const gridpico_local_graph_state *state,
                                                      uint8_t source_slot, uint8_t destination_slot)
{
    gridpico_pico_router *router = plan->router;
    const gridpico_patch_port *source = &state->registry.inventory.ports[source_slot];
    const gridpico_patch_port *destination = &state->registry.inventory.ports[destination_slot];
    const gridpico_local_route route = {source->id, destination->id, GRIDPICO_LOCAL_MANAGED};
    for (size_t i = 0; i < state->route_count; ++i) {
        const gridpico_local_enabled_route *existing = &state->routes[i];
        if (existing->route.mode != route.mode || existing->route.source != route.source ||
            existing->route.destination != route.destination) continue;
        if (existing->state == GRIDPICO_PATCH_ROUTE_ACTIVE) ++plan->summary.active;
        else ++plan->summary.pending;
        return 0;
    }
    if (source->device == destination->device) {
        return gridpico_local_native_virtual_cable_matches(&state->registry.native_virtual_cable,
            source->id, destination->id) ? GRIDPICO_AUTO_SKIP_COMPUTER_RETURN : GRIDPICO_AUTO_SKIP_SAME_PARENT;
    }
    if (gridpico_patch_session_manual_route_is_off(&router->session, source_slot, destination_slot)) {
        return GRIDPICO_AUTO_SKIP_USER_OFF;
    }
    if (source->state != GRIDPICO_PATCH_CONNECTED || destination->state != GRIDPICO_PATCH_CONNECTED ||
        lane_blocked(router, source->id, false, plan->blocked_inputs) ||
        lane_blocked(router, destination->id, true, plan->blocked_outputs)) return GRIDPICO_AUTO_SKIP_UNAVAILABLE;
    const gridpico_auto_route_skip_reason reason = admission_reason(router, state, &route);
    if (reason != 0) return reason;
    if (!plan->enable) return GRIDPICO_AUTO_SKIP_UNAVAILABLE;
    const gridpico_local_route_request request = {route, true};
    if (gridpico_patch_graph_set_local_route(&router->graph, &request) != GRIDPICO_OK) {
        return GRIDPICO_AUTO_SKIP_UNAVAILABLE;
    }
    gridpico_local_route_status committed;
    if (gridpico_patch_graph_read_local_route(&router->graph, &route, &committed) != GRIDPICO_OK) {
        return GRIDPICO_AUTO_SKIP_UNAVAILABLE;
    }
    plan->graph_revision = router->graph.revision;
    if (committed.state == GRIDPICO_PATCH_ROUTE_ACTIVE) ++plan->summary.active;
    else ++plan->summary.pending;
    return 0;
}

static void record_skip(gridpico_pico_auto_routes *plan, uint16_t index, gridpico_auto_route_skip_reason reason)
{
    plan->decisions[index] = (uint8_t)reason;
    if (reason == 0) return;
    switch (reason) {
    case GRIDPICO_AUTO_SKIP_USER_OFF: ++plan->summary.user_off; break;
    case GRIDPICO_AUTO_SKIP_COMPUTER_RETURN:
    case GRIDPICO_AUTO_SKIP_SAME_PARENT:
    case GRIDPICO_AUTO_SKIP_KNOWN_FEEDBACK:
    case GRIDPICO_AUTO_SKIP_CYCLE: ++plan->summary.feedback; break;
    case GRIDPICO_AUTO_SKIP_AUDITION_RESERVED:
    case GRIDPICO_AUTO_SKIP_BRANCH_LIMIT:
    case GRIDPICO_AUTO_SKIP_FANOUT_LIMIT: ++plan->summary.limit; break;
    case GRIDPICO_AUTO_SKIP_UNAVAILABLE: ++plan->summary.unavailable; break;
    }
}

static bool usb_only(const gridpico_local_registry *registry, gridpico_port_id source, gridpico_port_id destination)
{
    return registry->native_virtual_cable.source != source && registry->native_virtual_cable.destination != destination;
}

void gridpico_pico_auto_routes_service(gridpico_pico_auto_routes *plan, bool coherent,
                                      uint32_t blocked_inputs, uint32_t blocked_outputs)
{
    plan->coherent = coherent;
    if (!coherent || plan->summary.revision == UINT64_MAX) return;
    if (state_current(plan) && blocked_inputs == plan->blocked_inputs &&
        blocked_outputs == plan->blocked_outputs && plan->ready) return;

    gridpico_local_graph_state *state = &plan->router->projection_scratch.graph;
    if (gridpico_patch_graph_read_local_state(&plan->router->graph, state) != GRIDPICO_OK) {
        gridpico_pico_auto_routes_invalidate(plan);
        return;
    }
    if (!state_current(plan) || blocked_inputs != plan->blocked_inputs || blocked_outputs != plan->blocked_outputs) {
        start_plan(plan, &state->registry, blocked_inputs, blocked_outputs);
    }
    if (plan->next >= plan->summary.candidates) {
        if (++plan->sweep == 2 || plan->summary.candidates == 0) {
            if (gridpico_pico_router_read_routing(plan->router, plan->routing_scratch) == GRIDPICO_OK) {
                plan->routing_revision = plan->routing_scratch->revision;
                ++plan->summary.revision;
                plan->enable = false;
                plan->ready = true;
            } else {
                gridpico_pico_auto_routes_invalidate(plan);
            }
            return;
        }
        plan->next = 0;
    }
    const uint16_t index = plan->next++;
    const uint8_t source = plan->sources[index / plan->destination_count];
    const uint8_t destination = plan->destinations[index % plan->destination_count];
    if (usb_only(&state->registry, plan->ids[source], plan->ids[destination]) == (plan->sweep == 0)) {
        const gridpico_auto_route_skip_reason reason = evaluate_pair(plan, state, source, destination);
        record_skip(plan, index, reason);
    }
}

gridpico_result gridpico_pico_auto_routes_read(void *context, gridpico_auto_route_summary *out)
{
    const gridpico_pico_auto_routes *plan = context;
    if (plan == NULL || out == NULL) return GRIDPICO_INVALID;
    if (!plan->coherent || !plan->ready || !state_current(plan)) return GRIDPICO_UNAVAILABLE;
    *out = plan->summary;
    return GRIDPICO_OK;
}

gridpico_result gridpico_pico_auto_routes_explain(void *context, gridpico_revision expected_revision,
                                                   uint16_t offset, gridpico_auto_route_explain_page *out)
{
    const gridpico_pico_auto_routes *plan = context;
    if (plan == NULL || out == NULL || (expected_revision == 0 && offset != 0)) return GRIDPICO_INVALID;
    if (!plan->coherent || !plan->ready || !state_current(plan) ||
        (expected_revision != 0 && expected_revision != plan->summary.revision)) return GRIDPICO_UNAVAILABLE;
    const uint16_t total = (uint16_t)(plan->summary.candidates - plan->summary.active - plan->summary.pending);
    if (offset > total) return GRIDPICO_INVALID;
    *out = (gridpico_auto_route_explain_page){
        .revision = plan->summary.revision, .routing_revision = plan->routing_revision,
        .total_count = total, .next_offset = offset,
    };
    uint16_t seen = 0;
    for (uint16_t i = 0; i < plan->summary.candidates && out->record_count < GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS; ++i) {
        if (plan->decisions[i] == 0 || seen++ < offset) continue;
        const uint8_t source = plan->sources[i / plan->destination_count];
        const uint8_t destination = plan->destinations[i % plan->destination_count];
        out->records[out->record_count++] = (gridpico_auto_route_skip){
            plan->ids[source], plan->ids[destination], (gridpico_auto_route_skip_reason)plan->decisions[i],
        };
        ++out->next_offset;
    }
    return GRIDPICO_OK;
}
#endif
