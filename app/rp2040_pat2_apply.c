#include "app/rp2040_pat2_apply.h"

#if defined(GRIDPICO_PAT2_APPLY_HOST) || defined(GRIDPICO_RP2040_PAT2_LINK_FIXTURE)
#include <string.h>

_Static_assert(GRIDPICO_PATCH_MAX_PORTS <= 32, "Host fixture has Pico registry bounds");
_Static_assert(GRIDPICO_PICO_PAT2_SLOTS <= 16, "Slot ownership fits its bitmap");

static size_t port_slot(const gridpico_patch_inventory *inventory, gridpico_port_id id)
{
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (inventory->ports[i].id == id) return i;
    }
    return inventory->port_count;
}

#ifdef GRIDPICO_PAT2_APPLY_HOST
size_t gridpico_pico_pat2_slot_index(gridpico_patch_direction axis, uint8_t slot)
{
    if (axis == GRIDPICO_PATCH_INPUT && slot < GRIDPICO_PATCH_SOURCE_ROWS) return slot;
    if (axis == GRIDPICO_PATCH_OUTPUT && slot < GRIDPICO_PATCH_DESTINATION_COLUMNS) {
        return GRIDPICO_PATCH_SOURCE_ROWS + slot;
    }
    return GRIDPICO_PICO_PAT2_SLOTS;
}

bool gridpico_pico_pat2_has_reservation(const gridpico_pico_router *router, size_t slot)
{
    return router->pat2_apply != NULL && router->pat2_apply->intent != NULL &&
           slot < GRIDPICO_PICO_PAT2_SLOTS &&
           router->pat2_apply->progress.reservations[slot] != 0;
}

bool gridpico_pico_pat2_placement_blocked(const gridpico_pico_router *router,
                                          size_t slot)
{
    return router->pat2_apply != NULL && router->pat2_apply->intent != NULL &&
           slot < GRIDPICO_PICO_PAT2_SLOTS &&
           (gridpico_pico_pat2_has_reservation(router, slot) ||
            (router->pat2_apply->progress.touched_slots & (1u << slot)));
}

void gridpico_pico_pat2_own_slot(gridpico_pico_router *router, size_t slot,
                                gridpico_port_id previous, gridpico_port_id current)
{
    gridpico_pico_pat2_apply *apply = router->pat2_apply;
    if (apply == NULL || apply->intent == NULL || slot >= GRIDPICO_PICO_PAT2_SLOTS) return;

    const gridpico_patch_inventory *inventory = &router->graph.inventory;
    const size_t old_slot = port_slot(inventory, previous), new_slot = port_slot(inventory, current);
    if (old_slot < inventory->port_count) apply->progress.owned_directions |= UINT32_C(1) << old_slot;
    if (new_slot < inventory->port_count) apply->progress.owned_directions |= UINT32_C(1) << new_slot;
    apply->progress.touched_slots |= (uint16_t)(1u << slot);
    if (!(apply->progress.consumed_slots & (1u << slot))) {
        apply->progress.cancelled[slot] = apply->intent->slots[slot];
    }
    apply->progress.reservations[slot] = 0;
}

#endif

static size_t pair_bit(const gridpico_patch_inventory *inventory, const gridpico_local_route *route)
{
    const size_t source = port_slot(inventory, route->source);
    const size_t destination = port_slot(inventory, route->destination);
    if (source == inventory->port_count || destination == inventory->port_count) {
        return SIZE_MAX;
    }
    return source * GRIDPICO_PATCH_MAX_PORTS + destination;
}

