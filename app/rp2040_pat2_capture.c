#include "app/rp2040_pat2_capture.h"

#ifdef GRIDPICO_PAT2_APPLY_HOST
#include <string.h>

enum { NONE, OFF, ON };

gridpico_result gridpico_pico_pat2_bind_capture(gridpico_pico_router *router, gridpico_pico_pat2_apply *owner)
{
    if (router == NULL || owner == NULL) return GRIDPICO_INVALID;
    const gridpico_patch_manual_routes empty = {0};
    if (router->pat2_apply != NULL || router->graph.revision != 0 ||
        router->session.apc_layout_revision != 1 ||
        memcmp(&router->manual_routes, &empty, sizeof(empty)) != 0) return GRIDPICO_UNAVAILABLE;
    owner->capture_lifetime = true;
    router->pat2_apply = owner;
    return GRIDPICO_OK;
}

gridpico_result gridpico_pico_pat2_observe_capture(const gridpico_pico_router *router,
    gridpico_revision roles, gridpico_pico_pat2_capture_evidence *out)
{
    if (router == NULL || out == NULL || roles == 0) return GRIDPICO_INVALID;
    const gridpico_pico_pat2_apply *owner = router->pat2_apply;
    if (owner == NULL || !owner->capture_lifetime || owner->intent != owner->progress_intent ||
        owner->progress_generation != (owner->intent ? owner->intent->generation : 0) ||
        router->graph.revision == 0 || router->session.apc_layout_revision == 0) return GRIDPICO_UNAVAILABLE;
    *out = (gridpico_pico_pat2_capture_evidence){
        .owner = owner, .prior = owner->intent, .prior_generation = owner->progress_generation,
        .graph_revision = router->graph.revision, .layout_revision = router->session.apc_layout_revision,
        .role_revision = roles, .layout = router->session.apc_layout, .off = router->manual_routes,
    };
    memcpy(&out->progress, &owner->progress, sizeof(out->progress));
    memcpy(out->committed_off, owner->committed_off, sizeof(out->committed_off));
    return GRIDPICO_OK;
}

static bool evidence_current(const gridpico_pico_router *router, gridpico_revision roles,
                             const gridpico_pico_pat2_capture_evidence *e)
{
    gridpico_pico_pat2_capture_evidence now;
    if (gridpico_pico_pat2_observe_capture(router, roles, &now) != GRIDPICO_OK) return false;
    return now.owner == e->owner && now.prior == e->prior && now.prior_generation == e->prior_generation &&
           now.graph_revision == e->graph_revision && now.layout_revision == e->layout_revision &&
           now.role_revision == e->role_revision &&
           memcmp(&now.layout, &e->layout, sizeof(now.layout)) == 0 &&
           memcmp(&now.off, &e->off, sizeof(now.off)) == 0 &&
           memcmp(&now.progress, &e->progress, sizeof(now.progress)) == 0 &&
           memcmp(now.committed_off, e->committed_off, sizeof(now.committed_off)) == 0;
}

static bool same_identity(const gridpico_pico_pat2_identity *a, const gridpico_pico_pat2_identity *b)
{
    return a->vid == b->vid && a->pid == b->pid && a->bcd_device == b->bcd_device &&
           a->interface_number == b->interface_number &&
           a->input_endpoint == b->input_endpoint && a->output_endpoint == b->output_endpoint &&
           a->input_cables == b->input_cables && a->output_cables == b->output_cables &&
           a->serial_length == b->serial_length && memcmp(a->serial, b->serial, sizeof(a->serial)) == 0;
}

static gridpico_result add_direction(gridpico_pico_pat2_capture_scratch *s,
    const gridpico_pico_pat2_identity *identity, gridpico_pico_pat2_direction dir,
    gridpico_connection live, bool reserve, uint8_t *out)
{
    gridpico_pico_pat2_intent *intent = &s->intent;
    size_t id = 0;
    while (id < intent->identity_count && !same_identity(identity, &intent->identities[id])) ++id;
    if (id == intent->identity_count) {
        if (id == GRIDPICO_PICO_PAT2_IDENTITIES) return GRIDPICO_CAPACITY;
        intent->identities[intent->identity_count++] = *identity;
    }
    dir.identity_ref = (uint8_t)(id + 1u);
    size_t i = 0;
    while (i < intent->direction_count) {
        const gridpico_pico_pat2_direction *other = &intent->directions[i];
        if (other->identity_ref == dir.identity_ref && other->cable == dir.cable &&
            other->direction == dir.direction) break;
        ++i;
    }
    if (i == intent->direction_count) {
        if (i == GRIDPICO_PICO_PAT2_DIRECTIONS) return GRIDPICO_CAPACITY;
        intent->directions[intent->direction_count++] = dir;
    }
    const uint32_t bit = UINT32_C(1) << i;
    /* A reservation retains its saved label even when a live pair shares
     * the same directional key. There is only one direction per key.
     */
    if (reserve || (live.endpoint && !(s->reserved_refs & bit))) intent->directions[i] = dir;
    if (live.endpoint) s->live[i] = live;
    if (reserve) s->reserved_refs |= bit;
    *out = (uint8_t)(i + 1u);
    return GRIDPICO_OK;
}

