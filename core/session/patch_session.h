#ifndef GRIDPICO_PATCH_SESSION_H
#define GRIDPICO_PATCH_SESSION_H

#include "api/routing.h"

typedef struct {
    bool is_set;
    gridpico_rgb8 rgb;
} gridpico_patch_color_preference;

typedef struct {
    uint64_t sequence;
    uint64_t time_us;
} gridpico_patch_attack;

typedef struct {
    void *graph_context;
    gridpico_patch_read_graph_fn read_graph;
    gridpico_patch_set_route_fn set_graph_route;
    gridpico_local_set_route_fn set_local_graph_route;
    gridpico_local_read_route_fn read_local_graph_route;
    gridpico_local_read_graph_fn read_local_graph;
    gridpico_local_read_availability_fn read_availability;
    void *availability_context;
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

/* Opt into the configured routing projections and the first legacy window. */
void gridpico_patch_session_bind_local_routing(gridpico_patch_session *session,
                                              gridpico_local_read_graph_fn read_graph,
                                              gridpico_local_read_availability_fn read_availability,
                                              void *availability_context);

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
