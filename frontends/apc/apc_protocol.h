#ifndef GRIDPICO_APC_PROTOCOL_H
#define GRIDPICO_APC_PROTOCOL_H

#include "api/demo_apc_surface.h"

typedef enum {
    GRIDPICO_APC_PAD_INVALID,
    GRIDPICO_APC_PAD_IGNORED,
    GRIDPICO_APC_PAD_PRESSED,
    GRIDPICO_APC_PAD_RELEASED
} gridpico_apc_pad_event;

gridpico_apc_pad_event gridpico_apc_decode_button(
    const gridpico_apc_surface_message *message, uint8_t *note);
gridpico_apc_surface_message gridpico_apc_encode_rgb_pad(uint8_t note, uint8_t palette);

gridpico_apc_pad_event gridpico_apc_decode_pad(const gridpico_apc_surface_message *message);
gridpico_apc_surface_message gridpico_apc_encode_pad_led(bool lit);

#endif