#ifdef GRIDPICO_PAT2_APPLY_HOST
void gridpico_pico_pat2_own_pair(gridpico_pico_router *router, const gridpico_local_route *route)
{
    if (router->pat2_apply == NULL || router->pat2_apply->intent == NULL ||
        route->mode != GRIDPICO_LOCAL_MANAGED) return;
    const size_t bit = pair_bit(&router->graph.inventory, route);
    if (bit != SIZE_MAX) {
        router->pat2_apply->progress.owned_pairs.off[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
    }
}

#endif

static gridpico_port_id *layout_slot(gridpico_apc_patch_layout *layout, size_t position)
{
    if (position < GRIDPICO_PATCH_SOURCE_ROWS) return &layout->rows[position];
    return &layout->columns[position - GRIDPICO_PATCH_SOURCE_ROWS];
}

static gridpico_result stage_layout(const gridpico_pico_router *router, gridpico_pico_pat2_apply *apply)
{
    gridpico_port_id wanted[GRIDPICO_PICO_PAT2_SLOTS] = {0};
    apply->layout = router->session.apc_layout;
    const gridpico_local_registry *registry = &apply->projection.graph.registry;

    for (size_t i = 0; i < GRIDPICO_PICO_PAT2_SLOTS; ++i) {
        const gridpico_pico_pat2_slot *slot = &apply->plan.slots[i];
        const uint16_t slot_bit = (uint16_t)(1u << i);
        if (slot->direction_ref == 0 ||
            ((apply->next.touched_slots | apply->next.consumed_slots) & slot_bit)) continue;
        if (slot->binding.endpoint == 0) {
            apply->next.reservations[i] = slot->direction_ref;
            *layout_slot(&apply->layout, i) = 0;
            continue;
        }

        const size_t port = port_slot(&registry->inventory, slot->binding.endpoint);
        if (port == registry->inventory.port_count) return GRIDPICO_UNAVAILABLE;
        apply->next.reservations[i] = 0;
        apply->next.consumed_slots |= slot_bit;
        if (!(apply->next.owned_directions & (UINT32_C(1) << port))) {
            wanted[i] = slot->binding.endpoint;
        }
    }
    /* Remove previous ordinary placements of destinations in the batch before
     * assigning any of them, so swaps do not depend on per-slot setter order.
     */
    for (size_t i = 0; i < GRIDPICO_PICO_PAT2_SLOTS; ++i) {
        if (wanted[i] == 0) continue;
        for (size_t j = 0; j < GRIDPICO_PICO_PAT2_SLOTS; ++j) {
            gridpico_port_id *port = layout_slot(&apply->layout, j);
            if (*port == wanted[i]) *port = 0;
        }
    }
    for (size_t i = 0; i < GRIDPICO_PICO_PAT2_SLOTS; ++i) {
        if (wanted[i] != 0) *layout_slot(&apply->layout, i) = wanted[i];
    }
    if (!gridpico_patch_session_validate_apc_layout(registry, router->session.apc_audition_source,
                                                   &apply->layout)) return GRIDPICO_INVALID;

    apply->layout_revision = router->session.apc_layout_revision;
    if (memcmp(&apply->layout, &router->session.apc_layout, sizeof(apply->layout)) != 0 ||
        memcmp(apply->next.reservations, apply->before.progress.reservations,
               sizeof(apply->next.reservations)) != 0) {
        if (apply->layout_revision == UINT64_MAX) return GRIDPICO_CAPACITY;
        ++apply->layout_revision;
    }
    return GRIDPICO_OK;
}

static bool table_full(const gridpico_patch_graph *graph)
{
    for (size_t i = 0; i < graph->branch_limit; ++i) {
        if (!graph->branches[i].enabled) return false;
    }
    return true;
}

static gridpico_result stage_pairs(gridpico_pico_pat2_apply *apply, bool enable)
{
    for (size_t i = 0; i < apply->plan.pair_count; ++i) {
        const gridpico_pico_pat2_planned_pair *pair = &apply->plan.pairs[i];
        if (apply->next.pair_outcomes[i] != GRIDPICO_PAT2_DUE ||
            !pair->source.endpoint || !pair->destination.endpoint || pair->saved.enabled != enable) continue;

        const gridpico_local_route_request request = {
            {pair->source.endpoint, pair->destination.endpoint, GRIDPICO_LOCAL_MANAGED}, enable,
        };
        const size_t bit = pair_bit(&apply->shadow_graph.inventory, &request.route);
        if (bit == SIZE_MAX) return GRIDPICO_UNAVAILABLE;
        if (apply->next.owned_pairs.off[bit / 8u] & (1u << (bit % 8u))) {
            apply->next.pair_outcomes[i] = GRIDPICO_PAT2_OPERATOR;
            continue;
        }
        if (enable) {
            gridpico_patch_managed_enable_decision admission;
            const gridpico_result result = gridpico_patch_graph_check_managed_enable(
                &apply->shadow_graph, &request.route, &admission);
            if (result != GRIDPICO_OK) return result;
            if (admission == GRIDPICO_PATCH_MANAGED_ENABLE_BRANCH_LIMIT && table_full(&apply->shadow_graph)) {
                apply->next.pair_outcomes[i] = GRIDPICO_PAT2_FULL;
                continue;
            }
            if (admission != GRIDPICO_PATCH_MANAGED_ENABLE_ALLOWED) return GRIDPICO_CAPACITY;
        }

        if (request.route.destination == apply->before.audition_destination) {
            gridpico_local_route_status status;
            const gridpico_result result = gridpico_patch_graph_read_local_route(
                &apply->shadow_graph, &request.route, &status);
            if (result != GRIDPICO_OK) return result;
            if ((status.state != GRIDPICO_PATCH_ROUTE_OFF) != enable) return GRIDPICO_UNAVAILABLE;
        }
        const gridpico_result result = gridpico_patch_session_set_local_route(&apply->shadow_session, &request);
        if (result != GRIDPICO_OK) return result;
        apply->next.pair_outcomes[i] = GRIDPICO_PAT2_APPLIED;
    }
    return GRIDPICO_OK;
}

static bool current_revisions(const gridpico_pico_router *router, const gridpico_pico_pat2_verified_view *view)
{
    return view->graph_revision != 0 && view->graph_revision == router->graph.revision &&
           view->layout_revision != 0 && view->layout_revision == router->session.apc_layout_revision;
}

gridpico_result gridpico_pico_pat2_prepare(gridpico_pico_router *router, gridpico_revision roles)
{
    if (router == NULL || router->pat2_apply == NULL) return GRIDPICO_INVALID;
    gridpico_pico_pat2_apply *apply = router->pat2_apply;
    apply->prepared = false;
    if (roles == 0 || !current_revisions(router, &apply->projection.view)) return GRIDPICO_UNAVAILABLE;
    gridpico_result result = gridpico_pico_plan_saved_intent(apply->intent, &apply->projection.view, &apply->plan);
    if (result != GRIDPICO_OK) return result;

    apply->before.intent = apply->intent;
    apply->before.role_revision = roles;
    apply->before.audition_destination = router->audition.destination;
    memcpy(&apply->before.progress, &apply->progress, sizeof(apply->progress));
    memcpy(&apply->next, &apply->progress, sizeof(apply->progress));
    apply->before.off = router->manual_routes;
    apply->off = router->manual_routes;

    result = stage_layout(router, apply);
    if (result != GRIDPICO_OK) return result;
    apply->shadow_graph = router->graph;
    gridpico_patch_session_init(&apply->shadow_session, &apply->shadow_graph,
                                gridpico_patch_graph_read_graph, gridpico_patch_graph_set_route);
    gridpico_patch_session_bind_local_routes(&apply->shadow_session, gridpico_patch_graph_set_local_route,
                                            gridpico_patch_graph_read_local_route);
    gridpico_patch_session_bind_local_routing(&apply->shadow_session, gridpico_patch_graph_read_local_state,
                                             NULL, NULL);
    apply->shadow_session.apc_audition_source = router->session.apc_audition_source;
    gridpico_patch_session_bind_manual_routes(&apply->shadow_session, &apply->off);
    gridpico_patch_session_bind_projection_scratch(&apply->shadow_session, &apply->shadow_read);
    result = stage_pairs(apply, false);
    if (result == GRIDPICO_OK) result = stage_pairs(apply, true);
    if (result == GRIDPICO_OK) apply->prepared = true;
    return result;
}

gridpico_result gridpico_pico_pat2_commit(gridpico_pico_router *router, gridpico_revision roles)
{
    if (router == NULL || router->pat2_apply == NULL) return GRIDPICO_INVALID;
    gridpico_pico_pat2_apply *apply = router->pat2_apply;
    if (!apply->prepared) return GRIDPICO_UNAVAILABLE;
    apply->prepared = false;
    const gridpico_pico_pat2_verified_view *view = &apply->projection.view;
    if (apply->intent != apply->before.intent || apply->intent->generation != apply->plan.generation ||
        roles != apply->before.role_revision || router->audition.destination != apply->before.audition_destination ||
        !current_revisions(router, view) || view->graph_revision != apply->plan.graph_revision ||
        view->layout_revision != apply->plan.layout_revision ||
        memcmp(&apply->progress, &apply->before.progress, sizeof(apply->progress)) != 0 ||
        memcmp(&router->manual_routes, &apply->before.off, sizeof(router->manual_routes)) != 0) {
        return GRIDPICO_UNAVAILABLE;
    }
    const gridpico_result result = gridpico_pico_plan_saved_intent(apply->intent, view, &apply->current_plan);
    if (result != GRIDPICO_OK) return result;
    for (size_t i = 0; i < apply->plan.direction_count; ++i) {
        const gridpico_connection captured = apply->plan.directions[i], current = apply->current_plan.directions[i];
        if (captured.endpoint != current.endpoint || captured.generation != current.generation) {
            return GRIDPICO_UNAVAILABLE;
        }
    }

    const bool refresh = router->graph.revision != apply->shadow_graph.revision ||
                         router->session.apc_layout_revision != apply->layout_revision;
    memcpy(router->graph.branches, apply->shadow_graph.branches, sizeof(router->graph.branches));
    router->graph.revision = apply->shadow_graph.revision;
    router->session.apc_layout = apply->layout;
    router->session.apc_layout_revision = apply->layout_revision;
    router->manual_routes = apply->off;
    memcpy(&apply->progress, &apply->next, sizeof(apply->progress));
    if (refresh) gridpico_pico_pat2_refresh_panels(router);
    return GRIDPICO_OK;
}
#endif
