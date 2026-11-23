#ifndef GRIDPICO_APP_PATCH_INVENTORY_H
#define GRIDPICO_APP_PATCH_INVENTORY_H

#include "api/patchbay.h"

enum { KEYBED = 1, MIDI_A = 2, MIDI_B = 3, PANEL = 4, LED_FEEDBACK = 5 };

static const gridpico_patch_inventory patch_inventory = {
    .device_count = GRIDPICO_PATCH_DEVICES,
    .port_count = GRIDPICO_PATCH_PORTS,
    .devices = {
        {1, "APC Key 25 mk2", GRIDPICO_PATCH_CONNECTED},
        {2, "Simulated dual-port instrument", GRIDPICO_PATCH_CONNECTED},
    },
    .ports = {
        {KEYBED, 1, "Keybed", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, true},
        {PANEL, 1, "Panel", GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_CONNECTED, false},
        {LED_FEEDBACK, 1, "LED feedback", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, false},
        {MIDI_A, 2, "MIDI A", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, true},
        {MIDI_B, 2, "MIDI B", GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_CONNECTED, true},
    },
};

#endif