static gridpico_result live_direction(gridpico_pico_pat2_capture_scratch *s, const gridpico_patch_port *port,
                                     gridpico_patch_direction axis, uint8_t *ref)
{
    if (port->direction != axis) return GRIDPICO_UNAVAILABLE;
    for (size_t i = 0; i < s->projection.view.binding_count; ++i) {
        const gridpico_pico_pat2_binding *b = &s->projection.bindings[i];
        if (!b->eligible || b->binding.endpoint != port->id || b->direction != axis) continue;
        const gridpico_pico_pat2_observation *o = &s->projection.observations[b->observation_index];
        const char *end = memchr(port->name, '\0', sizeof(port->name));
        if (!o->revision_known || !o->identity.serial_length || end == NULL) return GRIDPICO_UNAVAILABLE;
        gridpico_pico_pat2_direction dir = {.cable = b->cable, .direction = axis};
        memcpy(dir.label, port->name, (size_t)(end - port->name));
        return add_direction(s, &o->identity, dir, b->binding, false, ref);
    }
    return GRIDPICO_UNAVAILABLE;
}

static gridpico_result prior_direction(gridpico_pico_pat2_capture_scratch *s,
    const gridpico_pico_pat2_intent *prior, uint8_t ref, bool reserve, uint8_t *out)
{
    const gridpico_pico_pat2_direction dir = prior->directions[ref - 1u];
    return add_direction(s, &prior->identities[dir.identity_ref - 1u], dir,
                         (gridpico_connection){0}, reserve, out);
}

static size_t port_index(const gridpico_patch_inventory *inventory, gridpico_port_id port)
{
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (inventory->ports[i].id == port) return i;
    }
    return inventory->port_count;
}

static bool bitmap_has(const gridpico_patch_manual_routes *bitmap, size_t source, size_t dest)
{
    const size_t bit = source * GRIDPICO_PATCH_MAX_PORTS + dest;
    return (bitmap->off[bit / 8u] & (1u << (bit % 8u))) != 0;
}

