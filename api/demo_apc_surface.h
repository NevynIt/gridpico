#ifndef GRIDPICO_DEMO_APC_SURFACE_H
#define GRIDPICO_DEMO_APC_SURFACE_H

#include "native_proof.h"

enum {
    GRIDPICO_APC_DEMO_PAD = 0x00,
    GRIDPICO_APC_DEMO_PRESS_STATUS = 0x90,
    GRIDPICO_APC_DEMO_RELEASE_STATUS = 0x80,
    GRIDPICO_APC_DEMO_PRESS_VELOCITY = 0x7f,
    GRIDPICO_APC_DEMO_LED_STATUS = 0x96,
    GRIDPICO_APC_DEMO_LED_GREEN = 0x15,
    GRIDPICO_APC_DEMO_LED_OFF = 0x00
};

typedef struct {
    uint8_t bytes[3];
} gridpico_apc_surface_message;

typedef void (*gridpico_apc_surface_feedback_fn)(
    void *context, const gridpico_apc_surface_message *message);

#endif
