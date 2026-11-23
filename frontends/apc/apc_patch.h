#ifndef GRIDPICO_APC_PATCH_H
#define GRIDPICO_APC_PATCH_H

#include "api/patchbay_controls.h"

typedef struct {
    void *session_context;
    gridpico_patch_read_snapshot_fn read_snapshot;
    gridpico_patch_set_route_fn set_route;
    gridpico_patch_read_device_color_fn read_color;
    gridpico_patch_set_device_color_fn set_color;
    void *feedback_context;
    gridpico_apc_surface_feedback_fn feedback;
    gridpico_patch_led_page_fn page_feedback;
    bool note_held[128];
    gridpico_device_id color_device;
} gridpico_apc_patch;

/* Calls are serialized with Session ingress. Feedback is synchronous and the
 * message is borrowed until the callback returns. Initialization is silent.
 */
void gridpico_apc_patch_init(gridpico_apc_patch *frontend, void *session_context,
                            gridpico_patch_read_snapshot_fn read_snapshot,
                            gridpico_patch_set_route_fn set_route,
                            gridpico_patch_read_device_color_fn read_color,
                            gridpico_patch_set_device_color_fn set_color,
                            void *feedback_context,
                            gridpico_apc_surface_feedback_fn feedback);

/* Optional, synchronous metadata on the same feedback context. Page data is
 * borrowed until the callback returns, just like each following MIDI frame.
 */
void gridpico_apc_patch_set_page_feedback(gridpico_apc_patch *frontend,
                                         gridpico_patch_led_page_fn page_feedback);

gridpico_result gridpico_apc_patch_receive(
    gridpico_apc_patch *frontend, const gridpico_apc_surface_message *message);
gridpico_result gridpico_apc_patch_refresh(gridpico_apc_patch *frontend);

/* Use the same snapshot as the caller's inventory output. An open color layer
 * reads the selected device's preference for the current display.
 */
gridpico_result gridpico_apc_patch_refresh_snapshot(
    gridpico_apc_patch *frontend, const gridpico_patch_snapshot *snapshot);

#endif
