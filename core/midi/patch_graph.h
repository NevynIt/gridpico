#ifndef GRIDPICO_MIDI_PATCH_GRAPH_H
#define GRIDPICO_MIDI_PATCH_GRAPH_H

#include "api/local_router.h"

typedef struct {
    uint32_t generation;
    bool apc_led_reserved;
    bool apc_panel_reserved;
    bool ephemeral_managed_output;
} gridpico_patch_port_runtime;

typedef struct {
    size_t source;
    size_t destination;
    gridpico_local_route_mode mode;
    bool enabled;
    gridpico_managed_note_transform transform;
} gridpico_patch_graph_branch;

typedef struct {
    gridpico_patch_inventory inventory;
    gridpico_revision revision;
    gridpico_deliver_note_fn deliver;
    gridpico_local_deliver_raw_fn deliver_raw;
    gridpico_managed_note_transform_fn transform_note;
    void *delivery_context;
    gridpico_patch_port_runtime ports[GRIDPICO_PATCH_MAX_PORTS];
    gridpico_local_port_capability port_capabilities[GRIDPICO_PATCH_MAX_PORTS];
    gridpico_local_native_virtual_cable native_virtual_cable;
    gridpico_patch_graph_branch branches[GRIDPICO_PATCH_MAX_BRANCHES];
    gridpico_local_profile local_profile;
    size_t branch_limit;
} gridpico_patch_graph;

/* Assembly owns storage and serializes calls, including the delivery callback.
 * The default and browser initializers use the stage-one topology. The configured
 * local initializer copies the registry and its admission profile. */
gridpico_result gridpico_patch_graph_init(gridpico_patch_graph *graph,
                                         const gridpico_patch_inventory *inventory,
                                         gridpico_deliver_note_fn deliver,
                                         void *delivery_context);
gridpico_result gridpico_patch_graph_init_browser(gridpico_patch_graph *graph,
                                                 const gridpico_patch_inventory *inventory,
                                                 gridpico_deliver_note_fn deliver,
                                                 void *delivery_context);
gridpico_result gridpico_patch_graph_init_monitor(gridpico_patch_graph *graph,
                                                 const gridpico_patch_inventory *inventory,
                                                 gridpico_deliver_note_fn deliver,
                                                 void *delivery_context);
gridpico_result gridpico_patch_graph_init_local(gridpico_patch_graph *graph,
                                               const gridpico_local_registry *registry,
                                               gridpico_deliver_note_fn deliver_note,
                                               gridpico_local_deliver_raw_fn deliver_raw,
                                               void *delivery_context);
gridpico_result gridpico_patch_graph_init_local_profile(
    gridpico_patch_graph *graph, const gridpico_local_registry *registry,
    const gridpico_local_profile *profile, gridpico_deliver_note_fn deliver_note,
    gridpico_local_deliver_raw_fn deliver_raw, void *delivery_context);
gridpico_result gridpico_patch_graph_append_managed_device(
    gridpico_patch_graph *graph, const gridpico_patch_device *device,
    const gridpico_patch_port *ports, size_t port_count);
/* Serialized portable lease transaction. The caller reserves resources before
 * publication and fences the exact owner before retirement. Ephemeral outputs
 * form a dense suffix after any permanent registrations. */
gridpico_result gridpico_patch_graph_publish_ephemeral_managed_output(
    gridpico_patch_graph *graph, const gridpico_patch_device *device,
    const gridpico_patch_port *port, gridpico_connection owner);
gridpico_result gridpico_patch_graph_retire_ephemeral_managed_output(
    gridpico_patch_graph *graph, gridpico_connection owner);
gridpico_result gridpico_patch_graph_update_port(void *context,
                                                const gridpico_patch_port_update *update);
gridpico_result gridpico_patch_graph_set_route(void *context,
                                               const gridpico_patch_route_request *request);
gridpico_result gridpico_patch_graph_read_graph(void *context, gridpico_patch_graph_state *out);
gridpico_result gridpico_patch_graph_read_local_state(void *context, gridpico_local_graph_state *out);
/* Caller checks output ownership and transport retirement before changing the role. */
gridpico_result gridpico_patch_graph_set_apc_led_reserved(
    gridpico_patch_graph *graph, gridpico_port_id output, bool reserved);
/* Caller fences this input lane before the role change and owns its generations. */
gridpico_result gridpico_patch_graph_set_apc_panel_reserved(
    gridpico_patch_graph *graph, gridpico_port_id input, bool reserved);
/* Bind the optional local processor before ingress. The caller guards edits in
 * the same serialized turn as the selected target and route validation. */
void gridpico_patch_graph_bind_note_transform(
    gridpico_patch_graph *graph, gridpico_managed_note_transform_fn transform);
gridpico_result gridpico_patch_graph_read_managed_note_transform(
    const gridpico_patch_graph *graph, const gridpico_local_route *route,
    gridpico_managed_note_transform *out);
gridpico_result gridpico_patch_graph_set_managed_note_transform(
    gridpico_patch_graph *graph, const gridpico_local_route *route,
    const gridpico_managed_note_transform *transform);
/* Dispatch this message once to each active managed branch, applying that
 * branch's current transform. A refused delivery does not stop sibling branches. */
gridpico_result gridpico_patch_graph_submit_note(void *context, const gridpico_ingress_note *input);
gridpico_result gridpico_patch_graph_set_local_route(void *context,
                                                    const gridpico_local_route_request *request);
typedef enum {
    GRIDPICO_PATCH_MANAGED_ENABLE_ALLOWED,
    GRIDPICO_PATCH_MANAGED_ENABLE_CYCLE,
    GRIDPICO_PATCH_MANAGED_ENABLE_BRANCH_LIMIT,
    GRIDPICO_PATCH_MANAGED_ENABLE_FANOUT_LIMIT,
    GRIDPICO_PATCH_MANAGED_ENABLE_REVISION_EXHAUSTED,
} gridpico_patch_managed_enable_decision;

/* Read-only Graph admission for a valid managed pair in the same serialized turn.
 * The caller checks attachment, ownership and policy reserves. */
gridpico_result gridpico_patch_graph_check_managed_enable(
    const gridpico_patch_graph *graph, const gridpico_local_route *route,
    gridpico_patch_managed_enable_decision *out);
gridpico_result gridpico_patch_graph_read_local_route(void *context, const gridpico_local_route *route,
                                                     gridpico_local_route_status *out);
gridpico_result gridpico_patch_graph_submit_raw(void *context, const gridpico_local_raw_ingress *input);

#endif
