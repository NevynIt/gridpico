#ifndef GRIDPICO_APC_FRONTEND_H
#define GRIDPICO_APC_FRONTEND_H

#include "api/demo_apc_surface.h"

typedef struct {
    void *session_context;
    gridpico_read_route_fn read_route;
    gridpico_set_route_enabled_fn set_route_enabled;
    void *feedback_context;
    gridpico_apc_surface_feedback_fn feedback;
    bool pad_held;
} gridpico_apc_frontend;

/* Bind the required callbacks to caller-owned storage. Calls are serialized
 * with Session ingress. Feedback messages are borrowed until the callback returns.
 */
void gridpico_apc_frontend_init(gridpico_apc_frontend *frontend,
                               void *session_context,
                               gridpico_read_route_fn read_route,
                               gridpico_set_route_enabled_fn set_route_enabled,
                               void *feedback_context,
                               gridpico_apc_surface_feedback_fn feedback);

void gridpico_apc_frontend_refresh(gridpico_apc_frontend *frontend);
gridpico_result gridpico_apc_frontend_receive(
    gridpico_apc_frontend *frontend, const gridpico_apc_surface_message *message);

#endif
