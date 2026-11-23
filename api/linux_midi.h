#ifndef GRIDPICO_LINUX_MIDI_H
#define GRIDPICO_LINUX_MIDI_H

#include "local_router.h"

typedef struct gridpico_linux_midi gridpico_linux_midi;

typedef struct {
    uint8_t client;
    uint8_t port;
} gridpico_linux_midi_address;

typedef struct {
    gridpico_linux_midi_address address;
    char name[GRIDPICO_PATCH_NAME_BYTES];
    bool can_read;
    bool can_write;
} gridpico_linux_midi_port;

typedef enum {
    GRIDPICO_LINUX_MIDI_RECEIVED,
    GRIDPICO_LINUX_MIDI_PORT_REMOVED,
    GRIDPICO_LINUX_MIDI_SUBSCRIPTION_REMOVED,
    GRIDPICO_LINUX_MIDI_IO_FAILED
} gridpico_linux_midi_event_kind;

typedef struct {
    gridpico_linux_midi_event_kind kind;
    gridpico_connection connection;
    /* Present only for GRIDPICO_LINUX_MIDI_RECEIVED. */
    uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES];
} gridpico_linux_midi_event;

gridpico_result gridpico_linux_midi_open(gridpico_linux_midi **out);
void gridpico_linux_midi_close(gridpico_linux_midi *midi);
gridpico_result gridpico_linux_midi_list(gridpico_linux_midi *midi,
                                        gridpico_linux_midi_port *ports,
                                        size_t capacity, size_t *count);
gridpico_result gridpico_linux_midi_bind(gridpico_linux_midi *midi,
                                        gridpico_linux_midi_address address,
                                        gridpico_patch_direction direction,
                                        gridpico_connection connection);
gridpico_result gridpico_linux_midi_unbind(gridpico_linux_midi *midi,
                                          gridpico_connection connection);
gridpico_result gridpico_linux_midi_send(gridpico_linux_midi *midi,
                                         gridpico_connection destination,
                                         const uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES]);
gridpico_result gridpico_linux_midi_next(gridpico_linux_midi *midi,
                                         gridpico_linux_midi_event *out,
                                         bool *has_event);

#endif