static gridpico_result read_prior_off(gridpico_pico_pat2_capture_scratch *s,
                                     const gridpico_pico_pat2_capture_evidence *e)
{
    const gridpico_patch_inventory *inv = &s->projection.graph.registry.inventory;
    const size_t n = inv->port_count;
    memset(&s->prior_off, 0, sizeof(s->prior_off));
    for (size_t i = 0; i < GRIDPICO_PICO_PAT2_PAIRS; ++i) {
        const bool applied = e->prior != NULL && i < e->prior->pair_count &&
            !e->prior->pairs[i].enabled && e->progress.pair_outcomes[i] == GRIDPICO_PAT2_APPLIED;
        const uint16_t recorded = e->committed_off[i];
        if (applied != (recorded != 0)) return GRIDPICO_UNAVAILABLE;
        if (!applied) continue;
        const size_t bit = recorded - 1u, a = bit / GRIDPICO_PATCH_MAX_PORTS, b = bit % GRIDPICO_PATCH_MAX_PORTS;
        const gridpico_pico_pat2_pair pair = e->prior->pairs[i];
        const gridpico_port_id src = s->plan.directions[pair.source_ref - 1u].endpoint;
        const gridpico_port_id dst = s->plan.directions[pair.destination_ref - 1u].endpoint;
        if (a >= n || b >= n || bitmap_has(&s->prior_off, a, b) ||
            inv->ports[a].direction != GRIDPICO_PATCH_INPUT || inv->ports[b].direction != GRIDPICO_PATCH_OUTPUT ||
            (src && src != inv->ports[a].id) || (dst && dst != inv->ports[b].id) ||
            (!bitmap_has(&e->off, a, b) && !bitmap_has(&e->progress.owned_pairs, a, b))) {
            return GRIDPICO_UNAVAILABLE;
        }
        s->prior_off.off[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
    }
    return GRIDPICO_OK;
}

static gridpico_result read_choices(gridpico_pico_pat2_capture_scratch *s,
    const gridpico_pico_pat2_capture_evidence *e, gridpico_port_id audition)
{
    const gridpico_local_graph_state *graph = &s->projection.graph;
    const gridpico_patch_inventory *inventory = &graph->registry.inventory;
    memset(s->choices, 0, sizeof(s->choices));
    for (size_t a = 0; a < inventory->port_count; ++a) {
        for (size_t b = 0; b < inventory->port_count; ++b) {
            const gridpico_port_id source = inventory->ports[a].id, dest = inventory->ports[b].id;
            if (source == audition || dest == audition) continue;
            const bool off = bitmap_has(&e->off, a, b);
            const bool touched = bitmap_has(&e->progress.owned_pairs, a, b);
            if (!off && !touched) continue;
            bool on = false;
            for (size_t i = 0; i < graph->route_count; ++i) {
                const gridpico_local_route *r = &graph->routes[i].route;
                if (r->mode == GRIDPICO_LOCAL_MANAGED && r->source == source && r->destination == dest) on = true;
            }
            if (off == on || graph->registry.port_capabilities[a] != GRIDPICO_LOCAL_PORT_MANAGED ||
                graph->registry.port_capabilities[b] != GRIDPICO_LOCAL_PORT_MANAGED) return GRIDPICO_UNAVAILABLE;
            if (!touched && off && bitmap_has(&s->prior_off, a, b)) continue;
            s->choices[a][b] = off ? OFF : ON;
        }
    }
    return GRIDPICO_OK;
}

static gridpico_result collect_slots(gridpico_pico_pat2_capture_scratch *s,
                                    const gridpico_pico_pat2_capture_evidence *e)
{
    const gridpico_patch_inventory *inventory = &s->projection.graph.registry.inventory;
    for (size_t i = 0; i < GRIDPICO_PICO_PAT2_SLOTS; ++i) {
        const bool row = i < GRIDPICO_PATCH_SOURCE_ROWS;
        const gridpico_port_id port = row ? e->layout.rows[i] : e->layout.columns[i - GRIDPICO_PATCH_SOURCE_ROWS];
        const uint8_t reserved = e->progress.reservations[i], cancelled = e->progress.cancelled[i];
        const uint16_t bit = (uint16_t)(1u << i);
        const bool touched = (e->progress.touched_slots & bit) != 0;
        if (cancelled && (!e->prior || e->prior->slots[i] != cancelled || !touched)) return GRIDPICO_UNAVAILABLE;
        gridpico_result result = GRIDPICO_OK;
        if (reserved) {
            if (port || !e->prior || e->prior->slots[i] != reserved || cancelled || touched ||
                (e->progress.consumed_slots & bit)) return GRIDPICO_UNAVAILABLE;
            result = prior_direction(s, e->prior, reserved, true, &s->intent.slots[i]);
        } else if (port) {
            const size_t slot = port_index(inventory, port);
            if (slot == inventory->port_count) return GRIDPICO_UNAVAILABLE;
            result = live_direction(s, &inventory->ports[slot], row ? GRIDPICO_PATCH_INPUT : GRIDPICO_PATCH_OUTPUT,
                                    &s->intent.slots[i]);
        }
        if (result != GRIDPICO_OK) return result;
    }
    return GRIDPICO_OK;
}

static gridpico_result save_pair(gridpico_pico_pat2_capture_scratch *s, gridpico_pico_pat2_pair pair)
{
    for (size_t i = 0; i < s->intent.pair_count; ++i) {
        gridpico_pico_pat2_pair *old = &s->intent.pairs[i];
        if (old->source_ref == pair.source_ref && old->destination_ref == pair.destination_ref) {
            old->enabled = pair.enabled;
            return GRIDPICO_OK;
        }
    }
    if (s->intent.pair_count == GRIDPICO_PICO_PAT2_PAIRS) return GRIDPICO_CAPACITY;
    s->intent.pairs[s->intent.pair_count++] = pair;
    return GRIDPICO_OK;
}

static gridpico_result live_pair(gridpico_pico_pat2_capture_scratch *s, size_t source, size_t dest)
{
    const gridpico_patch_inventory *inv = &s->projection.graph.registry.inventory;
    gridpico_pico_pat2_pair pair = {.enabled = s->choices[source][dest] == ON};
    gridpico_result r = live_direction(s, &inv->ports[source], GRIDPICO_PATCH_INPUT, &pair.source_ref);
    if (r != GRIDPICO_OK) return r;
    r = live_direction(s, &inv->ports[dest], GRIDPICO_PATCH_OUTPUT, &pair.destination_ref);
    if (r != GRIDPICO_OK) return r;
    s->choices[source][dest] = NONE;
    return save_pair(s, pair);
}

static gridpico_result collect_prior_pairs(gridpico_pico_pat2_capture_scratch *s,
                                          const gridpico_pico_pat2_capture_evidence *e)
{
    const gridpico_patch_inventory *inv = &s->projection.graph.registry.inventory;
    const size_t count = e->prior == NULL ? 0 : e->prior->pair_count;
    for (size_t i = count; i < GRIDPICO_PICO_PAT2_PAIRS; ++i) {
        if (e->progress.pair_outcomes[i] != GRIDPICO_PAT2_DUE) return GRIDPICO_UNAVAILABLE;
    }
    for (size_t i = 0; i < count; ++i) {
        const uint8_t outcome = e->progress.pair_outcomes[i];
        if (outcome > GRIDPICO_PAT2_OPERATOR) return GRIDPICO_UNAVAILABLE;
        gridpico_pico_pat2_pair pair = e->prior->pairs[i];
        const size_t a = port_index(inv, s->plan.directions[pair.source_ref - 1u].endpoint);
        const size_t b = port_index(inv, s->plan.directions[pair.destination_ref - 1u].endpoint);
        gridpico_result r = GRIDPICO_OK;
        if (a < inv->port_count && b < inv->port_count && s->choices[a][b] != NONE) {
            r = live_pair(s, a, b);
        } else if (outcome != GRIDPICO_PAT2_OPERATOR) {
            r = prior_direction(s, e->prior, pair.source_ref, false, &pair.source_ref);
            if (r == GRIDPICO_OK) r = prior_direction(s, e->prior, pair.destination_ref, false, &pair.destination_ref);
            if (r == GRIDPICO_OK) r = save_pair(s, pair);
        }
        if (r != GRIDPICO_OK) return r;
    }
    return GRIDPICO_OK;
}

gridpico_result gridpico_pico_pat2_capture(const gridpico_pico_router *router, gridpico_revision roles,
    const gridpico_pico_pat2_capture_evidence *e, uint32_t generation,
    gridpico_pico_pat2_capture_scratch *s, gridpico_pico_pat2_intent *out)
{
    if (router == NULL || e == NULL || s == NULL || out == NULL || generation == 0 || roles == 0) return GRIDPICO_INVALID;
    if (!evidence_current(router, roles, e)) return GRIDPICO_UNAVAILABLE;
    gridpico_result r = gridpico_pico_project_pat2_view(&s->projection);
    if (r != GRIDPICO_OK) return r;
    const gridpico_pico_pat2_verified_view *view = &s->projection.view;
    if (view->graph_revision != e->graph_revision || view->layout_revision != e->layout_revision) return GRIDPICO_UNAVAILABLE;
    if (e->prior != NULL) {
        r = gridpico_pico_plan_saved_intent(e->prior, view, &s->plan);
        if (r != GRIDPICO_OK) return r;
    }

    s->intent = (gridpico_pico_pat2_intent){.generation = generation};
    memset(s->live, 0, sizeof(s->live));
    s->reserved_refs = 0;
    r = read_prior_off(s, e);
    if (r == GRIDPICO_OK) r = read_choices(s, e, router->session.apc_audition_source);
    if (r == GRIDPICO_OK) r = collect_slots(s, e);
    if (r == GRIDPICO_OK) r = collect_prior_pairs(s, e);
    if (r != GRIDPICO_OK) return r;

    const size_t n = s->projection.graph.registry.inventory.port_count;
    for (size_t a = 0; a < n; ++a) {
        for (size_t b = 0; b < n; ++b) {
            if (s->choices[a][b] != NONE) {
                r = live_pair(s, a, b);
                if (r != GRIDPICO_OK) return r;
            }
        }
    }
    r = gridpico_pico_plan_saved_intent(&s->intent, view, &s->plan);
    if (r != GRIDPICO_OK) return r;
    for (size_t i = 0; i < s->intent.direction_count; ++i) {
        if (s->live[i].endpoint &&
            (s->live[i].endpoint != s->plan.directions[i].endpoint ||
             s->live[i].generation != s->plan.directions[i].generation)) return GRIDPICO_UNAVAILABLE;
    }
    if (!evidence_current(router, roles, e)) return GRIDPICO_UNAVAILABLE;
    *out = s->intent;
    return GRIDPICO_OK;
}
#endif
