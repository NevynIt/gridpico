#ifndef GRIDPICO_PATCH_SESSION_H
#define GRIDPICO_PATCH_SESSION_H

#include "api/routing.h"
#include "core/midi/patch_graph.h"

typedef struct {
    bool is_set;
    gridpico_rgb8 rgb;
} gridpico_patch_color_preference;

typedef struct {
    uint64_t sequence;
    uint64_t time_us;
} gridpico_patch_attack;

typedef struct {
    uint8_t off[(GRIDPICO_PATCH_MAX_PORTS * GRIDPICO_PATCH_MAX_PORTS + 7u) / 8u];
} gridpico_patch_manual_routes;

typedef gridpico_result (*gridpico_patch_read_route_metadata_fn)(
    const void *graph_context, gridpico_patch_graph_route_metadata *out);
typedef gridpico_result (*gridpico_patch_read_route_page_fn)(
    const void *graph_context, gridpico_revision graph_revision, size_t offset,
    gridpico_patch_graph_route_page *out);

typedef struct {
    gridpico_revision revision; /* Composite Routing guard. */
    gridpico_patch_graph_route_metadata graph;
    gridpico_device_color colors[GRIDPICO_PATCH_MAX_DEVICES];
    gridpico_local_port_availability availability[GRIDPICO_PATCH_MAX_PORTS];
} gridpico_patch_routing_metadata;

typedef struct {
    gridpico_patch_routing_metadata metadata;
    size_t record_count;
    size_t next_offset;
    size_t route_count; /* Route records in the suffix of this joined page. */
    gridpico_local_enabled_route routes[GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS];
} gridpico_patch_routing_page;

typedef struct {
    void *graph_context;
    gridpico_patch_read_graph_fn read_graph;
    gridpico_patch_set_route_fn set_graph_route;
    gridpico_local_set_route_fn set_local_graph_route;
    gridpico_local_read_route_fn read_local_graph_route;
    gridpico_local_read_graph_fn read_local_graph;
    gridpico_local_read_availability_fn read_availability;
    void *availability_context;
    gridpico_local_routing_snapshot *projection_scratch;
    gridpico_patch_read_route_metadata_fn read_route_metadata;
    gridpico_patch_read_route_page_fn read_route_page;
    gridpico_patch_routing_page *routing_page_scratch;
    gridpico_patch_manual_routes *manual_routes;
    gridpico_revision color_revision;
    /* Colors follow the graph's stable device registration slots. */
    gridpico_patch_color_preference colors[GRIDPICO_PATCH_MAX_DEVICES];
    gridpico_revision routing_revision;
    gridpico_revision routing_graph_revision;
    gridpico_revision routing_color_revision;
    size_t routing_port_count;
    gridpico_local_port_availability routing_availability[GRIDPICO_PATCH_MAX_PORTS];
    gridpico_apc_patch_layout apc_layout;
    gridpico_revision apc_layout_revision;
    bool flat_managed_routes;
    gridpico_port_id apc_audition_source;
    gridpico_port_id apc_attack_sources[GRIDPICO_PATCH_MAX_PORTS];
    uint64_t apc_attack_sequence;
    gridpico_patch_attack apc_attacks[GRIDPICO_PATCH_MAX_PORTS];
} gridpico_patch_session;

/* Bind both callbacks to the same graph. Assembly serializes commands and
 * reads with graph operations. The graph publishes the Patch revision.
 */
void gridpico_patch_session_init(gridpico_patch_session *session, void *graph_context,
                                gridpico_patch_read_graph_fn read_graph,
                                gridpico_patch_set_route_fn set_graph_route);

/* Enable typed route commands on the graph bound at initialization. */
void gridpico_patch_session_bind_local_routes(gridpico_patch_session *session,
                                             gridpico_local_set_route_fn set_route,
                                             gridpico_local_read_route_fn read_route);

/* Match the live editable APC page to the flat Managed provider before publishing it. */
void gridpico_patch_session_enable_flat_managed_routes(gridpico_patch_session *session);

/* Opt into the configured routing projections and the first legacy window. */
void gridpico_patch_session_bind_local_routing(gridpico_patch_session *session,
                                              gridpico_local_read_graph_fn read_graph,
                                              gridpico_local_read_availability_fn read_availability,
                                              void *availability_context);

/* Borrow scratch for serialized projections. Outputs must be disjoint from it.
 * Pass NULL to use the default stack fallback.
 */
void gridpico_patch_session_bind_projection_scratch(gridpico_patch_session *session,
                                                   gridpico_local_routing_snapshot *scratch);

/* Bind all three before serving this provider, using the existing Graph context.
 * The serialized owner lends disjoint lifetime scratch and binds exact pair
 * readback and availability for the same complete, authoritative registry.
 */
void gridpico_patch_session_bind_routing_pages(gridpico_patch_session *session,
                                               gridpico_patch_read_route_metadata_fn read_metadata,
                                               gridpico_patch_read_route_page_fn read_page,
                                               gridpico_patch_routing_page *scratch);

gridpico_result gridpico_patch_session_read_routing_metadata(void *context,
                                                           gridpico_patch_routing_metadata *out);
/* A zero revision starts at offset zero; a continuation supplies the exact
 * composite guard. Device and binding rows follow registry order.
 */
