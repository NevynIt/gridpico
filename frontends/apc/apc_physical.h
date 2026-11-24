#ifndef GRIDPICO_APC_PHYSICAL_H
#define GRIDPICO_APC_PHYSICAL_H

#include "api/routing.h"

enum {
    GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES =
        GRIDPICO_PATCH_LED_FRAMES + GRIDPICO_PATCH_SOURCE_ROWS +
        GRIDPICO_PATCH_DESTINATION_COLUMNS
};

typedef struct {
    bool route_page;
    gridpico_revision layout_revision;
    gridpico_revision graph_revision;
} gridpico_apc_physical_page_version;

typedef gridpico_result (*gridpico_apc_physical_feedback_fn)(
    void *context, const gridpico_apc_surface_message *messages, size_t count,
    const gridpico_apc_physical_page_version *version);

typedef enum {
    GRIDPICO_APC_ARRANGE_NONE,
    GRIDPICO_APC_ARRANGE_ASSIGN,
    GRIDPICO_APC_ARRANGE_MOVE,
    GRIDPICO_APC_ARRANGE_LEARN,
    GRIDPICO_APC_ARRANGE_AUDITION
} gridpico_apc_arrange_action;

typedef struct {
    gridpico_port_id port;
    uint64_t observed;
    uint64_t started_ms;
    bool has_flashed;
} gridpico_apc_row_activity;

typedef struct {
    void *session_context;
    gridpico_apc_patch_read_fn read;
    gridpico_apc_patch_set_cell_fn set_cell;
    gridpico_apc_patch_assign_fn assign;
    gridpico_apc_patch_move_fn move;
    void *panel_context;
    gridpico_apc_patch_audition_fn audition;
    gridpico_apc_physical_feedback_fn feedback;
    gridpico_apc_patch_snapshot *scratch;

    bool held[128];
    bool arranging;
    gridpico_apc_arrange_action action;
    uint8_t selected_row;
    uint8_t selected_column;
    gridpico_patch_direction axis;
    uint8_t slot;
    uint8_t move_to;
    gridpico_revision preview_revision;
    gridpico_port_id candidate;
    bool preview_error;
    uint64_t learn_after;
    bool learn_ambiguous;
    gridpico_port_id audition_destination;
    gridpico_revision visible_layout;
    gridpico_revision visible_graph;
    bool route_page_ready;
    gridpico_apc_row_activity activity[GRIDPICO_PATCH_SOURCE_ROWS];
} gridpico_apc_physical;

/* Serialized with Session. The caller supplies scratch for the frontend's
 * lifetime, and may share it across serialized panels. Callbacks borrow their
 * arguments only for the call and must not reenter the frontend. Feedback
 * admits a complete page or returns an error. Input comes from Port 1 of the
 * current attachment. now_ms uses the Session attack timestamps' clock epoch.
 */
void gridpico_apc_physical_init(gridpico_apc_physical *frontend, void *session_context,
                               gridpico_apc_patch_read_fn read,
                               gridpico_apc_patch_set_cell_fn set_cell,
                               gridpico_apc_patch_assign_fn assign,
                               gridpico_apc_patch_move_fn move, void *panel_context,
                               gridpico_apc_patch_audition_fn audition,
                               gridpico_apc_physical_feedback_fn feedback,
                               gridpico_apc_patch_snapshot *scratch);
gridpico_result gridpico_apc_physical_receive(
    gridpico_apc_physical *frontend, const gridpico_apc_surface_message *message, uint64_t now_ms);
gridpico_result gridpico_apc_physical_refresh(gridpico_apc_physical *frontend, uint64_t now_ms);
/* Call only after successful delivery of the full page on the current
 * receiver generation. Admission and partial delivery do not count.
 */
void gridpico_apc_physical_feedback_complete(
    gridpico_apc_physical *frontend, const gridpico_apc_physical_page_version *version);
/* Give the old peer a chance to cancel its audition, then clear held edges. */
gridpico_result gridpico_apc_physical_detach(gridpico_apc_physical *frontend);

#endif
