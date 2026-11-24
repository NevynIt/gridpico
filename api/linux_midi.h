#ifndef GRIDPICO_LINUX_MIDI_H
#define GRIDPICO_LINUX_MIDI_H

#include "local_router.h"

typedef struct gridpico_linux_midi gridpico_linux_midi;

typedef struct {
    uint8_t client;
    uint8_t port;
} gridpico_linux_midi_address;

enum { GRIDPICO_LINUX_USB_SERIAL_BYTES = 64 };

/* Stage 3 selector for one uniquely correlated USB MIDI port. */
typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t interface_number;
    uint8_t serial_size;
    uint8_t serial[GRIDPICO_LINUX_USB_SERIAL_BYTES];
} gridpico_linux_usb_selector;

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
/* Resolve an address for the current observation, including output identity.
 * Stage 3 bind supports inputs only. A valid output bind returns
 * GRIDPICO_UNAVAILABLE without a subscription or generation change.
 * Input bind verifies around subscription and publishes an address on success.
 */
gridpico_result gridpico_linux_midi_resolve_usb(
    gridpico_linux_midi *midi, const gridpico_linux_usb_selector *selector,
    gridpico_patch_direction direction, gridpico_linux_midi_address *out_address);
gridpico_result gridpico_linux_midi_bind_usb(
    gridpico_linux_midi *midi, const gridpico_linux_usb_selector *selector,
    gridpico_patch_direction direction, gridpico_connection connection,
    gridpico_linux_midi_address *out_address);
/* Check the existing verified input subscription for the exact generation. */
gridpico_result gridpico_linux_midi_check_usb_binding(
    gridpico_linux_midi *midi, const gridpico_linux_usb_selector *selector,
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