gridpico_result gridpico_patch_session_read_routing_page(void *context,
                                                        gridpico_revision expected_revision, size_t offset,
                                                        gridpico_patch_routing_page *out);
/* Check the composite guard and a structurally valid exact pair. Status uses
 * Graph revision. An authorized owner may write in the same serialized turn.
 */
gridpico_result gridpico_patch_session_read_guarded_route(void *context,
                                                         gridpico_revision expected_revision,
                                                         const gridpico_local_route *route,
                                                         gridpico_local_route_status *out);

/* Borrow zero-initialized lifetime storage once for the automatic profile.
 * Policy reads use stable slots from the same append-only Graph registry.
 */
void gridpico_patch_session_bind_manual_routes(gridpico_patch_session *session,
                                              gridpico_patch_manual_routes *storage);
bool gridpico_patch_session_manual_route_is_off(const gridpico_patch_session *session,
                                                size_t source_slot, size_t destination_slot);

/* Check both axes against a captured registry. The caller guards and commits edits. */
bool gridpico_patch_session_validate_apc_layout(const gridpico_local_registry *registry,
                                                gridpico_port_id audition_source,
                                                const gridpico_apc_patch_layout *layout);

/* Bind the editable layout once, after binding the local graph reader. */
gridpico_result gridpico_patch_session_bind_apc_patch(gridpico_patch_session *session,
                                                    const gridpico_apc_patch_layout *layout,
                                                    gridpico_port_id audition_source);
gridpico_result gridpico_patch_session_export_apc_snapshot(const gridpico_patch_session *session,
                                                         gridpico_apc_session_snapshot *out);
/* Restore into a fresh, unpublished Session and Graph before attachment or
 * ingress. Assembly discards or reinitializes the candidate after any failure.
 */
gridpico_result gridpico_patch_session_restore_apc_snapshot(gridpico_patch_session *session,
                                                          const gridpico_apc_session_snapshot *snapshot);
gridpico_result gridpico_patch_session_read_apc_patch(void *context,
                                                    gridpico_apc_patch_snapshot *out);
gridpico_result gridpico_patch_session_assign_apc_patch(void *context, gridpico_revision layout_revision,
                                                      gridpico_patch_direction axis, uint8_t slot,
                                                      gridpico_port_id port);
gridpico_result gridpico_patch_session_move_apc_patch(void *context, gridpico_revision layout_revision,
                                                    gridpico_patch_direction axis, uint8_t from,
                                                    uint8_t to);
typedef gridpico_result (*gridpico_patch_set_apc_led_reserved_fn)(
    void *graph_context, gridpico_port_id output, bool reserved);
/* The serialized caller verifies the role guard and transport readiness.
 * The callback commits Graph eligibility without entering Session or publishing
 * a view. On a successful mark, Session clears the matching column before return.
 */
gridpico_result gridpico_patch_session_set_apc_led_reserved(
    gridpico_patch_session *session, gridpico_patch_set_apc_led_reserved_fn set_reserved,
    gridpico_port_id output, bool reserved, bool transport_ready);
typedef gridpico_result (*gridpico_patch_set_apc_panel_reserved_fn)(
    void *graph_context, gridpico_port_id input, bool reserved);
/* The serialized caller verifies the selected input's identity, generation and
 * ingress fence before setting transport_ready. The callback commits Graph
 * eligibility without publishing a view. A successful mark clears its row.
 */
gridpico_result gridpico_patch_session_set_apc_panel_reserved(
    gridpico_patch_session *session, gridpico_patch_set_apc_panel_reserved_fn set_reserved,
    gridpico_port_id input, bool reserved, bool transport_ready);
gridpico_result gridpico_patch_session_set_apc_patch_cell(void *context,
                                                        gridpico_revision layout_revision,
                                                        gridpico_revision graph_revision,
                                                        uint8_t row, uint8_t column, bool enabled);
/* Notify Session of a newly admitted managed input before accepting its notes. */
gridpico_result gridpico_patch_session_add_apc_attack_source(
    gridpico_patch_session *session, size_t registry_index, gridpico_port_id source);
/* Assembly checks that ingress belongs to the current attachment. */
void gridpico_patch_session_observe_apc_patch_note(gridpico_patch_session *session,
                                                const gridpico_ingress_note *input, uint64_t now_us);

gridpico_result gridpico_patch_session_read_local_routing(void *context,
                                                         gridpico_local_routing_snapshot *out);
gridpico_result gridpico_patch_session_read_local_patch_page(void *context,
                                                            const gridpico_local_patch_viewport *request,
                                                            gridpico_local_patch_page *out);
gridpico_result gridpico_patch_session_read_snapshot(void *context, gridpico_patch_snapshot *out);
gridpico_result gridpico_patch_session_set_route(void *context,
                                               const gridpico_patch_route_request *request);
gridpico_result gridpico_patch_session_set_local_route(void *context,
                                                     const gridpico_local_route_request *request);
gridpico_result gridpico_patch_session_read_local_route(void *context, const gridpico_local_route *route,
                                                      gridpico_local_route_status *out);
gridpico_result gridpico_session_read_device_color(void *context, gridpico_device_id device,
                                                 gridpico_device_color *out);
gridpico_result gridpico_session_set_device_color(void *context, gridpico_device_id device,
                                                gridpico_rgb8 rgb);

#endif
