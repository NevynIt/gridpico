#include "apc_frontend.h"

#include "apc_protocol.h"

void gridpico_apc_frontend_init(gridpico_apc_frontend *frontend,
                               void *session_context,
                               gridpico_read_route_fn read_route,
                               gridpico_set_route_enabled_fn set_route_enabled,
                               void *feedback_context,
                               gridpico_apc_surface_feedback_fn feedback)
{
    *frontend = (gridpico_apc_frontend){
        .session_context = session_context,
        .read_route = read_route,
        .set_route_enabled = set_route_enabled,
        .feedback_context = feedback_context,
        .feedback = feedback,
        .pad_held = false
    };
}

void gridpico_apc_frontend_refresh(gridpico_apc_frontend *frontend)
{
    const gridpico_route_snapshot route = frontend->read_route(frontend->session_context);
    const gridpico_apc_surface_message led = gridpico_apc_encode_pad_led(route.active);
    frontend->feedback(frontend->feedback_context, &led);
}

gridpico_result gridpico_apc_frontend_receive(
    gridpico_apc_frontend *frontend, const gridpico_apc_surface_message *message)
{
    switch (gridpico_apc_decode_pad(message)) {
    case GRIDPICO_APC_PAD_INVALID:
        return GRIDPICO_INVALID;
    case GRIDPICO_APC_PAD_IGNORED:
        return GRIDPICO_OK;
    case GRIDPICO_APC_PAD_RELEASED:
        frontend->pad_held = false;
        return GRIDPICO_OK;
    case GRIDPICO_APC_PAD_PRESSED:
        break;
    }

    if (frontend->pad_held) {
        return GRIDPICO_OK;
    }
    frontend->pad_held = true;

    const gridpico_route_snapshot route = frontend->read_route(frontend->session_context);
    const gridpico_result result =
        frontend->set_route_enabled(frontend->session_context, !route.enabled);
    if (result == GRIDPICO_OK) {
        gridpico_apc_frontend_refresh(frontend);
    }
    return result;
}
