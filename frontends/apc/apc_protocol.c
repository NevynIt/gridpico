#include "apc_protocol.h"

gridpico_apc_pad_event gridpico_apc_decode_pad(const gridpico_apc_surface_message *message)
{
    if (message == NULL) {
        return GRIDPICO_APC_PAD_INVALID;
    }

    const uint8_t status = message->bytes[0];
    const uint8_t note = message->bytes[1];
    const uint8_t velocity = message->bytes[2];
    if (status < 0x80 || status >= 0xf0 || note >= 0x80 || velocity >= 0x80) {
        return GRIDPICO_APC_PAD_INVALID;
    }
    if (note != GRIDPICO_APC_DEMO_PAD) {
        return GRIDPICO_APC_PAD_IGNORED;
    }

    switch (status) {
    case GRIDPICO_APC_DEMO_PRESS_STATUS:
        return velocity == 0 ? GRIDPICO_APC_PAD_RELEASED : GRIDPICO_APC_PAD_PRESSED;
    case GRIDPICO_APC_DEMO_RELEASE_STATUS:
        return GRIDPICO_APC_PAD_RELEASED;
    default:
        return GRIDPICO_APC_PAD_IGNORED;
    }
}

gridpico_apc_surface_message gridpico_apc_encode_pad_led(bool lit)
{
    return (gridpico_apc_surface_message){{
        GRIDPICO_APC_DEMO_LED_STATUS,
        GRIDPICO_APC_DEMO_PAD,
        lit ? GRIDPICO_APC_DEMO_LED_GREEN : GRIDPICO_APC_DEMO_LED_OFF
    }};
}
