#ifndef GRIDPICO_APC_CHOOSER_H
#define GRIDPICO_APC_CHOOSER_H

#include "api/native_proof.h"

typedef enum {
    GRIDPICO_APC_CHOOSER_NO_REQUEST,
    GRIDPICO_APC_CHOOSER_PATCHBAY,
    GRIDPICO_APC_CHOOSER_KEYS
} gridpico_apc_chooser_mode;

typedef struct {
    bool consumed;
    gridpico_apc_chooser_mode requested_mode;
} gridpico_apc_chooser_result;

typedef enum {
    GRIDPICO_APC_CHOOSER_UP,
    GRIDPICO_APC_CHOOSER_ORDINARY_DOWN,
    GRIDPICO_APC_CHOOSER_LOCAL_DOWN
} gridpico_apc_chooser_press;

typedef struct {
    gridpico_connection attachment;
    bool shift_down;
    bool open;
    bool chord_used;
    gridpico_apc_chooser_press sustain;
    gridpico_apc_chooser_press selectors[2];
} gridpico_apc_chooser;

/* Bind one verified physical APC attachment. Both cable calls use the same
 * identity. The caller still validates and owns ordinary MIDI lifetimes.
 */
void gridpico_apc_chooser_init(gridpico_apc_chooser *chooser, gridpico_connection attachment);
void gridpico_apc_chooser_detach(gridpico_apc_chooser *chooser, gridpico_connection attachment);

/* Inspect one decoded cable MIDI message in serialized arrival order. Every
 * unconsumed message retains its exact bytes and normal press/release owner.
 * The caller commits a requested mode through its normal cutover. open is
 * the only display cue and is valid only while the chooser is visible.
 */
gridpico_apc_chooser_result gridpico_apc_chooser_receive(
    gridpico_apc_chooser *chooser, gridpico_connection attachment,
    uint8_t cable, const uint8_t *bytes, size_t byte_count);

#endif
